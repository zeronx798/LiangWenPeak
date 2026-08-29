#include "NotificationTests.h"

#include "Balance/BalanceSettings.h"
#include "Notifications/NotificationScheduler.h"
#include "Notifications/NotificationSettings.h"
#include "Time/BeijingTime.h"

#include <chrono>
#include <limits>
#include <iostream>

namespace
{
    using namespace std::chrono_literals;
    using liangwenpeak::balance::BalanceSettings;
    using liangwenpeak::balance::SettingsDraft;
    using liangwenpeak::notifications::NotificationDeliveryState;
    using liangwenpeak::notifications::NotificationEvent;
    using liangwenpeak::notifications::NotificationScheduler;
    using liangwenpeak::notifications::NotificationSettings;
    using liangwenpeak::notifications::NotificationType;
    using liangwenpeak::pricing::PricingPeriod;
    using liangwenpeak::time::BeijingTime;

    constexpr std::chrono::year_month_day MondayDate{
        std::chrono::year{ 2026 }, std::chrono::month{ 8 }, std::chrono::day{ 24 } };
    constexpr std::chrono::year_month_day FridayDate{
        std::chrono::year{ 2026 }, std::chrono::month{ 8 }, std::chrono::day{ 28 } };
    constexpr std::chrono::year_month_day SaturdayDate{
        std::chrono::year{ 2026 }, std::chrono::month{ 8 }, std::chrono::day{ 29 } };
    constexpr std::chrono::year_month_day FollowingMondayDate{
        std::chrono::year{ 2026 }, std::chrono::month{ 8 }, std::chrono::day{ 31 } };

    std::chrono::sys_seconds AtBeijingTime(
        std::chrono::year_month_day const date,
        int const hour,
        int const minute,
        int const second = 0)
    {
        const auto local = std::chrono::sys_days{ date }
            + std::chrono::hours{ hour }
            + std::chrono::minutes{ minute }
            + std::chrono::seconds{ second };
        return BeijingTime::FromUtc(local - 8h).UtcInstant();
    }

    std::chrono::sys_seconds AtBeijingTime(
        int const hour,
        int const minute,
        int const second = 0)
    {
        return AtBeijingTime(MondayDate, hour, minute, second);
    }

    bool ContainsType(
        std::vector<NotificationEvent> const& events,
        NotificationType const type)
    {
        for (auto const& event : events)
        {
            if (event.type == type)
            {
                return true;
            }
        }
        return false;
    }

    void VerifyDefaultsAndValidation(auto const& expect)
    {
        const NotificationSettings defaults;
        expect(!defaults.enabled, "notifications default to disabled");
        expect(defaults.advanceEnabled, "advance reminders default to enabled");
        expect(defaults.advanceMinutes == 10min, "advance reminders default to ten minutes");

        std::chrono::minutes parsed;
        expect(
            liangwenpeak::notifications::TryParseAdvanceMinutes(1.0, parsed) && parsed == 1min,
            "NumberBox accepts the minimum one minute");
        expect(
            liangwenpeak::notifications::TryParseAdvanceMinutes(30.0, parsed) && parsed == 30min,
            "NumberBox accepts the maximum thirty minutes");
        expect(
            !liangwenpeak::notifications::TryParseAdvanceMinutes(0.0, parsed),
            "NumberBox rejects zero");
        expect(
            !liangwenpeak::notifications::TryParseAdvanceMinutes(-1.0, parsed),
            "NumberBox rejects negative values");
        expect(
            !liangwenpeak::notifications::TryParseAdvanceMinutes(31.0, parsed),
            "NumberBox rejects values above thirty");
        expect(
            !liangwenpeak::notifications::TryParseAdvanceMinutes(1.5, parsed),
            "NumberBox rejects non-integer values");
        expect(
            !liangwenpeak::notifications::TryParseAdvanceMinutes(
                (std::numeric_limits<double>::quiet_NaN)(), parsed),
            "NumberBox rejects NaN");
    }

