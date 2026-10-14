# CrossDesk 项目交接说明：给 C 组员的 AI

> 本文件可独立发送给负责 Qt 界面的组员及其 AI。你是协作助手，需要帮助 C 学会并实现自己的模块。请先阅读实际仓库，再基于现状工作；本文中的规划功能不等于已有代码。

## 共同项目背景

CrossDesk 是三人用一个学期完成的 C++/Qt/CMake/Git 程序设计课程项目。目标是在 Windows 和 Linux X11 上提供类似 macOS Mission Control 的窗口总览与多 Workspace 工作流：扫描顶层窗口、分配 Workspace、切换、显示/隐藏、移动、缩放、自动布局、快捷键和本地配置。第一版不做 Wayland，不替换系统窗口管理器，也不承诺操作系统原生虚拟桌面。

预期依赖方向：

```text
Qt UI（C 负责）
    ↓
Workspace Manager / 核心规则（B 负责）
    ↓
统一 WindowApiAdapter 接口
    ↓
Win32 / X11 后端（A 负责）
```

A 是主负责人，负责平台 API、共享接口和最终集成；B 负责平台无关的核心逻辑；C 负责 Qt 界面。B 与 C 的模块范围相对集中，但每人都要能解释并演示自己的成果。

当前本地仓库有最小 Qt 启动程序、Windows 控制台扫描器、`WindowInfo`、查询型 `WindowApiAdapter` 和 Win32 实现。Workspace Manager、窗口控制、Mission Control UI 和 X11 后端尚未实现。已有文档：`README.md`、`docs/getting-started.md`、`docs/project-overview.md`、`docs/team-responsibilities.md`、`docs/windows-window-scanner.md`。现有代码重点看 `src/app/main.cpp` 与 `src/window_info.h`；窗口扫描接口可看 `src/platform/window_api_adapter.h`，但 UI 不应直接依赖平台适配器。

课程开发顺序是：CMake/Qt/Git 骨架 -> Windows 扫描 -> Windows 控制 -> Workspace Manager -> Qt 总览 -> Linux X11 -> 布局/快捷键/配置 -> 兼容性测试。C 可以提前用**模拟 Workspace 和窗口数据**制作 Qt 原型，正式接入 B 的核心服务放在对应阶段。

## C 的任务和交付

1. 学会最小 Qt/CMake 程序：窗口、事件循环、信号槽、基本布局和数据刷新。先保证组员能自己构建并解释程序。
2. 用假数据做总览原型：显示 Workspace 列表、当前 Workspace、每组窗口；用户能选择窗口、切换 Workspace、移动窗口归属。
3. 设计鼠标和键盘流程：打开/关闭总览、焦点移动、确认选择、失败反馈。避免操作后用户找不到窗口。
4. 与 B 协商 UI 所需状态和命令，不直接操作 `HWND` 或 X11 窗口标识；与 A 协商缩略图、图标和焦点能力的实际限制。
5. 接入核心服务后处理窗口刷新、已关闭窗口、空 Workspace、权限或平台操作失败等状态。
6. 在 Windows 与 Linux X11 测试不同分辨率、缩放、字体和键盘操作；缩略图不可用时用标题、应用名或图标回退。动画和实时缩略图是后续增强，不应阻塞可用总览。

**C 的最小验收**：只用假数据时，窗口能展示两个 Workspace 和多个窗口条目；鼠标与键盘都能选择一个窗口、切换 Workspace；空列表、窗口消失和操作失败有明确显示。接入 B 的核心后，同一条用户路径在 Windows 和 Linux X11 都可操作。

## 请 AI 先帮助 C 搞懂的问题

- Qt 的事件循环负责什么？信号槽如何让按钮点击触发核心命令，核心状态变化如何刷新界面？
- UI 应该持有窗口信息的快照，还是直接持有可变对象？刷新后选中项怎么处理？
- 用户按快捷键打开总览时，Qt 窗口如何取得焦点？如果焦点没有按预期变化，怎样给出合理回退？
- 如何用稳定的布局约束，让总览在小屏、高 DPI 和多窗口时仍可读、可操作？
- 缩略图不可用时，什么信息最能帮助用户识别窗口？
- Qt 层的快捷键和全局快捷键有什么不同？系统级注册由 A 负责时，C 需要接收什么事件？

## 与 AI 协作的要求

- 每遇到新概念，先解释为什么需要它，再讲相关 Qt API，给最小可运行例子、编译运行和测试方法，最后逐步合入正式模块。不要一次生成完整 Mission Control UI。
- 先阅读实际代码和现有文档；不要因为本文描述了未来功能，就假设它们已实现。
- UI 应从 B 的服务获取状态和发出命令；不要在 Qt 组件里直接调用 Win32/X11 API，也不要自己维护第二套 Workspace 真相。
- 先保证完整、清楚的操作流程，再考虑动画和精细视觉效果。对平台行为不确定时，与 A/B 定义最小实验。
- DeskBox 是同类参考项目，不能直接复制其代码；任何复用都要先核对许可证和课程要求。

## 现在可以发给 AI 的第一条任务

> 请先阅读 CrossDesk 仓库中的 `docs/project-overview.md`、`docs/team-responsibilities.md` 和 `src/window_info.h`。我是 C，负责 Qt UI。当前先不写完整项目：请解释 Qt 事件循环、信号槽和基本布局为什么适合窗口总览；然后给一个只使用假数据、展示两个 Workspace 和几个窗口条目的最小可运行 Qt/CMake 示例，以及编译、运行、手工测试步骤。不要调用 Win32/X11。
