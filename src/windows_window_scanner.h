#pragma once

#include "platform/window_api_adapter.h"

#include <vector>

namespace crossdesk {

struct WindowScanResult {
    // False means the returned list may be incomplete; callers must not use
    // it to infer that previously known windows have disappeared.
    bool complete = false;
    std::vector<WindowInfo> windows;
};

class WindowsWindowScanner {
public:
    explicit WindowsWindowScanner(const WindowApiAdapter& api);

    [[nodiscard]] WindowScanResult scan_with_status() const;

    // Kept for the existing diagnostic executable. Core code should use
    // scan_with_status() before reconciling window ownership.
    [[nodiscard]] std::vector<WindowInfo> scan() const;

private:
    const WindowApiAdapter& api_;
};

}  // namespace crossdesk
