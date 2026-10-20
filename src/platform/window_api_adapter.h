#pragma once

#include "window_info.h"

#include <cstdint>
#include <functional>
#include <string>

namespace crossdesk {

// This is the only handle representation exposed by the platform-neutral
// adapter interface. Windows maps it to HWND; X11 will later map it to Window.
using NativeWindowHandle = std::uintptr_t;

enum class WindowOperationStatus {
    ReachedTarget,
    AlreadyInState,
    RequestedUnconfirmed,
    InvalidWindow,
    InvalidArgument,
    Rejected,
};

struct WindowOperationResult {
    WindowOperationStatus status = WindowOperationStatus::Rejected;
    // Platform error code, if the API supplied one. Zero means unavailable.
    std::uint32_t platform_error = 0;

    [[nodiscard]] bool reached_target() const noexcept {
        return status == WindowOperationStatus::ReachedTarget ||
               status == WindowOperationStatus::AlreadyInState;
    }
};

class WindowApiAdapter {
public:
    using WindowCallback = std::function<bool(NativeWindowHandle)>;

    virtual ~WindowApiAdapter() = default;

    // Primary monitor work area; same coordinate context as window bounds.
    // Default preserves compatibility with existing test adapters.
    virtual bool get_primary_work_area(WindowBounds&) const { return false; }

    // Windows excludes shell surfaces before handing candidates to B.
    virtual bool is_manageable_window(NativeWindowHandle) const { return true; }

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

    virtual bool is_window_minimized(
        NativeWindowHandle window
    ) const = 0;

    // Requests are non-blocking where the platform permits. ReachedTarget
    // means an immediate observation matched; the caller must rescan later
    // because another process can change the window again.
    virtual WindowOperationResult hide_window(
        NativeWindowHandle window
    ) const = 0;

    // Shows without activating or restoring an originally minimized window.
    virtual WindowOperationResult show_window_without_activation(
        NativeWindowHandle window
    ) const = 0;

    virtual WindowOperationResult set_window_bounds(
        NativeWindowHandle window,
        const WindowBounds& bounds
    ) const = 0;

    virtual WindowOperationResult activate_window(
        NativeWindowHandle window
    ) const = 0;
};

}  // namespace crossdesk
