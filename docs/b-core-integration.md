# B 模块联调说明：窗口筛选、选择校验与两窗布局

> 对应分支 `b/core-mvp`。本文说明 B 的核心逻辑如何接进现有仓库，以及在 A 现有的扫描器、适配器代码里发现的三处会影响筛选与选择的问题。**`main` 分支未改动。**

## 1. 这次加了什么

| 文件 | 作用 |
| --- | --- |
| `src/core/core_types.h` | `WindowId`、`ScanSnapshot`、`boundsWidth/Height`、`sameBounds` |
| `src/core/window_filter.h/.cpp` | 交付 1：保留 `visible && 标题非空` 的窗口 |
| `src/core/window_selection.h/.cpp` | 交付 2：校验两个标识，区分「已关闭」与「扫描不全」 |
| `src/core/window_layout.h/.cpp` | 交付 3：50:50 / 60:40 两窗布局，固定间距 12 |
| `tests/core_tests.cpp` | 纯核心测试，6 个场景 / 61 项检查 |
| `tests/pipeline_tests.cpp` | A→B 联调测试，用假适配器驱动**真实的** `WindowsWindowScanner`，4 个场景 / 18 项检查 |

`CMakeLists.txt` 新增 `crossdesk_core`（STATIC，公开依赖 `crossdesk_window_api`）、`crossdesk_core_tests`、`crossdesk_pipeline_tests` 三个目标，以及 `CROSSDESK_BUILD_TESTS` 开关（默认 ON）。

构建与运行（不需要 Qt，也不需要真实桌面）：

```powershell
cmake -S . -B build-core -G "Visual Studio 17 2022" -A x64 -DCROSSDESK_BUILD_GUI=OFF
cmake --build build-core --config Release
.\build-core\Release\crossdesk_core_tests.exe      # checks: 61, failed: 0
.\build-core\Release\crossdesk_pipeline_tests.exe  # checks: 18, failed: 0
```

两份测试都在 `ctest` 里注册了，`ctest -C Release` 可一并跑。

## 2. 与 A 现有类型对齐后改了哪些地方

按 `src/window_info.h` 的实际字段重写了数据契约，不再有「照任务单猜字段」的临时类型：

| 项 | 实验版（`CrossDesk-CourseLab`） | 本分支（对齐 A） |
| --- | --- | --- |
| 标识字段 | `WindowId id` | `native_handle`，类型 `WindowId = std::uintptr_t` |
| 标题 | `std::string` | `std::wstring`（与 `GetWindowTextW` 一致，边界上不做转换） |
| 进程 | `std::string process` | `process_id` + `process_name`，**只显示，不参与任何判断** |
| 矩形 | `WindowBounds` 带 `width()/height()/operator==` | A 的结构体没有成员函数，改用 `boundsWidth()`、`boundsHeight()`、`sameBounds()` 三个自由函数 |
| 扫描完整性 | `ScanSnapshot{windows, complete}` | A 的 `scan()` 只返回 `vector<WindowInfo>`，`complete` 暂由 B 侧提供（见下一节） |

`WindowId` 没有直接复用 `NativeWindowHandle`：核心不调用适配器接口，就不该包含 `platform/window_api_adapter.h`。两者是否相等由 `tests/core_tests.cpp` 里的 `static_assert` 守住，一旦有人改动其中一处，编译会立刻失败，不会静默漂移。

另外给 `crossdesk_core` 和两个测试目标加了 MSVC 的 `/utf-8`：源文件里有中文提示语，不加这个开关 MSVC 会按系统代码页读源码，中文字面量会乱码。C 的 Qt 目标以后也需要同样处理。

## 3. 在 A 的代码里发现的三个问题（会影响 B 的结论）

### 3.1 查询失败的窗口会被整条丢弃，和「已关闭」长得一模一样

`src/windows_window_scanner.cpp:16-18`：`get_window_bounds()` 返回 false 时直接 `return true` 跳过该窗口，记录不会进结果。`GetWindowRect` 在窗口正在销毁、位于其他虚拟桌面、或权限不足时会失败——**窗口明明还在，扫描结果里却没有它**。

核心逻辑只会看到「不在列表里」，于是报「所选窗口已经关闭」(`tests/pipeline_tests.cpp` 场景 4 复现了这个结果)。这不是 B 能修的：需要 A 在扫描时告知「这次扫全了没有」。

建议的最小改动（对 C 影响只有一行）：

```cpp
// src/windows_window_scanner.h
struct ScanResult {
    std::vector<WindowInfo> windows;
    bool complete = true;   // false: 本次至少有一个窗口查询失败或被提前中断
};

[[nodiscard]] ScanResult scan() const;   // 原来是 std::vector<WindowInfo>
```

只要 `complete` 有真实来源，B 的 `ScanSnapshot` 就可以直接换成 `ScanResult`，筛选和校验的代码一行不用改（字段名一致）。

### 3.2 枚举完全没有过滤，任务栏和桌面会流到 B 这里

