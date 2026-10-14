# CrossDesk：CMake + Qt + Git 骨架

这一阶段的目标是让三位组员从同一份仓库构建出两个独立入口：一个最小 Qt 窗口，以及一个 Windows 扫描器示例。后续模块会逐步接到这些入口，而不是一次性实现完整产品。

## 1. 为什么需要这三样工具

**CMake** 描述“哪些源文件组成哪个程序、需要什么库”。CrossDesk 将来有 Qt UI、核心逻辑和两个平台后端；将它们分成构建目标后，某个模块可以单独编译和验证。`CMakeLists.txt` 是共享的构建说明，`build/` 是本机生成结果。

**Qt** 提供桌面界面和事件循环。程序调用 `window.show()` 后，`application.exec()` 开始等待鼠标、键盘和窗口系统事件。窗口没有事件循环，就无法持续响应用户操作。当前 Qt 目标只验证这条最短路径。

**Git** 记录每一步可运行的代码。三位组员应从同一仓库协作：主分支保持能构建，个人任务在独立分支完成，合并前由另一人检查构建与测试步骤。`build/` 和 IDE 私有文件写入 `.gitignore`，避免机器上的生成物进入仓库。

## 2. 现在的构建目标

| 目标 | 类型 | 当前作用 | 构建条件 |
| --- | --- | --- | --- |
| `crossdesk` | 可执行程序 | 启动一个最小 Qt 窗口 | Qt 6.2+ Widgets；默认开启 |
| `crossdesk_window_api` | 接口库 | 向后续核心模块提供平台无关头文件 | 始终存在 |
| `crossdesk_window_scanner_lib` | 静态库 | 扫描流程 | Windows 且启用扫描器 |
| `crossdesk_win32_backend` | 静态库 | Win32 API 实现 | Windows 且启用扫描器 |
| `crossdesk_window_scanner` | 可执行程序 | 打印当前顶层窗口信息 | Windows 且启用扫描器 |

`CROSSDESK_BUILD_GUI` 默认开启；`CROSSDESK_BUILD_SCANNER` 在 Windows 上默认开启、在 Linux 上默认关闭。这样 Linux 可以先验证 Qt 骨架，Windows 也可以在尚未安装 Qt 时只构建扫描器。

## 3. 最小可运行 Qt 代码

完整代码位于 `src/app/main.cpp`。核心只有以下几步：

```cpp
QApplication application(argc, argv);
QMainWindow window;
window.setWindowTitle(QStringLiteral("CrossDesk"));
window.show();
return application.exec();
```

`QApplication` 初始化 Qt 程序，`QMainWindow` 是桌面窗口，`show()` 请求显示窗口，`exec()` 保持事件循环运行。实际代码还放了一个居中的文字标签，便于确认程序启动成功。

## 4. Windows 构建和运行

准备 CMake 3.20+、Visual Studio 2022 的“使用 C++ 的桌面开发”组件、与 MSVC 2022 x64 匹配的 Qt 6.2+ Widgets。`CMAKE_PREFIX_PATH` 应指向你电脑上的 Qt Kit 目录；示例里的 `<version>` 需要替换为实际版本号。

在 Developer PowerShell for VS 2022 中执行：

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/<version>/msvc2022_64"
cmake --build build --config Debug
.\build\Debug\crossdesk.exe
.\build\Debug\crossdesk_window_scanner.exe
```

若只练习 Windows 扫描器，可暂时不安装 Qt：

```powershell
cmake -S . -B build-scanner -G "Visual Studio 17 2022" -A x64 -DCROSSDESK_BUILD_GUI=OFF
cmake --build build-scanner --config Debug
.\build-scanner\Debug\crossdesk_window_scanner.exe
```

## 5. Linux X11 构建和运行

准备 C++17 编译器、CMake 3.20+ 和 Qt 6.2+ Widgets 开发包。在 X11 桌面会话中执行：

```bash
cmake -S . -B build
cmake --build build
./build/crossdesk
```

Linux 当前只构建 Qt 骨架。X11 窗口后端会在对应阶段加入。若 CMake 找不到 Qt，可把 Qt Kit 的安装前缀传给 `-DCMAKE_PREFIX_PATH=/path/to/qt`。

## 6. 手工验收

1. `cmake` 配置成功，构建目录中的生成文件被 Git 忽略。
2. 运行 `crossdesk`，看到标题为 CrossDesk 的窗口；关闭窗口后程序退出。
3. Windows 上运行 `crossdesk_window_scanner`，输出若干顶层窗口；原理和进一步测试见 [Windows Window Scanner](windows-window-scanner.md)。
4. 在另一位组员的电脑上按同样步骤构建，并记录平台、编译器、Qt Kit 和实际结果。

本机目前未检测到 CMake、Qt 或 C++ 编译器，以上命令尚未在当前工作区实际运行。第一位完成构建的组员应把真实结果补进项目记录。
