# CrossDesk Agent 协作与 PR 规范

本文件从 AgentOSNext 的 PR、Git、文档、测试与多会话协作纪律中提取通用规则，并按 CrossDesk 的 GitHub、C++17、Qt 和 CMake 环境调整。产品范围与模块职责以本项目文档为准。根目录 `AGENTS.md` 只提供本文件的入口，不复制规则。

## 开始工作前

1. 查看 `git status --short`、当前分支和已有 worktree；发现其他会话的未提交文件时，立即换到独立 worktree，不清理或覆盖他人改动。
2. 扫描 `docs/`、`src/`、`tests/`、`scripts/` 和 `.github/workflows/`，再按下表读取相关文档与代码，避免只看任务涉及的单个文件。
3. 先核对目标、当前实现和验证证据。规划功能、已有代码、已验证行为分别记录；不能把文档中的未来功能当成已实现，也不能把编译成功当成桌面验收通过。
4. 按“原理 → API → 最小可运行实验 → 编译与测试 → 正式集成”推进；课程项目的提交者应能解释代码、接口前提和失败情况。

| 要了解的内容 | 入口 |
| --- | --- |
| 项目入口与构建概览 | [README.md](README.md) |
| 产品范围、需求、阶段与待确认决定 | [项目总览](docs/project-overview.md) |
| A/B/C 职责、接口交接与验收 | [三人分工](docs/team-responsibilities.md) |
| 已合入、分支原型和验证证据 | [仓库状态](docs/repository-status.md) |
| 分支流向、保护和仓库检查 | [仓库维护](docs/repository-maintenance.md) |
| 构建依赖、命令与手工测试 | [构建与启动](docs/getting-started.md) |
| Windows 扫描器原理与限制 | [扫描器说明](docs/windows-window-scanner.md) |
| B：核心逻辑交接 | [B 交接说明](docs/ai-handoff-b-core.md) |
| C：Qt 界面交接 | [C 交接说明](docs/ai-handoff-c-ui.md) |
| 实际构建目标与自动测试 | [CMakeLists.txt](CMakeLists.txt)、[tests/](tests/) |
| 云端自动检查 | [Windows workflow](.github/workflows/windows-platform.yml) |

## 文档权威与范围纪律

- `docs/` 描述目标、边界和验收要求；代码描述当前实现。两者偏离时，先判断是目标尚未实现还是变更漏同步；无法判定时报告并请求裁决，不擅自改变产品语义。
- 本项目目前没有 `docs/README.md`；使用根 README 和上表导航，不假定 AgentOS 的文档目录、ID、Makefile 或治理脚本已经存在。
- 一个事实只设一个权威位置，其他文件使用链接。需求由项目总览持有，分工由团队说明持有，构建命令由构建说明持有；避免复制多份进度表或待办清单。
- PR 分为纯文档或文档加代码。代码改变行为、接口、构建或验证方式时，在同一 PR 同步相关文档；当前状态说明必须与最终实现对账。
- 重大接口或行为决定先说明选项、取舍和失败路径，取得团队确认后记录到项目文档，再实现。项目总览中的待确认设计不得由 Agent 擅自定案。
- 不以“为未来预留”为由增加通用插件框架、兼容层、重复状态权威或未批准功能。Wayland、云账号、跨机器同步及操作系统原生虚拟桌面等范围以项目总览为准。
- 文档链接使用真实路径和稳定标题；无验证证据写“待验证”，不填写推测的通过结论。

## Git 与独立工作区

