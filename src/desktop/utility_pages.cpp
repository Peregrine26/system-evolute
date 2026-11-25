#include <QCoreApplication>
#include "desktop/page_factories.h"
#include "desktop/utility_data.h"
#include "desktop/media_session.h"
#include "desktop/shell.h"
#include <QApplication>
#include <QBuffer>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QFile>
#include <QDir>
#include <QHeaderView>
#include <QImageReader>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScrollArea>
#include <QSaveFile>
#include <QSlider>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <algorithm>

namespace crossdesk::desktop {
namespace {
using Notify = std::function<void(const QString&)>;
QPushButton* push(QBoxLayout* layout, const QString& text, const std::function<void()>& callback) {
    auto* button = new QPushButton(text); layout->addWidget(button);
    QObject::connect(button, &QPushButton::clicked, button, callback); return button;
}
void putData(Store& store, const QString& id, const QJsonObject& patch) {
    auto widget = store.widget(id); if (widget.isEmpty()) return;
    auto data = widget["data"].toObject(); for (auto i = patch.begin(); i != patch.end(); ++i) data[i.key()] = i.value();
    widget["data"] = data; store.putWidget(widget);
}
class WeatherPage final : public QWidget {
public:
    WeatherPage(Store& store, QString id) : store_(store), id_(std::move(id)) {
        setObjectName("weatherPage");
        auto* layout = new QVBoxLayout(this); auto* entry = new QHBoxLayout;
        city_ = new QLineEdit; city_->setObjectName("weatherCity"); city_->setPlaceholderText(QCoreApplication::translate("CrossDesk", "城市名称"));
        const auto data = store_.widget(id_)["data"].toObject(); city_->setText(data["city"].toString(QStringLiteral("Shanghai")));
        entry->addWidget(city_); push(entry, QCoreApplication::translate("CrossDesk", "刷新"), [this] { locate(); })->setObjectName("refreshWeather"); layout->addLayout(entry);
        auto* appearance = new QHBoxLayout;
        skin_ = new QComboBox; skin_->addItems({QCoreApplication::translate("CrossDesk", "跟随主题"), QCoreApplication::translate("CrossDesk", "天气配色"), QCoreApplication::translate("CrossDesk", "深色")});
        skin_->setCurrentIndex(std::clamp(data["weatherSkin"].toInt(), 0, 2)); appearance->addWidget(skin_);
        icons_ = new QComboBox; icons_->addItems({QCoreApplication::translate("CrossDesk", "符号"), QCoreApplication::translate("CrossDesk", "彩色"), QCoreApplication::translate("CrossDesk", "方框"), QCoreApplication::translate("CrossDesk", "文字")});
        icons_->setCurrentIndex(std::clamp(data["weatherIcons"].toInt(), 0, 3)); appearance->addWidget(icons_); layout->addLayout(appearance);
        summary_ = new QLabel; summary_->setObjectName("weatherSummary"); summary_->setWordWrap(true); summary_->setTextFormat(Qt::PlainText); layout->addWidget(summary_);
        status_ = new QLabel; status_->setObjectName("weatherStatus"); status_->setWordWrap(true); layout->addWidget(status_);
        auto* tabs = new QTabWidget; hourly_ = makeTable(); daily_ = makeTable(); hourly_->setObjectName("hourlyWeather"); daily_->setObjectName("dailyWeather");
        tabs->addTab(hourly_, QCoreApplication::translate("CrossDesk", "逐小时")); tabs->addTab(daily_, QCoreApplication::translate("CrossDesk", "未来 7 天")); layout->addWidget(tabs, 1);
        auto* credit = new QLabel(QCoreApplication::translate("CrossDesk", "天气数据：Open-Meteo · CC BY 4.0")); credit->setTextInteractionFlags(Qt::TextSelectableByMouse); layout->addWidget(credit);
        auto updateSkin = [this] { putData(store_, id_, {{"weatherSkin", skin_->currentIndex()}, {"weatherIcons", icons_->currentIndex()}}); render(); };
        connect(skin_, &QComboBox::currentIndexChanged, this, updateSkin); connect(icons_, &QComboBox::currentIndexChanged, this, updateSkin);
        connect(city_, &QLineEdit::returnPressed, this, [this] { locate(); });
        auto cached = data; // Read legacy inline cache once, then use the disposable cache directory.
        QFile file(cacheFile());
        if (file.open(QIODevice::ReadOnly) && file.size() <= 2 * 1024 * 1024) {
            const auto stored = QJsonDocument::fromJson(file.readAll()).object();
            if (parseForecast(stored["forecastCache"].toObject()).valid) cached = stored;
        }
        cache_ = cached["forecastCache"].toObject(); cacheCity_ = cached["forecastCity"].toString();
        cacheAt_ = QDateTime::fromString(cached["forecastFetchedAt"].toString(), Qt::ISODate);
        if (parseForecast(cache_).valid && cacheCity_ == city_->text()) { render(); status_->setText(cacheStatus()); }
        else summary_->setText(QCoreApplication::translate("CrossDesk", "输入城市并刷新天气"));
        timer_.setInterval(30 * 60 * 1000); connect(&timer_, &QTimer::timeout, this, [this] { if (isVisible()) locate(); }); timer_.start();
        const bool fresh = cacheAt_.isValid() && cacheAt_.secsTo(QDateTime::currentDateTimeUtc()) >= 0 && cacheAt_.secsTo(QDateTime::currentDateTimeUtc()) < 1800 && cacheCity_ == city_->text();
        if (!qEnvironmentVariableIsSet("CROSSDESK_OFFLINE") && !fresh) QTimer::singleShot(0, this, [this] { locate(); });
        else if (qEnvironmentVariableIsSet("CROSSDESK_OFFLINE")) status_->setText(QCoreApplication::translate("CrossDesk", "离线模式 · ") + cacheStatus());
    }
private:
    QString cacheFile() const { return store_.directory() + "/cache/weather-" + id_ + ".json"; }
    QTableWidget* makeTable() {
        auto* table = new QTableWidget(0, 4); table->setHorizontalHeaderLabels({QCoreApplication::translate("CrossDesk", "时间"), QCoreApplication::translate("CrossDesk", "天气"), QCoreApplication::translate("CrossDesk", "温度"), QCoreApplication::translate("CrossDesk", "降水概率")});
        table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents); table->horizontalHeader()->setStretchLastSection(true);
        table->verticalHeader()->hide(); table->setEditTriggers(QAbstractItemView::NoEditTriggers); return table;
    }
    QString cacheStatus() const {
        if (!cacheAt_.isValid() || !parseForecast(cache_).valid) return QCoreApplication::translate("CrossDesk", "无可用缓存");
        return QCoreApplication::translate("CrossDesk", "缓存更新于 %1%2").arg(cacheAt_.toLocalTime().toString("MM-dd HH:mm"), cacheAt_.secsTo(QDateTime::currentDateTimeUtc()) >= 1800 ? QCoreApplication::translate("CrossDesk", "（已过期）") : QString{});
    }
    void error(const QString& message) {
        status_->setText(message + QStringLiteral(" · ") + cacheStatus());
        if (!parseForecast(cache_).valid || cacheCity_ != city_->text().trimmed()) { summary_->setText(QCoreApplication::translate("CrossDesk", "天气暂不可用，请重试")); hourly_->setRowCount(0); daily_->setRowCount(0); }
    }
    void render() {
        const auto forecast = parseForecast(cache_); if (!forecast.valid) return;
        summary_->setText(QCoreApplication::translate("CrossDesk", "%1  %2  %3 °C\n%4 · 湿度 %5% · 风速 %6 km/h").arg(cacheCity_, weatherSymbol(forecast.code, icons_->currentIndex()))
            .arg(forecast.temperature).arg(weatherDescription(forecast.code)).arg(forecast.humidity).arg(forecast.wind));
        auto populate = [this](QTableWidget* table, const QList<WeatherRow>& rows, bool hourly) {
            table->setRowCount(0);
            for (const auto& row : rows) {
                if (hourly && row.time < cache_["current"].toObject()["time"].toString().left(13)) continue;
                if (table->rowCount() >= (hourly ? 24 : 7)) break;
                const int i = table->rowCount(); table->insertRow(i);
                const QStringList cells{hourly ? row.time.mid(5).replace('T', ' ') : row.time.mid(5), weatherSymbol(row.code, icons_->currentIndex()) + ' ' + weatherDescription(row.code),
                    hourly ? QStringLiteral("%1°").arg(row.high) : QStringLiteral("%1° / %2°").arg(row.low).arg(row.high), QStringLiteral("%1%").arg(row.rain)};
                for (int col = 0; col < cells.size(); ++col) table->setItem(i, col, new QTableWidgetItem(cells[col]));
            }
        };
        populate(hourly_, forecast.hourly, true); populate(daily_, forecast.daily, false);
        QString style;
        if (skin_->currentIndex() == 2) style = "#weatherPage {background:#182b3c;} #weatherPage QLabel {background:transparent;color:#f5f8fb;}";
        else if (skin_->currentIndex() == 1) style = QString("#weatherPage {background:%1;} #weatherPage QLabel {background:transparent;color:#213540;}").arg(forecast.code <= 2 ? "#f9edce" : forecast.code >= 71 ? "#e2edf4" : "#d9e6ed");
        setStyleSheet(style);
    }
    void locate() {
        if (qEnvironmentVariableIsSet("CROSSDESK_OFFLINE")) { error(QCoreApplication::translate("CrossDesk", "离线模式")); return; }
        const auto city = city_->text().trimmed(); if (city.isEmpty()) return;
        const auto generation = ++generation_; status_->setText(QCoreApplication::translate("CrossDesk", "正在查找城市…"));
        QUrl url("https://geocoding-api.open-meteo.com/v1/search"); QUrlQuery params;
        params.addQueryItem("name", city); params.addQueryItem("count", "5"); params.addQueryItem("language", "zh"); url.setQuery(params);
        QNetworkRequest request(url); request.setTransferTimeout(12000); auto* reply = network_.get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, city, generation] {
            const auto bytes = reply->readAll(); const auto failed = reply->error() != QNetworkReply::NoError; reply->deleteLater();
            if (generation != generation_) return;
            const auto locations = QJsonDocument::fromJson(bytes).object()["results"].toArray();
            if (failed || locations.isEmpty()) { error(QCoreApplication::translate("CrossDesk", "找不到城市或网络不可用")); return; }
            int selected = 0;
            if (locations.size() > 1) {
                QStringList choices;
                for (const auto& value : locations) { const auto item = value.toObject(); choices << item["name"].toString() + " · " + item["admin1"].toString() + " · " + item["country"].toString(); }
                bool ok = false; const auto choice = QInputDialog::getItem(this, QCoreApplication::translate("CrossDesk", "选择城市"), QCoreApplication::translate("CrossDesk", "城市"), choices, 0, false, &ok);
                if (!ok) { status_->setText(cacheStatus()); return; } selected = choices.indexOf(choice);
            }
            if (generation != generation_) return;
            const auto location = locations[selected].toObject();
            putData(store_, id_, {{"city", city}}); status_->setText(QCoreApplication::translate("CrossDesk", "正在获取预报…"));
            QUrl url("https://api.open-meteo.com/v1/forecast"); QUrlQuery query;
            query.addQueryItem("latitude", QString::number(location["latitude"].toDouble())); query.addQueryItem("longitude", QString::number(location["longitude"].toDouble()));
            query.addQueryItem("current", "temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m");
            query.addQueryItem("hourly", "temperature_2m,precipitation_probability,weather_code"); query.addQueryItem("forecast_hours", "48");
            query.addQueryItem("daily", "temperature_2m_max,temperature_2m_min,weather_code,precipitation_probability_max"); query.addQueryItem("timezone", "auto"); url.setQuery(query);
            QNetworkRequest request(url); request.setTransferTimeout(12000); auto* forecast = network_.get(request);
            connect(forecast, &QNetworkReply::finished, this, [this, forecast, city, generation] {
                const auto bytes = forecast->readAll(); const bool failed = forecast->error() != QNetworkReply::NoError; forecast->deleteLater();
                if (generation != generation_) return; const auto data = QJsonDocument::fromJson(bytes).object();
                if (failed || !parseForecast(data).valid) { error(QCoreApplication::translate("CrossDesk", "预报暂不可用，点击刷新重试")); return; }
                cache_ = data; cacheCity_ = city; cacheAt_ = QDateTime::currentDateTimeUtc();
                QDir().mkpath(store_.directory() + "/cache"); QSaveFile cache(cacheFile());
                if (cache.open(QIODevice::WriteOnly)) {
                    const auto bytes = QJsonDocument(QJsonObject{{"forecastCache", data}, {"forecastCity", city}, {"forecastFetchedAt", cacheAt_.toString(Qt::ISODate)}}).toJson(QJsonDocument::Compact);
                    if (cache.write(bytes) == bytes.size()) cache.commit(); else cache.cancelWriting();
                }
                render(); status_->setText(cacheStatus() + " · " + data["timezone"].toString());
            });
        });
    }
    Store& store_; QString id_; QLineEdit* city_; QComboBox* skin_; QComboBox* icons_; QLabel* summary_; QLabel* status_;
    QTableWidget* hourly_; QTableWidget* daily_; QNetworkAccessManager network_; QTimer timer_;
    QJsonObject cache_; QString cacheCity_; QDateTime cacheAt_; quint64 generation_ = 0;
};

