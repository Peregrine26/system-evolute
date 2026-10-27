# A 板块接口与联调记录

2026-10-20：A 板块代码已在本机 Windows 上完整编译，5 项 CTest 均通过（含原生句柄销毁后的离屏回归），原生与 Qt DWM 双预览注册冒烟通过。Qt 实际画面已确认双来源持续更新、resize 后位置正确、关闭一个来源时另一个继续更新，关闭目标窗口正常退出。全局快捷键在本机被占用（Win32 1409），占用提示路径通过，实际按键切换仍待空闲环境验证。B 的算法、C 的正式 UI 仍由对应模块提供。

## API 含义

- `EnumWindows` 枚举顶层窗口，数量远多于普通应用。扫描不完整时不能用缺席条目判断窗口关闭。
- `HWND` 会失效和复用。共享接口保持整数窗口标识，不向 B/C 暴露 Win32 类型。执行器同时核对 PID，但 HWND+PID 仍非永久身份标识，同进程复用仍有竞态。
- `SetForegroundWindow` 受 Windows 置前限制，可能拒绝；零错误码不代表成功。显式 `activate_window` 允许请求恢复最小化窗口，然后尝试置前；普通无激活显示不恢复最小化窗口。
- `SetWindowPos` 被接受不代表目标已到达。其他进程可能延迟响应、限制尺寸、保持最大化/最小化或拒绝请求；逐窗保存请求与观察结果。

参考：[置前限制](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setforegroundwindow)、[窗口位置](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-setwindowpos)。

## 给 B：共享接口和布局执行

`WindowApiAdapter` 新增两个带默认实现的虚函数，保留现有假适配器的编译兼容性：

- `get_primary_work_area(WindowBounds&)`：默认 false；Windows 读取主显示器扣除任务栏后的工作区，失败时不得硬编码屏幕尺寸替代。
- `is_manageable_window(handle)`：默认 true；Windows 排除桌面、任务栏、开始菜单/搜索壳层以及 cloaked 窗口，扫描器生成快照前调用。不会按 `explorer.exe`、`SystemSettings.exe` 或 `ApplicationFrameHost.exe` 排除系统应用。

工作区与窗口矩形须在同一线程 DPI 上下文获取和操作；B 不混入 Qt 逻辑坐标。仍限单显示器，不承诺复杂 DPI 场景。工作区来源：[SystemParametersInfoW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-systemparametersinfow)。

两种布局仍由 B 计算。将两个窗口标识、PID 和 B 的目标矩形包成两个 `WindowMoveTarget`，交给 `LayoutExecutor::submit`：

1. 新扫描不完整、标识重复、PID 不匹配或矩形无效时，不移动任何窗口。
2. 分别请求移动，不隐式回滚；`submitted` 只表示进入请求阶段。
3. 每窗 `request` 记录平台请求结果，`observation` 记录实际矩形。C 应同时判断两者，不把请求接受当作成功。
4. 用 UI 短定时器约 150 ms 后调用 `observe` 再扫描；仍不匹配应显示未达到目标或不支持，可有限重试观察，不在 UI 线程阻塞等待。

`tests/layout_executor_test.cpp` 用假窗口覆盖扫描失败、重复选择、PID 变化、无效矩形、延迟移动、逐窗拒绝及来源关闭，不操作桌面。`tests/qt_bridge_lifecycle_test.cpp` 使用 Qt 离屏平台，在销毁原生窗口后投递无关消息和未注册的快捷键消息，检查过滤器不会隐式重建窗口，并验证显式重建和再次销毁。这条反例在修复前的桥接代码上失败，在当前代码上通过；不注册全局快捷键、不使用 DWM、不操作桌面窗口。

CI 保留无 Qt 平台检查，并增加 Qt 6.8.3 / MSVC 2022 完整构建，运行上述两项非桌面测试；交互测试仍只编译。本地启用 Qt 后，CTest 另含三秒自动退出的 `qt_dwm_smoke`，覆盖真实 Qt/DWM 启动与退出；全部测试共五项，动态画面仍须实机观察。

## 给 C：快捷键与双预览

`desktop_services.h` 不含 Win32 类型。`OverviewHotkey` 在目标 UI 线程注册固定 `Ctrl+Alt+M`，禁用按住重复触发；失败返回 `Unavailable` 和 Win32 错误码。析构/reset 注销，同一目标只保留一个所有者。[注册与消息约束](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey)。