- 每个大任务使用独立分支和 worktree，例如：`git worktree add -b feat/workspace-manager ../crossdesk-workspace-manager main`。先确认本地基线与远端状态，再选择起点。
- 分支使用 `<conventional-type>/<小写-kebab-主题>`；类型为 `build|chore|ci|docs|feat|fix|perf|refactor|revert|style|test`。新分支不使用人名、`claude/*` 或 `codex/*`；已有分支不因本规范自动重命名。
- 集成目标为 `main`；C 已采用 `主题分支 → c → main` 的角色分支流向。两级均通过 PR 和 CI，不直接 push 集成分支，不改写公共历史；合入 `c` 不强制人工 Approve，进入 `main` 时完成组员评审。具体流向与保护见[仓库维护](docs/repository-maintenance.md)；新增其他角色/发布分支前先记录流向与保护规则。
- 未准备提交评审的工作保留本地。推送主题分支时同时创建对应 PR；未就绪时使用 GitHub Draft，并给标题加 `WIP: `，完成后再转为可评审。
- 提交遵循 Conventional Commits，例如 `fix: handle closed windows during refresh`。Agent 参与的提交在正文注明来源，例如 `Agent: Codex`。
- 禁止 `git add -A`；按明确路径暂存，提交前用 `git status --short` 和 `git diff --cached` 核对每个文件的来源和内容。
- 多个串行 PR 默认按依赖堆叠开发，自下而上合入；写清依赖，避免把上层改动误当作可独立合并。
- PR 合并且确认无未提交工作后，删除完成的主题分支，并从另一 worktree 执行 `git worktree remove <path>`；不要强制删除尚未交接的工作。

## PR 与评审

- 本仓库位于 GitHub，使用 GitHub PR 页面、GitHub API 或已认证的 `gh`；AgentOS 的 Forgejo 地址与 `tea` 命令不适用。
- 每个 PR 聚焦一个可审阅结果。标题描述最终改动；正文说明具体问题、改动后的行为、关联需求或 issue、验证结果、未验证项和集成影响。
- 正文至少让审阅者知道：如何构建和复现、正常与失败路径如何验证、共享接口的调用方式是否改变、是否影响另一平台或其他模块。
- 合入 `main` 前至少获得一名其他组员的 Approve，并完成本项目要求的复现检查。合入 `c` 的 PR 不以人工 Approve 为强制条件，但仍须通过 CI、解决评审对话，并在进入 `main` 前补齐评审。共享接口或行为变化须先提供调用示例与失败场景，并在进入 `main` 前由另外两位受影响组员审阅，遵守现有分工约定。
- Review 的具体问题写在 PR 对应代码行；Approve 或 Request Changes 通过 GitHub 正式评审提交。作者不能代替其他组员批准自己的变更。
- 在评审、修改、回复、rebase 或合并 PR 前，读取全部最新评论、逐行意见、Approve、Request Changes 和机器人结果；不要只读正文或只看 CI 颜色。
- 评审结论绑定其检查的 commit。更新 head 后，明确新增改动并确认相关意见已处理；旧 head 的批准或证据不能自动证明新 head 通过。
- `main` 至少一人 Approve 是团队流程要求，不表示远端保护已配置；`c` 明确不设置强制批准。以 GitHub 实际分支保护执行已记录的合并限制；发现策略漂移时报告，不擅自绕过检查。
- 缺陷使用 GitHub issue 跟踪，记录复现步骤、期望与实际行为、平台和版本。分层修复或待复验 PR 用 `Refs #N`；只有全部修复合入且最终复验通过的 PR 才用 `Fixes #N` 自动关闭。
- 合并前确认正文、文档和测试说明反映最终 diff；合并后对账主分支结果，不能只留下聊天中的完成声明。

## 模块边界与项目注意事项

- 技术基线为 C++17、CMake 3.20+、Qt 6.2+ Widgets；目标平台是 Windows 和 Linux X11，具体构建选项以 CMake 为准。
- 依赖方向遵守“Qt UI → Workspace 核心 → 统一窗口接口 → Win32/X11 后端”。A 主责平台与集成，B 主责核心规则，C 主责 Qt 体验；具体职责只在团队文档维护。
- 核心规则与 UI 不直接使用 Win32/X11 头文件或原生窗口类型；原生调用集中在平台层。已有 Qt/平台桥接模块应保留明确边界，UI 通过服务获取状态与发送命令。
- 修改共享窗口数据或接口前，说明调用方影响、参数、返回值和失败语义；模拟后端应能实现同样的接口，避免上层依赖某个平台的隐含行为。
- Workspace 状态由核心模块统一维护，UI 展示其投影，不另建第二套归属真相。先确认模型和状态转换，再接入流程。
- 窗口句柄随时可能失效或复用，不能当作跨重启的永久 ID。扫描期间窗口关闭、进程信息不可读或字段缺失时，应安全处理并保留合理的回退信息。
- 显示、隐藏、移动、缩放和焦点请求可能被系统或应用拒绝；必须区分请求发出和实际成功，向核心与用户反馈真实结果，避免成功假象或窗口失联。
- 核心逻辑用模拟适配器覆盖窗口消失、权限失败、空列表和部分失败；真实桌面结果另行记录。Qt 事件循环不能被长时间扫描或布局阻塞。
- Windows 与 X11 的能力、焦点和窗口管理语义可能不同；明确平台限制与降级，不由单平台成功推断跨平台通过。
- 第三方实现仅作研究输入；复用代码前核对许可证、来源和课程要求。凭证、个人环境路径、构建产物及 IDE 私有配置不提交到仓库。

