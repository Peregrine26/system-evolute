#pragma once

#include "crossdesk_service.h"
#include "window_item.h"

#include <QHash>
#include <QVector>
#include <QWidget>

class QGraphicsOpacityEffect;
class QLabel;
class QShowEvent;

namespace crossdesk::ui {

class CardStack;
class LayoutPreview;

// 主窗口：左边一摞窗口卡，右边是选中的那个窗口，铺满整个显示器。
//
// 两条硬规则写在代码里，答辩时可以直接指着讲：
//   1. 界面只认 CrossDeskService，不 include windows.h，不自己算窗口归属；
//   2. 界面不保存第二份"窗口在哪"的真相——每次刷新都重新读服务，失败状态也只记
//      "哪个窗口失败 + 原因"，位置永远以服务为准。
class MainWindow : public QWidget {
    Q_OBJECT

public:
    explicit MainWindow(CrossDeskService* service, QWidget* parent = nullptr);

    // 是否允许真正隐藏窗口。只有 A 的全局热键接入后才应打开，
    // 否则窗口一收起就从任务栏消失，用户再也叫不回来。
    void setAllowHide(bool allow);

    // 收起：Esc 或全局热键触发。
    void collapse();

    WindowId selectedWindow() const { return m_selected; }

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    void connectService();

    void applyWindows();
    void onCardClicked(WindowId id);
    void onCardActivated(WindowId id);  // 双击 = 把真实窗口切到前台
    void onRefresh();
    void onActivate();

    void showSelected(bool animate = true);
    void setStatus(const QString& text, bool error = false);
    void layoutStatus();

    const WindowItem* findWindow(WindowId id) const;

    CrossDeskService* m_service = nullptr;
    QVector<WindowItem> m_windows;
    WindowId m_selected = 0;
    QHash<WindowId, QString> m_failures;

    CardStack* m_cards = nullptr;
    LayoutPreview* m_canvas = nullptr;
    QLabel* m_statusLabel = nullptr;
    QGraphicsOpacityEffect* m_sceneEffect = nullptr;

    bool m_allowHide = false;
    bool m_firstShow = true;
    bool m_statusTakenByRefresh = false;
};

}  // namespace crossdesk::ui
