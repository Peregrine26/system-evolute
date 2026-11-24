# 实现与验证状态

2026-11-24 核对的基线：`main` 为 `1a05421`（PR #7 合入后），C 角色分支 `c` 为 `291f83d`。
本文维护实现位置与验证证据；产品目标由[项目总览](project-overview.md)持有，
角色职责由[三人分工](team-responsibilities.md)持有。后续合入时更新本表。

| 内容 | 实现位置与状态 | 证据或剩余验证 |
| --- | --- | --- |
| Windows 查询、控制及结果类型 | 已在 `main`；`WindowApiAdapter` 不只提供查询 | [A 接口与实验](a-platform-integration.md)，[PR #2](https://github.com/Peregrine26/system-evolute/pull/2) |
| 两窗布局执行器 | 已在 `main`；接收 B 提供的矩形，分别提交与观察 | 假适配器测试覆盖扫描失败、重复选择、PID 变化、拒绝、延迟和关闭 |
| 原生/Qt DWM、全局快捷键桥接 | A 的独立实验已在 `main`，不等于正式 UI 已接线 | [A 实机记录](a-platform-integration.md)；本机快捷键占用失败路径通过，真实按键仍待可用环境验证 |
| C 全屏卡片总览 | [PR #1](https://github.com/Peregrine26/system-evolute/pull/1) 已合入 `c`，尚未合入 `main` | head `507a82b` 经独立 AI 复核，本机 11/11 CTest、UI 63 项断言及 [CI](https://github.com/Peregrine26/system-evolute/actions/runs/40472354148) 通过；合并树与该 head 一致 |
| A 控件级预览桥接 | [PR #4](https://github.com/Peregrine26/system-evolute/pull/4)，head `8b6ff63`，已同步 `main` 与仓库检查，待组员正式审阅 | 映射用例改为等待 C 的淡入结束，不再依赖固定 250 ms；head 上 `repository`、`platform`、`qt` 三项检查通过，非桌面 CTest 5/5（含 `c_ui_bridge_mapping_test`）；旧 C 夹具与新 C 总览仍须区分 |
| B 筛选、选择与布局 | 远端 `b/core-mvp`；[PR #6](https://github.com/Peregrine26/system-evolute/pull/6) 已转为可评审 | 作者记录纯 C++ 单测和流水线测试；本轮不将作者报告改写为整改会话复验 |
| 主线 Qt 用户入口 | `src/app/main.cpp` 仍是最小窗口骨架 | 不将演示程序或角色分支的能力记为正式用户流程已完成 |
| Workspace、X11、配置及整体验收 | 尚未在 `main` 实现完整流程 | [项目总览](project-overview.md)；原型、编译通过与桌面验收分别记录 |

主线 CMake 当前的非桌面用例是布局假适配器、Qt 预览区域（默认与 150% 缩放各一项）
和 Qt 生命周期；Win32/DWM 自测、控件预览与演示程序标记为 `interactive-desktop`，
CI 用 `-LE interactive-desktop` 过滤。各主题/角色分支可新增测试，不能把一个分支的
数量写成所有分支的共同事实。构建入口统一见
[构建与测试](getting-started.md)，CI 范围见[仓库维护](repository-maintenance.md)。

已发现的缺陷使用 GitHub issue 跟踪：[Issue #5](https://github.com/Peregrine26/system-evolute/issues/5)
记录扫描器在边界查询失败后仍可能报告完整，整改核对确认存在；应使用带失败查询的假适配器复现与修复。它是待修缺陷，
不以“有完整性字段”推断已经覆盖所有失败路径。

本轮仓库整改针对管理流程、文档事实和自动检查。工作区里尚未提交的课程范围改写
不自动替换已发布的主线需求；角色分支中的课程简化文档须随集成 PR 由全组对账。
