#include "main_window.h"

#include "preview_widgets.h"
#include "window_cards.h"

#include <QCoreApplication>
#include <QEasingCurve>
#include <QGraphicsBlurEffect>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPainter>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QShortcut>
#include <QShowEvent>
#include <QTimer>
#include <QWidget>

namespace crossdesk::ui {

namespace {

// 左边卡片栏的宽度。它是"挑窗口"的地方，不该跟着屏幕无限变宽；
// 但也不能在 1366 的笔记本屏上占掉三分之一，所以按窗口宽度取比例再夹住。
constexpr int kBarMinWidth = 260;
constexpr int kBarMaxWidth = 380;

// ---- Task View：右键窗口后全屏展开子窗口，背景模糊，不分左右 ----
class TaskView : public QWidget {
public:
    TaskView(QWidget* host, const QVector<WindowItem>& wins, WindowId selected)
        : QWidget(host), m_selected(selected) {
        setAttribute(Qt::WA_DeleteOnClose);
        setAttribute(Qt::WA_QuitOnClose, false);
        setGeometry(host->rect());
        setFocusPolicy(Qt::StrongFocus);
        // 预渲染一张模糊背景：抓 host 截图，套 QGraphicsBlurEffect 再 grab 出来。
        QPixmap shot = host->grab();
        auto* tmp = new QLabel;
        tmp->setPixmap(shot);
        tmp->resize(shot.size());
        auto* blur = new QGraphicsBlurEffect(tmp);
        blur->setBlurRadius(22);
        tmp->setGraphicsEffect(blur);
        m_blurred = tmp->grab();
        delete tmp;
        m_wins = wins.mid(0, 4);  // 最多 4 个子窗口
        show();
        setFocus();
        raise();
    }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.drawPixmap(rect(), m_blurred, m_blurred.rect());
        p.fillRect(rect(), QColor(15, 17, 21, 90));  // 暗化叠加

        const int n = m_wins.size();
        if (n == 0) return;
        const int cw = qBound(180, (width() - 60) / n - 22, 300);
        const int ch = qBound(120, height() - 140, 190);
        const int gap = 22;
        const int total = n * cw + (n - 1) * gap;
        int x = (width() - total) / 2;
        const int y = (height() - ch) / 2;
        for (int i = 0; i < n; ++i) {
            m_rects[i] = QRect(x, y, cw, ch);
            drawCard(p, m_rects[i], m_wins[i], m_wins[i].id == m_selected);
            x += cw + gap;
        }
        p.setPen(QColor(0x8a, 0x8f, 0x99));
        QFont hf = font();
        hf.setPixelSize(12);
        p.setFont(hf);
        p.drawText(QRect(0, height() - 44, width(), 30), Qt::AlignCenter,
                   QStringLiteral("右键展开子窗口 · 点空白或按 Esc 返回"));
    }
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() != Qt::LeftButton) {
            QWidget::mousePressEvent(e);
            return;
        }
        close();
    }
    void keyPressEvent(QKeyEvent* e) override {
        if (e->key() == Qt::Key_Escape) {
            close();
            return;
        }
        QWidget::keyPressEvent(e);
    }
private:
    void drawCard(QPainter& p, const QRect& r, const WindowItem& w, bool sel) {
        p.setBrush(QColor(0x1b, 0x1e, 0x25));
        p.setPen(QPen(sel ? QColor(0x0a, 0x84, 0xff) : QColor(255, 255, 255, 20), sel ? 2 : 1));
        p.drawRoundedRect(r, 10, 10);
        const QRect screen(r.x(), r.y(), r.width(), r.height() - 34);
        p.save();
        p.setClipRect(screen);
        p.fillRect(screen, QColor(0x15, 0x18, 0x1e));
        p.fillRect(QRect(screen.x(), screen.y(), screen.width(), 20), QColor(0x2b, 0x30, 0x3a));
        qreal ly = screen.y() + 32;
        for (int i = 0; i < 3 && ly + 6 < screen.bottom(); ++i) {
            p.setBrush(QColor(255, 255, 255, 14));
            p.setPen(Qt::NoPen);
            p.drawRoundedRect(QRectF(screen.x() + 14, ly,
                                     (screen.width() - 28) * (i == 2 ? 0.55 : 0.9), 4), 2, 2);
            ly += 16;
        }
        p.restore();
        const QRect tb(r.x(), r.y() + r.height() - 34, r.width(), 34);
        p.fillRect(tb, QColor(0x20, 0x24, 0x2c));
        p.setPen(QColor(0xd6, 0xda, 0xe1));
        QFont f = font();
        f.setPixelSize(12);
        f.setBold(true);
        p.setFont(f);
        p.drawText(tb.adjusted(12, 0, -12, 0), Qt::AlignLeft | Qt::AlignVCenter,
                   QFontMetrics(f).elidedText(w.title, Qt::ElideRight, tb.width() - 24));
    }
    QVector<WindowItem> m_wins;
    WindowId m_selected = 0;
    QPixmap m_blurred;
    QRect m_rects[4];
};

}  // namespace

