# CrossDesk 项目交接说明：给 B 组员的 AI

> ⚠️ **本文已过期。** 2026-10-20 起的必交付基线是 [`course-mvp.md`](course-mvp.md)：多 Workspace 方案整体移出本学期范围，B 的任务改为「窗口筛选、选择校验、等宽/60:40 两窗布局计算 + 纯假数据单元测试」。本文中的 Workspace Manager、状态转换表、归属规则**不再需要实现**；本文保留仅作扩展参考，冲突时以 `course-mvp.md` 为准。

> 本文件可独立发送给负责核心逻辑的组员及其 AI。你是协作助手，需要帮助 B 学会并实现自己的模块。请先阅读实际仓库，再基于现状工作；本文中的规划功能不等于已有代码。

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

当前本地仓库有最小 Qt 启动程序、Windows 控制台扫描器、`WindowInfo`、查询型 `WindowApiAdapter` 和 Win32 实现。Workspace Manager、窗口控制、Mission Control UI 和 X11 后端尚未实现。已有文档：`README.md`、`docs/getting-started.md`、`docs/project-overview.md`、`docs/team-responsibilities.md`、`docs/windows-window-scanner.md`。现有代码重点看 `src/window_info.h` 与 `src/platform/window_api_adapter.h`。接口以后可能调整，不能把当前名字和返回值视为最终设计。

课程开发顺序是：CMake/Qt/Git 骨架 -> Windows 扫描 -> Windows 控制 -> Workspace Manager -> Qt 总览 -> Linux X11 -> 布局/快捷键/配置 -> 兼容性测试。B 可以提前用**模拟窗口数据**探索核心逻辑，但正式集成要尊重当前阶段。

## B 的任务和交付

1. 定义 Workspace 的状态：唯一标识、名称、当前选中项、窗口归属，以及创建/重命名/删除/切换的规则。先给出状态转换表和失败场景。
2. 明确窗口与 Workspace 的生命周期：新增窗口默认归属、窗口关闭后的清理、扫描刷新后的同步、删除非空 Workspace 时窗口去向。
3. 与 A 确认接口语义：窗口标识可能失效，控制操作可能失败；核心如何接受扫描结果、如何发出命令、如何向 C 报告结果。
4. 用模拟适配器实现并测试 Workspace Manager。测试不应依赖真实 Windows/X11 桌面，也不应包含 Win32/X11/Qt 头文件。
5. 核心稳定后加入自动布局与配置保存。布局应对相同输入给出可预测的位置；配置不能把会话内窗口句柄当作永久 ID 保存。
6. 给 C 提供简洁的状态查询与命令接口，包含当前 Workspace、窗口分组、切换/移动操作和失败反馈。接口变化要经 A/C 审阅。

**B 的最小验收**：用假窗口 A、B、C 创建两个 Workspace，把 B 移入第二个 Workspace，切换后状态正确；关闭 B 后刷新不会留残影；删除第二个 Workspace 时窗口去向符合约定；同一套测试可在无图形桌面的环境运行。

## 请 AI 先帮助 B 搞懂的问题

- Workspace 究竟是 CrossDesk 的逻辑分组，还是系统虚拟桌面？第一版“切换”对真实窗口做什么？这是全组待决定事项，请列出候选规则及可验证实验，不要擅自定案。
- 一个窗口关闭和新窗口出现时，状态同步怎样保持正确？窗口句柄可能失效意味着什么？
- 命令失败后，核心状态应该回滚、重试还是报告部分成功？如何让 UI 显示真实结果？
- 布局算法需要哪些输入：可用屏幕区域、窗口最小尺寸、显示器信息？这些信息应由谁提供？
- 哪些配置可以跨重启保存，哪些只能保留在当前运行会话？
- 如何设计模拟适配器，使失败、窗口消失、空标题等情况能被测试？

## 与 AI 协作的要求

- 每遇到新概念，先解释为什么需要它，再讲相关 API/数据结构，给最小可运行例子、编译运行和测试方法，最后逐步合入正式模块。不要一次生成整个项目。
- 先阅读实际代码和现有文档；不要因为本文描述了未来功能，就假设它们已实现。
- 优先遵守现有模块边界。若需要改 `WindowApiAdapter`，写清楚对 A 和 C 的影响，并给出调用示例。
- 对不能确定的窗口系统行为，设计最小实验，并记录测试平台。不要把推测写成事实。
- DeskBox 是同类参考项目，不能直接复制其代码；任何复用都要先核对许可证和课程要求。

## 现在可以发给 AI 的第一条任务

> 请先阅读 CrossDesk 仓库中的 `docs/project-overview.md`、`docs/team-responsibilities.md`、`src/window_info.h` 和 `src/platform/window_api_adapter.h`。我是 B，负责 Workspace Manager。当前先不写完整项目：请解释 Workspace 状态为什么需要独立于 Win32/X11，列出创建、切换、移动窗口、关闭窗口、删除 Workspace 的状态转换和待全组决定的问题；然后设计一个只用假窗口数据的最小实验及测试步骤。
