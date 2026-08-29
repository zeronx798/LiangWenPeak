#pragma once

#include <chrono>

namespace liangwenpeak::notifications
{
    inline constexpr auto DefaultAdvanceMinutes = std::chrono::minutes{ 10 };
    inline constexpr auto MinimumAdvanceMinutes = std::chrono::minutes{ 1 };
    inline constexpr auto MaximumAdvanceMinutes = std::chrono::minutes{ 30 };

    struct NotificationSettings
    {
        bool enabled = false;
        bool advanceEnabled = true;
        std::chrono::minutes advanceMinutes = DefaultAdvanceMinutes;
    };

    [[nodiscard]] bool IsValidAdvanceMinutes(std::chrono::minutes value) noexcept;
    [[nodiscard]] bool TryParseAdvanceMinutes(
        double value,
        std::chrono::minutes& result) noexcept;
    void NormalizeNotificationSettings(NotificationSettings& settings) noexcept;
}
