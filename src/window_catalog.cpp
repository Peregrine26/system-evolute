#include "window_catalog.h"
namespace crossdesk {
std::vector<WindowInfo> applicationWindows(const std::vector<WindowInfo>& windows,
                                         std::uint32_t ownProcessId) {
    std::vector<WindowInfo> result;
    for (const auto& window : windows) {
        if (window.process_id == ownProcessId || !window.native_handle ||
            !window.visible || window.title.empty()) continue;
        result.push_back(window);
    }
    return result;
}
}
