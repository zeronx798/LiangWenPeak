#include "PricingScheduleService.h"

#include <array>

namespace liangwenpeak::pricing
{
    using namespace std::chrono_literals;

    namespace
    {
        constexpr std::array PricingBoundaries{ 9h, 12h, 14h, 18h };

        struct TransitionPoint
        {
            std::chrono::sys_days date;
            std::chrono::seconds timeOfDay;
            PricingPeriod nextPeriod;
        };

        [[nodiscard]] bool IsWeekend(std::chrono::weekday const weekday) noexcept
        {
            return weekday == std::chrono::Saturday || weekday == std::chrono::Sunday;
        }

        [[nodiscard]] PricingPeriod GetPeriod(
            std::chrono::sys_days const date,
            std::chrono::seconds const timeOfDay) noexcept
        {
            if (IsWeekend(std::chrono::weekday{ date }))
            {
                return PricingPeriod::Valley;
            }

            const bool morningPeak = timeOfDay >= 9h && timeOfDay < 12h;
            const bool afternoonPeak = timeOfDay >= 14h && timeOfDay < 18h;
            return morningPeak || afternoonPeak ? PricingPeriod::Peak : PricingPeriod::Valley;
        }

        [[nodiscard]] TransitionPoint FindNextTransition(
            std::chrono::sys_days const currentDate,
            std::chrono::seconds const currentTimeOfDay) noexcept
        {
            for (int dayOffset = 0; dayOffset <= 7; ++dayOffset)
            {
                const auto candidateDate = currentDate + std::chrono::days{ dayOffset };
                for (auto const boundary : PricingBoundaries)
                {
                    if (dayOffset == 0 && boundary <= currentTimeOfDay)
                    {
                        continue;
                    }

                    const auto periodBefore = GetPeriod(candidateDate, boundary - 1s);
                    const auto periodAfter = GetPeriod(candidateDate, boundary);
                    if (periodBefore != periodAfter)
                    {
                        return { candidateDate, boundary, periodAfter };
                    }
                }
            }

            // A weekday 09:00 boundary always exists within seven days.
            const auto fallbackDate = currentDate + std::chrono::days{ 7 };
            return { fallbackDate, 9h, PricingPeriod::Peak };
        }
    }

    PricingPeriod PricingScheduleService::GetPricingPeriod(time::BeijingTime const& beijingTime) const noexcept
    {
        return GetPeriod(beijingTime.LocalDate(), beijingTime.TimeOfDay());
    }

    PricingTransition PricingScheduleService::GetNextTransition(time::BeijingTime const& beijingTime) const noexcept
    {
        const auto currentDate = beijingTime.LocalDate();
        const auto currentLocalInstant = currentDate + beijingTime.TimeOfDay();
        const auto next = FindNextTransition(currentDate, beijingTime.TimeOfDay());
        const auto rangeEnd = FindNextTransition(next.date, next.timeOfDay);
        const auto nextLocalInstant = next.date + next.timeOfDay;

        return {
            beijingTime.UtcInstant() + (nextLocalInstant - currentLocalInstant),
            next.nextPeriod,
            {
                next.timeOfDay,
                rangeEnd.timeOfDay,
                next.date - currentDate,
                rangeEnd.date - currentDate,
                std::chrono::weekday{ next.date },
                std::chrono::weekday{ rangeEnd.date },
            },
        };
    }

    std::chrono::seconds PricingScheduleService::GetRemainingTime(time::BeijingTime const& beijingTime) const noexcept
    {
        return GetNextTransition(beijingTime).utcInstant - beijingTime.UtcInstant();
    }

    PricingSnapshot PricingScheduleService::GetSnapshot(time::BeijingTime const& beijingTime) const noexcept
    {
        const auto transition = GetNextTransition(beijingTime);
        return { GetPricingPeriod(beijingTime), transition, transition.utcInstant - beijingTime.UtcInstant() };
    }
}
