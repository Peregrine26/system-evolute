#include <QCoreApplication>
#include "desktop/utility_data.h"
#include <QJsonArray>
#include <QMap>
#include <QStringList>
#include <cmath>
#include <algorithm>
namespace crossdesk::desktop {
WeatherForecast parseForecast(const QJsonObject& data) {
    WeatherForecast result; const auto current = data["current"].toObject();
    if (!current["temperature_2m"].isDouble() || !current["weather_code"].isDouble()) return result;
    result.valid = true; result.temperature = current["temperature_2m"].toDouble();
    result.humidity = current["relative_humidity_2m"].toDouble(); result.wind = current["wind_speed_10m"].toDouble();
    result.code = current["weather_code"].toInt(-1); result.observed = current["time"].toString(); result.timezone = data["timezone"].toString();
    for (bool hourly : {true, false}) {
        const auto section = data[hourly ? "hourly" : "daily"].toObject();
        const auto times = section["time"].toArray(), codes = section["weather_code"].toArray();
        const auto highs = section[hourly ? "temperature_2m" : "temperature_2m_max"].toArray();
        const auto lows = hourly ? highs : section["temperature_2m_min"].toArray();
        const auto rain = section[hourly ? "precipitation_probability" : "precipitation_probability_max"].toArray();
        const auto count = std::min({times.size(), highs.size(), lows.size(), codes.size()});
        for (int i = 0; i < count; ++i) {
            if (!times[i].isString() || !highs[i].isDouble() || !lows[i].isDouble() || !codes[i].isDouble()) continue;
            WeatherRow row{times[i].toString(), highs[i].toDouble(), lows[i].toDouble(), i < rain.size() ? rain[i].toDouble() : 0, codes[i].toInt(-1)};
            if (hourly) result.hourly.append(row); else result.daily.append(row);
        }
    }
    return result;
}
QString weatherDescription(int code) {
    if (code == 0) return QCoreApplication::translate("CrossDesk", "晴");
    if (code == 1) return QCoreApplication::translate("CrossDesk", "晴间多云"); if (code == 2) return QCoreApplication::translate("CrossDesk", "多云"); if (code == 3) return QCoreApplication::translate("CrossDesk", "阴");
    if (code == 45 || code == 48) return QCoreApplication::translate("CrossDesk", "雾");
    if (code >= 51 && code <= 57) return QCoreApplication::translate("CrossDesk", "毛毛雨");
    if (code >= 61 && code <= 67) return QCoreApplication::translate("CrossDesk", "雨");
    if (code >= 71 && code <= 77) return QCoreApplication::translate("CrossDesk", "雪");
    if (code >= 80 && code <= 82) return QCoreApplication::translate("CrossDesk", "阵雨");
    if (code == 85 || code == 86) return QCoreApplication::translate("CrossDesk", "阵雪");
    if (code >= 95 && code <= 99) return QCoreApplication::translate("CrossDesk", "雷雨"); return QCoreApplication::translate("CrossDesk", "天气未知");
}
QString weatherSymbol(int code, int style) {
    if (style == 3) return weatherDescription(code);
    const int type = code == 0 ? 0 : code <= 3 ? 1 : code == 45 || code == 48 ? 2 : code >= 95 ? 5 : (code >= 71 && code <= 77) || code == 85 || code == 86 ? 4 : 3;
    static const QStringList symbols{QStringLiteral("☀"), QStringLiteral("☁"), QStringLiteral("≋"), QStringLiteral("☂"), QStringLiteral("❄"), QStringLiteral("ϟ")};
    static const QStringList emojis{QStringLiteral("☀️"), QStringLiteral("☁️"), QStringLiteral("🌫️"), QStringLiteral("🌧️"), QStringLiteral("🌨️"), QStringLiteral("⛈️")};
    return style == 1 ? emojis[type] : style == 2 ? '[' + symbols[type] + ']' : symbols[type];
}
namespace {
double longitude(double julian) {
    // Meeus/NOAA apparent solar longitude. Used only for a calendar date label.
    constexpr double rad = 3.14159265358979323846 / 180;
    const double t = (julian - 2451545.0) / 36525;
    const double mean = 280.46646 + t * (36000.76983 + t * 0.0003032);
    const double anomaly = (357.52911 + t * (35999.05029 - 0.0001537 * t)) * rad;
    const double center = std::sin(anomaly) * (1.914602 - t * (0.004817 + 0.000014 * t)) +
        std::sin(2 * anomaly) * (0.019993 - 0.000101 * t) + std::sin(3 * anomaly) * 0.000289;
    const double apparent = mean + center - 0.00569 - 0.00478 * std::sin((125.04 - 1934.136 * t) * rad);
    double value = std::fmod(apparent, 360.0); return value < 0 ? value + 360 : value;
}
double difference(double a, double b) { return std::fmod(a - b + 540, 360) - 180; }
}
QString solarTerm(const QDate& date) {
    if (!date.isValid() || date.year() < 1901 || date.year() > 2099) return {};
    static const QStringList names{QCoreApplication::translate("CrossDesk", "小寒"), QCoreApplication::translate("CrossDesk", "大寒"), QCoreApplication::translate("CrossDesk", "立春"), QCoreApplication::translate("CrossDesk", "雨水"), QCoreApplication::translate("CrossDesk", "惊蛰"), QCoreApplication::translate("CrossDesk", "春分"), QCoreApplication::translate("CrossDesk", "清明"), QCoreApplication::translate("CrossDesk", "谷雨"), QCoreApplication::translate("CrossDesk", "立夏"), QCoreApplication::translate("CrossDesk", "小满"), QCoreApplication::translate("CrossDesk", "芒种"), QCoreApplication::translate("CrossDesk", "夏至"), QCoreApplication::translate("CrossDesk", "小暑"), QCoreApplication::translate("CrossDesk", "大暑"), QCoreApplication::translate("CrossDesk", "立秋"), QCoreApplication::translate("CrossDesk", "处暑"), QCoreApplication::translate("CrossDesk", "白露"), QCoreApplication::translate("CrossDesk", "秋分"), QCoreApplication::translate("CrossDesk", "寒露"), QCoreApplication::translate("CrossDesk", "霜降"), QCoreApplication::translate("CrossDesk", "立冬"), QCoreApplication::translate("CrossDesk", "小雪"), QCoreApplication::translate("CrossDesk", "大雪"), QCoreApplication::translate("CrossDesk", "冬至")};
    for (int index : {(date.month() - 1) * 2, (date.month() - 1) * 2 + 1}) {
        const double target = std::fmod(285 + index * 15.0, 360.0);
        double low = QDate(date.year(), date.month(), index % 2 ? 16 : 1).toJulianDay() - 0.5;
        double high = low + 15;
        for (int pass = 0; pass < 45; ++pass) { const double middle = (low + high) / 2; if (difference(longitude(middle), target) < 0) low = middle; else high = middle; }
        // Traditional Chinese calendar uses UTC+8, independent of the PC time zone.
        const auto day = QDate::fromJulianDay(qint64(std::floor((low + high) / 2 + 0.5 + 8.0 / 24)));
        if (day == date) return names[index];
    }
    return {};
}
QString calendarEvents(const QDate& date, const QString& lunar) {
    QStringList names; const auto term = solarTerm(date); if (!term.isEmpty()) names << term;
    const QString key = QString::number(date.month()) + '-' + QString::number(date.day());
    const QMap<QString, QString> fixed{{"1-1", QCoreApplication::translate("CrossDesk", "元旦")}, {"2-14", QCoreApplication::translate("CrossDesk", "情人节")}, {"3-8", QCoreApplication::translate("CrossDesk", "妇女节")}, {"5-1", QCoreApplication::translate("CrossDesk", "劳动节")}, {"6-1", QCoreApplication::translate("CrossDesk", "儿童节")}, {"10-1", QCoreApplication::translate("CrossDesk", "国庆节")}, {"12-25", QCoreApplication::translate("CrossDesk", "圣诞节")}};
    if (fixed.contains(key)) names << fixed[key];
    if (!lunar.contains(QCoreApplication::translate("CrossDesk", "闰"))) {
        const QMap<QString, QString> festivals{{QCoreApplication::translate("CrossDesk", "正月初一"), QCoreApplication::translate("CrossDesk", "春节")}, {QCoreApplication::translate("CrossDesk", "正月十五"), QCoreApplication::translate("CrossDesk", "元宵节")}, {QCoreApplication::translate("CrossDesk", "五月初五"), QCoreApplication::translate("CrossDesk", "端午节")}, {QCoreApplication::translate("CrossDesk", "七月初七"), QCoreApplication::translate("CrossDesk", "七夕")}, {QCoreApplication::translate("CrossDesk", "八月十五"), QCoreApplication::translate("CrossDesk", "中秋节")}, {QCoreApplication::translate("CrossDesk", "九月初九"), QCoreApplication::translate("CrossDesk", "重阳节")}, {QCoreApplication::translate("CrossDesk", "腊月初八"), QCoreApplication::translate("CrossDesk", "腊八节")}};
        for (auto it = festivals.cbegin(); it != festivals.cend(); ++it) if (lunar.endsWith(it.key())) names << it.value();
    }
    return names.join(QStringLiteral(" · "));
}
QString mediaTime(qint64 ticks) { const auto seconds = qMax(qint64(0), ticks / 10000000); return QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0')); }
}
