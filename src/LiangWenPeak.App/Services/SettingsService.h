#pragma once

#include "Balance/BalanceSettings.h"

#include <chrono>
#include <string>

namespace liangwenpeak::services
{
    class SettingsService final
    {
    public:
        [[nodiscard]] balance::BalanceSettings LoadBalanceSettings() const noexcept;
        [[nodiscard]] bool SaveBalanceSettings(balance::BalanceSettings const& settings) const noexcept;
        [[nodiscard]] bool SaveSelectedCurrency(std::string const& currency) const noexcept;
        [[nodiscard]] bool SaveForecastEnabled(bool enabled) const noexcept;

        [[nodiscard]] std::chrono::minutes LoadBalanceRefreshInterval() const noexcept;
        [[nodiscard]] bool SaveBalanceRefreshInterval(std::chrono::minutes interval) const noexcept;
    };
}
