#pragma once

#include "crossdesk_service.h"
#include "window_item.h"

#include <QHash>
#include <QVector>
#include <QWidget>

#include <functional>

class QGraphicsOpacityEffect;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QRadioButton;

namespace crossdesk::ui {

class LayoutPreview;

// 主窗口：左边一条任务栏，右边一整块主屏幕画面，铺满整个显示器。
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

    bool isAnimating() const { return m_animating; }
    bool isArranging() const { return m_arranging; }

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void buildUi();
    void connectService();

    void applyWindows();
    void rebuildList();
    QString textFor(const WindowItem& window) const;
    void refreshRow(WindowId id);
    void updateActionStates();
    void updateScene();
    void fitRows();
    void layoutOverlay();

    void onItemChanged(QListWidgetItem* item);
    void onRefresh();
    void onActivate();
    void onConfirm();
    void onBack();
    void onModeChanged();
    void onOperationFinished(const OperationResult& result);

    void enterArrangement();
    void leaveArrangement();
    bool buildPlacements();
    void finishConfirm();
    LayoutMode currentMode() const;

    // 画面切换动画：先淡出，在透明的那一瞬间换数据，再淡入。
    void fadeScene(const std::function<void()>& swap);

    void setStatus(const QString& text, bool error = false);

    const WindowItem* findWindow(WindowId id) const;

    CrossDeskService* m_service = nullptr;
    QVector<WindowItem> m_windows;
    QVector<WindowId> m_checked;  // 按勾选先后排序，最多两个
    QVector<Placement> m_placements;
    QHash<WindowId, QString> m_failures;

    QListWidget* m_list = nullptr;
    LayoutPreview* m_canvas = nullptr;  // 右侧整块主屏幕

    // 浮在画面上的操作条：不占布局，没得点的时候整条隐藏。
    QWidget* m_actionBar = nullptr;
    QPushButton* m_activateButton = nullptr;
    QPushButton* m_confirmButton = nullptr;
    QPushButton* m_backButton = nullptr;
    QRadioButton* m_equalRadio = nullptr;
    QRadioButton* m_masterRadio = nullptr;
    QLabel* m_statusLabel = nullptr;

    QGraphicsOpacityEffect* m_sceneEffect = nullptr;

    bool m_syncingChecks = false;  // 防止程序改勾选状态时递归触发 itemChanged
    bool m_animating = false;      // 动画期间不接受新命令，避免重复触发
    bool m_arranging = false;      // 当前画面是"排列预览"还是"桌面总览"
    bool m_allowHide = false;
    bool m_firstShow = true;
    bool m_statusTakenByRefresh = false;
    int m_confirmPending = 0;
    int m_confirmFailures = 0;
};

}  // namespace crossdesk::ui
