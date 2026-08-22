#pragma once

#include "BeijingTime.h"

#include <array>
#include <chrono>

namespace liangwenpeak::time
{
    inline constexpr auto DefaultBalanceRefreshInterval = std::chrono::minutes{ 1 };
    inline constexpr std::array<std::chrono::minutes, 6> SupportedBalanceRefreshIntervals = {
        std::chrono::minutes{ 1 },
        std::chrono::minutes{ 5 },
        std::chrono::minutes{ 10 },
        std::chrono::minutes{ 15 },
        std::chrono::minutes{ 30 },
        std::chrono::minutes{ 60 },
    };

    [[nodiscard]] bool IsSupportedBalanceRefreshInterval(std::chrono::minutes interval) noexcept;

    [[nodiscard]] std::chrono::sys_seconds GetNextAlignedRefreshTime(
        BeijingTime const& now,
        std::chrono::minutes interval);
}
