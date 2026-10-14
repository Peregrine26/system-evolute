#pragma once

#include "platform/window_api_adapter.h"

#include <vector>

namespace crossdesk {

class WindowsWindowScanner {
public:
    explicit WindowsWindowScanner(const WindowApiAdapter& api);

    [[nodiscard]] std::vector<WindowInfo> scan() const;

private:
    const WindowApiAdapter& api_;
};

}  // namespace crossdesk
