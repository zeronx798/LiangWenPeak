#pragma once

#include "NotificationSettings.h"
#include "../Pricing/PricingScheduleService.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace liangwenpeak::notifications
{
    inline constexpr auto ArrivedCatchUpWindow = std::chrono::minutes{ 15 };

    enum class NotificationType
    {
        Advance,
        Arrived,
    };

    struct NotificationEvent
    {
        NotificationType type;
        std::chrono::sys_seconds transitionInstant;
        pricing::PricingPeriod nextPeriod;
        std::chrono::minutes advanceMinutes;
    };

    struct NotificationContent
    {
        std::wstring title;
        std::wstring body;
    };

    struct NotificationDeliveryState
    {
        std::optional<std::chrono::sys_seconds> lastAdvanceTransition;
        std::optional<std::chrono::sys_seconds> lastArrivedTransition;

        [[nodiscard]] bool WasDelivered(NotificationEvent const& event) const noexcept;
        void MarkDelivered(NotificationEvent const& event) noexcept;
    };

    class NotificationScheduler final
    {
    public:
        [[nodiscard]] std::vector<NotificationEvent> GetDueNotifications(
            std::chrono::sys_seconds now,
            NotificationSettings settings,
            NotificationDeliveryState const& deliveryState) const noexcept;

        [[nodiscard]] std::optional<std::chrono::sys_seconds> GetNextWake(
            std::chrono::sys_seconds now,
            NotificationSettings settings) const noexcept;

    private:
        pricing::PricingScheduleService m_pricingSchedule;
    };

    [[nodiscard]] NotificationContent GetNotificationContent(NotificationEvent const& event);
}
