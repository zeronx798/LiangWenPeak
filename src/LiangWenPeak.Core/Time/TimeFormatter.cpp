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

        std::wstring FormatWeekday(std::chrono::weekday const weekday)
        {
            switch (weekday.c_encoding())
            {
            case 0:
                return L"\u5468\u65e5";
            case 1:
                return L"\u5468\u4e00";
            case 2:
                return L"\u5468\u4e8c";
            case 3:
                return L"\u5468\u4e09";
            case 4:
                return L"\u5468\u56db";
            case 5:
                return L"\u5468\u4e94";
            case 6:
                return L"\u5468\u516d";
            default:
                return {};
            }
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
        const bool showStartWeekday = range.startDayOffset > std::chrono::days{ 1 }
            || (range.startDayOffset == std::chrono::days{ 1 }
                && range.startWeekday == std::chrono::Monday);
        const bool showEndWeekday = range.endDayOffset - range.startDayOffset > std::chrono::days{ 1 };

        auto start = FormatHourMinute(range.start);
        if (showStartWeekday)
        {
            start = FormatWeekday(range.startWeekday) + L" " + start;
        }

        auto end = FormatHourMinute(range.end);
        if (showEndWeekday)
        {
            end = FormatWeekday(range.endWeekday) + L" " + end;
        }

        return start + L" \u2014 " + end;
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
