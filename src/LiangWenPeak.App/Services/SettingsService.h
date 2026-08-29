#pragma once

#include "Balance/BalanceSettings.h"
#include "Notifications/NotificationScheduler.h"
#include "StateProfile.h"

#include <chrono>
#include <string>

namespace liangwenpeak::services
{
    class SettingsService final
    {
    public:
        explicit SettingsService(StateProfile const& profile);

        [[nodiscard]] balance::BalanceSettings LoadBalanceSettings() const noexcept;
        [[nodiscard]] bool SaveBalanceSettings(balance::BalanceSettings const& settings) const noexcept;
        [[nodiscard]] bool SaveSelectedCurrency(std::string const& currency) const noexcept;
        [[nodiscard]] bool SaveForecastEnabled(bool enabled) const noexcept;

        [[nodiscard]] std::chrono::minutes LoadBalanceRefreshInterval() const noexcept;
        [[nodiscard]] bool SaveBalanceRefreshInterval(std::chrono::minutes interval) const noexcept;

        [[nodiscard]] notifications::NotificationDeliveryState
            LoadNotificationDeliveryState() const noexcept;
        [[nodiscard]] bool SaveNotificationDeliveryState(
            notifications::NotificationDeliveryState const& state) const noexcept;
        [[nodiscard]] bool DeleteAllSettings() const noexcept;
        [[nodiscard]] std::wstring const& RegistrySubkey() const noexcept;

    private:
        std::wstring m_settingsPath;
    };
}
