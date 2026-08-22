#pragma once

#include <chrono>

namespace liangwenpeak::services
{
    class SettingsService final
    {
    public:
        [[nodiscard]] std::chrono::minutes LoadBalanceRefreshInterval() const noexcept;
        [[nodiscard]] bool SaveBalanceRefreshInterval(std::chrono::minutes interval) const noexcept;
    };
}