    void VerifySettingsTransaction(auto const& expect)
    {
        BalanceSettings persisted;
        const auto menuIsChecked = [&persisted]()
        {
            return persisted.notifications.enabled;
        };

        SettingsDraft cancelledDraft{ persisted, false };
        expect(
            cancelledDraft.Settings().notifications.enabled == persisted.notifications.enabled,
            "opening settings copies persisted notification settings into the draft");
        cancelledDraft.Settings().notifications.enabled = true;
        expect(!menuIsChecked(), "editing draft notificationEnabled does not update the menu");
        cancelledDraft.Cancel();
        expect(
            !persisted.notifications.enabled && !cancelledDraft.Settings().notifications.enabled,
            "cancel leaves persisted notifications unchanged");

        SettingsDraft savedDraft{ persisted, false };
        savedDraft.Settings().notifications.enabled = true;
        persisted = savedDraft.Settings();
        expect(menuIsChecked(), "save commits notificationEnabled and synchronizes the menu");

        persisted.notifications.enabled = false;
        persisted.notifications.advanceEnabled = true;
        SettingsDraft snapshotDraft{ persisted, false };
        persisted.notifications.enabled = true; // Main-menu immediate commit while settings is open.
        expect(
            !snapshotDraft.Settings().notifications.enabled,
            "an external menu commit does not mutate an already-open draft snapshot");
        snapshotDraft.Settings().notifications.advanceEnabled = false;
        snapshotDraft.Settings().notifications.advanceMinutes = 17min;
        persisted = snapshotDraft.Settings();
        expect(
            !menuIsChecked()
                && !persisted.notifications.advanceEnabled
                && persisted.notifications.advanceMinutes == 17min,
            "saving the complete draft uses last-commit-wins over an external menu change");
    }

