#include "pch.h"
#include "MainViewModel.h"

#include "Time/BeijingTime.h"
#include "Time/TimeFormatter.h"

#include <utility>

namespace liangwenpeak::viewmodels
{
    namespace
    {
        winrt::hstring ToHString(std::wstring const& value)
        {
            return winrt::hstring{ value };
        }
    }

    MainViewModel::MainViewModel(
        std::shared_ptr<services::CredentialService> credentialService,
        std::shared_ptr<services::DeepSeekClient> deepSeekClient)
        : m_credentialService(std::move(credentialService)),
          m_deepSeekClient(std::move(deepSeekClient))
    {
    }

    void MainViewModel::Initialize()
    {
        m_state.hasApiKey = m_credentialService->HasApiKey();
        m_state.balanceText = m_state.hasApiKey ? L"\u66f4\u65b0\u4e2d\u2026" : L"\u672a\u914d\u7f6e";
        UpdateClock();
    }

    void MainViewModel::UpdateClock(std::chrono::system_clock::time_point const now)
    {
        const auto utcNow = std::chrono::floor<std::chrono::seconds>(now);
        const auto beijingTime = time::BeijingTime::FromUtc(utcNow);
        const auto pricing = m_pricingSchedule.GetSnapshot(beijingTime);

        m_state.currentTime = ToHString(time::FormatClock(beijingTime));
        m_state.pricingPeriod = pricing.currentPeriod;
        if (pricing.currentPeriod == pricing::PricingPeriod::Peak)
        {
            m_state.statusText = L"\u6881 \u6587 \u5cf0 \u00b7 \u539f \u4ef7";
            m_state.countdownText = L"\u8ddd\u79bb\u6881\u6587\u8c37\u8fd8\u6709 " + ToHString(time::FormatDuration(pricing.remaining));
        }
        else
        {
            m_state.statusText = L"\u6881 \u6587 \u8c37 \u00b7 \u534a \u4ef7";
            m_state.countdownText = L"\u8ddd\u79bb\u6881\u6587\u5cf0\u8fd8\u6709 " + ToHString(time::FormatDuration(pricing.remaining));
        }
        m_state.nextPeriodText = ToHString(time::FormatPeriodRange(pricing.nextTransition.nextRange));
        UpdateBalancePresentation(utcNow);
    }

    winrt::Windows::Foundation::IAsyncAction MainViewModel::RefreshBalanceAsync()
    {
        if (m_state.isRefreshing)
        {
            co_return;
        }

        auto apiKey = m_credentialService->TryGetApiKey();
        m_state.hasApiKey = apiKey.has_value();
        if (!apiKey)
        {
            m_balance.reset();
            m_lastSuccessfulUpdate.reset();
            m_lastRefreshFailed = false;
            m_state.balanceText = L"\u672a\u914d\u7f6e";
            m_state.updateStatusText = {};
            co_return;
        }

        m_state.isRefreshing = true;
        if (!m_balance)
        {
            m_state.balanceText = L"\u66f4\u65b0\u4e2d\u2026";
        }
        UpdateBalancePresentation(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));

        try
        {
            const auto balance = co_await m_deepSeekClient->GetCnyBalanceAsync(*apiKey);
            m_balance = balance;
            m_lastSuccessfulUpdate = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
            m_lastRefreshFailed = false;
        }
        catch (...)
        {
            m_lastRefreshFailed = true;
        }

        apiKey.reset();
        m_state.isRefreshing = false;
        const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
        UpdateBalancePresentation(now);
    }

    void MainViewModel::SaveApiKey(winrt::hstring const& apiKey)
    {
        m_credentialService->SaveApiKey(apiKey);
        m_state.hasApiKey = m_credentialService->HasApiKey();
        m_balance.reset();
        m_lastSuccessfulUpdate.reset();
        m_lastRefreshFailed = false;
        m_state.balanceText = m_state.hasApiKey ? L"\u66f4\u65b0\u4e2d\u2026" : L"\u672a\u914d\u7f6e";
        m_state.updateStatusText = {};
    }

    MainViewState const& MainViewModel::State() const noexcept
    {
        return m_state;
    }

    void MainViewModel::UpdateBalancePresentation(std::chrono::sys_seconds const now)
    {
        if (!m_state.hasApiKey)
        {
            m_state.balanceText = L"\u672a\u914d\u7f6e";
            m_state.updateStatusText = {};
            return;
        }

        if (m_balance)
        {
            m_state.balanceText = ToHString(time::FormatCnyBalance(*m_balance));
        }
        else if (!m_state.isRefreshing && m_lastRefreshFailed)
        {
            m_state.balanceText = L"\u6682\u4e0d\u53ef\u7528";
        }

        if (m_state.isRefreshing)
        {
            m_state.updateStatusText = L"\u6b63\u5728\u66f4\u65b0";
            return;
        }

        if (m_lastSuccessfulUpdate)
        {
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - *m_lastSuccessfulUpdate);
            auto updateText = time::FormatLastUpdated(elapsed);
            if (m_lastRefreshFailed)
            {
                updateText = L"\u66f4\u65b0\u5931\u8d25 \u00b7 " + updateText;
            }
            m_state.updateStatusText = ToHString(updateText);
        }
        else if (m_lastRefreshFailed)
        {
            m_state.updateStatusText = L"\u66f4\u65b0\u5931\u8d25";
        }
        else
        {
            m_state.updateStatusText = {};
        }
    }
}
