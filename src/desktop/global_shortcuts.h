#pragma once
#include <QObject>
#include <QAbstractNativeEventFilter>
#include <QKeySequence>
#include <QMap>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <memory>
namespace crossdesk::desktop {
class GlobalShortcuts final : public QObject, public QAbstractNativeEventFilter {
public:
    explicit GlobalShortcuts(QWidget& destination);
    ~GlobalShortcuts() override;
    QStringList configure(const QMap<QString, QKeySequence>& bindings);
    std::function<void(const QString&)> triggered;
    bool nativeEventFilter(const QByteArray&, void*, qintptr*) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
