#pragma once

#include "PricingCalendar.h"
#include "../Time/BeijingTime.h"

#include <chrono>

namespace liangwenpeak::pricing
{
    enum class PricingPeriod
    {
        Valley,
        Peak,
    };

    struct PeriodRange
    {
        std::chrono::seconds start;
        std::chrono::seconds end;
        std::chrono::days startDayOffset;
        std::chrono::days endDayOffset;
        std::chrono::weekday startWeekday;
        std::chrono::weekday endWeekday;
        std::chrono::year_month_day startDate;
        std::chrono::year_month_day endDate;
    };

    struct PricingTransition
    {
        std::chrono::sys_seconds utcInstant;
        PricingPeriod nextPeriod;
        PeriodRange nextRange;
    };

    struct PricingSnapshot
    {
        PricingPeriod currentPeriod;
        PricingTransition nextTransition;
        std::chrono::seconds remaining;
    };

    class PricingScheduleService final
    {
    public:
        PricingScheduleService() noexcept;
        explicit PricingScheduleService(PricingCalendar calendar) noexcept;

        [[nodiscard]] PricingPeriod GetPricingPeriod(time::BeijingTime const& beijingTime) const noexcept;
        [[nodiscard]] PricingTransition GetNextTransition(time::BeijingTime const& beijingTime) const noexcept;
        [[nodiscard]] std::chrono::seconds GetRemainingTime(time::BeijingTime const& beijingTime) const noexcept;
        [[nodiscard]] PricingSnapshot GetSnapshot(time::BeijingTime const& beijingTime) const noexcept;

    private:
        [[nodiscard]] PricingPeriod GetPeriod(
            std::chrono::sys_days date,
            std::chrono::seconds timeOfDay) const noexcept;

        PricingCalendar m_calendar;
    };
}
