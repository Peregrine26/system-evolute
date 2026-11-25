#include "desktop/page_factories.h"
#include "desktop/utility_data.h"
#include "desktop/media_session.h"
#include "desktop/language.h"
#include <QCoreApplication>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLabel>
#include <QTableWidget>
#include <QTemporaryDir>
#include <iostream>
#include <memory>
using namespace crossdesk::desktop;
bool check(bool value, const char* description) { if (!value) std::cerr << description << '\n'; return value; }
int main(int argc, char** argv) {
    QApplication app(argc, argv); qputenv("CROSSDESK_OFFLINE", "1"); bool ok = true;
    if (app.arguments().contains("--media-readonly")) {
        const auto snapshot = readMediaSession({}, {});
        std::cout << "Native media read: sessions=" << snapshot.sessions.size() << "; available=" << snapshot.available << "; coverBytes=" << snapshot.cover.size() << '\n';
        const auto missing = readMediaSession("CrossDesk-fixture-1b9d6abc-nonexistent", {});
        return !missing.available && !missing.error.isEmpty() ? 0 : 1;
    }
    const QJsonObject fixture{{"timezone", "Asia/Shanghai"}, {"current", QJsonObject{{"time", "2026-11-25T10:00"}, {"temperature_2m", 18.5}, {"weather_code", 3}, {"relative_humidity_2m", 60}, {"wind_speed_10m", 8}}},
        {"hourly", QJsonObject{{"time", QJsonArray{"2026-11-25T10:00", "2026-11-25T11:00"}}, {"temperature_2m", QJsonArray{18.5, 20}}, {"weather_code", QJsonArray{3, 1}}, {"precipitation_probability", QJsonArray{20, 10}}}},
        {"daily", QJsonObject{{"time", QJsonArray{"2026-11-25"}}, {"temperature_2m_min", QJsonArray{10}}, {"temperature_2m_max", QJsonArray{20}}, {"weather_code", QJsonArray{3}}, {"precipitation_probability_max", QJsonArray{30}}}}};
    const auto parsed = parseForecast(fixture);
    ok &= check(parsed.valid && parsed.temperature == 18.5 && parsed.hourly.size() == 2 && parsed.daily.size() == 1 && parsed.daily[0].low == 10, "weather payload parsing failed");
    ok &= check(!parseForecast({}).valid && !parseForecast({{"current", QJsonObject{{"temperature_2m", "unknown"}, {"weather_code", 0}}}}).valid, "malformed weather payload accepted as real zero-temperature weather");
    auto truncated = fixture; auto hourly = fixture["hourly"].toObject(); hourly["weather_code"] = QJsonArray{3}; truncated["hourly"] = hourly;
    ok &= check(parseForecast(truncated).hourly.size() == 1, "mismatched forecast arrays were indexed beyond available values");
    ok &= check(weatherDescription(95).contains(QStringLiteral("雷")) && weatherDescription(71).contains(QStringLiteral("雪")) && weatherDescription(999).contains(QStringLiteral("未知")), "WMO weather descriptions incorrect");
    ok &= check(solarTerm(QDate(2026, 3, 20)) == QStringLiteral("春分") && solarTerm(QDate(2026, 6, 21)) == QStringLiteral("夏至") && solarTerm(QDate(2026, 12, 22)) == QStringLiteral("冬至"), "solar term dates incorrect");
    ok &= check(calendarEvents(QDate(2026, 2, 17), QStringLiteral("农历 正月初一")).contains(QStringLiteral("春节")) && !calendarEvents(QDate(2026, 2, 17), QStringLiteral("农历 闰正月初一")).contains(QStringLiteral("春节")), "lunar festival incorrectly handles leap month");
    ok &= check(mediaTime(650000000) == "1:05" && mediaTime(-1) == "0:00", "media tick formatting incorrect");
    QTemporaryDir root; Store store(root.path()); ok &= store.load(); const auto id = store.addWidget("weather", "weather", {{"city", "Shanghai"}});
    QDir().mkpath(root.path() + "/cache"); QFile cache(root.path() + "/cache/weather-" + id + ".json");
    cache.open(QIODevice::WriteOnly); cache.write(QJsonDocument(QJsonObject{{"forecastCache", fixture}, {"forecastCity", "Shanghai"}, {"forecastFetchedAt", "2026-11-25T01:00:00Z"}}).toJson()); cache.close();
    std::unique_ptr<QWidget> weather(createUtilityPage(store, id, [](const QString&) {}));
    auto* summary = weather->findChild<QLabel*>("weatherSummary"); auto* hours = weather->findChild<QTableWidget*>("hourlyWeather"); auto* days = weather->findChild<QTableWidget*>("dailyWeather");
    ok &= check(summary && summary->text().contains("18.5") && hours && hours->rowCount() == 2 && days && days->rowCount() == 1, "offline cached weather not restored into Qt UI");
    const auto clockId = store.addWidget("clock", "clock"); std::unique_ptr<QWidget> clock(createUtilityPage(store, clockId, [](const QString&) {}));
    ok &= check(clock->findChild<QLabel*>("clockTime") && !clock->findChild<QLabel*>("clockTime")->text().isEmpty(), "clock page did not initialize");
    const auto musicId = store.addWidget("music", "music"); std::unique_ptr<QWidget> music(createUtilityPage(store, musicId, [](const QString&) {}));
    ok &= check(music->findChild<QWidget*>("mediaSessions") && music->findChild<QWidget*>("mediaPosition") && !music->findChild<QWidget*>("mediaPosition")->isEnabled(), "music controls should be disabled before a capable session is loaded");
    initializeLanguage("en");
    ok &= check(QCoreApplication::translate("CrossDesk", "文件格子") == "Files" && QCoreApplication::translate("CrossDesk", "完成所选") == "Complete selected", "English catalog not installed");
    return ok ? 0 : 1;
}
