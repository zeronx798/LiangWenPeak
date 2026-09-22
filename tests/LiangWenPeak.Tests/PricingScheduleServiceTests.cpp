#include "Pricing/PricingScheduleService.h"
#include "Pricing/PricingCalendar.h"
#include "Balance/DeploymentPaths.h"
#include "Time/BalanceRefreshSchedule.h"
#include "Time/BeijingTime.h"
#include "Time/TimeFormatter.h"
#include "BalanceStatisticsTests.h"
#include "NotificationTests.h"
#include "AppTheme/WindowsVersionDetector.h"
#include "MainWindowLayout.h"
#include "MenuPresentation.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string_view>

namespace
{
    using liangwenpeak::pricing::PricingPeriod;
    using liangwenpeak::pricing::PricingCalendar;
    using liangwenpeak::pricing::PricingScheduleService;
    using liangwenpeak::time::BeijingTime;
    using namespace std::chrono_literals;

    constexpr std::chrono::year_month_day MondayDate{
        std::chrono::year{ 2026 }, std::chrono::month{ 8 }, std::chrono::day{ 24 } };
    constexpr std::chrono::year_month_day FridayDate{
        std::chrono::year{ 2026 }, std::chrono::month{ 8 }, std::chrono::day{ 28 } };
    constexpr std::chrono::year_month_day SaturdayDate{
        std::chrono::year{ 2026 }, std::chrono::month{ 8 }, std::chrono::day{ 29 } };
    constexpr std::chrono::year_month_day SundayDate{
        std::chrono::year{ 2026 }, std::chrono::month{ 8 }, std::chrono::day{ 30 } };
    constexpr std::chrono::year_month_day FollowingMondayDate{
        std::chrono::year{ 2026 }, std::chrono::month{ 8 }, std::chrono::day{ 31 } };

    class TestRunner final
    {
    public:
        void Expect(bool const condition, std::string_view const name)
        {
            if (!condition)
            {
                ++m_failures;
                std::cerr << "FAILED: " << name << '\n';
            }
        }

        [[nodiscard]] int Result() const noexcept
        {
            return m_failures == 0 ? 0 : 1;
        }

        [[nodiscard]] int FailureCount() const noexcept
        {
            return m_failures;
        }

    private:
        int m_failures = 0;
    };

    BeijingTime AtBeijingTime(
        std::chrono::year_month_day const date,
        int const hour,
        int const minute,
        int const second)
    {
        const auto beijingInstant = std::chrono::sys_days{ date } + std::chrono::hours{ hour }
            + std::chrono::minutes{ minute } + std::chrono::seconds{ second };
        return BeijingTime::FromUtc(beijingInstant - 8h);
    }

    BeijingTime AtBeijingTime(int const hour, int const minute, int const second)
    {
        return AtBeijingTime(MondayDate, hour, minute, second);
    }

