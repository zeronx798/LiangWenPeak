#include "BalanceRefreshSchedule.h"

#include <algorithm>
#include <stdexcept>

namespace liangwenpeak::time
{
    bool IsSupportedBalanceRefreshInterval(std::chrono::minutes const interval) noexcept
    {
        return std::find(
            SupportedBalanceRefreshIntervals.begin(),
            SupportedBalanceRefreshIntervals.end(),
            interval) != SupportedBalanceRefreshIntervals.end();
    }

    std::chrono::sys_seconds GetNextAlignedRefreshTime(
        BeijingTime const& now,
        std::chrono::minutes const interval)
    {
        if (!IsSupportedBalanceRefreshInterval(interval))
        {
            throw std::invalid_argument("Unsupported balance refresh interval");
        }

        const auto intervalSeconds = std::chrono::duration_cast<std::chrono::seconds>(interval).count();
        const auto timeOfDay = now.TimeOfDay();
        const auto elapsedSeconds = timeOfDay.count();
        const auto nextElapsedSeconds = ((elapsedSeconds / intervalSeconds) + 1) * intervalSeconds;
        const auto remaining = std::chrono::seconds{ nextElapsedSeconds } - timeOfDay;
        return now.UtcInstant() + remaining;
    }

    bool IsCurrentRefreshTarget(
        std::chrono::sys_seconds const now,
        std::chrono::sys_seconds const target,
        std::chrono::minutes const interval) noexcept
    {
        return IsSupportedBalanceRefreshInterval(interval)
            && now >= target
            && now < target + interval;
    }
}
