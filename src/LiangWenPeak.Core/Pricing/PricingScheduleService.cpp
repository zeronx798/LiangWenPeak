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

    }

    PricingScheduleService::PricingScheduleService() noexcept
        : m_calendar(PricingCalendar::LoadManaged())
    {
    }

    PricingScheduleService::PricingScheduleService(PricingCalendar calendar) noexcept
        : m_calendar(std::move(calendar))
    {
    }

    PricingPeriod PricingScheduleService::GetPeriod(
        std::chrono::sys_days const date,
        std::chrono::seconds const timeOfDay) const noexcept
    {
        if (IsWeekend(std::chrono::weekday{ date }) || m_calendar.IsAllDayOffPeak(date))
        {
            return PricingPeriod::Valley;
        }

        const bool morningPeak = timeOfDay >= 9h && timeOfDay < 12h;
        const bool afternoonPeak = timeOfDay >= 14h && timeOfDay < 18h;
        return morningPeak || afternoonPeak ? PricingPeriod::Peak : PricingPeriod::Valley;
    }

    PricingPeriod PricingScheduleService::GetPricingPeriod(time::BeijingTime const& beijingTime) const noexcept
    {
        return GetPeriod(beijingTime.LocalDate(), beijingTime.TimeOfDay());
    }

    PricingTransition PricingScheduleService::GetNextTransition(time::BeijingTime const& beijingTime) const noexcept
    {
        const auto currentDate = beijingTime.LocalDate();
        const auto currentLocalInstant = currentDate + beijingTime.TimeOfDay();
        const auto findNextTransition = [this](
            std::chrono::sys_days const fromDate,
            std::chrono::seconds const fromTimeOfDay) noexcept
        {
            for (std::chrono::days dayOffset{};; dayOffset += std::chrono::days{ 1 })
            {
                const auto candidateDate = fromDate + dayOffset;
                for (auto const boundary : PricingBoundaries)
                {
                    if (dayOffset == std::chrono::days::zero() && boundary <= fromTimeOfDay)
                    {
                        continue;
                    }

                    const auto periodBefore = GetPeriod(candidateDate, boundary - 1s);
                    const auto periodAfter = GetPeriod(candidateDate, boundary);
                    if (periodBefore != periodAfter)
                    {
                        return TransitionPoint{ candidateDate, boundary, periodAfter };
                    }
                }
            }
        };
        const auto next = findNextTransition(currentDate, beijingTime.TimeOfDay());
        const auto rangeEnd = findNextTransition(next.date, next.timeOfDay);
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
                std::chrono::year_month_day{ next.date },
                std::chrono::year_month_day{ rangeEnd.date },
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
