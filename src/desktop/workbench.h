#pragma once
#include "desktop/store.h"
#include <QMainWindow>
#include <QPointer>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QSet>
#include <QStringList>
#include <functional>

class QListWidget;
class QLabel;
class QCloseEvent;

namespace crossdesk::desktop {
class WidgetFrame;
class Workbench final : public QMainWindow {
public:
    explicit Workbench(Store& store, QWidget* parent = nullptr);
    ~Workbench() override;
    void toggleOverview();
    void notify(const QString& message);
    void setShortcutStatus(const QString& message);
    void shortcutAction(const QString& action);
    std::function<void()> openWindowOverview;
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    void create(const QString& kind);
    void refreshList();
    void rebuildFrames();
    void showSelected();
    void groupSelected(bool group);
    void preferences();
    void organize();
    void undoOrganization();
    void importBackup();
    void reminders();
    void restoreScreenLayout();
    void applyTheme();
    QStringList selectedIds() const;
    Store& store_;
    QListWidget* list_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* shortcut_ = nullptr;
    QSystemTrayIcon tray_;
    QList<QPointer<WidgetFrame>> frames_;
    QTimer reminderTimer_;
    QSet<QString> notified_;
    bool quitting_ = false;
};
} // namespace crossdesk::desktop
