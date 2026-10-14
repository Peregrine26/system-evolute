# Windows Window Scanner

这一步只验证一个能力：获取当前 Windows 桌面上的顶层窗口信息。

扫描器示例是控制台程序，不依赖 Qt。仓库另有最小 Qt 程序；现在先把 Windows API 的行为单独看清楚，便于定位问题。

## 1. Window、Process、HWND 的关系

### Window

Window 是桌面上的一个窗口对象。它有标题、位置、大小、可见状态等属性。一个应用程序可以创建多个 Window，例如主窗口、设置窗口和弹出菜单。

### Process

Process 是正在运行的程序实例。一个 Process 可以创建零个、一个或多个 Window。窗口和进程不是一对一关系，所以扫描窗口时必须分别读取窗口信息和所属进程信息。

### HWND

`HWND` 是 Win32 用来代表窗口的句柄（handle）。它不是窗口数据本身，而是系统分配的一个“引用窗口的值”。拿到 `HWND` 后，才能调用 `GetWindowTextW`、`GetWindowRect` 等 API 查询这个窗口。

本项目的 `WindowInfo::native_handle` 用整数保存 `HWND` 的值。这样后续统一的窗口数据结构不需要把 `HWND` 暴露给 Workspace Manager；真正需要调用 Win32 的地方仍然留在 `src/platform/windows/win32_api_adapter.*` 中。

## 2. `EnumWindows` 的工作方式

`EnumWindows` 是一个“回调式” API。CrossDesk 没有让扫描器直接处理 Win32 回调，而是由 `Win32ApiAdapter` 把 Win32 回调转换为 C++ 的 `WindowCallback`：

1. `WindowsWindowScanner` 准备一个保存结果的 `std::vector`。
2. 扫描器向适配器传入一个 `WindowCallback`。
3. 适配器把这个 C++ 回调交给 Win32 的 `EnumWindows`。
4. Windows 找到一个顶层窗口时，适配器把 `HWND` 转成整数句柄，再调用 C++ 回调。
5. 扫描器通过适配器读取标题、PID、进程名、边界和可见状态。
6. Win32 回调返回 `TRUE` 继续枚举，返回 `FALSE` 停止枚举。

这里的“顶层窗口”是桌面窗口层级中的顶级窗口，不等于“所有可见窗口”。因此结果里可能包含不可见窗口、没有标题的窗口或系统窗口。第一版把 `visible` 也打印出来，不急着在扫描层过滤，方便观察真实系统数据。

## 3. API 适配器为什么单独存在

如果 `WindowsWindowScanner` 直接包含 `windows.h`，它就只能在 Windows 上编译，也会让上层逻辑逐渐出现 `HWND`、`RECT`、`DWORD` 等平台类型。后面实现 Linux 时，Workspace Manager 需要被迫理解两套 API，跨平台边界就会失效。

现在的依赖方向是：

```text
WindowsWindowScanner
        ↓
WindowApiAdapter
        ↓
Win32ApiAdapter
        ↓
Win32 API
```

未来 Linux 版本只需要增加 `X11ApiAdapter`，并让构建系统选择它：

```text
WindowsWindowScanner
        ↓
WindowApiAdapter
        ↓
X11ApiAdapter
        ↓
X11
```

## 4. 本程序使用的 Win32 API

- `EnumWindows`：枚举所有顶层窗口。
- `GetWindowThreadProcessId`：通过 `HWND` 获取所属进程的 PID。
- `GetWindowTextLengthW` / `GetWindowTextW`：读取窗口标题。`W` 表示 Unicode 版本。
- `GetWindowRect`：读取窗口在屏幕坐标中的矩形边界。
- `IsWindowVisible`：判断窗口是否处于可见状态。
- `OpenProcess`：按 PID 打开一个进程句柄。
- `QueryFullProcessImageNameW`：读取进程可执行文件路径，再取最后的文件名。
- `CloseHandle`：释放由 `OpenProcess` 得到的进程句柄。

读取进程名可能失败，例如目标进程权限更高。这不是扫描失败；程序会显示 `<access denied>` 或 `<unavailable>`，而不是让整个扫描退出。

## 5. 在 Windows 上构建

需要：

- Visual Studio 2022，并安装“使用 C++ 的桌面开发”
- CMake 3.20 或更高版本

在“Developer PowerShell for VS 2022”中运行：

```powershell
cmake -S . -B build-scanner -G "Visual Studio 17 2022" -A x64 -DCROSSDESK_BUILD_GUI=OFF
cmake --build build-scanner --config Debug
.\build-scanner\Debug\crossdesk_window_scanner.exe
```

也可以使用 Qt Creator 打开根目录中的 `CMakeLists.txt`，选择一个 Windows 编译器 Kit 后构建运行。

## 6. 手工测试

运行程序后检查：

1. 输出中至少有当前终端、文件管理器或编辑器等窗口。
2. `Title` 与屏幕上看到的窗口标题大致一致。
3. `PID` 对应的 `Process` 名称合理。
4. 移动或调整一个窗口大小后再次运行，`Bounds` 会发生变化。
5. 最小化一个窗口后再次运行，观察 `Visible` 和边界信息。

第一版不承诺窗口顺序稳定，也不把“哪些窗口应该出现在 Mission Control”作为本阶段问题。过滤规则会在后续 Workspace 和 UI 需求明确后再加入。