`src/platform/windows/win32_api_adapter.cpp:38` 直接用 `EnumWindows` 枚举全部顶层窗口，没有排除 `Shell_TrayWnd`、`Progman`、`WorkerW` 等壳层窗口。任务单要求这些在交给 B 之前就排除掉，因为「任务栏」「Program Manager」这类窗口**标题非空且可见**，只凭标题和可见性根本认不出来。

`tests/pipeline_tests.cpp` 场景 2 把这一点写成了断言：假数据里放一个标题为「任务栏」的窗口，它顺利通过了 B 的两条规则。所以这条不是「B 以后补规则」，而是接口分工必须在 A 侧落实。判断壳层的常用做法是 `GetShellWindow()`、`FindWindow(L"Shell_TrayWnd", nullptr)`、`GetWindowLong(GWL_EXSTYLE) & WS_EX_TOOLWINDOW`，具体口径由 A 定。

### 3.3 `visible` 用的是 `IsWindowVisible`，最小化窗口仍然是 `true`

`src/platform/windows/win32_api_adapter.cpp:132-136`。Win32 语义下最小化不等于隐藏，`IsWindowVisible` 只看 `WS_VISIBLE` 位。这对筛选其实**是好事**：最小化的窗口仍然是候选，用户能在列表里找到并恢复它。

需要确认的只有一点：如果以后「切换 Workspace」用 `ShowWindow(SW_HIDE)` 实现，隐藏后 `visible` 会变成 false，进了 `NotCandidate`；这是符合预期的。真正要避免的是有人为了「让最小化窗口不出现在列表里」而把 `IsWindowVisible` 换成 `IsIconic` 判断，那会让最小化窗口无法被找回。

### 3.4 一个不影响结论但值得知道的点

`get_process_name()` 读不到进程时返回 `"<access denied>"` / `"<unavailable>"` 这类占位字符串（`win32_api_adapter.cpp:83-98`）。核心逻辑只把它当显示用的文本，任何字符串都不会被当作判断依据，所以没有影响；但 C 在列表里要按「未知应用」这类中性文案显示，不要把占位符原样抛给用户。

## 4. 给 C 的调用顺序

```cpp
// 1) 列表：A 扫描 -> B 筛选
std::vector<crossdesk::WindowInfo> scanned = scanner.scan().windows;   // 现在是 vector
std::vector<crossdesk::WindowInfo> candidates = crossdesk::selectCandidates(scanned);

// 2) 用户选中两个 native_handle -> B 校验
crossdesk::ScanSnapshot snapshot{scanned, /*complete=*/true};
crossdesk::SelectionResult picked = crossdesk::validateSelection(snapshot, first, second);
if (!picked.ok()) { show(picked.message); return; }        // message 可直接显示

// 3) 选模式 -> B 算矩形 -> C 画预览
crossdesk::LayoutResult layout = crossdesk::computeLayout(workArea, crossdesk::LayoutMode::Equal5050);
if (layout.ok()) { drawPreview(layout.left, layout.right); }  // 否则 show(layout.message)
```

`layoutModeName(mode)` 已经给出中文模式名，`kWindowGapUnits` 给预览画间距用。工作区矩形由 C 或 A 提供**单块显示器**的可用区域（已扣任务栏），B 不猜分辨率、不做 DPI 换算。

## 5. 验收对照

| 任务单要求 | 测试位置 | 结果 |
| --- | --- | --- |
| 三个假窗口，只留正常的 | core 场景 1 | 1 个 |
| 文件资源管理器、Windows 设置保留；不按进程名排除 | core 场景 2 | 4 个全保留 |
| 同标题、同 PID 仍是两个窗口 | core 场景 2 | 通过 |
| 选同一个窗口两次 → 可解释失败 | core 场景 3 | SameWindow |
| 选已关闭窗口 → 可解释失败 | core 场景 3 | WindowGone |
| 扫描不完整不误报已关闭 | core 场景 3 | ScanIncomplete |
| 工作区 (100,50,1101,850) 等宽 | core 场景 4 | 左 (100,50,594,850) / 右 (606,50,1101,850) |
| 同工作区 60:40 | core 场景 4 | 左 (100,50,693,850) / 右 (705,50,1101,850) |
| 不重叠、不越界、宽度守恒 | core 场景 4/5 | `checkGeometry()` 逐条断言 |
| 空矩形 / 不足 14 单位 → 失败 | core 场景 5 | InvalidWorkArea / TooNarrow |
| 原点不在 (0,0) | core 场景 5 | (-1920,0,0,1080) 正确 |
| 鼠标与快捷键结果一致 | core 场景 6 | 同一纯函数，无状态 |
| 不依赖 Qt / Win32 / 真实桌面 | 全部 | 只构造结构体与假适配器 |

## 6. 待三人确认

1. `scan()` 是否改成返回 `ScanResult`（3.1）。这是唯一一条会影响 B 结论正确性的接口问题。
2. 壳层窗口的排除口径（3.2），由 A 在适配器里实现，B 不硬编码进程名。
3. `WindowBounds` 是否加 `width()/height()` 成员。加了更顺手，B 可以删掉两个自由函数；不加也不影响结果。
4. `complete` 的判定标准：是「枚举回调全部走完」就算完整，还是「过程中有任何一次查询失败」就算不完整？后者更保守，建议采用。
