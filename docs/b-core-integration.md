# B 模块联调说明：窗口筛选、选择校验与两窗布局

> 对应分支 `b/core-mvp`，基线为 `main@c5174ea`（已含 A 的 Windows 平台服务、Qt 预览集成与 `agent.md` 协作规范）。本文说明 B 的核心逻辑如何接进现有仓库、与 A 接口的对齐结果，以及还剩下的一处语义缺口。**`main` 分支未改动。**

`9f819ca..c5174ea` 这段只改了 Qt 桥接、桌面服务自检、CI 与文档，`window_info.h`、`windows_window_scanner.*`、`layout_executor.*` 一行未动，所以 B 的代码不需要跟着改，只需变基。

## 1. 这次加了什么

| 文件 | 作用 |
| --- | --- |
| `src/core/core_types.h` | `WindowId`、`ScanSnapshot`、`boundsWidth/Height`、`sameBounds` |
| `src/core/window_filter.h/.cpp` | 交付 1：保留 `visible && 标题非空` 的窗口 |
| `src/core/window_selection.h/.cpp` | 交付 2：校验两个标识，区分「已关闭」与「扫描不全」 |
| `src/core/window_layout.h/.cpp` | 交付 3：50:50 / 60:40 两窗布局，固定间距 12 |
| `tests/core_tests.cpp` | 纯核心测试，6 个场景 / 63 项检查 |
| `tests/pipeline_tests.cpp` | A→B 联调测试，用假适配器驱动**真实的** `WindowsWindowScanner` 与 `LayoutExecutor`，6 个场景 / 31 项检查 |

`CMakeLists.txt` 新增 `crossdesk_core`（STATIC，公开依赖 `crossdesk_window_api`）、`crossdesk_core_tests`、`crossdesk_pipeline_tests`，测试都挂在 A 已经引入的 `include(CTest)` / `BUILD_TESTING` 下，没有再造一个开关。

构建与运行（不需要 Qt，也不需要真实桌面）：

```powershell
cmake -S . -B build-core -G "Visual Studio 17 2022" -A x64 -DCROSSDESK_BUILD_GUI=OFF
cmake --build build-core --config Release
ctest --test-dir build-core -C Release
```

本机实测（Windows 11、MSVC 14.44、Release）：

```text
ctest -C Release -LE interactive-desktop
1/3 core_tests ........... Passed  0.52 sec   （B，63 项检查）
2/3 layout_executor_test . Passed  0.34 sec   （A）
3/3 pipeline_tests ....... Passed  0.98 sec   （B，31 项检查）
100% tests passed, 0 tests failed out of 3
```

标了 `interactive-desktop` 的 `win32_backend_selftest` / `desktop_services_selftest` 会移动和隐藏真实窗口，本地未执行，属于待验证项；它们不在本分支的行为范围内，也没有被本分支改动。

云端检查：`.github/workflows/windows-platform.yml` 的 `platform` 任务原先只跑 `layout_executor_test`，本分支把它扩成 `^(layout_executor_test|core_tests|pipeline_tests)$`，让 B 的两个测试在没有 Qt 的快速任务里就能跑（它们不需要桌面）。`qt` 任务用的 `-LE interactive-desktop` 本来就会带上这两个用例，属于重复覆盖，不是唯一覆盖。

## 2. 与 A 的新接口对齐

A 在 `9f819ca` 里把之前提的四点全部补上了，B 因此改了三处：

| A 提供的 | B 的处理 |
| --- | --- |
| `WindowScanResult{complete, windows}` + `scan_with_status()` | `ScanSnapshot` 保留同样形状，默认值改成 `complete = false`（保守：没标过完整的快照不许用来判「已关闭」）。调用方一行转换：`ScanSnapshot{scan.windows, scan.complete}` |
| `is_manageable_window()`（壳层排除已进 `scan_with_status`） | 筛选规则里删掉「猜测壳层」的说明，任务栏不再可能进入候选；`pipeline` 场景 1/2 断言了这一点 |
| `WindowInfo::minimized` + `is_window_minimized()` | **不参与筛选**：最小化窗口仍带 `WS_VISIBLE`，必须留在列表里供用户找回。`minimized` 只给 C 画角标 |
| `get_primary_work_area()` | 工作区矩形从适配器拿，B 仍不猜分辨率、不做 DPI 换算 |
| `LayoutExecutor` + `WindowMoveTarget{window, process_id, bounds}` | 分工确认：**A 的注释写明「B supplies these rectangles; this class never computes a layout」**，B 只出矩形，A 负责移动与观察 |

`LayoutExecutor::find()` 会用 `process_id` 复核窗口，所以 B 的 `SelectionResult` 直接回带完整的 `WindowInfo`（含 `process_id`），C 拼装 `WindowMoveTarget` 时不需要再查一次：

