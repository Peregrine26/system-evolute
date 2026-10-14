#pragma once

#include "platform/window_api_adapter.h"

namespace crossdesk {

class Win32ApiAdapter final : public WindowApiAdapter {
public:
    bool enumerate_top_level_windows(
        const WindowCallback& callback
    ) const override;

    std::wstring get_window_title(
        NativeWindowHandle window
    ) const override;

    std::uint32_t get_process_id(
        NativeWindowHandle window
    ) const override;

    std::wstring get_process_name(
        std::uint32_t process_id
    ) const override;

    bool get_window_bounds(
        NativeWindowHandle window,
        WindowBounds& bounds
    ) const override;

    bool is_window_visible(
        NativeWindowHandle window
    ) const override;
};

}  // namespace crossdesk
