#include "NotificationSettings.h"

#include <cmath>
#include <cstdint>

namespace liangwenpeak::notifications
{
    bool IsValidAdvanceMinutes(std::chrono::minutes const value) noexcept
    {
        return value >= MinimumAdvanceMinutes && value <= MaximumAdvanceMinutes;
    }

    bool TryParseAdvanceMinutes(
        double const value,
        std::chrono::minutes& result) noexcept
    {
        if (!std::isfinite(value) || std::trunc(value) != value
            || value < static_cast<double>(MinimumAdvanceMinutes.count())
            || value > static_cast<double>(MaximumAdvanceMinutes.count()))
        {
            return false;
        }

        result = std::chrono::minutes{ static_cast<std::int64_t>(value) };
        return true;
    }

    void NormalizeNotificationSettings(NotificationSettings& settings) noexcept
    {
        if (!IsValidAdvanceMinutes(settings.advanceMinutes))
        {
            settings.advanceMinutes = DefaultAdvanceMinutes;
        }
    }
}
