#include "pch.h"
#include "SettingsService.h"

#include <charconv>
#include <optional>
#include <string_view>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace liangwenpeak::services
{
    namespace
    {
        constexpr wchar_t SettingsPath[] = L"Software\\LiangWenPeak";
        constexpr wchar_t ApiFeatureEnabledName[] = L"ApiFeatureEnabled";
        constexpr wchar_t ForecastEnabledName[] = L"BalanceForecastEnabled";
        constexpr wchar_t SelectedCurrencyName[] = L"SelectedCurrency";
        constexpr wchar_t BalanceRefreshIntervalName[] = L"BalanceRefreshIntervalMinutes";
        constexpr wchar_t RateWindowName[] = L"BalanceRateWindowSeconds";
        constexpr wchar_t PreferredAlgorithmName[] = L"PreferredPredictionAlgorithm";
        constexpr wchar_t WarningBalancesName[] = L"WarningBalances";
        constexpr wchar_t KnownCurrenciesName[] = L"KnownCurrencies";

        std::optional<DWORD> ReadDword(wchar_t const* const name) noexcept
        {
            DWORD value{};
            DWORD size = sizeof(value);
            if (::RegGetValueW(
                HKEY_CURRENT_USER,
                SettingsPath,
                name,
                RRF_RT_REG_DWORD,
                nullptr,
                &value,
                &size) != ERROR_SUCCESS)
            {
                return std::nullopt;
            }
            return value;
        }

        std::optional<std::wstring> ReadString(wchar_t const* const name) noexcept
        {
            DWORD size{};
            if (::RegGetValueW(
                HKEY_CURRENT_USER,
                SettingsPath,
                name,
                RRF_RT_REG_SZ,
                nullptr,
                nullptr,
                &size) != ERROR_SUCCESS
                || size < sizeof(wchar_t))
            {
                return std::nullopt;
            }

            std::vector<wchar_t> buffer(size / sizeof(wchar_t));
            if (::RegGetValueW(
                HKEY_CURRENT_USER,
                SettingsPath,
                name,
                RRF_RT_REG_SZ,
                nullptr,
                buffer.data(),
                &size) != ERROR_SUCCESS)
            {
                return std::nullopt;
            }
            return std::wstring{ buffer.data() };
        }

        bool WriteDword(HKEY const key, wchar_t const* const name, DWORD const value) noexcept
        {
            return ::RegSetValueExW(
                key,
                name,
                0,
                REG_DWORD,
                reinterpret_cast<BYTE const*>(&value),
                sizeof(value)) == ERROR_SUCCESS;
        }

        bool WriteString(HKEY const key, wchar_t const* const name, std::wstring const& value) noexcept
        {
            return ::RegSetValueExW(
                key,
                name,
                0,
                REG_SZ,
                reinterpret_cast<BYTE const*>(value.c_str()),
                static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
        }

        std::wstring SerializeWarnings(balance::BalanceSettings const& settings)
        {
            std::string serialized;
            for (auto const& [currency, amount] : settings.warningBalances)
            {
                if (!serialized.empty())
                {
                    serialized.push_back(';');
                }
                serialized += currency + '=' + std::to_string(amount.ScaledValue());
            }
            return winrt::to_hstring(serialized).c_str();
        }

        void ParseWarnings(std::wstring const& value, balance::BalanceSettings& settings)
        {
            const auto serialized = winrt::to_string(value);
            size_t start{};
            while (start < serialized.size())
            {
                const auto end = serialized.find(';', start);
                const auto item = std::string_view{ serialized }.substr(
                    start,
                    end == std::string::npos ? std::string::npos : end - start);
                const auto equals = item.find('=');
                if (equals != std::string_view::npos)
                {
                    std::int64_t scaled{};
                    const auto number = item.substr(equals + 1);
                    const auto [numberEnd, error] = std::from_chars(
                        number.data(), number.data() + number.size(), scaled);
                    if (error == std::errc{}
                        && numberEnd == number.data() + number.size()
                        && scaled >= 0)
                    {
                        try
                        {
                            balance::SetWarningBalance(
                                settings,
                                std::string{ item.substr(0, equals) },
                                balance::DecimalAmount::FromScaled(scaled));
                        }
                        catch (...)
                        {
                            // Invalid persisted currency entries are ignored.
                        }
                    }
                }
                if (end == std::string::npos)
                {
                    break;
                }
                start = end + 1;
            }
        }

        std::wstring SerializeCurrencies(std::vector<std::string> const& currencies)
        {
            std::string serialized;
            for (auto const& currency : currencies)
            {
                if (!serialized.empty())
                {
                    serialized.push_back(';');
                }
                serialized += currency;
            }
            return winrt::to_hstring(serialized).c_str();
        }

        std::vector<std::string> ParseCurrencies(std::wstring const& value)
        {
            std::vector<std::string> result;
            const auto serialized = winrt::to_string(value);
            size_t start{};
            while (start <= serialized.size())
            {
                const auto end = serialized.find(';', start);
                auto currency = serialized.substr(
                    start,
                    end == std::string::npos ? std::string::npos : end - start);
                if (!currency.empty())
                {
                    result.push_back(std::move(currency));
                }
                if (end == std::string::npos)
                {
                    break;
                }
                start = end + 1;
            }
            return result;
        }
    }

    balance::BalanceSettings SettingsService::LoadBalanceSettings() const noexcept
    {
        balance::BalanceSettings settings;
        try
        {
            if (const auto value = ReadDword(ApiFeatureEnabledName))
            {
                settings.apiFeatureEnabled = *value != 0;
            }
            if (const auto value = ReadDword(ForecastEnabledName))
            {
                settings.forecastEnabled = *value != 0;
            }
            if (const auto value = ReadString(SelectedCurrencyName))
            {
                settings.selectedCurrency = winrt::to_string(*value);
            }
            if (const auto value = ReadDword(BalanceRefreshIntervalName))
            {
                settings.refreshInterval = std::chrono::minutes{ *value };
            }
            if (const auto value = ReadDword(RateWindowName))
            {
                settings.rateWindow = std::chrono::seconds{ *value };
            }
            if (const auto value = ReadDword(PreferredAlgorithmName); value && *value <= 2)
            {
                settings.preferredAlgorithm = static_cast<balance::PredictionAlgorithm>(*value);
            }
            if (const auto value = ReadString(WarningBalancesName))
            {
                ParseWarnings(*value, settings);
            }
            if (const auto value = ReadString(KnownCurrenciesName))
            {
                settings.knownCurrencies = ParseCurrencies(*value);
            }
            balance::NormalizeBalanceSettings(settings);
        }
        catch (...)
        {
            settings = {};
        }
        return settings;
    }

    bool SettingsService::SaveBalanceSettings(balance::BalanceSettings const& source) const noexcept
    {
        try
        {
            auto settings = source;
            balance::NormalizeBalanceSettings(settings);
            HKEY key{};
            if (::RegCreateKeyExW(
                HKEY_CURRENT_USER,
                SettingsPath,
                0,
                nullptr,
                REG_OPTION_NON_VOLATILE,
                KEY_SET_VALUE,
                nullptr,
                &key,
                nullptr) != ERROR_SUCCESS)
            {
                return false;
            }

            const bool saved = WriteDword(key, ApiFeatureEnabledName, settings.apiFeatureEnabled ? 1U : 0U)
                && WriteDword(key, ForecastEnabledName, settings.forecastEnabled ? 1U : 0U)
                && WriteString(key, SelectedCurrencyName, winrt::to_hstring(settings.selectedCurrency).c_str())
                && WriteDword(key, BalanceRefreshIntervalName, static_cast<DWORD>(settings.refreshInterval.count()))
                && WriteDword(key, RateWindowName, static_cast<DWORD>(settings.rateWindow.count()))
                && WriteDword(key, PreferredAlgorithmName, static_cast<DWORD>(settings.preferredAlgorithm))
                && WriteString(key, WarningBalancesName, SerializeWarnings(settings))
                && WriteString(key, KnownCurrenciesName, SerializeCurrencies(settings.knownCurrencies));
            ::RegCloseKey(key);
            return saved;
        }
        catch (...)
        {
            return false;
        }
    }

    bool SettingsService::SaveSelectedCurrency(std::string const& currency) const noexcept
    {
        auto settings = LoadBalanceSettings();
        settings.selectedCurrency = currency;
        return SaveBalanceSettings(settings);
    }

    bool SettingsService::SaveForecastEnabled(bool const enabled) const noexcept
    {
        auto settings = LoadBalanceSettings();
        settings.forecastEnabled = enabled;
        return SaveBalanceSettings(settings);
    }

    std::chrono::minutes SettingsService::LoadBalanceRefreshInterval() const noexcept
    {
        return LoadBalanceSettings().refreshInterval;
    }

    bool SettingsService::SaveBalanceRefreshInterval(std::chrono::minutes const interval) const noexcept
    {
        auto settings = LoadBalanceSettings();
        settings.refreshInterval = interval;
        return SaveBalanceSettings(settings);
    }
}