    PricingCalendar TestHolidayCalendar()
    {
        return PricingCalendar::FromJson(R"json({
            "schema_version": 1,
            "timezone": "Asia/Shanghai",
            "years": {
                "2026": {
                    "all_day_off_peak": [
                        ["2026-09-25", "2026-09-27"],
                        ["2026-10-01", "2026-10-07"],
                        ["2026-11-11", "2026-11-11"],
                        ["2026-11-30", "2026-12-02"],
                        ["2026-12-31", "2027-01-01"]
                    ]
                }
            }
        })json");
    }

    void VerifyWeekdayPeriodBoundaries(TestRunner& tests)
    {
        const PricingScheduleService service;
        struct Case
        {
            int hour;
            int minute;
            int second;
            PricingPeriod expected;
            std::string_view name;
        };

        constexpr Case cases[] = {
            { 0, 0, 0, PricingPeriod::Valley, "Monday 00:00:00 is valley" },
            { 8, 59, 59, PricingPeriod::Valley, "Monday 08:59:59 is valley" },
            { 9, 0, 0, PricingPeriod::Peak, "Monday 09:00:00 is peak" },
            { 11, 59, 59, PricingPeriod::Peak, "Monday 11:59:59 is peak" },
            { 12, 0, 0, PricingPeriod::Valley, "Monday 12:00:00 is valley" },
            { 13, 59, 59, PricingPeriod::Valley, "Monday 13:59:59 is valley" },
            { 14, 0, 0, PricingPeriod::Peak, "Monday 14:00:00 is peak" },
            { 17, 59, 59, PricingPeriod::Peak, "Monday 17:59:59 is peak" },
            { 18, 0, 0, PricingPeriod::Valley, "Monday 18:00:00 is valley" },
            { 23, 59, 59, PricingPeriod::Valley, "Monday 23:59:59 is valley" },
        };

        for (auto const& testCase : cases)
        {
            tests.Expect(
                service.GetPricingPeriod(AtBeijingTime(testCase.hour, testCase.minute, testCase.second)) == testCase.expected,
                testCase.name);
        }
    }

    void VerifyWeekendPeriodBoundaries(TestRunner& tests)
    {
        const PricingScheduleService service;
        struct Case
        {
            std::chrono::year_month_day date;
            int hour;
            int minute;
            int second;
            PricingPeriod expected;
            std::string_view name;
        };

        constexpr Case cases[] = {
            { FridayDate, 17, 59, 59, PricingPeriod::Peak, "Friday 17:59:59 is peak" },
            { FridayDate, 18, 0, 0, PricingPeriod::Valley, "Friday 18:00:00 is valley" },
            { SaturdayDate, 8, 59, 59, PricingPeriod::Valley, "Saturday 08:59:59 is valley" },
            { SaturdayDate, 9, 0, 0, PricingPeriod::Valley, "Saturday 09:00:00 stays valley" },
            { SaturdayDate, 11, 0, 0, PricingPeriod::Valley, "Saturday 11:00:00 stays valley" },
            { SaturdayDate, 14, 0, 0, PricingPeriod::Valley, "Saturday 14:00:00 stays valley" },
            { SaturdayDate, 17, 0, 0, PricingPeriod::Valley, "Saturday 17:00:00 stays valley" },
            { SaturdayDate, 23, 59, 59, PricingPeriod::Valley, "Saturday 23:59:59 stays valley" },
            { SundayDate, 0, 0, 0, PricingPeriod::Valley, "Sunday 00:00:00 is valley" },
            { SundayDate, 9, 0, 0, PricingPeriod::Valley, "Sunday 09:00:00 stays valley" },
            { SundayDate, 14, 0, 0, PricingPeriod::Valley, "Sunday 14:00:00 stays valley" },
            { SundayDate, 18, 0, 0, PricingPeriod::Valley, "Sunday 18:00:00 stays valley" },
            { SundayDate, 23, 59, 59, PricingPeriod::Valley, "Sunday 23:59:59 stays valley" },
            { FollowingMondayDate, 8, 59, 59, PricingPeriod::Valley, "following Monday 08:59:59 is valley" },
            { FollowingMondayDate, 9, 0, 0, PricingPeriod::Peak, "following Monday 09:00:00 is peak" },
        };

        for (auto const& testCase : cases)
        {
            tests.Expect(
                service.GetPricingPeriod(AtBeijingTime(
                    testCase.date,
                    testCase.hour,
                    testCase.minute,
                    testCase.second)) == testCase.expected,
                testCase.name);
        }
    }

    void VerifyCountdownExamples(TestRunner& tests)
    {
        const PricingScheduleService service;
        tests.Expect(service.GetRemainingTime(AtBeijingTime(8, 30, 0)) == 30min, "Monday 08:30 countdown is 00:30:00");
        tests.Expect(service.GetRemainingTime(AtBeijingTime(10, 0, 0)) == 2h, "Monday 10:00 countdown is 02:00:00");
        tests.Expect(service.GetRemainingTime(AtBeijingTime(13, 0, 0)) == 1h, "Monday 13:00 countdown is 01:00:00");
        tests.Expect(service.GetRemainingTime(AtBeijingTime(16, 0, 0)) == 2h, "Monday 16:00 countdown is 02:00:00");
        tests.Expect(service.GetRemainingTime(AtBeijingTime(20, 0, 0)) == 13h, "Monday 20:00 countdown crosses midnight");
        tests.Expect(service.GetRemainingTime(AtBeijingTime(23, 59, 59)) == 9h + 1s, "Monday 23:59:59 countdown crosses date");

        tests.Expect(
            service.GetRemainingTime(AtBeijingTime(FridayDate, 18, 0, 0)) == 63h,
            "Friday 18:00 countdown skips the full weekend");
        tests.Expect(
            service.GetRemainingTime(AtBeijingTime(FridayDate, 20, 0, 0)) == 61h,
            "Friday 20:00 countdown targets Monday 09:00");
        tests.Expect(
            service.GetRemainingTime(AtBeijingTime(SaturdayDate, 9, 0, 0)) == 48h,
            "Saturday 09:00 countdown targets Monday 09:00");
        tests.Expect(
            service.GetRemainingTime(AtBeijingTime(SundayDate, 8, 0, 0)) == 25h,
            "Sunday 08:00 countdown is 25:00:00");
    }

    void VerifyNextTransitions(TestRunner& tests)
    {
        const PricingScheduleService service;
        struct Case
        {
            std::chrono::year_month_day date;
            int hour;
            int minute;
            int second;
            std::chrono::year_month_day expectedDate;
            int expectedHour;
            PricingPeriod expectedPeriod;
            std::string_view name;
        };

        constexpr Case cases[] = {
            { FridayDate, 17, 0, 0, FridayDate, 18, PricingPeriod::Valley, "Friday 17:00 transitions at Friday 18:00" },
            { FridayDate, 18, 0, 0, FollowingMondayDate, 9, PricingPeriod::Peak, "Friday 18:00 transitions at Monday 09:00" },
            { SaturdayDate, 9, 30, 0, FollowingMondayDate, 9, PricingPeriod::Peak, "Saturday 09:30 transitions at Monday 09:00" },
            { SaturdayDate, 23, 59, 0, FollowingMondayDate, 9, PricingPeriod::Peak, "Saturday 23:59 transitions at Monday 09:00" },
            { SundayDate, 14, 30, 0, FollowingMondayDate, 9, PricingPeriod::Peak, "Sunday 14:30 transitions at Monday 09:00" },
            { MondayDate, 8, 0, 0, MondayDate, 9, PricingPeriod::Peak, "Monday 08:00 transitions at Monday 09:00" },
            { MondayDate, 10, 0, 0, MondayDate, 12, PricingPeriod::Valley, "Monday 10:00 transitions at Monday 12:00" },
        };

        for (auto const& testCase : cases)
        {
            const auto transition = service.GetNextTransition(AtBeijingTime(
                testCase.date,
                testCase.hour,
                testCase.minute,
                testCase.second));
            const auto expected = AtBeijingTime(testCase.expectedDate, testCase.expectedHour, 0, 0);
            tests.Expect(
                transition.utcInstant == expected.UtcInstant() && transition.nextPeriod == testCase.expectedPeriod,
                testCase.name);
        }
    }

    void VerifyTransitionMetadata(TestRunner& tests)
    {
        const PricingScheduleService service;

        const auto noonValley = service.GetNextTransition(AtBeijingTime(12, 56, 32));
        tests.Expect(noonValley.nextPeriod == PricingPeriod::Peak, "Monday 12:56 next period is peak");
        tests.Expect(noonValley.nextRange.start == 14h, "Monday 12:56 next period starts at 14:00");
        tests.Expect(noonValley.nextRange.end == 18h, "Monday 12:56 next period ends at 18:00");
        tests.Expect(noonValley.nextRange.endDayOffset == std::chrono::days{ 0 }, "Monday 14:00-18:00 stays on one date");
        tests.Expect(
            liangwenpeak::time::FormatPeriodRange(noonValley.nextRange) == L"14:00 - 18:00",
            "ordinary same-day range keeps the compact format");

        const auto afternoonPeak = service.GetNextTransition(AtBeijingTime(17, 0, 0));
        tests.Expect(afternoonPeak.nextPeriod == PricingPeriod::Valley, "Monday 17:00 next period is valley");
        tests.Expect(afternoonPeak.nextRange.start == 18h, "Monday 17:00 next period starts at 18:00");
        tests.Expect(afternoonPeak.nextRange.end == 9h, "Monday overnight valley ends at 09:00");
        tests.Expect(afternoonPeak.nextRange.endDayOffset == std::chrono::days{ 1 }, "Monday overnight valley crosses one date");
        tests.Expect(
            liangwenpeak::time::FormatPeriodRange(afternoonPeak.nextRange) == L"18:00 - 09:00",
            "ordinary overnight range keeps the compact format");

        const auto fridayPeak = service.GetNextTransition(AtBeijingTime(FridayDate, 17, 0, 0));
        tests.Expect(fridayPeak.nextRange.startDayOffset == std::chrono::days{ 0 }, "Friday valley starts the same day");
        tests.Expect(fridayPeak.nextRange.endDayOffset == std::chrono::days{ 3 }, "Friday valley ends three dates later");
        tests.Expect(fridayPeak.nextRange.endWeekday == std::chrono::Monday, "Friday valley ends on Monday");
        tests.Expect(
            liangwenpeak::time::FormatPeriodRange(fridayPeak.nextRange) == L"18:00 - \u5468\u4e00 09:00",
            "Friday next valley range keeps the ordinary Monday label");

        const auto weekendValley = service.GetNextTransition(AtBeijingTime(FridayDate, 20, 0, 0));
        tests.Expect(weekendValley.nextRange.startDayOffset == std::chrono::days{ 3 }, "Friday evening next peak starts Monday");
        tests.Expect(weekendValley.nextRange.startWeekday == std::chrono::Monday, "Friday evening next range starts on Monday");
        tests.Expect(
            liangwenpeak::time::FormatPeriodRange(weekendValley.nextRange) == L"\u5468\u4e00 09:00 - 12:00",
            "weekend valley next peak range keeps the ordinary Monday label");
    }

    void VerifyHolidayCalendar(TestRunner& tests)
    {
        const auto calendar = TestHolidayCalendar();
        tests.Expect(calendar.LoadedSuccessfully(), "in-memory managed calendar parses");
        tests.Expect(calendar.HasCalendarData(std::chrono::year{ 2026 }), "calendar reports managed 2026 data");
        tests.Expect(!calendar.HasCalendarData(std::chrono::year{ 2028 }), "unknown calendar year has no data");

        const auto date = [](int const year, unsigned const month, unsigned const day)
        {
            return std::chrono::sys_days{ std::chrono::year{ year } / month / day };
        };
        tests.Expect(!calendar.IsAllDayOffPeak(date(2026, 9, 24)), "day before holiday is not overridden");
        tests.Expect(calendar.IsAllDayOffPeak(date(2026, 9, 25)), "holiday range first day is overridden");
        tests.Expect(calendar.IsAllDayOffPeak(date(2026, 9, 27)), "holiday range last day is overridden");
        tests.Expect(!calendar.IsAllDayOffPeak(date(2026, 9, 28)), "day after holiday is not overridden");
        tests.Expect(calendar.IsAllDayOffPeak(date(2026, 11, 11)), "single-day holiday is overridden");
        tests.Expect(calendar.IsAllDayOffPeak(date(2026, 12, 1)), "continuous range can cross a month");
        tests.Expect(calendar.IsAllDayOffPeak(date(2026, 12, 31)), "cross-year range starts in December");
        tests.Expect(calendar.IsAllDayOffPeak(date(2027, 1, 1)), "cross-year range continues in January");
        tests.Expect(calendar.HasCalendarData(std::chrono::year{ 2027 }), "cross-year range reports destination-year data");

        tests.Expect(!PricingCalendar::FromJson("not json").LoadedSuccessfully(), "malformed calendar falls back");
        tests.Expect(
            !PricingCalendar::FromJson(R"({"schema_version":2,"timezone":"Asia/Shanghai","years":{}})")
                .LoadedSuccessfully(),
            "unsupported calendar schema falls back");
        tests.Expect(
            !PricingCalendar::LoadFromFile(L"missing-pricing-calendar-fixture.json").LoadedSuccessfully(),
            "missing calendar file falls back");

        const PricingScheduleService service{ calendar };
        tests.Expect(
            service.GetPricingPeriod(AtBeijingTime({ std::chrono::year{ 2026 }, std::chrono::month{ 9 }, std::chrono::day{ 22 } }, 9, 0, 0)) == PricingPeriod::Peak,
            "ordinary weekday remains peak at 09:00");
        tests.Expect(
            service.GetPricingPeriod(AtBeijingTime({ std::chrono::year{ 2026 }, std::chrono::month{ 9 }, std::chrono::day{ 20 } }, 9, 0, 0)) == PricingPeriod::Valley,
            "make-up work Sunday remains all-day valley");
        tests.Expect(
            service.GetPricingPeriod(AtBeijingTime({ std::chrono::year{ 2026 }, std::chrono::month{ 9 }, std::chrono::day{ 25 } }, 9, 0, 0)) == PricingPeriod::Valley,
            "weekday statutory holiday is all-day valley");
        tests.Expect(
            service.GetPricingPeriod(AtBeijingTime({ std::chrono::year{ 2026 }, std::chrono::month{ 10 }, std::chrono::day{ 1 } }, 14, 0, 0)) == PricingPeriod::Valley,
            "October holiday first day is valley");
        tests.Expect(
            service.GetPricingPeriod(AtBeijingTime({ std::chrono::year{ 2026 }, std::chrono::month{ 10 }, std::chrono::day{ 7 } }, 14, 0, 0)) == PricingPeriod::Valley,
            "October holiday last day is valley");
        tests.Expect(
            service.GetPricingPeriod(AtBeijingTime({ std::chrono::year{ 2026 }, std::chrono::month{ 10 }, std::chrono::day{ 8 } }, 9, 0, 0)) == PricingPeriod::Peak,
            "first weekday after October holiday is peak");
        tests.Expect(
            service.GetPricingPeriod(AtBeijingTime({ std::chrono::year{ 2026 }, std::chrono::month{ 10 }, std::chrono::day{ 10 } }, 9, 0, 0)) == PricingPeriod::Valley,
            "make-up work Saturday remains all-day valley");

        const auto septemberValley = service.GetNextTransition(AtBeijingTime(
            { std::chrono::year{ 2026 }, std::chrono::month{ 9 }, std::chrono::day{ 24 } }, 17, 0, 0));
        tests.Expect(
            liangwenpeak::time::FormatPeriodRange(septemberValley.nextRange)
                == L"18:00 - 9 \u6708 28 \u65e5 09:00",
            "holiday range ending on Monday uses the unambiguous date");

        const auto september = service.GetNextTransition(AtBeijingTime(
            { std::chrono::year{ 2026 }, std::chrono::month{ 9 }, std::chrono::day{ 24 } }, 18, 0, 0));
        tests.Expect(
            september.utcInstant == AtBeijingTime(
                { std::chrono::year{ 2026 }, std::chrono::month{ 9 }, std::chrono::day{ 28 } }, 9, 0, 0).UtcInstant(),
            "September transition skips holiday and weekend");
        tests.Expect(
            liangwenpeak::time::FormatPeriodRange(september.nextRange)
                == L"9 \u6708 28 \u65e5 09:00 - 12:00",
            "holiday transition formatter shows the actual date");

        const auto octoberValley = service.GetNextTransition(AtBeijingTime(
            { std::chrono::year{ 2026 }, std::chrono::month{ 9 }, std::chrono::day{ 30 } }, 17, 0, 0));
        tests.Expect(
            liangwenpeak::time::FormatPeriodRange(octoberValley.nextRange)
                == L"18:00 - 10 \u6708 8 \u65e5 09:00",
            "National Day holiday range uses the unambiguous date");

        const auto october = service.GetNextTransition(AtBeijingTime(
            { std::chrono::year{ 2026 }, std::chrono::month{ 9 }, std::chrono::day{ 30 } }, 18, 0, 0));
        tests.Expect(
            october.utcInstant == AtBeijingTime(
                { std::chrono::year{ 2026 }, std::chrono::month{ 10 }, std::chrono::day{ 8 } }, 9, 0, 0).UtcInstant(),
            "October transition skips the entire managed holiday range");
        tests.Expect(
            liangwenpeak::time::FormatPeriodRange(october.nextRange)
                == L"10 \u6708 8 \u65e5 09:00 - 12:00",
            "long holiday transition formatter shows the actual date");

        const PricingScheduleService fallback{ PricingCalendar{} };
        tests.Expect(
            fallback.GetPricingPeriod(AtBeijingTime(
                { std::chrono::year{ 2028 }, std::chrono::month{ 10 }, std::chrono::day{ 2 } }, 9, 0, 0)) == PricingPeriod::Peak,
            "unknown year conservatively uses weekday schedule");
    }

    void VerifyMenuPresentation(TestRunner& tests)
    {
        const auto off = liangwenpeak::ui::GetNotificationMenuPresentation(false);
        tests.Expect(!off.checked && off.text == L"\u542f\u7528\u901a\u77e5" && off.glyph == L"\uE7ED",
            "notification off presentation uses enable text and silent icon");
        const auto on = liangwenpeak::ui::GetNotificationMenuPresentation(true);
        tests.Expect(on.checked && on.text == L"\u5173\u95ed\u901a\u77e5" && on.glyph == L"\uEA8F",
            "notification on presentation uses disable text and ringing icon");
    }

    void VerifyInstanceIdentity(TestRunner& tests)
    {
        using liangwenpeak::balance::BuildDataRootInstanceKey;
        using liangwenpeak::balance::CanonicalizeDataRoot;
        using liangwenpeak::balance::ResolveDataRoot;

        const auto first = CanonicalizeDataRoot(L"C:\\App\\data");
        const auto second = CanonicalizeDataRoot(L"c:/app/./data/");
        tests.Expect(first == second, "data-root identity normalizes case slashes and dot segments");
        tests.Expect(
            BuildDataRootInstanceKey(first) == BuildDataRootInstanceKey(second),
            "equivalent canonical data roots produce one logical instance key");
        tests.Expect(
            BuildDataRootInstanceKey(CanonicalizeDataRoot(L"C:\\Apps\\A\\data"))
                != BuildDataRootInstanceKey(CanonicalizeDataRoot(L"C:\\Apps\\B\\data")),
            "different data roots produce different logical instance keys");

        const auto root = std::filesystem::path{ L"C:\\Portable\\LiangWenPeak" };
        const auto oldData = CanonicalizeDataRoot(ResolveDataRoot(root / "app-1.1.2" / "LiangWenPeak.App.exe"));
        const auto newData = CanonicalizeDataRoot(ResolveDataRoot(root / "app-1.1.3" / "LiangWenPeak.App.exe"));
        tests.Expect(
            BuildDataRootInstanceKey(oldData) == BuildDataRootInstanceKey(newData),
            "app version directory is excluded from logical instance identity");
    }

    void VerifyUtcPlusEightConversion(TestRunner& tests)
    {
        const auto utcDay = std::chrono::sys_days{ std::chrono::year{ 2026 } / 8 / 24 };
        tests.Expect(BeijingTime::FromUtc(utcDay + 1h).TimeOfDay() == 9h, "01:00 UTC maps to 09:00 Beijing");
        tests.Expect(BeijingTime::FromUtc(utcDay + 16h).TimeOfDay() == 0h, "16:00 UTC maps to next-day midnight Beijing");
        tests.Expect(BeijingTime::FromUtc(utcDay + 1h).DayOfWeek() == std::chrono::Monday, "Beijing date exposes Monday");
        tests.Expect(BeijingTime::FromUtc(utcDay + 16h).DayOfWeek() == std::chrono::Tuesday, "Beijing date advances at local midnight");
    }

    void VerifyFormatting(TestRunner& tests)
    {
        tests.Expect(liangwenpeak::time::FormatClock(AtBeijingTime(9, 2, 7)) == L"09:02:07", "clock formatting is fixed width");
        tests.Expect(liangwenpeak::time::FormatDuration(5h + 32min + 10s) == L"05:32:10", "duration formats a short countdown");
        tests.Expect(liangwenpeak::time::FormatDuration(23h + 59min + 59s) == L"23:59:59", "duration formats the last second below one day");
        tests.Expect(liangwenpeak::time::FormatDuration(24h) == L"24:00:00", "duration does not wrap at 24 hours");
        tests.Expect(liangwenpeak::time::FormatDuration(37h + 42min + 11s) == L"37:42:11", "duration preserves total hours above one day");
        tests.Expect(liangwenpeak::time::FormatDuration(63h) == L"63:00:00", "duration supports the full weekend countdown");
        tests.Expect(liangwenpeak::time::FormatCnyBalance(83.26) == L"\u00a5 83.26", "CNY balance has two decimals");
    }

    void VerifyBalanceRefreshAlignment(TestRunner& tests)
    {
        using liangwenpeak::time::GetNextAlignedRefreshTime;

        struct Case
        {
            int hour;
            int minute;
            int second;
            std::chrono::minutes interval;
            int expectedHour;
            int expectedMinute;
            int expectedSecond;
            std::string_view name;
        };

        constexpr Case cases[] = {
            { 15, 7, 43, 1min, 15, 8, 0, "15:07:43 + 1 minute aligns to 15:08:00" },
            { 15, 7, 43, 5min, 15, 10, 0, "15:07:43 + 5 minutes aligns to 15:10:00" },
            { 15, 7, 43, 10min, 15, 10, 0, "15:07:43 + 10 minutes aligns to 15:10:00" },
            { 15, 14, 59, 15min, 15, 15, 0, "15:14:59 + 15 minutes aligns to 15:15:00" },
            { 15, 15, 0, 15min, 15, 30, 0, "an exact boundary advances to the next boundary" },
            { 15, 15, 1, 15min, 15, 30, 0, "15:15:01 + 15 minutes aligns to 15:30:00" },
            { 15, 26, 0, 15min, 15, 30, 0, "15:26 resume realigns to 15:30:00" },
            { 15, 17, 2, 30min, 15, 30, 0, "15:17:02 + 30 minutes aligns to 15:30:00" },
            { 15, 59, 40, 60min, 16, 0, 0, "15:59:40 + 60 minutes aligns to 16:00:00" },
        };

        for (auto const& testCase : cases)
        {
            const auto actual = GetNextAlignedRefreshTime(
                AtBeijingTime(testCase.hour, testCase.minute, testCase.second),
                testCase.interval);
            const auto expected = AtBeijingTime(
                testCase.expectedHour,
                testCase.expectedMinute,
                testCase.expectedSecond).UtcInstant();
            tests.Expect(actual == expected, testCase.name);
        }

        const auto beforeMidnight = AtBeijingTime(23, 59, 40);
        tests.Expect(
            GetNextAlignedRefreshTime(beforeMidnight, 60min) == beforeMidnight.UtcInstant() + 20s,
            "23:59:40 + 60 minutes aligns to next-day 00:00:00");
    }

    void VerifyMainWindowLayout(TestRunner& tests)
    {
        using liangwenpeak::ui::MainWindowBaseBottomPaddingDips;
        using liangwenpeak::ui::MainWindowClientHeightDips;
        using liangwenpeak::ui::MainWindowLayoutState;
        using liangwenpeak::ui::MainWindowUpdateTimeRowHeightDips;
        using liangwenpeak::ui::MainWindowVisibleContentBottomDips;

        constexpr MainWindowLayoutState apiOff{};
        constexpr MainWindowLayoutState balanceWithoutUpdate{ true, false, false };
        constexpr MainWindowLayoutState balanceWithUpdate{ true, false, true };
        constexpr MainWindowLayoutState forecastWithoutUpdate{ true, true, false };
        constexpr MainWindowLayoutState forecastWithUpdate{ true, true, true };

        tests.Expect(MainWindowClientHeightDips(apiOff) == 173, "API-off client height stays compact");
        tests.Expect(
            MainWindowClientHeightDips(balanceWithoutUpdate) == 200,
            "balance-only state without an observation omits the update row");
        tests.Expect(
            MainWindowClientHeightDips(balanceWithUpdate) == 213,
            "balance-only state reserves the visible update row");
        tests.Expect(
            MainWindowClientHeightDips(forecastWithoutUpdate) == 246,
            "forecast state without an observation omits the update row");
        tests.Expect(
            MainWindowClientHeightDips(forecastWithUpdate) == 259,
            "forecast state reserves the visible update row");
        tests.Expect(
            MainWindowClientHeightDips(balanceWithUpdate)
                - MainWindowClientHeightDips(balanceWithoutUpdate)
                == MainWindowUpdateTimeRowHeightDips,
            "balance-only height grows by exactly one visible update row");
        tests.Expect(
            MainWindowClientHeightDips(forecastWithUpdate)
                - MainWindowClientHeightDips(forecastWithoutUpdate)
                == MainWindowUpdateTimeRowHeightDips,
            "forecast height grows by exactly one visible update row");

        constexpr MainWindowLayoutState observedStates[]{ balanceWithUpdate, forecastWithUpdate };
        for (auto const state : observedStates)
        {
            tests.Expect(
                MainWindowVisibleContentBottomDips(state)
                    <= MainWindowClientHeightDips(state) - MainWindowBaseBottomPaddingDips,
                "visible update text ends above the required client-area bottom padding");
        }
    }

    void VerifyWindowsVersionClassification(TestRunner& tests)
    {
        using liangwenpeak::apptheme::WindowsVersionDetector;

        tests.Expect(
            !WindowsVersionDetector::IsWindows11Version(10, 19045),
            "Windows 10 build 19045 does not expose Fluent theme");
        tests.Expect(
            !WindowsVersionDetector::IsWindows11Version(10, 21999),
            "the build immediately below Windows 11 stays unsupported");
        tests.Expect(
            WindowsVersionDetector::IsWindows11Version(10, 22000),
            "Windows 11 build 22000 exposes Fluent theme");
        tests.Expect(
            WindowsVersionDetector::IsWindows11Version(10, 26100),
            "current Windows 11 builds expose Fluent theme");
        tests.Expect(
            WindowsVersionDetector::IsWindows11Version(11, 0),
            "a future Windows major version remains supported");
    }
}

int main()
{
    TestRunner tests;
    VerifyWeekdayPeriodBoundaries(tests);
    VerifyWeekendPeriodBoundaries(tests);
    VerifyCountdownExamples(tests);
    VerifyNextTransitions(tests);
    VerifyTransitionMetadata(tests);
    VerifyHolidayCalendar(tests);
    VerifyMenuPresentation(tests);
    VerifyInstanceIdentity(tests);
    VerifyUtcPlusEightConversion(tests);
    VerifyFormatting(tests);
    VerifyBalanceRefreshAlignment(tests);
    VerifyMainWindowLayout(tests);
    VerifyWindowsVersionClassification(tests);
    VerifyBalanceStatistics([&tests](bool const condition, std::string_view const name)
    {
        tests.Expect(condition, name);
    });
    VerifyNotifications([&tests](bool const condition, std::string_view const name)
    {
        tests.Expect(condition, name);
    });

    if (tests.FailureCount() == 0)
    {
        std::cout << "All pricing, notification, settings, formatting, scheduling, layout, platform, history, forecast, and ETA tests passed.\n";
    }
    else
    {
        std::cerr << tests.FailureCount() << " pricing schedule test(s) failed.\n";
    }
    return tests.Result();
}
