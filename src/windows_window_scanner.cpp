#include "windows_window_scanner.h"

#include <utility>
#include <vector>

namespace crossdesk {
WindowsWindowScanner::WindowsWindowScanner(const WindowApiAdapter& api)
    : api_(api) {}

std::vector<WindowInfo> WindowsWindowScanner::scan() const {
    std::vector<WindowInfo> windows;

    api_.enumerate_top_level_windows(
        [this, &windows](NativeWindowHandle window) {
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
            windows.push_back(std::move(info));
            return true;
        }
    );

    return windows;
}

}  // namespace crossdesk
