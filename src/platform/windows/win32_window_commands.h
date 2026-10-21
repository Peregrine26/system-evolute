#pragma once

#include <cstdint>
#include <string>

namespace crossdesk {

// 平台命令层：置前与设置位置的最小实现。
//
// 为什么单独一个文件、而不是写进界面代码：
// 界面层（src/ui）不许出现 windows.h，也不许出现 HWND——这是三人分工的边界。
// 但 A 的 Win32ApiAdapter 目前只有查询接口（枚举/标题/进程/矩形），没有命令，
// 于是"点一下真的切过去"这件事没人做。这里先给一份兜底实现，位置放在
// src/platform/windows/ 下，属于 A 的地盘。
//
// A 把他的命令接口写好后：删掉这两个文件，把 LiveService 里的两处调用换成
// 适配器的命令，界面代码一行都不用动。
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
