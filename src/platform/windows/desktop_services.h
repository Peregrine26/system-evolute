#pragma once

#include "platform/window_api_adapter.h"

#include <memory>
#include <vector>

namespace crossdesk {

enum class DesktopStatus { Ready, InvalidWindow, InvalidRectangle, Unavailable };
struct DesktopResult {
    DesktopStatus status = DesktopStatus::Unavailable;
    // Win32 error for hotkeys; HRESULT bit pattern for DWM.
    std::uint32_t platform_error = 0;
    bool ready() const { return status == DesktopStatus::Ready; }
};

// Construct, use and destroy on the destination window's UI thread.
// Qt's nativeEvent forwards message/wParam/lParam to handle_message().
class OverviewHotkey {
public:
    OverviewHotkey();
    ~OverviewHotkey();
    OverviewHotkey(const OverviewHotkey&) = delete;
    OverviewHotkey& operator=(const OverviewHotkey&) = delete;
    DesktopResult register_for(NativeWindowHandle destination);
    void reset();
    bool handle_message(std::uint32_t message, std::uintptr_t wparam,
                        std::intptr_t lparam) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct PreviewRequest {
    NativeWindowHandle source = 0;
    // Destination TOP-LEVEL client coordinates in native pixels, not Qt DIPs.
    WindowBounds destination;
};

// Keeps all HTHUMBNAIL values private. DWM continuously updates the content;
// call refresh on a new scan and replace on resize / mode transitions.
class WindowPreviews {
public:
    WindowPreviews();
    ~WindowPreviews();
    WindowPreviews(const WindowPreviews&) = delete;
    WindowPreviews& operator=(const WindowPreviews&) = delete;
    std::vector<DesktopResult> replace(NativeWindowHandle destination,
                                      const std::vector<PreviewRequest>& requests);
    void refresh();  // Releases sources/destinations that have disappeared.
    void clear();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace crossdesk
