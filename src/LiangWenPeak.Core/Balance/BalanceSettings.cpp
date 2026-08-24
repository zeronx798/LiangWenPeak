#include "BalanceSettings.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace liangwenpeak::balance
{
    namespace
    {
        bool IsValidCurrency(std::string const& currency) noexcept
        {
            return !currency.empty()
                && currency.size() <= 16
                && std::all_of(currency.begin(), currency.end(), [](char const character)
                {
                    return (character >= 'A' && character <= 'Z')
                        || (character >= 'a' && character <= 'z')
                        || (character >= '0' && character <= '9')
                        || character == '_' || character == '-' || character == '.';
                });
        }
    }

    ApiSettingsDraft::ApiSettingsDraft(BalanceSettings persistedSettings, bool const hasApiKey)
        : m_persisted(std::move(persistedSettings)),
          m_settings(m_persisted),
          m_hasApiKey(hasApiKey)
    {
        NormalizeBalanceSettings(m_persisted);
        m_settings = m_persisted;
    }

    BalanceSettings const& ApiSettingsDraft::Settings() const noexcept
    {
        return m_settings;
    }

    BalanceSettings& ApiSettingsDraft::Settings() noexcept
    {
        return m_settings;
    }

    ApiKeyDraftAction ApiSettingsDraft::KeyAction() const noexcept
    {
        return m_keyAction;
    }

    bool ApiSettingsDraft::PersistedHasApiKey() const noexcept
    {
        return m_hasApiKey;
    }

    void ApiSettingsDraft::RequestApiKeyClear() noexcept
    {
        m_keyAction = ApiKeyDraftAction::Clear;
    }

    void ApiSettingsDraft::UndoApiKeyClear() noexcept
    {
        m_keyAction = ApiKeyDraftAction::Keep;
    }

    void ApiSettingsDraft::OnApiKeyInputChanged(bool const hasReplacementText) noexcept
    {
        if (hasReplacementText)
        {
            m_keyAction = ApiKeyDraftAction::Replace;
        }
        else if (m_keyAction == ApiKeyDraftAction::Replace)
        {
            m_keyAction = ApiKeyDraftAction::Keep;
        }
    }

    void ApiSettingsDraft::SetRefreshInterval(std::chrono::minutes const interval)
    {
        if (!time::IsSupportedBalanceRefreshInterval(interval))
        {
            throw std::invalid_argument("Unsupported balance refresh interval");
        }
        m_settings.refreshInterval = interval;
        if (m_settings.rateWindow < interval)
        {
            m_settings.rateWindow = interval;
        }
    }

    void ApiSettingsDraft::SetRateWindow(std::chrono::seconds const window)
    {
        if (!IsSupportedRateWindow(window) || window < m_settings.refreshInterval)
        {
            throw std::invalid_argument("Unsupported balance rate window");
        }
        m_settings.rateWindow = window;
    }

    void ApiSettingsDraft::Cancel()
    {
        m_settings = m_persisted;
        m_keyAction = ApiKeyDraftAction::Keep;
    }

    bool IsSupportedRateWindow(std::chrono::seconds const window) noexcept
    {
        return std::find(SupportedRateWindows.begin(), SupportedRateWindows.end(), window)
            != SupportedRateWindows.end();
    }

    std::vector<std::chrono::seconds> GetAvailableRateWindows(
        std::chrono::minutes const refreshInterval)
    {
        if (!time::IsSupportedBalanceRefreshInterval(refreshInterval))
        {
            throw std::invalid_argument("Unsupported balance refresh interval");
        }

        std::vector<std::chrono::seconds> result;
        std::copy_if(
            SupportedRateWindows.begin(),
            SupportedRateWindows.end(),
            std::back_inserter(result),
            [refreshInterval](std::chrono::seconds const window)
            {
                return window >= refreshInterval;
            });
        return result;
    }

    void NormalizeBalanceSettings(BalanceSettings& settings)
    {
        if (!time::IsSupportedBalanceRefreshInterval(settings.refreshInterval))
        {
            settings.refreshInterval = time::DefaultBalanceRefreshInterval;
        }
        if (!IsSupportedRateWindow(settings.rateWindow))
        {
            settings.rateWindow = DefaultRateWindow;
        }
        if (settings.rateWindow < settings.refreshInterval)
        {
            settings.rateWindow = settings.refreshInterval;
        }
        if (!IsValidCurrency(settings.selectedCurrency))
        {
            settings.selectedCurrency = "CNY";
        }
        settings.knownCurrencies.erase(
            std::remove_if(settings.knownCurrencies.begin(), settings.knownCurrencies.end(), [](auto const& currency)
            {
                return !IsValidCurrency(currency);
            }),
            settings.knownCurrencies.end());
        std::sort(settings.knownCurrencies.begin(), settings.knownCurrencies.end());
        settings.knownCurrencies.erase(
            std::unique(settings.knownCurrencies.begin(), settings.knownCurrencies.end()),
            settings.knownCurrencies.end());
        if (std::find(settings.knownCurrencies.begin(), settings.knownCurrencies.end(), settings.selectedCurrency)
            == settings.knownCurrencies.end())
        {
            settings.knownCurrencies.push_back(settings.selectedCurrency);
        }
        for (auto iterator = settings.warningBalances.begin(); iterator != settings.warningBalances.end();)
        {
            if (!IsValidCurrency(iterator->first) || iterator->second.ScaledValue() < 0)
            {
                iterator = settings.warningBalances.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
        if (settings.preferredAlgorithm == PredictionAlgorithm::LastValidSample)
        {
            settings.preferredAlgorithm = PredictionAlgorithm::SlidingAverage;
        }
    }

    PredictionAlgorithm GetEffectiveAlgorithm(BalanceSettings const& settings) noexcept
    {
        return settings.rateWindow == settings.refreshInterval
            ? PredictionAlgorithm::LastValidSample
            : settings.preferredAlgorithm;
    }

    DecimalAmount GetWarningBalance(
        BalanceSettings const& settings,
        std::string const& currency) noexcept
    {
        const auto found = settings.warningBalances.find(currency);
        return found == settings.warningBalances.end() ? DecimalAmount{} : found->second;
    }

    void SetWarningBalance(
        BalanceSettings& settings,
        std::string currency,
        DecimalAmount const amount)
    {
        if (!IsValidCurrency(currency) || amount.ScaledValue() < 0)
        {
            throw std::invalid_argument("Invalid currency warning balance");
        }
        settings.warningBalances.insert_or_assign(std::move(currency), amount);
    }

    bool ReconcileSelectedCurrency(
        BalanceSettings& settings,
        std::vector<std::string> const& availableCurrencies)
    {
        if (availableCurrencies.empty())
        {
            return false;
        }
        if (std::any_of(availableCurrencies.begin(), availableCurrencies.end(), [](auto const& currency)
            {
                return !IsValidCurrency(currency);
            }))
        {
            throw std::invalid_argument("Available currency list contains an invalid code");
        }

        settings.knownCurrencies = availableCurrencies;
        std::sort(settings.knownCurrencies.begin(), settings.knownCurrencies.end());
        settings.knownCurrencies.erase(
            std::unique(settings.knownCurrencies.begin(), settings.knownCurrencies.end()),
            settings.knownCurrencies.end());
        if (std::find(availableCurrencies.begin(), availableCurrencies.end(), settings.selectedCurrency)
            != availableCurrencies.end())
        {
            return false;
        }
        settings.selectedCurrency = availableCurrencies.front();
        return true;
    }
}