A 的 `QtDesktopBridge` 封装原生消息，C 无需解析 `MSG`：

```cpp
QMainWindow window;
// 初始化 UI，保持主窗口原生句柄稳定。
window.show();
crossdesk::QtDesktopBridge bridge(window); // 必须先于 window 析构
bridge.toggle_overview = [&] { /* 与总览按钮共用的命令 */ };
const auto hotkey = bridge.register_hotkey(); // 失败时提示，按钮继续可用
```

`set_previews` 接收来源标识和 Qt 矩形；用 `mapTo(&window, ...)` 转成主窗口客户区逻辑坐标，桥接层再转原生像素。预览保持来源宽高比，区域应避开标题和按钮。返回结果与请求一一对应；单窗失败显示标题/进程名占位，不删除列表项，不影响其他预览。DWM 的错误码是 HRESULT 位模式，不能按 Win32 错误解释。

刷新、选择变化、resize、DPI 变化后重调 `set_previews`，释放旧关系再注册。动画开始/收起时 `clear_previews`，结束后重建；隐藏、关闭和析构也会清理。原生 `WindowPreviews::refresh` 清理已消失或不再可见的来源，但不代替 UI 的扫描刷新和占位反馈；`replace` 对隐藏来源返回 `InvalidWindow`，其余来源独立注册。

Qt 桥接层在安装原生过滤器前缓存主窗口句柄，处理原生消息时只比较缓存值，不调用可能创建平台窗口的 `winId()`。原生句柄变化时释放旧资源并通过 `internalWinId()` 观察新句柄；若主窗口确需重建，C 应重新注册快捷键并更新预览。

DWM 持续更新内容，不需截图；注册成功本身不能证明画面正常更新。来源与目标都须顶层，目标属于本进程；缩略图句柄仅在 A 私有持有。参考：[注册约束](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmregisterthumbnail)、[预览属性](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ns-dwmapi-dwm_thumbnail_properties)。

## 开发机验证命令

构建、依赖、Kit 路径、非桌面/桌面 CTest 和演示启动命令统一见
[构建与测试](getting-started.md)，不在这里复制第二套命令。
已有实机记录使用 Windows / Qt 6.8.3 / MinGW 13.1.0 / CMake 4.4.4 /
Ninja 1.13.2；会话脚本可通过 `CROSSDESK_TOOLCHAIN_ROOT` 指定工具包，
不写系统 PATH。该环境记录不表示每台机器已安装或验收。

原生实验创建两个每 100 ms 更新内容的来源和一个预览窗口。`--smoke`
约三秒退出，只检查注册；人工模式观察双预览同时更新、缩放位置、关闭单个来源、
快捷键切换与退出清理。快捷键被占用会提示，仍可关闭退出。

Qt 实验也用两个自有顶层来源，按钮与全局快捷键共用命令；每 500 ms
重建矩形跟随 resize，正式 UI 应改用 resize/扫描/模式事件。来源窗口不会阻止
关闭主窗口退出。`crossdesk` 主入口仍是独立的 UI 骨架，不能把实验视为正式接线。

## 验证状态与待办

| 项目 | 当前状态 |
| --- | --- |
| `git diff --check` | 通过；不等于编译通过 |
| C++ 编译、假数据测试、Win32 自测与 Qt 生命周期回归 | 完整构建成功，5/5 CTest 通过 |
| 原生 DWM 双预览注册冒烟 | 通过；动态画面仍待目视验收 |
| Qt 双预览 | 冒烟通过；两张间隔截图中左右预览均有像素变化，resize 后目视位置正确；关闭 A 后 B 仍更新，关闭主窗口退出码 0 |
| 全局快捷键 | 本机占用提示（1409）已验证，实际按键仍待空闲环境验证 |
| 窗口扫描 | 只读扫描退出码 0，当时返回 216 个经壳层排除的顶层候选，不代表 216 个可见应用 |
| B 的两种布局输出接入 | 执行接口已提供，等待实际输出联调 |
| 文件管理器/设置的扫描、控制与预览 | 保留候选规则，实际支持情况未验证 |
| M1–M6 整体验收 | 尚未完成 |

实机记录系统、编译器、CMake、Qt 版本和测试输出；以浏览器、文件管理器、编辑器为手动指定目标，记录普通、最小化、最大化、关闭、全屏、高权限情况的请求与延时观察结果。不要以批量隐藏作实验。自动隐藏、Workspace、X11 不在本轮范围。
