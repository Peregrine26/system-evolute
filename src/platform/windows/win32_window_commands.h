#pragma once

#include <cstdint>
#include <string>

namespace crossdesk {

// C prototype command facade. A's adapter also exposes request/status commands;
// this facade adds bounded observation for the current bool-based UI service.
// Calls may observe for up to 1.5 s and MUST run off the GUI/target owner thread.
// LiveService runs them on a worker. No input queues are attached. A timeout
// means unconfirmed, not canceled: a busy target may execute the request later.
class Win32WindowCommands {
public:
    // 把窗口切到前台。最小化的先还原。
    // 失败时把原因写进 error（可为 nullptr）。
    bool activate(std::uintptr_t handle, std::wstring* error = nullptr);

    // 把窗口移到目标矩形（屏幕坐标，单位与窗口矩形一致）。
    // 不改 Z 序、不激活窗口，所以不会打断用户正在做的事。
    bool place(std::uintptr_t handle,
               int left,
               int top,
               int width,
               int height,
               std::wstring* error = nullptr);
};

}  // namespace crossdesk
