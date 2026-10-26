# PR #1 审查修复与验证

2026-10-27：基于 C 的 `5de55c0`，同步 `main` 的 `c5174ea`，保留 PR 的目标分支 `c`。
本轮按负责人要求直接修复，完成后由独立子代理会话只读审查，CI 与审查通过后才合并。
独立代理结论与最终 SHA 记录在 PR 中，不冒充另一位组员的 GitHub 身份。

| 原问题 | 修复与反例覆盖 |
| --- | --- |
| 叠放卡片双击激活错误目标 | 保存首次点击的 WindowId、位置和时限；重排后按该身份激活，目标已消失则不激活替补。UI 测试覆盖首击触发重排后再双击。 |
| DWM 返回子 HWND | `WindowCard` / `ThumbnailSlot::hostHandle()` 返回所属顶层句柄，不创建子 HWND；新增 `contentRect()` 与 `previewRect()`，后者是顶层客户区逻辑坐标。UI 测试覆盖坐标/句柄，桌面测试用自有来源实际注册。 |
| 真机测试误选同名窗口 | 按 CreateProcess 返回的 PID 查找并在操作前复核；创建同名不同 PID 的自有诱饵窗口验证排除。目标使用窗口模式和满足最小尺寸的 1000×680。 |
| 真实候选静默截断为 10 个 | 移除数量截断，仅保留显示排序。注入假平台适配器，验证 12 个合法候选均保留；不依赖当前用户桌面。 |
| 悬停提起与回位动画竞争 | 启动提起时停止第一张卡的回位动画；重排/开始拖动也停止过期几何动画。UI 测试覆盖拖动、松手、立即悬停及回位时长结束后的最终几何。 |
| 独立审查新增：移动期间关闭窗口仍报成功 | `GetWindowRect` 失败时返回 false 和可读错误。隐藏自有窗口在 `WM_WINDOWPOSCHANGED` 中关闭，验证拒绝假成功，并保留正常移动和无效尺寸控制组；该测试进入平台与 Qt CI。 |

扫描还改用 `scan_with_status()`：不完整枚举保留上次完整列表并报告失败；初始化和
Ctrl+R 不再用说明/成功文字覆盖该错误。假适配器测试同时覆盖服务与主窗口快捷键路径。

## A 接口调用与边界

`hostHandle()` 是顶层目标；`previewRect()` 是客户区内容矩形；`screenRect()` 仍表示
屏幕逻辑坐标，仅用于原有调用兼容，不能直接交给 DWM。调用 A 的
`QtDesktopBridge::set_previews({{source, widget->previewRect()}})` 时，由 A 统一换算 DPI。
`setPreview(QPixmap)` 是普通位图回退，与 DWM 的合成关系是不同路径。

PR #4 中 A 的 `set_widget_previews` 可进一步直接接收控件与 `contentRect()`，并检查
祖先淡入/兄弟遮挡；本 PR 不依赖未合并的 #4。动画和浮动状态条的遮挡处理仍需在
正式实时预览接线时按 A/C 接入说明处理。这里验证接口可注册，不声称已完成正式 UI
的 DWM、全局快捷键、两窗排列或整个课程验收。

## 验证

Windows / Qt 6.8.3 / MinGW 13.1.0：

```powershell
. .\scripts\dev-env.ps1
cmake -S . -B build-review -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build-review --parallel 4
ctest --test-dir build-review --output-on-failure
```

本机完整构建与 10/10 CTest 已通过，UI 冒烟 63 项断言通过。交互测试仅使用自有窗口，
真机命令测试只操作按 PID 锁定的子进程。已同步 `main` 的 Qt/MSVC CI，离屏 UI、
假适配器和生命周期测试在云端运行；DWM 与置前/移动等交互测试只在云端编译。

真实内容持续更新的像素变化、全局按键、系统应用兼容性及动画手感不由这些断言证明。