class ClockPage final : public QWidget {
public:
    ClockPage(Store& store, QString id) : store_(store), id_(std::move(id)) {
        setObjectName("clockPage"); auto* layout = new QVBoxLayout(this); auto* actions = new QHBoxLayout;
        push(actions, QCoreApplication::translate("CrossDesk", "时光设置"), [this] { preferences(); });
        push(actions, QCoreApplication::translate("CrossDesk", "日历"), [this] {
            QDialog dialog(this); dialog.setWindowTitle(QCoreApplication::translate("CrossDesk", "日历")); auto* layout = new QVBoxLayout(&dialog); auto* calendar = new QCalendarWidget;
            calendar->setGridVisible(true); layout->addWidget(calendar); auto* label = new QLabel; label->setWordWrap(true); layout->addWidget(label);
            auto update = [calendar, label] { const auto date = calendar->selectedDate(); const auto lunar = lunarDate(date); label->setText(lunar + '\n' + calendarEvents(date, lunar)); };
            connect(calendar, &QCalendarWidget::selectionChanged, &dialog, update); update(); dialog.exec();
        }); layout->addLayout(actions); layout->addStretch();
        clock_ = new QLabel; clock_->setObjectName("clockTime"); clock_->setAlignment(Qt::AlignCenter); clock_->setStyleSheet("font-size:32px;background:transparent;"); layout->addWidget(clock_);
        date_ = new QLabel; date_->setAlignment(Qt::AlignCenter); date_->setWordWrap(true); date_->setStyleSheet("background:transparent;"); layout->addWidget(date_);
        events_ = new QLabel; events_->setObjectName("clockEvents"); events_->setAlignment(Qt::AlignCenter); events_->setWordWrap(true); events_->setStyleSheet("background:transparent;"); layout->addWidget(events_); layout->addStretch();
        connect(&timer_, &QTimer::timeout, this, [this] { if (isVisible()) tick(); }); timer_.start(1000); tick();
    }
protected:
    void paintEvent(QPaintEvent* event) override {
        QWidget::paintEvent(event); if (image_.isNull()) return;
        const auto data = store_.widget(id_)["data"].toObject(); QPainter painter(this); painter.setOpacity(data["clockBackgroundOpacity"].toInt(35) / 100.0);
        QSize size = image_.size(); size.scale(this->size(), data["clockBackgroundFit"].toInt() ? Qt::KeepAspectRatio : Qt::KeepAspectRatioByExpanding);
        painter.drawPixmap(QRect(QPoint((width() - size.width()) / 2, (height() - size.height()) / 2), size), image_);
    }
private:
    void tick() {
        const auto data = store_.widget(id_)["data"].toObject(); const auto now = QDateTime::currentDateTime();
        const auto time = data["clock12Hour"].toBool() ? (data["clockSeconds"].toBool(true) ? "hh:mm:ss AP" : "hh:mm AP") : data["clockSeconds"].toBool(true) ? "HH:mm:ss" : "HH:mm";
        clock_->setText(now.time().toString(time)); const auto lunar = lunarDate(now.date());
        date_->setText(QLocale().toString(now.date(), "yyyy年M月d日 dddd") + '\n' + lunar); events_->setText(calendarEvents(now.date(), lunar));
        const auto backgrounds = data["clockBackgrounds"].toArray(); QString path;
        if (!backgrounds.isEmpty()) {
            const auto interval = qBound(5, data["clockSlideSeconds"].toInt(60), 86400);
            const auto index = (now.toSecsSinceEpoch() / interval) % backgrounds.size(); path = backgrounds[int(index)].toString();
        }
        if (path != imagePath_) {
            imagePath_ = path; QImageReader reader(path); const auto size = reader.size();
            if (size.isValid() && size.width() <= 32000 && size.height() <= 32000) {
                QSize target = size; target.scale(1920, 1080, Qt::KeepAspectRatio); reader.setScaledSize(target); image_ = QPixmap::fromImage(reader.read());
            } else image_ = {};
            update();
        }
    }
    void preferences() {
        auto data = store_.widget(id_)["data"].toObject(); QDialog dialog(this); dialog.setWindowTitle(QCoreApplication::translate("CrossDesk", "时光设置")); auto* form = new QFormLayout(&dialog);
        auto* seconds = new QCheckBox(QCoreApplication::translate("CrossDesk", "显示秒")); seconds->setChecked(data["clockSeconds"].toBool(true)); form->addRow(seconds);
        auto* hour = new QCheckBox(QCoreApplication::translate("CrossDesk", "12 小时制")); hour->setChecked(data["clock12Hour"].toBool()); form->addRow(hour);
        auto* interval = new QSpinBox; interval->setRange(5, 86400); interval->setValue(data["clockSlideSeconds"].toInt(60)); form->addRow(QCoreApplication::translate("CrossDesk", "背景切换间隔（秒）"), interval);
        auto* opacity = new QSlider(Qt::Horizontal); opacity->setRange(0, 100); opacity->setValue(data["clockBackgroundOpacity"].toInt(35)); form->addRow(QCoreApplication::translate("CrossDesk", "背景透明度"), opacity);
        auto* fit = new QComboBox; fit->addItems({QCoreApplication::translate("CrossDesk", "填充裁切"), QCoreApplication::translate("CrossDesk", "完整显示")}); fit->setCurrentIndex(data["clockBackgroundFit"].toInt()); form->addRow(QCoreApplication::translate("CrossDesk", "图片适应"), fit);
        auto backgrounds = data["clockBackgrounds"].toArray(); auto* images = new QPushButton(QCoreApplication::translate("CrossDesk", "选择背景图片（%1 张）").arg(backgrounds.size())); form->addRow(images);
        connect(images, &QPushButton::clicked, &dialog, [&] {
            const auto paths = QFileDialog::getOpenFileNames(&dialog, QCoreApplication::translate("CrossDesk", "选择轮播图片"), {}, "Images (*.png *.jpg *.jpeg *.webp *.bmp)");
            if (paths.isEmpty()) return; backgrounds = {}; for (const auto& path : paths) backgrounds.append(path);
            images->setText(QCoreApplication::translate("CrossDesk", "选择背景图片（%1 张）").arg(backgrounds.size()));
        });
        auto* clear = new QPushButton(QCoreApplication::translate("CrossDesk", "清除背景")); form->addRow(clear); connect(clear, &QPushButton::clicked, &dialog, [&] { backgrounds = {}; images->setText(QCoreApplication::translate("CrossDesk", "选择背景图片（0 张）")); });
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel); form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept); connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() != QDialog::Accepted) return;
        putData(store_, id_, {{"clockSeconds", seconds->isChecked()}, {"clock12Hour", hour->isChecked()}, {"clockSlideSeconds", interval->value()}, {"clockBackgroundOpacity", opacity->value()}, {"clockBackgroundFit", fit->currentIndex()}, {"clockBackgrounds", backgrounds}});
        imagePath_ = {}; tick(); update();
    }
    Store& store_; QString id_; QLabel* clock_; QLabel* date_; QLabel* events_; QTimer timer_; QPixmap image_; QString imagePath_;
};