    void VerifyScheduler(auto const& expect)
    {
        const NotificationScheduler scheduler{};
        NotificationSettings settings;
        settings.enabled = true;
        NotificationDeliveryState deliveryState;

        auto due = scheduler.GetDueNotifications(
            AtBeijingTime(8, 50), settings, deliveryState);
        expect(
            due.size() == 1
                && due.front().type == NotificationType::Advance
                && due.front().nextPeriod == PricingPeriod::Peak
                && due.front().transitionInstant == AtBeijingTime(9, 0),
            "peak advance reminder fires exactly transition minus X minutes");
        const auto peakAdvanceContent = liangwenpeak::notifications::GetNotificationContent(due.front());
        expect(
            peakAdvanceContent.title == L"\u6881\u6587\u5cf0\u5feb\u5230\u4e86"
                && peakAdvanceContent.body
                    == L"\u8fd8\u6709 10 \u5206\u949f\u8fdb\u5165\u539f\u4ef7\u65f6\u6bb5\uff0c\u8bb0\u5f97\u63a7\u5236\u6210\u672c\u3002",
            "peak advance notification uses the fixed title and configured minute text");
        deliveryState.MarkDelivered(due.front());
        expect(
            scheduler.GetDueNotifications(AtBeijingTime(8, 50), settings, deliveryState).empty(),
            "timer reentry cannot duplicate an advance reminder");

        due = scheduler.GetDueNotifications(AtBeijingTime(9, 0), settings, deliveryState);
        expect(
            due.size() == 1
                && due.front().type == NotificationType::Arrived
                && due.front().nextPeriod == PricingPeriod::Peak,
            "arrived notification fires at the real transition");
        const auto peakArrivedContent = liangwenpeak::notifications::GetNotificationContent(due.front());
        expect(
            peakArrivedContent.title == L"\u6881\u6587\u5cf0\u5230\u4e86"
                && peakArrivedContent.body
                    == L"\u5f53\u524d\u5df2\u8fdb\u5165\u539f\u4ef7\u65f6\u6bb5\uff0c\u8bb0\u5f97\u63a7\u5236\u6210\u672c\u3002",
            "peak arrived notification uses the fixed copy");
        deliveryState.MarkDelivered(due.front());
        expect(
            scheduler.GetDueNotifications(AtBeijingTime(9, 0), settings, deliveryState).empty(),
            "timer reentry cannot duplicate an arrived notification");

        NotificationDeliveryState freshState;
        due = scheduler.GetDueNotifications(AtBeijingTime(11, 50), settings, freshState);
        expect(
            due.size() == 1
                && due.front().type == NotificationType::Advance
                && due.front().nextPeriod == PricingPeriod::Valley,
            "valley advance reminder targets the real noon transition");
        const auto valleyAdvanceContent = liangwenpeak::notifications::GetNotificationContent(due.front());
        expect(
            valleyAdvanceContent.title == L"\u6881\u6587\u8c37\u5feb\u5230\u4e86"
                && valleyAdvanceContent.body
                    == L"\u8fd8\u6709 10 \u5206\u949f\u8fdb\u5165\u534a\u4ef7\u65f6\u6bb5\uff0c10 \u5206\u949f\u540e\u53ef\u4ee5\u5f00\u8e6c\u3002",
            "valley advance notification uses X in both required positions");
        due = scheduler.GetDueNotifications(AtBeijingTime(12, 0), settings, freshState);
        const auto valleyArrivedContent = liangwenpeak::notifications::GetNotificationContent(due.front());
        expect(
            valleyArrivedContent.title == L"\u6881\u6587\u8c37\u5230\u4e86"
                && valleyArrivedContent.body
                    == L"\u5f53\u524d\u5df2\u8fdb\u5165\u534a\u4ef7\u65f6\u6bb5\uff0c\u53ef\u4ee5\u5f00\u8e6c\u4e86\u3002",
            "valley arrived notification uses the fixed copy");

        expect(
            scheduler.GetDueNotifications(
                AtBeijingTime(SaturdayDate, 8, 50), settings, {}).empty(),
            "weekend boundaries do not create fake transition notifications");
        expect(
            scheduler.GetNextWake(
                AtBeijingTime(FridayDate, 18, 0), settings)
                == AtBeijingTime(FollowingMondayDate, 8, 50),
            "Friday after 18:00 schedules the next advance reminder for Monday 09:00");
        due = scheduler.GetDueNotifications(
            AtBeijingTime(FollowingMondayDate, 8, 50), settings, {});
        expect(
            due.size() == 1
                && due.front().transitionInstant == AtBeijingTime(FollowingMondayDate, 9, 0)
                && due.front().nextPeriod == PricingPeriod::Peak,
            "the cross-week reminder identifies Monday's real peak transition");

        due = scheduler.GetDueNotifications(AtBeijingTime(8, 51), settings, {});
        expect(
            !ContainsType(due, NotificationType::Advance),
            "a missed advance reminder is never sent late");
        due = scheduler.GetDueNotifications(AtBeijingTime(9, 10), settings, {});
        expect(
            due.size() == 1 && due.front().type == NotificationType::Arrived,
            "an arrived notification catches up within fifteen minutes");
        due = scheduler.GetDueNotifications(AtBeijingTime(9, 15), settings, {});
        expect(
            due.size() == 1 && due.front().type == NotificationType::Arrived,
            "the fifteen-minute arrived catch-up boundary is inclusive");
        expect(
            scheduler.GetDueNotifications(AtBeijingTime(9, 15, 1), settings, {}).empty(),
            "an arrived notification expires beyond fifteen minutes");
        expect(
            scheduler.GetDueNotifications(AtBeijingTime(10, 0), settings, {}).empty(),
            "ordinary startup does not notify merely because the current period is peak");

        settings.enabled = false;
        expect(
            scheduler.GetDueNotifications(AtBeijingTime(9, 0), settings, {}).empty()
                && !scheduler.GetNextWake(AtBeijingTime(8, 0), settings),
            "disabled persisted settings produce no delivery or schedule");
    }
}

void VerifyNotifications(
    std::function<void(bool, std::string_view)> const& expect)
{
    std::cout << "[Notifications] Defaults and NumberBox validation\n" << std::flush;
    VerifyDefaultsAndValidation(expect);
    std::cout << "[Notifications] Settings transaction\n" << std::flush;
    VerifySettingsTransaction(expect);
    std::cout << "[Notifications] Scheduling, catch-up, and dedup\n" << std::flush;
    VerifyScheduler(expect);
}
