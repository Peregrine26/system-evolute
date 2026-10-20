# A 板块接口与联调记录

2026-10-20：第一轮 A 板块代码已在本机 Windows 上完整编译，3 项 CTest 均通过，原生 DWM 双预览注册冒烟通过。环境已安装到用户目录，见下方命令。全局快捷键在本机被占用（Win32 1409），占用提示路径通过；实际按键切换和动态预览目视验收仍待完成。B 的算法、C 的正式 UI 仍由对应模块提供。

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

`tests/layout_executor_test.cpp` 用假窗口覆盖扫描失败、重复选择、PID 变化、无效矩形、延迟移动、逐窗拒绝及来源关闭，不操作桌面。CI 新增只运行该测试的步骤，交互测试仍只编译。

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

刷新、选择变化、resize、DPI 变化后重调 `set_previews`，释放旧关系再注册。动画开始/收起时 `clear_previews`，结束后重建；隐藏、关闭和析构也会清理。原生 `WindowPreviews::refresh` 清理已消失的来源，但不代替 UI 的扫描刷新和占位反馈。

DWM 持续更新内容，不需截图；注册成功本身不能证明画面正常更新。来源与目标都须顶层，目标属于本进程；缩略图句柄仅在 A 私有持有。参考：[注册约束](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmregisterthumbnail)、[预览属性](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ns-dwmapi-dwm_thumbnail_properties)。

## 开发机验证命令

本机已安装 Qt 6.8.3（Widgets 基础组件）、MinGW 13.1.0、CMake 4.4.4、Ninja 1.13.2，路径为 `%USERPROFILE%/.codex/tools/crossdesk`。使用 Qt 仓库镜像下载，并保留 aqt 的归档校验；未改写系统 PATH。在项目目录执行：

```powershell
# 配置、编译，并运行 3 项测试（含自有窗口的桌面测试）。
.\scripts\build.ps1 -DesktopTests
# 单独加载当前 PowerShell 会话的开发环境后运行演示。
. .\scripts\dev-env.ps1
.\build-local\crossdesk_dwm_demo.exe --smoke
.\build-local\crossdesk_qt_dwm_demo.exe --smoke
```

安装后已修复 Qt `slots` 宏与变量名冲突，以及 MinGW 编译扫描器 `wmain` 所需的 `-municode` 链接参数。Qt 实验增加三秒自动退出的 `--smoke` 参数；来源窗口不阻止关闭主窗口时退出。其他机器的工具链根目录可以用 `CROSSDESK_TOOLCHAIN_ROOT` 环境变量指定。

也可在 Visual Studio 2022 Developer PowerShell 中：

```powershell
cmake -S . -B build-platform -G "Visual Studio 17 2022" -A x64 -DCROSSDESK_BUILD_GUI=OFF
cmake --build build-platform --config Debug --parallel
ctest --test-dir build-platform -C Debug -R "^layout_executor_test$" --output-on-failure
# 以下必须在交互式 Windows 登录桌面运行，只操作自有测试窗口。
ctest --test-dir build-platform -C Debug -L interactive-desktop --output-on-failure
.\build-platform\Debug\crossdesk_dwm_demo.exe --smoke
.\build-platform\Debug\crossdesk_dwm_demo.exe
```

原生实验创建两个每 100 ms 更新内容的来源和一个预览窗口。`--smoke` 约三秒后退出，只检查注册；人工模式需观察双预览同时更新、缩放位置、关闭单个来源、快捷键切换和退出清理。快捷键被占用会提示，窗口仍可关闭退出。

安装 Qt 6 Widgets 后，在对应的 MSVC 环境构建（路径换成实际版本）：

```powershell
cmake -S . -B build-qt -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/6.x.x/msvc2022_64"
cmake --build build-qt --config Debug --parallel
# Qt bin 加入 PATH，或使用 windeployqt 部署。
.\build-qt\Debug\crossdesk_qt_dwm_demo.exe
```

Qt 实验用两个自有顶层来源每 100 ms 更新文字，目标有独立控制区和两个预览区，按钮与全局快捷键共用命令。实验每 500 ms 重建矩形跟随 resize；正式 UI 应改用 resize/扫描/模式事件。`crossdesk` 入口保留 C 的原有 UI 骨架。

## 验证状态与待办

| 项目 | 当前状态 |
| --- | --- |
| `git diff --check` | 通过；不等于编译通过 |
| C++ 编译、假数据测试、Win32 自测 | 完整构建成功，3/3 CTest 通过 |
| 原生 DWM 双预览注册冒烟 | 通过；动态画面仍待目视验收 |
| Qt 双预览与全局快捷键 | 已编译；Qt 冒烟结果未确认，快捷键占用提示（1409）已验证，实际按键仍待验证 |
| B 的两种布局输出接入 | 执行接口已提供，等待实际输出联调 |
| 文件管理器/设置的扫描、控制与预览 | 保留候选规则，实际支持情况未验证 |
| M1–M6 整体验收 | 尚未完成 |

实机记录系统、编译器、CMake、Qt 版本和测试输出；以浏览器、文件管理器、编辑器为手动指定目标，记录普通、最小化、最大化、关闭、全屏、高权限情况的请求与延时观察结果。不要以批量隐藏作实验。自动隐藏、Workspace、X11 不在本轮范围。
