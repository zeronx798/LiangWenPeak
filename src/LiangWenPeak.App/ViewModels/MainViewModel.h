#pragma once

#include "../Services/CredentialService.h"
#include "../Services/DeepSeekClient.h"
#include "../Services/HistoryIdentityService.h"
#include "../Services/SettingsService.h"
#include "Balance/BalanceForecastService.h"
#include "Balance/BalanceHistoryStore.h"
#include "Balance/BalanceSettings.h"
#include "Pricing/PricingScheduleService.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <winrt/Windows.Foundation.h>

namespace liangwenpeak::viewmodels
{
    struct MainViewState
    {
        winrt::hstring currentTime;
        winrt::hstring statusText;
        winrt::hstring countdownText;
        winrt::hstring balanceText;
        winrt::hstring burnRateText;
        winrt::hstring etaText;
        winrt::hstring nextPeriodText;
        winrt::hstring updateStatusText;
        pricing::PricingPeriod pricingPeriod = pricing::PricingPeriod::Valley;
        bool apiFeatureEnabled = true;
        bool forecastEnabled = false;
        bool hasApiKey = false;
        bool hasSuccessfulObservation = false;
        bool isRefreshing = false;
    };

    struct SettingsCommitResult
    {
        bool succeeded = false;
        bool scheduleChanged = false;
        std::optional<balance::BalanceRefreshReason> immediateRefreshReason;
    };

    class MainViewModel final
    {
    public:
        MainViewModel(
            std::shared_ptr<services::CredentialService> credentialService,
            std::shared_ptr<services::DeepSeekClient> deepSeekClient,
            std::shared_ptr<services::SettingsService> settingsService,
            std::shared_ptr<services::HistoryIdentityService> identityService,
            std::shared_ptr<balance::BalanceHistoryStore> historyStore);

        void Initialize();
        void UpdateClock(std::chrono::system_clock::time_point now = std::chrono::system_clock::now());
        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction RefreshBalanceAsync(
            balance::BalanceRefreshReason reason,
            std::optional<std::chrono::sys_seconds> scheduledTimestamp = std::nullopt);
        [[nodiscard]] balance::ApiSettingsDraft CreateSettingsDraft() const;
        [[nodiscard]] SettingsCommitResult CommitSettings(
            balance::BalanceSettings settings,
            balance::ApiKeyDraftAction keyAction,
            winrt::hstring const& replacementApiKey,
            std::chrono::sys_seconds now = std::chrono::floor<std::chrono::seconds>(
                std::chrono::system_clock::now()));
        [[nodiscard]] bool SetForecastEnabled(bool enabled);
        [[nodiscard]] bool ResetStatistics(
            std::chrono::sys_seconds now = std::chrono::floor<std::chrono::seconds>(
                std::chrono::system_clock::now()));

        [[nodiscard]] MainViewState const& State() const noexcept;
        [[nodiscard]] balance::BalanceSettings const& Settings() const noexcept;

    private:
        void ResetObservationState();
        void UpdateBalancePresentation(std::chrono::sys_seconds now);
        [[nodiscard]] std::optional<balance::DecimalAmount> CurrentBalance() const;

        pricing::PricingScheduleService m_pricingSchedule;
        balance::BalanceForecastService m_forecastService;
        std::shared_ptr<services::CredentialService> m_credentialService;
        std::shared_ptr<services::DeepSeekClient> m_deepSeekClient;
        std::shared_ptr<services::SettingsService> m_settingsService;
        std::shared_ptr<services::HistoryIdentityService> m_identityService;
        std::shared_ptr<balance::BalanceHistoryStore> m_historyStore;
        balance::BalanceSettings m_settings;
        MainViewState m_state;
        std::map<std::string, balance::DecimalAmount, std::less<>> m_latestBalances;
        std::optional<std::chrono::sys_seconds> m_lastObservationTime;
        std::optional<balance::BalanceRefreshReason> m_pendingObservationReason;
        std::uint64_t m_refreshGeneration = 0;
        bool m_lastRefreshFailed = false;
        bool m_historyWriteFailed = false;
    };
}
