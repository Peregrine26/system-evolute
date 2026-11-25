#pragma once
#include <QDate>
#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>
namespace crossdesk::desktop {
struct WeatherRow { QString time; double high = 0, low = 0, rain = 0; int code = -1; };
struct WeatherForecast {
    bool valid = false;
    double temperature = 0, humidity = 0, wind = 0;
    int code = -1;
    QList<WeatherRow> hourly, daily;
    QString observed, timezone;
};
WeatherForecast parseForecast(const QJsonObject&);
QString weatherDescription(int code);
QString weatherSymbol(int code, int style = 0);
QString solarTerm(const QDate&);
QString calendarEvents(const QDate&, const QString& lunar);
QString mediaTime(qint64 ticks);
}