```cpp
const auto scan = scanner.scan_with_status();
const crossdesk::ScanSnapshot snapshot{scan.windows, scan.complete};

const auto picked = crossdesk::validateSelection(snapshot, first, second);
if (!picked.ok()) { show(picked.message); return; }

crossdesk::WindowBounds work_area;
adapter.get_primary_work_area(work_area);
const auto layout = crossdesk::computeLayout(work_area, mode);
if (!layout.ok()) { show(layout.message); return; }

const std::array<crossdesk::WindowMoveTarget, 2> targets{{
    {picked.first.native_handle,  picked.first.process_id,  layout.left},
    {picked.second.native_handle, picked.second.process_id, layout.right},
}};
const auto execution = crossdesk::LayoutExecutor(adapter).submit(targets);
```

`pipeline` 场景 3 就是把上面这段跑通：B 算出左右各 954（工作区 1920、间距 12），执行器报 `Reached`，假适配器里的窗口坐标确实变成 `(0,0,954,1040)`。场景 4 让浏览器拒绝移动，执行器报 `Rejected` + `DifferentBounds`，窗口保持原样。

## 3. 仍然存在的一处语义缺口

`scan_with_status()` 现在是：

```cpp
result.complete = api_.enumerate_top_level_windows(...);   // 只看 EnumWindows 的返回值
```

`complete` 只反映「枚举有没有跑完」。但回调里 `get_window_bounds()` 失败时仍然是 `return true` 跳过该窗口——**这个窗口从列表里消失，而 `complete` 依旧是 `true`**。`GetWindowRect` 在窗口正在销毁、位于其他虚拟桌面、权限不足时都会失败，于是核心会对着一个还开着的窗口报「已关闭」。

`pipeline` 场景 6 把这个行为钉成了断言：扫描只返回 4 个窗口、`complete == true`，而 `validateSelection` 对浏览器给出 `WindowGone`。

建议的最小改法（对 B、C 都没有影响）：

```cpp
bool complete = true;
api_.enumerate_top_level_windows([&](NativeWindowHandle w) {
    ...
    if (!api_.get_window_bounds(w, bounds)) { complete = false; return true; }
    ...
});
result.complete = complete && enumerate_result;
```

也就是把「任何一次单窗口查询失败」也算作不完整。这样一来 B 会改口说「请刷新重试」，而不是误报已关闭。

## 4. 验收对照

| 任务单要求 | 测试位置 | 结果 |
| --- | --- | --- |
| 三个假窗口，只留正常的 | core 场景 1 | 1 个 |
| 文件资源管理器、Windows 设置保留；不按进程名排除 | core 场景 2 | 4 个全保留 |
| 同标题、同 PID 仍是两个窗口 | core 场景 2 | 通过 |
| 最小化窗口不被误删 | core 场景 2 / pipeline 场景 2 | 保留，`minimized` 透出给 UI |
| 选同一个窗口两次 → 可解释失败 | core 场景 3 | SameWindow |
| 选已关闭窗口 → 可解释失败 | core 场景 3 | WindowGone |
| 扫描不完整不误报已关闭 | core 场景 3 / pipeline 场景 5 | ScanIncomplete |
| 工作区 (100,50,1101,850) 等宽 | core 场景 4 | 左 (100,50,594,850) / 右 (606,50,1101,850) |
| 同工作区 60:40 | core 场景 4 | 左 (100,50,693,850) / 右 (705,50,1101,850) |
| 不重叠、不越界、宽度守恒 | core 场景 4/5 | `checkGeometry()` 逐条断言 |
| 空矩形 / 不足 14 单位 → 失败 | core 场景 5 | InvalidWorkArea / TooNarrow |
| 原点不在 (0,0) | core 场景 5 | (-1920,0,0,1080) 正确 |
| 鼠标与快捷键结果一致 | core 场景 6 | 同一纯函数，无状态 |
| 不依赖 Qt / Win32 / 真实桌面 | 全部 | 只构造结构体与假适配器 |

## 5. 待三人确认

1. 第 3 节那处 `complete` 语义（唯一影响结论正确性的一条）。
2. `ScanSnapshot` 与 `WindowScanResult` 现在是一对同形结构。建议把 `WindowScanResult` 从 `windows_window_scanner.h` 挪到 `window_info.h`（它只是数据），B 就能删掉 `ScanSnapshot` 直接复用；现在保留的代价只是调用方多写一行转换。
3. `WindowBounds` 是否加 `width()/height()` 成员。加了 B 可以删掉两个自由函数；不加也不影响结果。
4. `crossdesk_core` 目前只有 B 的三个 .cpp。C 接入 Qt 时直接 `target_link_libraries(... crossdesk_core)` 即可，注意也要开 `/utf-8`，否则界面里的中文会乱码。
