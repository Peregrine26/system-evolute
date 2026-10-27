# CrossDesk

CrossDesk 是三人的 C++17 / Qt Widgets / CMake 桌面窗口管理课程项目。
产品需求与待确认设计见[项目总览](docs/project-overview.md)，职责见[三人分工](docs/team-responsibilities.md)。

`main` 提供 Qt 启动骨架、Windows 扫描与控制接口、布局执行器，以及原生/Qt
双 DWM 预览实验。C 的总览已合入角色分支 `c`，A 的控件桥接和 B 的核心规则
另行评审；各分支不能等同于主线已交付。实现、证据和未验收项统一见
[仓库状态](docs/repository-status.md)。

- [构建、运行与测试](docs/getting-started.md)：依赖、CMake、Qt 和非桌面/桌面测试命令。
- [Agent 与 PR 规范](agent.md)：独立工作区、提交、评审、接口边界及合并要求。
- [仓库维护](docs/repository-maintenance.md)：分支流向、GitHub 保护、自动检查和清理。
- [A 平台接口与实验](docs/a-platform-integration.md)：请求与观察结果、失败路径、DWM 与快捷键限制。
- [B 交接](docs/ai-handoff-b-core.md)、[C 交接](docs/ai-handoff-c-ui.md)、[扫描器原理](docs/windows-window-scanner.md)。
