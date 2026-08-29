#include "NotificationScheduler.h"

#include "../Time/BeijingTime.h"

namespace liangwenpeak::notifications
{
    using namespace std::chrono_literals;

    bool NotificationDeliveryState::WasDelivered(NotificationEvent const& event) const noexcept
    {
        auto const& lastTransition = event.type == NotificationType::Advance
            ? lastAdvanceTransition
            : lastArrivedTransition;
        return lastTransition && event.transitionInstant <= *lastTransition;
    }

    void NotificationDeliveryState::MarkDelivered(NotificationEvent const& event) noexcept
    {
        auto& lastTransition = event.type == NotificationType::Advance
            ? lastAdvanceTransition
            : lastArrivedTransition;
        if (!lastTransition || event.transitionInstant > *lastTransition)
        {
            lastTransition = event.transitionInstant;
        }
    }

    std::vector<NotificationEvent> NotificationScheduler::GetDueNotifications(
        std::chrono::sys_seconds const now,
        NotificationSettings settings,
        NotificationDeliveryState const& deliveryState) const noexcept
    {
        NormalizeNotificationSettings(settings);
        std::vector<NotificationEvent> result;
        if (!settings.enabled)
        {
            return result;
        }

        const auto nextTransition = m_pricingSchedule.GetNextTransition(
            time::BeijingTime::FromUtc(now));
        if (settings.advanceEnabled
            && nextTransition.utcInstant - settings.advanceMinutes == now)
        {
            const NotificationEvent event{
                NotificationType::Advance,
                nextTransition.utcInstant,
                nextTransition.nextPeriod,
                settings.advanceMinutes };
            if (!deliveryState.WasDelivered(event))
            {
                result.push_back(event);
            }
        }

        // Probe one second before the inclusive catch-up window. GetNextTransition
        // intentionally skips a transition exactly at the supplied instant.
        const auto probe = now - ArrivedCatchUpWindow - 1s;
        const auto recentTransition = m_pricingSchedule.GetNextTransition(
            time::BeijingTime::FromUtc(probe));
        if (recentTransition.utcInstant <= now
            && now - recentTransition.utcInstant <= ArrivedCatchUpWindow)
        {
            const NotificationEvent event{
                NotificationType::Arrived,
                recentTransition.utcInstant,
                recentTransition.nextPeriod,
                settings.advanceMinutes };
            if (!deliveryState.WasDelivered(event))
            {
                result.push_back(event);
            }
        }

        return result;
    }

    std::optional<std::chrono::sys_seconds> NotificationScheduler::GetNextWake(
        std::chrono::sys_seconds const now,
        NotificationSettings settings) const noexcept
    {
        NormalizeNotificationSettings(settings);
        if (!settings.enabled)
        {
            return std::nullopt;
        }

        const auto transition = m_pricingSchedule.GetNextTransition(
            time::BeijingTime::FromUtc(now));
        const auto advance = transition.utcInstant - settings.advanceMinutes;
        if (settings.advanceEnabled && advance > now)
        {
            return advance;
        }
        return transition.utcInstant;
    }

    NotificationContent GetNotificationContent(NotificationEvent const& event)
    {
        const bool peak = event.nextPeriod == pricing::PricingPeriod::Peak;
        if (event.type == NotificationType::Arrived)
        {
            return peak
                ? NotificationContent{
                    L"\u6881\u6587\u5cf0\u5230\u4e86",
                    L"\u5f53\u524d\u5df2\u8fdb\u5165\u539f\u4ef7\u65f6\u6bb5\uff0c\u8bb0\u5f97\u63a7\u5236\u6210\u672c\u3002" }
                : NotificationContent{
                    L"\u6881\u6587\u8c37\u5230\u4e86",
                    L"\u5f53\u524d\u5df2\u8fdb\u5165\u534a\u4ef7\u65f6\u6bb5\uff0c\u53ef\u4ee5\u5f00\u8e6c\u4e86\u3002" };
        }

        const auto minutes = std::to_wstring(event.advanceMinutes.count());
        return peak
            ? NotificationContent{
                L"\u6881\u6587\u5cf0\u5feb\u5230\u4e86",
                L"\u8fd8\u6709 " + minutes + L" \u5206\u949f\u8fdb\u5165\u539f\u4ef7\u65f6\u6bb5\uff0c\u8bb0\u5f97\u63a7\u5236\u6210\u672c\u3002" }
            : NotificationContent{
                L"\u6881\u6587\u8c37\u5feb\u5230\u4e86",
                L"\u8fd8\u6709 " + minutes + L" \u5206\u949f\u8fdb\u5165\u534a\u4ef7\u65f6\u6bb5\uff0c" + minutes
                    + L" \u5206\u949f\u540e\u53ef\u4ee5\u5f00\u8e6c\u3002" };
    }
}
