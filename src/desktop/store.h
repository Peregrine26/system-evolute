#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QDateTime>
#include <QSet>

namespace crossdesk::desktop {

// A Qt-native persistence model. Widget IDs survive reordering and restart.
class Store final : public QObject {
    Q_OBJECT
public:
    explicit Store(QString directory, QObject* parent = nullptr);
    bool load(QString* error = nullptr);
    bool save(QString* error = nullptr);
    QString directory() const { return directory_; }
    QJsonArray widgets() const { return state_["widgets"].toArray(); }
    QJsonObject widget(const QString& id) const;
    QString addWidget(const QString& kind, const QString& title,
                      const QJsonObject& data = {});
    bool putWidget(const QJsonObject& widget, QString* error = nullptr);
    bool removeWidget(const QString& id);
    QJsonArray records(const QString& key) const { return state_[key].toArray(); }
    bool setRecords(const QString& key, const QJsonArray& records);
    QString convertRecord(const QString& source, const QString& destination, const QString& id);
    QJsonObject settings() const { return state_["settings"].toObject(); }
    bool setSettings(const QJsonObject& settings);
    bool exportBackup(const QString& file, QString* error = nullptr) const;
    bool importBackup(const QString& file, QString* error = nullptr);
    static bool validate(const QJsonObject& state, QString* error = nullptr);
signals:
    void changed();
    void persistenceFailed(const QString& error);
private:
    bool commit(const QJsonObject& next, QString* error = nullptr);
    QString directory_;
    QJsonObject state_;
};

// These operations never overwrite a target or delete a source after a failed
// copy. Move uses rename; cross-volume folder moves report failure explicitly.
struct FileResult { bool ok = false; QString error; QString destination; };
FileResult copyPath(const QString& source, const QString& destination);
FileResult movePath(const QString& source, const QString& destination);
QString categoryFor(const QString& path);
bool recordMove(Store& store, const QString& source, const QString& destination);
struct Reminder { QString key; QString title; };
QList<Reminder> dueReminders(const QJsonArray& tasks, const QDateTime& now,
                            const QSet<QString>& acknowledged = {});

} // namespace crossdesk::desktop
