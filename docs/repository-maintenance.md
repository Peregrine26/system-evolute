# 仓库维护

协作要求由根目录 [agent.md](../agent.md)持有；本文说明其落地方式。
仓库设置的可审阅配置为 [.github/repository-policy.json](../.github/repository-policy.json)，
远端设置不随 Git 文件自动生效，管理员修改后必须从 GitHub API 回读核对。

## 分支流向

```text
A/B 主题分支 ── PR ──> main
C 主题分支   ── PR ──> c ── 集成 PR ──> main
```

`main` 是交付与全组集成线，`c` 是 C 的长期角色分支。角色分支存在不表示
其内容已进入主线；两级合入都须 PR、CI 和最新 head 的验证。合入 `c` 不强制
人工 Approve，进入 `main` 前仍须完成组员评审。共享接口改动在进入 `main` 前
由另外两位受影响组员审阅，普通改动至少一名其他组员 Approve。AI 会话审查
可以辅助，不能登记成另一位组员的人工批准或由作者自批。

本次整改前已存在的 `b/core-mvp`、`c-ui-fullscreen`、`codex/qt-preview-runtime-fixes`
等旧分支不改写历史或自动改名；新建任务遵循 `agent.md` 的 Conventional 分支名。
旧分支完成后按正常清理规则退出，不把旧名用作新任务模板。

GitHub 对 `main` 要求：最新分支基线、至少一次其他人的评审批准、最后推送者
以外的批准、过期批准失效、评审对话全部解决、必需 CI 成功。`c` 不配置强制
评审批准，但仍要求最新分支基线、评审对话解决及必需 CI 成功。管理员也受约束，
两个分支均禁止强推与删除。基础必需检查是 `platform`、`qt`；`main` 另要求
本轮新增的 `repository`。`c` 尚未包含仓库检查 workflow，暂不要求一个不会
运行的检查；后续同步该 workflow 时，再将其加入 `c` 的必需检查。

全部已推送分支还受 `Preserve published branch history` ruleset 约束：
禁止 non-fast-forward 更新，无绕过名单；普通主题分支仍可正常追加提交。
已推送分支需要同步时使用 merge；不要为整理提交改写其历史。

仓库开启合并后自动删除主题分支；受保护的 `c` 角色分支保留。旧分支只有在
tip 已可从合并目标到达、没有开放 PR，且没有未交接工作区改动时才删除。
本地 worktree 必须先核对 `git status --short`、分支及依赖，不用强制删除。
被其他任务当作测试夹具的工作区等该任务交接后再清理；不通过清理覆盖脏工作区。

保护参数对应 [GitHub 分支保护 API](https://docs.github.com/en/rest/branches/branch-protection)
和 [ruleset API](https://docs.github.com/en/rest/repos/rules)，
自动清理对应 [GitHub 合并后删除分支](https://docs.github.com/en/repositories/configuring-branches-and-merges-in-your-repository/configuring-pull-request-merges/managing-the-automatic-deletion-of-branches)。

## 本地与云端检查

先将本任务的新文件按明确路径暂存，再运行：

```powershell
git diff --check
python scripts/check_repository.py
python -m unittest discover -s tests -p test_repository_checks.py -v
```

检查器只读取 Git 跟踪的文件：本地未提交的辅助文件不能掩盖下一台机器的断链。
它验证 Markdown 本地文件/目录和标题链接、路径大小写、UTF-8、已跟踪生成物、
UI/核心/中立接口中的原生头文件与类型。它不会联网检查外链，不替代许可证、
凭证检查、产品语义、完整 C++ 编译、动态桌面验收或人工评审。

`.github/workflows/repository.yml` 在 push/PR 运行同一检查及 8 项反例测试。
CI 另将 PR base SHA 传给 `--base`，对完整变更运行 `git diff --check`；首个分支
push 没有有效 before SHA 时，先检查最新提交，随后 PR 检查完整 base...head 范围。
测试包含真实临时 Git 仓库，确认已提交的尾随空格也会使门禁失败。

Windows 的 `platform` 与 `qt` 作业均使用 `ctest -LE interactive-desktop`
并要求非空测试集，不再只白名单一个测试名称。新增非桌面测试进入 CTest 后
自动进入这两类适用构建；桌面测试需正确标记，具体命令由[构建说明](getting-started.md)维护。
三个作业只需 contents:read，不向 PR 提供写凭证。

## PR 与未完成任务

使用 [PR 模板](../.github/pull_request_template.md)记录问题、依赖、接口影响、
当前 head 的普通/失败路径验证和未验证项。未完成的远端工作以 `WIP: ` Draft
登记；CI 绿色仍不能代替作者确认就绪、接口讨论和正式批准。

合并后对账目标分支与审查的 SHA，再清理已结束的分支、工作区和临时交接。
本地生成物、日志和 `plans/` 被忽略；忽略不会自动删除或提交现有用户文件。
