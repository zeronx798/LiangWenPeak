#include "TimeFormatter.h"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>

namespace liangwenpeak::time
{
    namespace
    {
        std::wstring FormatHourMinute(std::chrono::seconds const value)
        {
            const auto totalMinutes = std::chrono::duration_cast<std::chrono::minutes>(value).count();
            const auto hours = totalMinutes / 60;
            const auto minutes = totalMinutes % 60;

            std::wostringstream output;
            output.imbue(std::locale::classic());
            output << std::setfill(L'0') << std::setw(2) << hours << L':' << std::setw(2) << minutes;
            return output.str();
        }

        std::wstring FormatDate(std::chrono::year_month_day const date)
        {
            return std::to_wstring(static_cast<unsigned>(date.month())) + L" \u6708 "
                + std::to_wstring(static_cast<unsigned>(date.day())) + L" \u65e5";
        }

        std::wstring FormatWeekday(std::chrono::weekday const weekday)
        {
            return weekday == std::chrono::Monday ? L"\u5468\u4e00" : L"";
        }

        bool IsOrdinaryWeekendRange(pricing::PeriodRange const& range) noexcept
        {
            return std::chrono::sys_days{ range.endDate } - std::chrono::sys_days{ range.startDate }
                    == std::chrono::days{ 3 }
                && range.startWeekday == std::chrono::Friday
                && range.endWeekday == std::chrono::Monday;
        }

        bool IsOrdinaryStartDelay(pricing::PeriodRange const& range) noexcept
        {
            if (range.startDayOffset == std::chrono::days::zero())
            {
                return true;
            }

            const auto currentDate = std::chrono::sys_days{ range.startDate } - range.startDayOffset;
            const auto currentWeekday = std::chrono::weekday{ currentDate };
            if (range.startDayOffset == std::chrono::days{ 1 })
            {
                return currentWeekday != std::chrono::Friday
                    && currentWeekday != std::chrono::Saturday;
            }
            if (range.startDayOffset == std::chrono::days{ 2 })
            {
                return currentWeekday == std::chrono::Saturday
                    && range.startWeekday == std::chrono::Monday;
            }
            return range.startDayOffset == std::chrono::days{ 3 }
                && currentWeekday == std::chrono::Friday
                && range.startWeekday == std::chrono::Monday;
        }
    }

    std::wstring FormatClock(BeijingTime const& beijingTime)
    {
        const auto totalSeconds = beijingTime.TimeOfDay().count();
        const auto hours = totalSeconds / 3600;
        const auto minutes = (totalSeconds % 3600) / 60;
        const auto seconds = totalSeconds % 60;

        std::wostringstream output;
        output.imbue(std::locale::classic());
        output << std::setfill(L'0') << std::setw(2) << hours << L':'
               << std::setw(2) << minutes << L':' << std::setw(2) << seconds;
        return output.str();
    }

    std::wstring FormatDuration(std::chrono::seconds const duration)
    {
        const auto totalSeconds = std::max<std::int64_t>(0, duration.count());
        const auto hours = totalSeconds / 3600;
        const auto minutes = (totalSeconds % 3600) / 60;
        const auto seconds = totalSeconds % 60;

        std::wostringstream output;
        output.imbue(std::locale::classic());
        output << std::setfill(L'0') << std::setw(2) << hours << L':'
               << std::setw(2) << minutes << L':' << std::setw(2) << seconds;
        return output.str();
    }

    std::wstring FormatPeriodRange(pricing::PeriodRange const& range)
    {
        const auto rangeDaySpan = std::chrono::sys_days{ range.endDate }
            - std::chrono::sys_days{ range.startDate };
        const bool ordinaryWeekendRange = IsOrdinaryWeekendRange(range);
        const bool showEndDate = rangeDaySpan > std::chrono::days{ 1 } && !ordinaryWeekendRange;
        const bool showEndWeekday = ordinaryWeekendRange;
        const bool showStartDate = !IsOrdinaryStartDelay(range);
        const bool showStartWeekday = !showStartDate
            && (range.startDayOffset > std::chrono::days{ 1 }
                || (range.startDayOffset == std::chrono::days{ 1 }
                    && range.startWeekday == std::chrono::Monday));

        auto start = FormatHourMinute(range.start);
        if (showStartDate)
        {
            start = FormatDate(range.startDate) + L" " + start;
        }
        else if (showStartWeekday)
        {
            start = FormatWeekday(range.startWeekday) + L" " + start;
        }

        auto end = FormatHourMinute(range.end);
        if (showEndDate)
        {
            end = FormatDate(range.endDate) + L" " + end;
        }
        else if (showEndWeekday)
        {
            end = FormatWeekday(range.endWeekday) + L" " + end;
        }

        return start + L" - " + end;
    }

    std::wstring FormatCnyBalance(double const balance)
    {
        std::wostringstream output;
        output.imbue(std::locale::classic());
        output << L'\u00a5' << L' ' << std::fixed << std::setprecision(2) << balance;
        return output.str();
    }

    std::wstring FormatLastUpdated(std::chrono::seconds elapsed)
    {
        elapsed = std::max(elapsed, std::chrono::seconds::zero());
        if (elapsed < std::chrono::seconds{ 5 })
        {
            return L"\u521a\u521a\u66f4\u65b0";
        }
        if (elapsed < std::chrono::minutes{ 1 })
        {
            return std::to_wstring(elapsed.count()) + L" \u79d2\u524d\u66f4\u65b0";
        }
        if (elapsed < std::chrono::hours{ 1 })
        {
            return std::to_wstring(std::chrono::duration_cast<std::chrono::minutes>(elapsed).count()) + L" \u5206\u949f\u524d\u66f4\u65b0";
        }
        return std::to_wstring(std::chrono::duration_cast<std::chrono::hours>(elapsed).count()) + L" \u5c0f\u65f6\u524d\u66f4\u65b0";
    }
}
