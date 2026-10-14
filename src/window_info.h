#pragma once

#include <cstdint>
#include <string>

namespace crossdesk {

struct WindowBounds {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

struct WindowInfo {
    // Stored as an integer to keep Win32 HWND/X11 Window types out of the
    // platform-neutral data model.
    std::uintptr_t native_handle = 0;
    std::uint32_t process_id = 0;
    std::wstring process_name;
    std::wstring title;
    WindowBounds bounds;
    bool visible = false;
};

}  // namespace crossdesk
