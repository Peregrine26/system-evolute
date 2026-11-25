#pragma once
#include <QWidget>
#include <memory>
#include <functional>
class QCloseEvent;
namespace crossdesk { class WindowApiAdapter; }
namespace crossdesk::desktop {
class WindowOverview final : public QWidget {
public:
    explicit WindowOverview(QWidget* parent = nullptr);
    WindowOverview(const WindowApiAdapter& api, QWidget* parent = nullptr);
    ~WindowOverview() override;
    void showOverview();
    void toggleOverview();
    void setShortcutStatus(const QString& message);
    std::function<void()> openDesktopTools;
    std::function<void()> closeRequested;
protected:
    void resizeEvent(QResizeEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
    void closeEvent(QCloseEvent*) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
