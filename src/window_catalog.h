#pragma once
#include "window_info.h"
#include <vector>
namespace crossdesk {
// Shell surfaces have already been removed by the platform scanner. Filter
// ownership by PID, never by a title substring or executable name.
std::vector<WindowInfo> applicationWindows(const std::vector<WindowInfo>& windows,
                                         std::uint32_t ownProcessId);
}