class MusicPage final : public QWidget {
public:
    MusicPage(Store& store, QString id, Notify notify) : store_(store), id_(std::move(id)), notify_(std::move(notify)) {
        setObjectName("musicPage"); auto* layout = new QVBoxLayout(this);
        sessions_ = new QComboBox; sessions_->setObjectName("mediaSessions"); sessions_->addItem(QCoreApplication::translate("CrossDesk", "跟随系统当前播放器"), ""); layout->addWidget(sessions_);
        auto* options = new QHBoxLayout; mode_ = new QComboBox; mode_->addItems({QCoreApplication::translate("CrossDesk", "封面"), QCoreApplication::translate("CrossDesk", "控制"), QCoreApplication::translate("CrossDesk", "唱片"), QCoreApplication::translate("CrossDesk", "紧凑")});
        mode_->setCurrentIndex(std::clamp(store_.widget(id_)["data"].toObject()["musicLayout"].toInt(), 0, 3)); options->addWidget(mode_);
        ambient_ = new QCheckBox(QCoreApplication::translate("CrossDesk", "封面配色")); ambient_->setChecked(store_.widget(id_)["data"].toObject()["musicAmbient"].toBool()); options->addWidget(ambient_); layout->addLayout(options);
        cover_ = new QLabel; cover_->setObjectName("mediaCover"); cover_->setAlignment(Qt::AlignCenter); cover_->setMinimumHeight(100); layout->addWidget(cover_, 1);
        metadata_ = new QLabel(QCoreApplication::translate("CrossDesk", "正在读取媒体会话…")); metadata_->setObjectName("mediaMetadata"); metadata_->setAlignment(Qt::AlignCenter); metadata_->setWordWrap(true); metadata_->setTextFormat(Qt::PlainText); layout->addWidget(metadata_);
        position_ = new QSlider(Qt::Horizontal); position_->setObjectName("mediaPosition"); position_->setRange(0, 10000); layout->addWidget(position_);
        time_ = new QLabel("0:00 / 0:00"); time_->setAlignment(Qt::AlignCenter); layout->addWidget(time_);
        auto* controls = new QHBoxLayout; previous_ = push(controls, QCoreApplication::translate("CrossDesk", "上一首"), [this] { control("previous"); });
        play_ = push(controls, QCoreApplication::translate("CrossDesk", "播放 / 暂停"), [this] { control("play"); }); next_ = push(controls, QCoreApplication::translate("CrossDesk", "下一首"), [this] { control("next"); }); layout->addLayout(controls);
        auto* modes = new QHBoxLayout; shuffle_ = new QCheckBox(QCoreApplication::translate("CrossDesk", "随机")); modes->addWidget(shuffle_);
        repeat_ = new QComboBox; repeat_->addItems({QCoreApplication::translate("CrossDesk", "不循环"), QCoreApplication::translate("CrossDesk", "单曲循环"), QCoreApplication::translate("CrossDesk", "列表循环")}); modes->addWidget(repeat_); layout->addLayout(modes);
        auto* volumeRow = new QHBoxLayout; volumeRow->addWidget(new QLabel(QCoreApplication::translate("CrossDesk", "系统音量"))); volume_ = new QSlider(Qt::Horizontal); volume_->setRange(0, 100); volumeRow->addWidget(volume_); layout->addLayout(volumeRow);
        status_ = new QLabel; status_->setObjectName("mediaStatus"); status_->setWordWrap(true); layout->addWidget(status_);
        const auto savedSession = store_.widget(id_)["data"].toObject()["musicSession"].toString(); selected_ = savedSession;
        connect(sessions_, &QComboBox::currentIndexChanged, this, [this] { selected_ = sessions_->currentData().toString(); trackKey_.clear(); putData(store_, id_, {{"musicSession", selected_}}); refresh(); });
        connect(mode_, &QComboBox::currentIndexChanged, this, [this] { putData(store_, id_, {{"musicLayout", mode_->currentIndex()}}); renderCover(); });
        connect(ambient_, &QCheckBox::toggled, this, [this] { putData(store_, id_, {{"musicAmbient", ambient_->isChecked()}}); renderCover(); });
        connect(position_, &QSlider::sliderReleased, this, [this] {
            const auto ticks = current_.start + qRound64((current_.end - current_.start) * (position_->value() / 10000.0));
            control("seek", std::clamp(ticks, current_.minimum, qMax(current_.minimum, current_.maximum)));
        });
        connect(shuffle_, &QCheckBox::clicked, this, [this](bool checked) { control("shuffle", checked); });
        connect(repeat_, &QComboBox::activated, this, [this](int value) { control("repeat", value); });
        connect(volume_, &QSlider::sliderReleased, this, [this] { if (!setSystemVolume(volume_->value())) notify_(QCoreApplication::translate("CrossDesk", "系统音量不可用")); });
        connect(&poll_, &QTimer::timeout, this, [this] { if (isVisible()) refresh(); }); poll_.start(1500);
        connect(&artTimer_, &QTimer::timeout, this, [this] { if (isVisible() && current_.playing && mode_->currentIndex() == 2 && !pixmap_.isNull() && !store_.settings()["reducedMotion"].toBool()) { angle_ = (angle_ + 3) % 360; renderCover(); } }); artTimer_.start(100);
        connect(&cacheTimer_, &QTimer::timeout, this, [this] {
            const auto seconds = store_.settings()["cacheIdleSeconds"].toInt(120);
            if (!isVisible() && seconds > 0 && hiddenAt_.isValid() && hiddenAt_.secsTo(QDateTime::currentDateTime()) >= seconds) { pixmap_ = {}; cover_->clear(); trackKey_.clear(); }
        }); cacheTimer_.start(10000);
        QTimer::singleShot(0, this, [this] { if (isVisible()) refresh(); }); updateControls();
    }
protected:
    void resizeEvent(QResizeEvent* event) override { QWidget::resizeEvent(event); renderCover(); }
    void showEvent(QShowEvent* event) override { QWidget::showEvent(event); refresh(); }
    void hideEvent(QHideEvent* event) override { hiddenAt_ = QDateTime::currentDateTime(); QWidget::hideEvent(event); }
private:
    void updateControls() {
        previous_->setEnabled(current_.available && current_.canPrevious && !actionBusy_); next_->setEnabled(current_.available && current_.canNext && !actionBusy_);
        play_->setEnabled(current_.available && current_.canToggle && !actionBusy_); position_->setEnabled(current_.available && current_.canSeek && current_.end > current_.start && !actionBusy_);
        shuffle_->setEnabled(current_.available && current_.canShuffle && !actionBusy_); repeat_->setEnabled(current_.available && current_.canRepeat && !actionBusy_);
    }
    void refresh() {
        if (busy_) return; busy_ = true; const auto selected = selected_; const auto previous = trackKey_;
        auto* watcher = new QFutureWatcher<MediaSnapshot>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, selected] {
            const auto result = watcher->result(); watcher->deleteLater(); busy_ = false;
            if (selected != selected_) { refresh(); return; }
            current_ = result; QSignalBlocker block(sessions_); sessions_->clear(); sessions_->addItem(QCoreApplication::translate("CrossDesk", "跟随系统当前播放器"), "");
            for (const auto& item : result.sessions) sessions_->addItem(item.label, item.id);
            int index = sessions_->findData(selected_); if (index < 0 && !selected_.isEmpty()) { sessions_->addItem(selected_ + QCoreApplication::translate("CrossDesk", "（已关闭）"), selected_); index = sessions_->count() - 1; } sessions_->setCurrentIndex(qMax(0, index));
            const auto key = result.source + '\n' + result.title + '\n' + result.artist;
            if (key != trackKey_) {
                trackKey_ = key; auto bytes = result.cover; QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly); QImageReader reader(&buffer);
                auto size = reader.size(); if (size.isValid() && size.width() <= 32000 && size.height() <= 32000) { size.scale(600, 600, Qt::KeepAspectRatio); reader.setScaledSize(size); pixmap_ = QPixmap::fromImage(reader.read()); } else pixmap_ = {};
                renderCover();
            }
            metadata_->setText(result.available ? (result.title.isEmpty() ? result.source : result.title) + '\n' + result.artist + (result.album.isEmpty() ? QString{} : " · " + result.album) : QCoreApplication::translate("CrossDesk", "没有活动的播放器"));
            status_->setText(result.error.isEmpty() ? (result.playing ? QCoreApplication::translate("CrossDesk", "正在播放") : QCoreApplication::translate("CrossDesk", "已暂停")) : result.error);
            play_->setText(result.playing ? QCoreApplication::translate("CrossDesk", "暂停") : QCoreApplication::translate("CrossDesk", "播放"));
            if (!position_->isSliderDown()) position_->setValue(result.end > result.start ? int(std::clamp((result.position - result.start) * 10000.0 / (result.end - result.start), 0.0, 10000.0)) : 0);
            time_->setText(mediaTime(result.position - result.start) + " / " + mediaTime(result.end - result.start));
            { QSignalBlocker s(shuffle_); shuffle_->setChecked(result.shuffle); } { QSignalBlocker s(repeat_); repeat_->setCurrentIndex(std::clamp(result.repeat, 0, 2)); }
            if (!volume_->isSliderDown()) { const auto volume = systemVolume(); volume_->setEnabled(volume >= 0); volume_->setValue(qMax(0, volume)); }
            updateControls();
        });
        watcher->setFuture(QtConcurrent::run([selected, previous] { return readMediaSession(selected, previous); }));
    }
    void control(const QString& action, qint64 value = 0) {
        if (actionBusy_ || !current_.available) return; actionBusy_ = true; updateControls();
        const auto selected = current_.source; auto* watcher = new QFutureWatcher<QString>(this);
        connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
            const auto error = watcher->result(); watcher->deleteLater(); actionBusy_ = false;
            if (!error.isEmpty()) { status_->setText(error); notify_(error); } updateControls(); refresh();
        });
        watcher->setFuture(QtConcurrent::run([selected, action, value] { QString error; controlMediaSession(selected, action, value, &error); return error; }));
    }
    void renderCover() {
        const bool visible = mode_->currentIndex() == 0 || mode_->currentIndex() == 2; cover_->setVisible(visible);
        if (!visible) return; const int size = qBound(80, qMin(cover_->width() - 12, cover_->height() - 8), 600);
        if (pixmap_.isNull()) { cover_->setPixmap({}); cover_->setText(QStringLiteral("♫")); } else {
            auto scaled = pixmap_.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            if (mode_->currentIndex() == 2) {
                QPixmap disc(size, size); disc.fill(Qt::transparent); QPainter painter(&disc); painter.setRenderHint(QPainter::Antialiasing);
                QPainterPath path; path.addEllipse(1, 1, size - 2, size - 2); painter.setClipPath(path); painter.translate(size / 2.0, size / 2.0); painter.rotate(angle_);
                painter.drawPixmap(-scaled.width() / 2, -scaled.height() / 2, scaled); painter.setClipping(false); painter.setBrush(QColor("#202629")); painter.setPen(Qt::NoPen); painter.drawEllipse(QPointF(0, 0), size * 0.055, size * 0.055); scaled = disc;
            }
            cover_->setText({}); cover_->setPixmap(scaled);
        }
        if (ambient_->isChecked() && !pixmap_.isNull()) {
            const auto image = pixmap_.toImage().scaled(1, 1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation); auto color = image.pixelColor(0, 0).lighter(175); color.setAlpha(180);
            setStyleSheet(QString("#musicPage { background:%1; } #musicPage QLabel {background:transparent;}").arg(color.name(QColor::HexArgb)));
        } else setStyleSheet({});
    }
    Store& store_; QString id_; Notify notify_; QComboBox* sessions_; QComboBox* mode_; QCheckBox* ambient_; QLabel* cover_; QLabel* metadata_; QLabel* time_; QLabel* status_;
    QSlider* position_; QSlider* volume_; QPushButton* previous_; QPushButton* next_; QPushButton* play_; QCheckBox* shuffle_; QComboBox* repeat_;
    QTimer poll_, artTimer_, cacheTimer_; QDateTime hiddenAt_; bool busy_ = false, actionBusy_ = false; QString selected_, trackKey_; MediaSnapshot current_; QPixmap pixmap_; int angle_ = 0;
};
}
QWidget* createUtilityPage(Store& store, const QString& id, const Notify& notify) {
    const auto kind = store.widget(id)["kind"].toString();
    if (kind == "weather") return new WeatherPage(store, id);
    if (kind == "clock") return new ClockPage(store, id);
    if (kind == "music") return new MusicPage(store, id, notify);
    return new QWidget;
}
}
