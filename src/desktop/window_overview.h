#pragma once
#include <QWidget>
#include <memory>
namespace crossdesk::desktop {
class WindowOverview final : public QWidget {
public:
    explicit WindowOverview(QWidget* parent = nullptr);
    ~WindowOverview() override;
protected:
    void resizeEvent(QResizeEvent*) override;
    void showEvent(QShowEvent*) override;
    void hideEvent(QHideEvent*) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
