#pragma once

#include "crossdesk_service.h"
#include "window_item.h"

#include <QHash>
#include <QVector>
#include <QWidget>

class QGraphicsOpacityEffect;
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QRadioButton;
class QStackedWidget;

namespace crossdesk::ui {

class LayoutPreview;
class ThumbnailSlot;

// 主窗口：一页总览 + 一页左右排列预览。
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

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void buildUi();
    QWidget* buildOverviewPage();
    QWidget* buildPreviewPage();
    void connectService();

    void applyWindows();
    void rebuildList();
    QString textFor(const WindowItem& window) const;
    void refreshRow(WindowId id);
    void updateActionStates();
    void updateThumbnails();

    void onItemChanged(QListWidgetItem* item);
    void onRefresh();
    void onActivate();
    void onArrange();
    void onConfirm();
    void onBack();
    void onModeChanged();
    void onOperationFinished(const OperationResult& result);

    bool buildPlacements();
    void finishConfirm();
    LayoutMode currentMode() const;

    void fadeToPage(int index);
    void setStatus(const QString& text, bool error = false);

    const WindowItem* findWindow(WindowId id) const;

    CrossDeskService* m_service = nullptr;
    QVector<WindowItem> m_windows;
    QVector<WindowId> m_checked;  // 按勾选先后排序，最多两个
    QVector<Placement> m_placements;
    QHash<WindowId, QString> m_failures;

    QStackedWidget* m_pages = nullptr;
    QListWidget* m_list = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QPushButton* m_activateButton = nullptr;
    QPushButton* m_arrangeButton = nullptr;
    QPushButton* m_confirmButton = nullptr;
    QPushButton* m_backButton = nullptr;
    QRadioButton* m_equalRadio = nullptr;
    QRadioButton* m_masterRadio = nullptr;
    QLabel* m_statusLabel = nullptr;
    LayoutPreview* m_screenPreview = nullptr;  // 总览右侧的"主屏幕"
    LayoutPreview* m_preview = nullptr;        // 排列页的确认预览
    QGraphicsOpacityEffect* m_pageEffect = nullptr;

    bool m_syncingChecks = false;   // 防止程序改勾选状态时递归触发 itemChanged
    bool m_animating = false;       // 动画期间不接受新命令，避免重复触发
    bool m_allowHide = false;
    bool m_firstShow = true;
    bool m_statusTakenByRefresh = false;
    int m_confirmPending = 0;
    int m_confirmFailures = 0;
};

}  // namespace crossdesk::ui