MainWindow::MainWindow(CrossDeskService* service, QWidget* parent)
    : QWidget(parent), m_service(service) {
    buildUi();
    connectService();
    // 先扫一次：真实实现不主动扫描的话，启动时左栏会是空的（看着像坏了）。
    applyWindows();
    m_service->refresh();
    if (!m_statusTakenByRefresh) {
        setStatus(QStringLiteral("点左边的窗口，右边就显示它。双击可以把窗口切到前台。"));
    }
}

void MainWindow::buildUi() {
    setWindowTitle(QStringLiteral("CrossDesk"));
    setObjectName(QStringLiteral("MainWindow"));
    // 无边框 + 全屏：这块画面就是桌面本身，不跟用户抢标题栏和边框。
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    setMinimumSize(960, 600);

    setStyleSheet(QStringLiteral(
        "QWidget#MainWindow { background:#0f1114; }"
        "QWidget { color:#e6e8ec; font-size:13px; }"
        "QLabel#statusLabel { background:rgba(16,18,22,0.80);"
        "  border:1px solid rgba(255,255,255,0.10); border-radius:9px;"
        "  padding:5px 12px; color:#b9bfc8; font-size:12px; }"));

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ---- 左：一摞窗口卡 ----
    m_cards = new CardStack(this);
    m_cards->setObjectName(QStringLiteral("cardStack"));
    m_cards->setFixedWidth(kBarMaxWidth);
    root->addWidget(m_cards, 0);

    // ---- 右：选中的那个窗口 ----
    m_canvas = new LayoutPreview(this);
    m_canvas->setObjectName(QStringLiteral("screenPreview"));
    m_sceneEffect = new QGraphicsOpacityEffect(m_canvas);
    m_sceneEffect->setOpacity(1.0);
    m_canvas->setGraphicsEffect(m_sceneEffect);
    root->addWidget(m_canvas, 1);

    // ---- 状态：浮在画面左下角，不占布局 ----
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("statusLabel"));
    m_statusLabel->setWordWrap(false);

    // 应用内快捷键。Enter 不单独注册：点击已经能换画面，
    // 定位统一走 Ctrl+Enter，免得和卡片栏的键盘操作抢同一个键。
    new QShortcut(QKeySequence(QStringLiteral("Ctrl+R")), this, [this] { onRefresh(); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this, [this] { onActivate(); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Enter), this, [this] { onActivate(); });
}

void MainWindow::connectService() {
    connect(m_service, &CrossDeskService::windowsChanged, this, &MainWindow::applyWindows);
    connect(m_service, &CrossDeskService::operationFinished, this,
            [this](const OperationResult& result) {
                if (!result.success && result.window == 0) m_statusTakenByRefresh = true;
                if (result.window != 0) {
                    if (result.success) {
                        m_failures.remove(result.window);
                    } else {
                        m_failures.insert(result.window, result.message);
                    }
                }
                setStatus(result.message, !result.success);
            });

    connect(m_cards, &CardStack::windowClicked, this, &MainWindow::onCardClicked);
    connect(m_cards, &CardStack::windowActivated, this, &MainWindow::onCardActivated);
    connect(m_cards, &CardStack::windowContextMenu, this, [this](WindowId id) {
        if (id != 0) {
            m_selected = id;
            m_cards->setSelected(id);
        }
        openTaskView();
    });
}

void MainWindow::setAllowHide(bool allow) {
    m_allowHide = allow;
}

void MainWindow::applyWindows() {
    m_windows = m_service->windows();
    m_statusTakenByRefresh = false;

    for (auto it = m_failures.begin(); it != m_failures.end();) {
        if (findWindow(it.key())) {
            ++it;
        } else {
            it = m_failures.erase(it);
        }
    }

    // 选中的窗口被关掉了：换到第一个，而不是让右边空着——
    // 空画面会让人以为程序坏了。
    const bool needsSelection = !m_windows.isEmpty() && findWindow(m_selected) == nullptr;
    const bool lostSelection = needsSelection && m_selected != 0;
    if (needsSelection) {
        m_selected = m_windows.first().id;
    } else if (m_windows.isEmpty()) {
        m_selected = 0;
    }

    m_cards->setWindows(m_windows);
    m_cards->setSelected(m_selected);
    // 刷新时画面内容没换人，别再淡入一次，否则 Ctrl+R 会闪一下。
    showSelected(needsSelection);

    if (lostSelection) {
        m_statusTakenByRefresh = true;
        setStatus(QStringLiteral("刚才看的那个窗口已经关闭，已经换成另一个。"), true);
    }
}