## 验证与 CI

- 所有变更运行 `git diff --check` 和 `python scripts/check_repository.py`；后者检查已跟踪文件的本地 Markdown 链接/标题、UTF-8、生成物和原生 API 边界。新文件先明确暂存，避免未提交文件掩盖断链。文档命令和远程来源仍需人工核对。本仓库没有 `make docs-lint`。
- 纯文档改动默认做轻量检查；代码改动按影响运行定向编译、单元测试和静态检查。保留真实退出码，不通过截断输出或管道掩盖失败。
- 新增或改变行为的自动测试必须进入云端检查范围；现有 workflow 能执行时直接复用，不能执行时在同一 PR 补充相应自动化。不得跳过测试、缩小范围或降低断言来制造通过。
- Windows CI 分为无 Qt 平台构建和 Qt 6.8.3 / MSVC 2022 完整构建，均运行全部非 `interactive-desktop` 测试且不允许空测试集；仓库检查由独立 Python 作业运行。交互桌面测试只在云端编译，Linux X11 未纳入当前矩阵。覆盖范围以 workflow 和测试代码为准。
- 完整构建矩阵、跨平台回归等重复重检查优先交给 CI；只有复现明确失败、调试本地问题或用户要求时才本地运行对应重检查，先说明预计耗时和环境影响。
- 窗口操作与交互桌面自测会移动、隐藏或改变焦点，必须使用受控普通测试窗口和适合的桌面环境；不为纯文档任务启动这些测试。
- 缺少交互桌面、Qt Kit 或目标平台时如实记录限制。必要的桌面验收由审阅者或对应平台负责人复现，不能以模拟测试或无桌面的 CI 结果替代。
- 测试记录绑定精确 commit，并写明操作系统、桌面环境、编译器、Qt Kit、命令、正常路径、失败路径及结果；未执行的步骤标为待验证。
- 每个 PR 评估构建、打包、运行配置和发布影响；只有相应输入或流程变化时才改 workflow 或脚本。本项目没有 AgentOS 的 Kubernetes、Runner 或企业凭证管理要求。

现有 Windows 非桌面检查可复用以下命令；仅在相关代码变更需要时本地执行：

```powershell
cmake -S . -B build-platform -G "Visual Studio 17 2022" -A x64 -DCROSSDESK_BUILD_GUI=OFF
cmake --build build-platform --config Debug --parallel
ctest --test-dir build-platform -C Debug -LE interactive-desktop --output-on-failure --no-tests=error
```

Qt 与 Linux 构建方式见 [构建与启动](docs/getting-started.md)。`interactive-desktop` 标签的测试与真实用户旅程需要单独的桌面验收记录。

## 多会话交接与完成标准

- `plans/` 只保存临时工作记忆，不作为需求、架构或验证权威；任务完成后删除对应快照，持久结论归入文档、代码或 issue。
- 只有未完成任务需要交接。快照放在任务专属 worktree 的 `plans/YYYY-MM-DD-HHmmss-<topic>-handoff.md`，不放共享 checkout 或桌面。
- 快照至少记录创建时间、owner、worktree、branch、PR、base/head SHA、staged/unstaged/untracked 状态与可复算指纹、权威文档链接、失效条件及唯一下一动作；不得包含凭证。
- 接手者先核对快照和实际状态，不一致时停止并报告，避免继续操作已变更现场。
- 完成前确认：范围已对账、相关文档已同步、轻量检查通过、行为测试进入 CI、PR 写清验证与限制、评审与复验符合要求。最终报告说明改了什么、为何修改、如何验证及尚存限制。
