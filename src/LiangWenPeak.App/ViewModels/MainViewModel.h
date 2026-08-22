#pragma once

#include "../Services/CredentialService.h"
#include "../Services/DeepSeekClient.h"
#include "Pricing/PricingScheduleService.h"

#include <chrono>
#include <memory>
#include <optional>
#include <winrt/Windows.Foundation.h>

namespace liangwenpeak::viewmodels
{
    struct MainViewState
    {
        winrt::hstring currentTime;
        winrt::hstring statusText;
        winrt::hstring countdownText;
        winrt::hstring balanceText;
        winrt::hstring nextPeriodText;
        winrt::hstring updateStatusText;
        pricing::PricingPeriod pricingPeriod = pricing::PricingPeriod::Valley;
        bool hasApiKey = false;
        bool isRefreshing = false;
    };

    class MainViewModel final
    {
    public:
        MainViewModel(
            std::shared_ptr<services::CredentialService> credentialService,
            std::shared_ptr<services::DeepSeekClient> deepSeekClient);

        void Initialize();
        void UpdateClock(std::chrono::system_clock::time_point now = std::chrono::system_clock::now());
        [[nodiscard]] winrt::Windows::Foundation::IAsyncAction RefreshBalanceAsync();
        void SaveApiKey(winrt::hstring const& apiKey);

        [[nodiscard]] MainViewState const& State() const noexcept;

    private:
        void UpdateBalancePresentation(std::chrono::sys_seconds now);

        pricing::PricingScheduleService m_pricingSchedule;
        std::shared_ptr<services::CredentialService> m_credentialService;
        std::shared_ptr<services::DeepSeekClient> m_deepSeekClient;
        MainViewState m_state;
        std::optional<double> m_balance;
        std::optional<std::chrono::sys_seconds> m_lastSuccessfulUpdate;
        bool m_lastRefreshFailed = false;
    };
}

