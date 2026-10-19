# C 模块 · 第一批代码讲解卡

> 分支 `c/ui-skeleton`。这批代码**尚未编译验证**（开发机还没装 Qt），第一次构建的意义就是把它们跑通。

## 本次提交了什么

| 文件 | 作用 | 为什么单独存在 |
| --- | --- | --- |
| `src/ui/overview_types.h` | `WindowItem` / `WorkspaceItem` / `OverviewSnapshot` | UI 只认识"快照"这一种数据，不碰 `HWND`，也不碰 B 的内部状态 |
| `src/ui/workspace_service.h` | 抽象接口：拉快照、发 4 个命令、2 个信号 | 这是 UI 与核心之间**唯一**的缝。B 的真实实现来了，只换实现，UI 一行不改 |
| `src/ui/fake_workspace_service.*` | 假数据：2 个 Workspace、6 个窗口 | 没有它，B 没代码时 UI 完全无法开工 |
| `src/ui/overview_window.*` | 顶部条 + 左侧列表 + 底部状态 | 真正的界面 |
| `CMakeLists.txt` | 加 `CMAKE_AUTOMOC`、加源文件、加 include 路径 | 见下 |
| `src/app/main.cpp` | 装假服务、开总览窗口 | 19 行的空壳被替换 |

## 四个必须能讲清的点

**1. 为什么必须有 `set(CMAKE_AUTOMOC ON)`**
`Q_OBJECT` 宏会让类需要一个由 moc 生成的 `xxx.moc`。没有它，编译能过但链接会报 `undefined reference to vtable for ...`。这不是代码写错，是构建系统没给 moc 交活儿。

**2. 信号槽到底怎么把"点按钮"变成"界面更新"**
`QPushButton::clicked` → 我们连的 lambda → `service->switchWorkspace(id)` → 服务内部改状态 → `emit snapshotChanged(...)` → 界面 `applySnapshot()` 重建。**界面从不自己推断状态，只负责画服务给的东西。**

**3. 为什么 `snapshotChanged` 用 `Qt::QueuedConnection`**
如果用直连，点击按钮的瞬间就会同步重建顶部条，把**正在处理点击的那个按钮**删掉——这是典型的自删崩溃。队列连接让重建排到当前事件之后。配套代价：自定义类型必须 `qRegisterMetaType`，否则运行时信号被静默丢弃。

**4. 刷新时选中项怎么处理**
重建列表前记住 `m_selectedWindow`；重建后按 id 找回。找不回说明窗口已被外部关闭，于是**清除选择并提示**，绝不把下一条命令自动转给相邻窗口。

## 失败路径在哪

- 受限窗口：`ControlState::Restricted`，点它会返回失败提示，不假装成功。
- 迁移受限窗口：归属改变但显示状态未完成，`failureMessage` 同时表达两件事。
- 窗口消失：`simulateWindowClosed()` 可手动触发，验证选中清除。
- 操作返回 ≠ 成功：真正的结果走 `operationFinished`，界面只认它。

## 手工测试（构建成功之后逐条做）

1. 启动应看到 2 个 Workspace 胶囊、左侧 3 个窗口。
2. 点「学习」→ 顶部高亮切换、左侧换成 3 个窗口。
3. 点列表里的窗口 → 底部出现"已切到…并尝试置前…"。
4. 点「系统设置」（受限）→ 底部**红色**失败提示，列表项出现 `!` 标记。
5. 按 Tab 把焦点移到顶部按钮，空格切换；再 Tab 回列表，方向键移动，回车激活。
6. 把「记事本」归到「学习」（当前未接命令，需临时断点或后续菜单验证）。

## 三个答辩追问题

1. **UI 为什么持有快照而不是指向窗口对象的指针？**
   因为窗口随时会关闭、句柄会失效。快照是一次性的只读事实，界面按 id 重新匹配；持有指针就会拿到悬空对象，或者不小心改到 B 的状态。
2. **服务返回"请求已发出"，界面凭什么说操作成功？**
   不凭什么。界面只在 `operationFinished` 且 `success=true` 时显示成功；平台调用返回只代表请求被受理。
3. **`main.cpp` 现在 new 的是假服务，将来换真服务要改几处？**
   一处：把 `FakeWorkspaceService` 换成真实实现即可，`OverviewWindow` 的构造参数类型是抽象基类指针。
