#include "pch.h"
#include "MainViewModel.h"

#include "Balance/BalanceFormatter.h"
#include "Balance/SeriesIdentity.h"
#include "Time/BeijingTime.h"
#include "Time/TimeFormatter.h"

#include <algorithm>
#include <utility>

namespace liangwenpeak::viewmodels
{
    namespace
    {
        winrt::hstring ToHString(std::wstring const& value)
        {
            return winrt::hstring{ value };
        }

        bool SameCurrencies(
            std::vector<std::string> const& left,
            std::vector<std::string> const& right)
        {
            auto sortedLeft = left;
            auto sortedRight = right;
            std::sort(sortedLeft.begin(), sortedLeft.end());
            std::sort(sortedRight.begin(), sortedRight.end());
            return sortedLeft == sortedRight;
        }
    }

    MainViewModel::MainViewModel(
        std::shared_ptr<services::CredentialService> credentialService,
        std::shared_ptr<services::DeepSeekClient> deepSeekClient,
        std::shared_ptr<services::SettingsService> settingsService,
        std::shared_ptr<services::HistoryIdentityService> identityService,
        std::shared_ptr<balance::BalanceHistoryStore> historyStore)
        : m_credentialService(std::move(credentialService)),
          m_deepSeekClient(std::move(deepSeekClient)),
          m_settingsService(std::move(settingsService)),
          m_identityService(std::move(identityService)),
          m_historyStore(std::move(historyStore))
    {
    }

