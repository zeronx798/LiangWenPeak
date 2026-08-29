#pragma once

#include "DecimalAmount.h"
#include "../Notifications/NotificationSettings.h"
#include "../Time/BalanceRefreshSchedule.h"

#include <array>
#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace liangwenpeak::balance
{
    enum class PredictionAlgorithm
    {
        SlidingAverage,
        ExponentialAverage,
        RobustTrend,
        LastValidSample,
    };

    inline constexpr auto DefaultRateWindow = std::chrono::hours{ 24 * 30 };
    inline constexpr std::array<std::chrono::seconds, 14> SupportedRateWindows = {
        std::chrono::minutes{ 1 },
        std::chrono::minutes{ 5 },
        std::chrono::minutes{ 10 },
        std::chrono::minutes{ 15 },
        std::chrono::minutes{ 30 },
        std::chrono::hours{ 1 },
        std::chrono::hours{ 3 },
        std::chrono::hours{ 6 },
        std::chrono::hours{ 12 },
        std::chrono::hours{ 24 },
        std::chrono::hours{ 24 * 3 },
        std::chrono::hours{ 24 * 7 },
        std::chrono::hours{ 24 * 14 },
        std::chrono::hours{ 24 * 30 },
    };

    struct BalanceSettings
    {
        bool apiFeatureEnabled = true;
        bool forecastEnabled = false;
        std::string selectedCurrency = "CNY";
        std::chrono::minutes refreshInterval = time::DefaultBalanceRefreshInterval;
        std::chrono::seconds rateWindow = DefaultRateWindow;
        PredictionAlgorithm preferredAlgorithm = PredictionAlgorithm::SlidingAverage;
        std::map<std::string, DecimalAmount, std::less<>> warningBalances;
        std::vector<std::string> knownCurrencies{ "CNY" };
        notifications::NotificationSettings notifications;
    };

    enum class ApiKeyDraftAction
    {
        Keep,
        Clear,
        Replace,
    };

    class SettingsDraft final
    {
    public:
        SettingsDraft(BalanceSettings persistedSettings, bool hasApiKey);

        [[nodiscard]] BalanceSettings const& Settings() const noexcept;
        [[nodiscard]] BalanceSettings& Settings() noexcept;
        [[nodiscard]] ApiKeyDraftAction KeyAction() const noexcept;
        [[nodiscard]] bool PersistedHasApiKey() const noexcept;

        void RequestApiKeyClear() noexcept;
        void UndoApiKeyClear() noexcept;
        void OnApiKeyInputChanged(bool hasReplacementText) noexcept;
        void SetRefreshInterval(std::chrono::minutes interval);
        void SetRateWindow(std::chrono::seconds window);
        void Cancel();

    private:
        BalanceSettings m_persisted;
        BalanceSettings m_settings;
        bool m_hasApiKey = false;
        ApiKeyDraftAction m_keyAction = ApiKeyDraftAction::Keep;
    };

    [[nodiscard]] bool IsSupportedRateWindow(std::chrono::seconds window) noexcept;
    [[nodiscard]] std::vector<std::chrono::seconds> GetAvailableRateWindows(
        std::chrono::minutes refreshInterval);
    void NormalizeBalanceSettings(BalanceSettings& settings);
    [[nodiscard]] PredictionAlgorithm GetEffectiveAlgorithm(BalanceSettings const& settings) noexcept;
    [[nodiscard]] DecimalAmount GetWarningBalance(
        BalanceSettings const& settings,
        std::string const& currency) noexcept;
    void SetWarningBalance(
        BalanceSettings& settings,
        std::string currency,
        DecimalAmount amount);
    [[nodiscard]] bool ReconcileSelectedCurrency(
        BalanceSettings& settings,
        std::vector<std::string> const& availableCurrencies);
}
