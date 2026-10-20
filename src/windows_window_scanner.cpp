#include "windows_window_scanner.h"

#include <utility>
#include <vector>

namespace crossdesk {
WindowsWindowScanner::WindowsWindowScanner(const WindowApiAdapter& api)
    : api_(api) {}

WindowScanResult WindowsWindowScanner::scan_with_status() const {
    WindowScanResult result;

    result.complete = api_.enumerate_top_level_windows(
        [this, &result](NativeWindowHandle window) {
            if (!api_.is_manageable_window(window)) return true;
            WindowBounds bounds;
            if (!api_.get_window_bounds(window, bounds)) {
                return true;
            }

            const std::uint32_t process_id =
                api_.get_process_id(window);

            WindowInfo info;
            info.native_handle = window;
            info.process_id = process_id;
            info.process_name = api_.get_process_name(process_id);
            info.title = api_.get_window_title(window);
            info.bounds = bounds;
            info.visible = api_.is_window_visible(window);
            info.minimized = api_.is_window_minimized(window);
            result.windows.push_back(std::move(info));
            return true;
        }
    );

    return result;
}

std::vector<WindowInfo> WindowsWindowScanner::scan() const {
    return scan_with_status().windows;
}

}  // namespace crossdesk