    void MainViewModel::Initialize()
    {
        m_settings = m_settingsService->LoadBalanceSettings();
        try
        {
            static_cast<void>(m_historyStore->Load());
        }
        catch (...)
        {
            m_historyWriteFailed = true;
        }
        m_state.apiFeatureEnabled = m_settings.apiFeatureEnabled;
        m_state.forecastEnabled = m_settings.forecastEnabled;
        m_state.notificationEnabled = m_settings.notifications.enabled;
        m_state.hasApiKey = m_credentialService->HasApiKey();
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
            m_state.countdownText = L"\u8ddd\u79bb\u6881\u6587\u8c37\u8fd8\u6709 "
                + ToHString(time::FormatDuration(pricing.remaining));
        }
        else
        {
            m_state.statusText = L"\u6881 \u6587 \u8c37 \u00b7 \u534a \u4ef7";
            m_state.countdownText = L"\u8ddd\u79bb\u6881\u6587\u5cf0\u8fd8\u6709 "
                + ToHString(time::FormatDuration(pricing.remaining));
        }
        m_state.nextPeriodText = ToHString(time::FormatPeriodRange(pricing.nextTransition.nextRange));
        UpdateBalancePresentation(utcNow);
    }

    winrt::Windows::Foundation::IAsyncAction MainViewModel::RefreshBalanceAsync(
        balance::BalanceRefreshReason const reason,
        std::optional<std::chrono::sys_seconds> const scheduledTimestamp)
    {
        if (balance::WritesHistory(reason) && !scheduledTimestamp)
        {
            throw winrt::hresult_invalid_argument(L"Scheduled balance refresh requires its target timestamp.");
        }
        if (!m_settings.apiFeatureEnabled)
        {
            co_return;
        }
        if (m_state.isRefreshing)
        {
            if (reason == balance::BalanceRefreshReason::SavedKeyObservation
                || reason == balance::BalanceRefreshReason::ApiReenabledObservation)
            {
                m_pendingObservationReason = reason;
            }
            co_return;
        }

        m_state.isRefreshing = true;
        UpdateBalancePresentation(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
        auto currentReason = reason;
        auto currentTimestamp = scheduledTimestamp;
        for (;;)
        {
            if (!m_settings.apiFeatureEnabled)
            {
                break;
            }

            const auto generation = m_refreshGeneration;
            auto apiKey = m_credentialService->TryGetApiKey();
            m_state.hasApiKey = apiKey.has_value();
            if (!apiKey)
            {
                ResetObservationState();
                break;
            }

            try
            {
                const auto responseText = co_await m_deepSeekClient->GetBalanceResponseAsync(*apiKey);
                if (generation == m_refreshGeneration && m_settings.apiFeatureEnabled)
                {
                    const auto balances = services::DeepSeekClient::ParseBalanceResponse(responseText);
                    std::map<std::string, balance::DecimalAmount, std::less<>> latest;
                    std::vector<std::string> currencies;
                    currencies.reserve(balances.size());
                    for (auto const& value : balances)
                    {
                        latest.insert_or_assign(value.currency, value.balance);
                        currencies.push_back(value.currency);
                    }

                    const auto previousCurrencies = m_settings.knownCurrencies;
                    const auto selectedChanged = balance::ReconcileSelectedCurrency(m_settings, currencies);
                    if (selectedChanged || !SameCurrencies(previousCurrencies, m_settings.knownCurrencies))
                    {
                        static_cast<void>(m_settingsService->SaveBalanceSettings(m_settings));
                    }

                    m_latestBalances = std::move(latest);
                    m_lastObservationTime = std::chrono::floor<std::chrono::seconds>(
                        std::chrono::system_clock::now());
                    m_lastRefreshFailed = false;

                    if (balance::WritesHistory(currentReason))
                    {
                        try
                        {
                            const auto seriesId = m_identityService->GetSeriesId(*apiKey);
                            m_historyStore->AppendSamples(balance::CreateHistorySampleBatch(
                                currentReason,
                                currentTimestamp,
                                seriesId,
                                balances));
                            m_historyWriteFailed = false;
                        }
                        catch (...)
                        {
                            m_historyWriteFailed = true;
                        }
                    }
                }
            }
            catch (...)
            {
                if (generation == m_refreshGeneration)
                {
                    m_lastRefreshFailed = true;
                }
            }

            apiKey.reset();
            if (m_pendingObservationReason && m_settings.apiFeatureEnabled)
            {
                currentReason = *m_pendingObservationReason;
                currentTimestamp.reset();
                m_pendingObservationReason.reset();
                continue;
            }
            break;
        }

        m_state.isRefreshing = false;
        UpdateBalancePresentation(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
    }

    balance::SettingsDraft MainViewModel::CreateSettingsDraft() const
    {
        return balance::SettingsDraft{ m_settings, m_credentialService->HasApiKey() };
    }

    SettingsCommitResult MainViewModel::CommitSettings(
        balance::BalanceSettings settings,
        balance::ApiKeyDraftAction const keyAction,
        winrt::hstring const& replacementApiKey,
        std::chrono::sys_seconds const now)
    {
        balance::NormalizeBalanceSettings(settings);
        if (keyAction == balance::ApiKeyDraftAction::Replace && replacementApiKey.empty())
        {
            return {};
        }

        const auto previousSettings = m_settings;
        if (!m_settings.knownCurrencies.empty())
        {
            settings.knownCurrencies = m_settings.knownCurrencies;
            if (std::find(
                    settings.knownCurrencies.begin(),
                    settings.knownCurrencies.end(),
                    settings.selectedCurrency) == settings.knownCurrencies.end())
            {
                settings.selectedCurrency = settings.knownCurrencies.front();
            }
        }
        auto previousApiKey = m_credentialService->TryGetApiKey();
        if (!m_settingsService->SaveBalanceSettings(settings))
        {
            return {};
        }

        try
        {
            if (keyAction == balance::ApiKeyDraftAction::Clear)
            {
                m_credentialService->ClearApiKey();
            }
            else if (keyAction == balance::ApiKeyDraftAction::Replace)
            {
                m_credentialService->SaveApiKey(replacementApiKey);
            }

            if (previousSettings.apiFeatureEnabled != settings.apiFeatureEnabled)
            {
                m_historyStore->AppendMarker(
                    settings.apiFeatureEnabled
                        ? balance::HistoryEntryKind::ApiOn
                        : balance::HistoryEntryKind::ApiOff,
                    now);
            }
        }
        catch (...)
        {
            static_cast<void>(m_settingsService->SaveBalanceSettings(previousSettings));
            try
            {
                if (previousApiKey)
                {
                    m_credentialService->SaveApiKey(*previousApiKey);
                }
                else
                {
                    m_credentialService->ClearApiKey();
                }
            }
            catch (...)
            {
                // The original exception remains the authoritative save failure.
            }
            return {};
        }

        m_settings = std::move(settings);
        const bool keyChanged = keyAction != balance::ApiKeyDraftAction::Keep;
        if (keyChanged || previousSettings.apiFeatureEnabled != m_settings.apiFeatureEnabled)
        {
            ++m_refreshGeneration;
        }
        if (keyChanged)
        {
            ResetObservationState();
        }
        m_state.apiFeatureEnabled = m_settings.apiFeatureEnabled;
        m_state.forecastEnabled = m_settings.forecastEnabled;
        m_state.notificationEnabled = m_settings.notifications.enabled;
        m_state.hasApiKey = m_credentialService->HasApiKey();
        UpdateBalancePresentation(now);

        SettingsCommitResult result;
        result.succeeded = true;
        result.scheduleChanged = keyChanged
            || previousSettings.apiFeatureEnabled != m_settings.apiFeatureEnabled
            || previousSettings.refreshInterval != m_settings.refreshInterval;
        result.notificationScheduleChanged =
            previousSettings.notifications.enabled != m_settings.notifications.enabled
            || previousSettings.notifications.advanceEnabled != m_settings.notifications.advanceEnabled
            || previousSettings.notifications.advanceMinutes != m_settings.notifications.advanceMinutes;
        if (m_settings.apiFeatureEnabled && m_state.hasApiKey)
        {
            if (keyAction == balance::ApiKeyDraftAction::Replace)
            {
                result.immediateRefreshReason = balance::BalanceRefreshReason::SavedKeyObservation;
            }
            else if (!previousSettings.apiFeatureEnabled)
            {
                result.immediateRefreshReason = balance::BalanceRefreshReason::ApiReenabledObservation;
            }
        }
        previousApiKey.reset();
        return result;
    }

    bool MainViewModel::SetForecastEnabled(bool const enabled)
    {
        auto updated = m_settings;
        updated.forecastEnabled = enabled;
        if (!m_settingsService->SaveBalanceSettings(updated))
        {
            return false;
        }
        m_settings = std::move(updated);
        m_state.forecastEnabled = enabled;
        UpdateBalancePresentation(std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now()));
        return true;
    }

    bool MainViewModel::SetNotificationEnabled(bool const enabled)
    {
        auto updated = m_settings;
        updated.notifications.enabled = enabled;
        if (!m_settingsService->SaveBalanceSettings(updated))
        {
            return false;
        }
        m_settings = std::move(updated);
        m_state.notificationEnabled = enabled;
        return true;
    }

    void MainViewModel::ReloadPersistedStateAfterCleanup(std::chrono::sys_seconds const now)
    {
        ++m_refreshGeneration;
        m_pendingObservationReason.reset();
        m_state.isRefreshing = false;
        m_settings = m_settingsService->LoadBalanceSettings();
        ResetObservationState();
        m_lastRefreshFailed = false;
        m_historyWriteFailed = false;
        m_state.apiFeatureEnabled = m_settings.apiFeatureEnabled;
        m_state.forecastEnabled = m_settings.forecastEnabled;
        m_state.notificationEnabled = m_settings.notifications.enabled;
        m_state.hasApiKey = m_credentialService->HasApiKey();
        UpdateClock(now);
    }

    bool MainViewModel::ResetStatistics(std::chrono::sys_seconds const now)
    {
        try
        {
            static_cast<void>(m_historyStore->Rollover(now));
            ++m_refreshGeneration;
            m_historyWriteFailed = false;
            UpdateBalancePresentation(now);
            return true;
        }
        catch (...)
        {
            m_historyWriteFailed = true;
            return false;
        }
    }

    MainViewState const& MainViewModel::State() const noexcept
    {
        return m_state;
    }

    balance::BalanceSettings const& MainViewModel::Settings() const noexcept
    {
        return m_settings;
    }

    void MainViewModel::ResetObservationState()
    {
        m_latestBalances.clear();
        m_lastObservationTime.reset();
        m_lastRefreshFailed = false;
        m_state.hasSuccessfulObservation = false;
    }

    void MainViewModel::UpdateBalancePresentation(std::chrono::sys_seconds const now)
    {
        m_state.apiFeatureEnabled = m_settings.apiFeatureEnabled;
        m_state.forecastEnabled = m_settings.forecastEnabled;
        m_state.notificationEnabled = m_settings.notifications.enabled;
        m_state.hasApiKey = m_credentialService->HasApiKey();
        m_state.hasSuccessfulObservation = m_lastObservationTime.has_value();

        if (!m_settings.apiFeatureEnabled)
        {
            m_state.balanceText = {};
            m_state.burnRateText = {};
            m_state.etaText = {};
            m_state.updateStatusText = {};
            return;
        }

        if (!m_state.hasApiKey)
        {
            m_state.balanceText = L"\u672a\u914d\u7f6e";
            m_state.burnRateText = m_settings.forecastEnabled ? L"\u2014\u2014" : winrt::hstring{};
            m_state.etaText = m_settings.forecastEnabled ? L"\u2014\u2014" : winrt::hstring{};
            m_state.updateStatusText = {};
            return;
        }

        const auto currentBalance = CurrentBalance();
        if (currentBalance)
        {
            m_state.balanceText = ToHString(balance::FormatCurrencyAmount(
                m_settings.selectedCurrency,
                *currentBalance));
        }
        else if (m_lastRefreshFailed)
        {
            m_state.balanceText = L"\u6682\u4e0d\u53ef\u7528";
        }
        else
        {
            m_state.balanceText = L"\u66f4\u65b0\u4e2d\u2026";
        }

        if (m_settings.forecastEnabled)
        {
            const auto forecast = m_forecastService.Forecast(
                m_historyStore->Entries(),
                m_settings.selectedCurrency,
                m_settings.rateWindow,
                balance::GetEffectiveAlgorithm(m_settings));
            m_state.burnRateText = forecast.hasValidInterval
                ? ToHString(balance::FormatBurnRate(m_settings.selectedCurrency, forecast.burnPerHour))
                : winrt::hstring{ L"\u83b7\u53d6\u4e2d" };
            if (currentBalance)
            {
                m_state.etaText = ToHString(balance::FormatEta(balance::CalculateEta(
                    *currentBalance,
                    balance::GetWarningBalance(m_settings, m_settings.selectedCurrency),
                    forecast)));
            }
            else
            {
                m_state.etaText = L"\u83b7\u53d6\u4e2d";
            }
        }
        else
        {
            m_state.burnRateText = {};
            m_state.etaText = {};
        }

        if (m_lastObservationTime)
        {
            auto updateText = time::FormatLastUpdated(
                std::chrono::duration_cast<std::chrono::seconds>(now - *m_lastObservationTime));
            if (m_lastRefreshFailed)
            {
                updateText = L"\u66f4\u65b0\u5931\u8d25 \u00b7 " + updateText;
            }
            if (m_historyWriteFailed && m_settings.forecastEnabled)
            {
                updateText = L"\u5386\u53f2\u5199\u5165\u5931\u8d25 \u00b7 " + updateText;
            }
            m_state.updateStatusText = ToHString(updateText);
        }
        else
        {
            m_state.updateStatusText = {};
        }
    }

    std::optional<balance::DecimalAmount> MainViewModel::CurrentBalance() const
    {
        const auto found = m_latestBalances.find(m_settings.selectedCurrency);
        return found == m_latestBalances.end()
            ? std::nullopt
            : std::optional<balance::DecimalAmount>{ found->second };
    }
}
