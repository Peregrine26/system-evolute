# A 对 C 卡片总览的接入说明

对应 C 的 [PR #1](https://github.com/Peregrine26/system-evolute/pull/1)，核对版本为
`5de55c0b083c4eb17c936e40aacfdd5b69bfd536`。本次仅更新 A 的桥接层、实验和测试，
不修改 C 的 `src/ui/`、角色分支或 PR。产品范围仍以课程需求为准；当前 C 的单选总览
不等于两窗排列、实时预览等全部需求已完成。

## 将 C 的控件交给 A

`WindowCard` 和 `ThumbnailSlot` 是子控件，不能把它们的 `hostHandle()` 返回值直接
作为 DWM 目标。A 现在接收控件和内容矩形，使用主窗口的顶层句柄注册，不创建子 HWND：

```cpp
crossdesk::QtDesktopBridge bridge(window); // window 是 C 的顶层 MainWindow
const auto results = bridge.set_widget_previews({
    {card->windowId(), card, cardContentRect},
    {pane->windowId(), pane, paneContentRect}
});
```

- `QtWidgetPreview::source`：真实来源窗口标识；假数据编号不能注册 DWM。
- `widget`：调用期间存活、属于这个主窗口的控件；A 不保留指针，也不取得控件所有权。
- `content`：控件自身坐标中的逻辑像素矩形。C 提供不含标题、进程名、按钮、角标与
  浮动状态文字的区域；不要传 `screenRect()`，不要自行乘 DPI 比例。
- A 将矩形映射到主窗口客户区，然后统一转原生像素并保持来源宽高比。
- 返回值与输入一一对应。单个空指针、错误矩形、失效/隐藏来源不会阻止其他合法区域注册。

`map_preview_region(widget, content, mapped)` 可独立预检，返回主窗口客户区逻辑矩形，
不会调用 DWM、不会调用子控件的 `winId()`。外国顶层窗口返回 `InvalidWindow`，
空/越界内容返回 `InvalidRectangle`；隐藏、裁切、被较高兄弟控件遮挡或正在淡入的区域
返回 `Unavailable`。部分遮挡时保守保留占位，不把预览缩进标题带，也不盖住上层卡片。

例如当前 C 卡片的画面位于 `(12, 12)`，宽为 `width - 24`，高为 `height - 66`；
底部留出 46 像素标题/进程名和 8 像素间隔。这个计算属于 C 的绘制规则，不写死在 A 的
生产代码中。C 改版后应由 C 同步内容矩形。右侧预览还须避开底部浮动状态条；不应直接
把整个 `ThumbnailSlot::rect()` 当作实时画面区。

## 选择、移动与动画

C 完成选择更新、窗口刷新、resize、DPI 变化或卡片移动后，重新计算内容矩形并调用
`set_widget_previews`。接口是一次性的提交，不自动跟踪控件，也不缓存 C 的选择状态。
拖动/悬停等几何动画可每帧重提交；另一种做法是动画前 `clear_previews()`，动画完成后
重建。实验使用定时器更新，仅供联调，不要求正式界面轮询。

DWM 画面由系统合成，不是 QPixmap，不通过 C 的 `setPreview()` 传递，也不会自动参与
Qt 的透明度、遮罩或特效。动画开始前先清理旧注册，结束后再提交。桥接层还会在每次
提交时拒绝隐藏/最小化/淡入中的主窗口和处于 Qt 淡入祖先下的区域；这不能替代动画
开始时的主动清理。C 的旧标题/进程名占位始终保留，A 的注册失败不删除列表项。

## 全局快捷键与收起回退

```cpp
bridge.hotkey_state_changed = [&](crossdesk::DesktopResult state) {
    window.setAllowHide(state.ready());
};
bridge.toggle_overview = [&] {
    bridge.clear_previews();
    if (window.isVisible() && !window.isMinimized()) {
        window.collapse();
    } else {
        window.showFullScreen();
        window.raise();
        window.activateWindow();
        // 在打开动画完成后提交控件预览。
    }
};
const auto key = bridge.register_hotkey();
// !key.ready() 时显示占用/失败提示，C 保持最小化到任务栏的回退。
```

新增 `hotkey_state()` 查询与 `unregister_hotkey()` 显式释放。注册结果、显式释放以及
原生句柄变化都会通知 `hotkey_state_changed`，避免 C 在快捷键失效后仍允许隐藏。
回调只更新能力状态，不在回调内创建/重建原生窗口。若句柄失效时窗口已隐藏，应将
重新显示到任务栏的回退排入 Qt 事件循环。原生窗口确需重建时，等重建完成后显式
重新注册快捷键；A 不自动重试或抢占组合键。

保持 bridge 比主窗口先析构。不要让回调捕获已经销毁的 UI；析构只释放资源，不再调用
状态回调。主窗口 Hide/Close 时清理预览，隐藏期间重复定时提交也不会重新注册。

## 扫描与窗口命令继续使用 A 的原接口

C 的临时 `LiveService` / `Win32WindowCommands` 不成为另一套平台实现：

- 刷新使用 `WindowsWindowScanner::scan_with_status()`；不完整扫描不能当作所有窗口关闭。
- 工作区使用 `WindowApiAdapter::get_primary_work_area()`，与实际窗口矩形保持相同坐标系。
- 定位和移动分别走 `activate_window()` / `set_window_bounds()`；请求被接受不等于成功。
- 两窗移动仍走 A 的 `LayoutExecutor` 并延时观察，B 的筛选和布局计算职责保持不变。

这些接口已经存在，本次不新增 Workspace、不改变 C 的选择/卡片交互，也不把 C 的
10 项截断、双击错位、动画竞争和测试 PID 问题悄悄移进 A 修复。

## 构建与验证

默认 A 构建不依赖 C 的未合并代码：

```powershell
.\scripts\build.ps1 -DesktopTests
. .\scripts\dev-env.ps1
.\build-local\crossdesk_qt_dwm_demo.exe --fullscreen --smoke
```

可用另一个干净检出目录验证 C 的原版界面；该目录只读使用，不能指向带有未经核对
修改的共享 checkout。路径由执行者填写，版本核对为上面的 C SHA：

```powershell
cmake -S . -B build-local -DCROSSDESK_C_UI_SOURCE_DIR="<C 的干净检出目录>"
cmake --build build-local --parallel 4
ctest --test-dir build-local -R "^c_ui_bridge_" --output-on-failure
```

默认共 8 项测试：含控件坐标、150% 缩放、遮挡/淡入/隐藏降级、快捷键失效通知及两个
自有来源的 DWM 注册。启用 C 检出目录后共 10 项，增加原版 C 的离屏映射与交互桌面
注册：两张卡片和右侧选中窗共三处关系（同一来源可同时出现在卡片和右侧）。

Qt CI 固定检出上述 C SHA 作为测试夹具，编译原版 C 控件，运行非桌面兼容测试；
不合并它的分支、不执行它的窗口控制测试。交互测试仅编译，开发机运行时只操作
自己创建的来源。注册通过不证明实际像素持续更新、动画手感或真实全局按键通过，
这些仍需演示机验收；本机快捷键被占用时只验证失败反馈与任务栏回退。
