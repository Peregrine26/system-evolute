#pragma once

#include "window_info.h"

#include <cstdint>
#include <functional>
#include <string>

namespace crossdesk {

// This is the only handle representation exposed by the platform-neutral
// adapter interface. Windows maps it to HWND; X11 will later map it to Window.
using NativeWindowHandle = std::uintptr_t;

class WindowApiAdapter {
public:
    using WindowCallback = std::function<bool(NativeWindowHandle)>;

    virtual ~WindowApiAdapter() = default;

    // Calls callback once for each top-level window. Returning false stops
    // enumeration.
    virtual bool enumerate_top_level_windows(
        const WindowCallback& callback
    ) const = 0;

    virtual std::wstring get_window_title(
        NativeWindowHandle window
    ) const = 0;

    virtual std::uint32_t get_process_id(
        NativeWindowHandle window
    ) const = 0;

    virtual std::wstring get_process_name(
        std::uint32_t process_id
    ) const = 0;

    virtual bool get_window_bounds(
        NativeWindowHandle window,
        WindowBounds& bounds
    ) const = 0;

    virtual bool is_window_visible(
        NativeWindowHandle window
    ) const = 0;
};

}  // namespace crossdesk
