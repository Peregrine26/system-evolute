# CrossDesk

CrossDesk 是一个面向 Windows 和 Linux 的 Mac-like 窗口管理器课程项目。

当前阶段提供两个可运行入口：最小 Qt 桌面程序 `crossdesk`，以及 Windows 控制台示例 `crossdesk_window_scanner`。后者通过独立的 Win32 API 适配器枚举顶层窗口，输出窗口句柄、标题、进程名、PID、位置、大小和可见状态。

从 [构建与启动](docs/getting-started.md) 开始；项目目标、需求和分工见 [项目总览](docs/project-overview.md)，扫描器原理见 [Windows Window Scanner](docs/windows-window-scanner.md)。

## 构建入口

需要 C++17 编译器、CMake 3.20+ 和 Qt 6.2+ Widgets。Windows 建议使用 Visual Studio 2022 C++ 工具链，并安装与该工具链匹配的 Qt Kit。

Windows Developer PowerShell：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/<version>/msvc2022_64"
cmake --build build --config Debug
.\build\Debug\crossdesk.exe
.\build\Debug\crossdesk_window_scanner.exe
```

Linux X11（已安装 Qt 6 开发包）：

```bash
cmake -S . -B build
cmake --build build
./build/crossdesk
```

Qt 暂未安装时，Windows 扫描器可单独构建：

```powershell
cmake -S . -B build-scanner -G "Visual Studio 17 2022" -A x64 -DCROSSDESK_BUILD_GUI=OFF
cmake --build build-scanner --config Debug
.\build-scanner\Debug\crossdesk_window_scanner.exe
```

当前代码结构：

- `CMakeLists.txt`：构建入口，按目标组织 Qt 程序、接口和 Windows 示例。
- `src/app/main.cpp`：最小 Qt 程序入口。
- `docs/getting-started.md`：CMake、Qt、Git 的原理及构建测试步骤。
- `docs/project-overview.md`：项目目标、需求分析、三人分工和阶段验收。
- `docs/team-responsibilities.md`：每位组员的任务、交接标准与 AI 研究问题。
- `docs/ai-handoff-b-core.md`：给核心逻辑组员及其 AI 的独立交接说明。
- `docs/ai-handoff-c-ui.md`：给 Qt 界面组员及其 AI 的独立交接说明。
- `src/window_info.h`：平台无关的窗口数据。
- `src/platform/window_api_adapter.h`：上层使用的统一 API 接口。
- `src/platform/windows/win32_api_adapter.*`：Win32 API 适配器，唯一直接包含 `windows.h` 的模块。
- `src/windows_window_scanner.*`：扫描流程，不直接依赖 Win32 类型。

## 当前进度

- Qt 程序目前只验证窗口和事件循环能启动。
- Windows 扫描器是独立控制台示例；Linux X11 扫描与窗口控制尚未实现。
- Workspace、Mission Control UI、自动布局、快捷键和配置保存将在后续阶段加入。
