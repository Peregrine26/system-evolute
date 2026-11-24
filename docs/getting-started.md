# 构建、运行与测试

依赖：C++17 编译器、CMake 3.20+；构建 GUI 还需要 Qt 6.2+ Widgets。
Windows 使用 Visual Studio 2022 C++ 工具链及匹配的 Qt Kit，或匹配 Qt 的
MinGW Kit。CI 固定 Windows 2022 / MSVC 2022 / Qt 6.8.3。
实现和实机证据见[仓库状态](repository-status.md)，协作规则见 [agent.md](../agent.md)。

## 构建目标与选项

| 目标 | 用途 | 条件 |
| --- | --- | --- |
| `crossdesk` | 主线最小 Qt 窗口 | `CROSSDESK_BUILD_GUI=ON`，默认开启 |
| `crossdesk_window_api` | 平台无关接口头文件 | 始终存在 |
| `crossdesk_window_scanner_lib` | 扫描流程与布局执行器 | Windows，`CROSSDESK_BUILD_SCANNER=ON`，Windows 默认开启 |
| `crossdesk_win32_backend` | Win32 查询、控制和桌面服务 | 同上 |
| `crossdesk_window_scanner` | 控制台扫描诊断 | 同上 |
| `crossdesk_dwm_demo` | 原生双来源预览实验 | 同上 |
| `crossdesk_qt_platform` / `crossdesk_qt_dwm_demo` | Qt 桥接与双来源预览实验 | Windows，GUI 和扫描器均开启 |

`include(CTest)` 默认开启 `BUILD_TESTING`；关闭它会移除测试目标，不能用来
绕过 PR 验证。实际目标以 [CMakeLists.txt](../CMakeLists.txt) 为准，角色分支的
额外目标不代表主线已有。编译器和 Qt 的 ABI/架构须匹配，不共用不同 Kit 的构建目录。

## Windows 与 Qt

在 Developer PowerShell for VS 2022 中执行，Qt 路径替换为本机实际 Kit：

```powershell
cmake -S . -B build-qt -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/Qt/<version>/msvc2022_64"
cmake --build build-qt --config Debug --parallel
ctest --test-dir build-qt -C Debug -LE interactive-desktop --output-on-failure --no-tests=error
```

运行前把 Qt Kit 的 `bin` 加入当前会话 PATH，或使用该 Kit 的 `windeployqt`
部署。不要修改仓库里的路径适配某台电脑。启动主线骨架：

```powershell
.\build-qt\Debug\crossdesk.exe
```

若使用项目已安装的 Qt 6.8.3 / MinGW 13.1 工具包，可用会话级脚本：

```powershell
# 可选：用环境变量指定工具包根目录；默认从 USERPROFILE 下的工具目录查找。
# $env:CROSSDESK_TOOLCHAIN_ROOT = '<toolchain-root>'
.\scripts\build.ps1
```

脚本只配置当前 PowerShell 的环境、使用独立 `build-local` 目录并检查每条命令
退出码；不会下载工具或写系统 PATH。缺少工具时按错误安装匹配 Kit，或使用上面的
MSVC 命令。[dev-env.ps1](../scripts/dev-env.ps1) 说明其预期目录结构。

## Windows 无 Qt

```powershell
cmake -S . -B build-platform -G "Visual Studio 17 2022" -A x64 -DCROSSDESK_BUILD_GUI=OFF
cmake --build build-platform --config Debug --parallel
ctest --test-dir build-platform -C Debug -LE interactive-desktop --output-on-failure --no-tests=error
.\build-platform\Debug\crossdesk_window_scanner.exe
```

该测试命令排除桌面操作，而不按单个名称白名单；未来新增非桌面 CTest 会自动纳入。
扫描只读，输出与失败处理原理见[扫描器说明](windows-window-scanner.md)。

## 交互式桌面验收

仅在 Windows 登录桌面运行，测试只操作其自有窗口，可能改变测试窗口的焦点和位置：

```powershell
ctest --test-dir build-qt -C Debug -L interactive-desktop --output-on-failure --no-tests=error
.\build-qt\Debug\crossdesk_dwm_demo.exe --smoke
.\build-qt\Debug\crossdesk_qt_dwm_demo.exe --smoke
```

MinGW 工具包可运行 `scripts/build.ps1 -DesktopTests`，会执行包含桌面测试的
完整 CTest。GUI 实验的 `--smoke` 约三秒退出，只证明启动/注册/清理；真实画面持续
更新、resize 后区域、单来源关闭、快捷键真实按键和动画手感仍须人工观察。
记录系统、编译器、Qt Kit、精确 SHA、命令、普通/失败路径及实际结果；
[A 实验记录](a-platform-integration.md)是已有证据，不替代每台演示机的验收。

## Linux 骨架

安装 C++17、CMake 和 Qt Widgets 开发包后：

```bash
cmake -S . -B build-linux -DCROSSDESK_BUILD_SCANNER=OFF
cmake --build build-linux --parallel
./build-linux/crossdesk
```

目前只构建 Qt 骨架，X11 窗口后端未实现，也未进入 Windows CI。
Linux 上不能用空的 CTest 集合证明核心行为已验证。

## 仓库轻量检查

不需要 Qt 或桌面；Python 3.10+ 和 Git 即可：

```powershell
python scripts/check_repository.py
python -m unittest discover -s tests -p test_repository_checks.py -v
```

新文件先按明确路径暂存。检查范围及 PR base 参数见[仓库维护](repository-maintenance.md)。