void MainWindow::showSelected(bool animate) {
    const WindowItem* window = findWindow(m_selected);
    if (!window) {
        m_canvas->clear();
        return;
    }
    m_canvas->showSingle(m_service->workArea(), *window);

    if (!animate) {
        m_sceneEffect->setOpacity(1.0);
        return;
    }

    // 换窗口时让画面轻轻淡进去：不然一整块内容瞬间替换，像闪了一下。
    m_sceneEffect->setOpacity(0.25);
    auto* fade = new QPropertyAnimation(m_sceneEffect, "opacity", this);
    fade->setDuration(150);
    fade->setStartValue(0.25);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::onCardClicked(WindowId id) {
    if (id == 0 || id == m_selected) {
        return;
    }
    m_selected = id;
    m_cards->setSelected(id);
    showSelected();
}

void MainWindow::onCardActivated(WindowId id) {
    m_selected = id;
    m_cards->setSelected(id);
    showSelected();
    onActivate();
}

void MainWindow::onActivate() {
    if (m_selected == 0) {
        setStatus(QStringLiteral("先点一个窗口，再定位。"), true);
        return;
    }
    m_service->activateWindow(m_selected);
}

void MainWindow::onRefresh() {
    m_statusTakenByRefresh = false;
    m_service->refresh();
    if (!m_statusTakenByRefresh) {
        setStatus(QStringLiteral("已刷新，共 %1 个窗口。").arg(m_windows.size()));
    }
}

void MainWindow::openTaskView() {
    // 右键展开 Task View：全屏、模糊背景、最多 4 个子窗口铺开。
    new TaskView(this, m_windows, m_selected);
}

void MainWindow::collapse() {
    if (!isVisible()) {
        return;
    }

    auto* fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setDuration(140);
    fade->setStartValue(windowOpacity());
    fade->setEndValue(0.0);
    fade->setEasingCurve(QEasingCurve::InCubic);
    connect(fade, &QPropertyAnimation::finished, this, [this] {
        setWindowOpacity(1.0);
        if (m_allowHide) {
            hide();  // 全局热键已接入，可以用 Ctrl+Alt+M 叫回来
        } else {
            // 没有全局热键时隐藏等于"窗口消失且无法找回"，所以退到任务栏。
            // 这是交接文档要求的那条回退路径，不是临时凑合。
            showMinimized();
        }
    });
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        collapse();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
}

void MainWindow::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    if (!m_firstShow) {
        setWindowOpacity(1.0);
        return;
    }

    // 第一次出现时淡入：M4 说的"打开总览有过渡"就落在这里。
    m_firstShow = false;
    setWindowOpacity(0.0);
    auto* fade = new QPropertyAnimation(this, "windowOpacity", this);
    fade->setDuration(180);
    fade->setStartValue(0.0);
    fade->setEndValue(1.0);
    fade->setEasingCurve(QEasingCurve::OutCubic);
    fade->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);

    const int barWidth = qBound(kBarMinWidth, width() * 22 / 100, kBarMaxWidth);
    if (m_cards->width() != barWidth) {
        m_cards->setFixedWidth(barWidth);
    }
    layoutStatus();
}

void MainWindow::layoutStatus() {
    if (!m_statusLabel || !m_canvas) {
        return;
    }
    const QRect area = m_canvas->geometry();
    const QSize hint = m_statusLabel->sizeHint();
    const int width = qMin(hint.width(), qMax(220, area.width() - 48));
    m_statusLabel->setGeometry(area.left() + 20, area.bottom() - 20 - 30, width, 30);
    m_statusLabel->raise();
}

void MainWindow::setStatus(const QString& text, bool error) {
    m_statusLabel->setText(text);
    m_statusLabel->setStyleSheet(error ? QStringLiteral("color:#ff6b60;")
                                       : QStringLiteral("color:#b9bfc8;"));
    m_statusLabel->setVisible(!text.isEmpty());
    layoutStatus();
}

const WindowItem* MainWindow::findWindow(WindowId id) const {
    for (const WindowItem& window : m_windows) {
        if (window.id == id) {
            return &window;
        }
    }
    return nullptr;
}

}  // namespace crossdesk::ui
