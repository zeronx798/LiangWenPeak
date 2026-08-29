#include "pch.h"
#include "SettingsService.h"

#include <charconv>
#include <limits>
#include <optional>
#include <string_view>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace liangwenpeak::services
{
    namespace
    {
        constexpr wchar_t ApiFeatureEnabledName[] = L"ApiFeatureEnabled";
        constexpr wchar_t ForecastEnabledName[] = L"BalanceForecastEnabled";
        constexpr wchar_t SelectedCurrencyName[] = L"SelectedCurrency";
        constexpr wchar_t BalanceRefreshIntervalName[] = L"BalanceRefreshIntervalMinutes";
        constexpr wchar_t RateWindowName[] = L"BalanceRateWindowSeconds";
        constexpr wchar_t PreferredAlgorithmName[] = L"PreferredPredictionAlgorithm";
        constexpr wchar_t WarningBalancesName[] = L"WarningBalances";
        constexpr wchar_t KnownCurrenciesName[] = L"KnownCurrencies";
        constexpr wchar_t NotificationEnabledName[] = L"NotificationEnabled";
        constexpr wchar_t NotificationAdvanceEnabledName[] = L"NotificationAdvanceEnabled";
        constexpr wchar_t NotificationAdvanceMinutesName[] = L"NotificationAdvanceMinutes";
        constexpr wchar_t LastAdvanceNotificationTransitionName[] =
            L"LastAdvanceNotificationTransitionUnixSeconds";
        constexpr wchar_t LastArrivedNotificationTransitionName[] =
            L"LastArrivedNotificationTransitionUnixSeconds";

        std::optional<DWORD> ReadDword(
            std::wstring const& settingsPath,
            wchar_t const* const name) noexcept
        {
            DWORD value{};
            DWORD size = sizeof(value);
            if (::RegGetValueW(
                HKEY_CURRENT_USER,
                settingsPath.c_str(),
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

        std::optional<std::wstring> ReadString(
            std::wstring const& settingsPath,
            wchar_t const* const name) noexcept
        {
            DWORD size{};
            if (::RegGetValueW(
                HKEY_CURRENT_USER,
                settingsPath.c_str(),
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
                settingsPath.c_str(),
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

        std::optional<ULONGLONG> ReadQword(
            std::wstring const& settingsPath,
            wchar_t const* const name) noexcept
        {
            ULONGLONG value{};
            DWORD size = sizeof(value);
            if (::RegGetValueW(
                HKEY_CURRENT_USER,
                settingsPath.c_str(),
                name,
                RRF_RT_REG_QWORD,
                nullptr,
                &value,
                &size) != ERROR_SUCCESS)
            {
                return std::nullopt;
            }
            return value;
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

        bool WriteQword(HKEY const key, wchar_t const* const name, ULONGLONG const value) noexcept
        {
            return ::RegSetValueExW(
                key,
                name,
                0,
                REG_QWORD,
                reinterpret_cast<BYTE const*>(&value),
                sizeof(value)) == ERROR_SUCCESS;
        }

        std::optional<std::chrono::sys_seconds> ParseTransitionTimestamp(
            std::optional<ULONGLONG> const value) noexcept
        {
            if (!value || *value > static_cast<ULONGLONG>((std::numeric_limits<std::int64_t>::max)()))
            {
                return std::nullopt;
            }
            return std::chrono::sys_seconds{
                std::chrono::seconds{ static_cast<std::int64_t>(*value) } };
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

    SettingsService::SettingsService(StateProfile const& profile)
        : m_settingsPath(profile.RegistrySubkey())
    {
    }

    balance::BalanceSettings SettingsService::LoadBalanceSettings() const noexcept
    {
        balance::BalanceSettings settings;
        try
        {
            if (const auto value = ReadDword(m_settingsPath, ApiFeatureEnabledName))
            {
                settings.apiFeatureEnabled = *value != 0;
            }
            if (const auto value = ReadDword(m_settingsPath, ForecastEnabledName))
            {
                settings.forecastEnabled = *value != 0;
            }
            if (const auto value = ReadString(m_settingsPath, SelectedCurrencyName))
            {
                settings.selectedCurrency = winrt::to_string(*value);
            }
            if (const auto value = ReadDword(m_settingsPath, BalanceRefreshIntervalName))
            {
                settings.refreshInterval = std::chrono::minutes{ *value };
            }
            if (const auto value = ReadDword(m_settingsPath, RateWindowName))
            {
                settings.rateWindow = std::chrono::seconds{ *value };
            }
            if (const auto value = ReadDword(m_settingsPath, PreferredAlgorithmName); value && *value <= 2)
            {
                settings.preferredAlgorithm = static_cast<balance::PredictionAlgorithm>(*value);
            }
            if (const auto value = ReadString(m_settingsPath, WarningBalancesName))
            {
                ParseWarnings(*value, settings);
            }
            if (const auto value = ReadString(m_settingsPath, KnownCurrenciesName))
            {
                settings.knownCurrencies = ParseCurrencies(*value);
            }
            if (const auto value = ReadDword(m_settingsPath, NotificationEnabledName))
            {
                settings.notifications.enabled = *value != 0;
            }
            if (const auto value = ReadDword(m_settingsPath, NotificationAdvanceEnabledName))
            {
                settings.notifications.advanceEnabled = *value != 0;
            }
            if (const auto value = ReadDword(m_settingsPath, NotificationAdvanceMinutesName))
            {
                settings.notifications.advanceMinutes = std::chrono::minutes{ *value };
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
                m_settingsPath.c_str(),
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
                && WriteString(key, KnownCurrenciesName, SerializeCurrencies(settings.knownCurrencies))
                && WriteDword(key, NotificationEnabledName, settings.notifications.enabled ? 1U : 0U)
                && WriteDword(
                    key,
                    NotificationAdvanceEnabledName,
                    settings.notifications.advanceEnabled ? 1U : 0U)
                && WriteDword(
                    key,
                    NotificationAdvanceMinutesName,
                    static_cast<DWORD>(settings.notifications.advanceMinutes.count()));
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

    notifications::NotificationDeliveryState
        SettingsService::LoadNotificationDeliveryState() const noexcept
    {
        notifications::NotificationDeliveryState state;
        try
        {
            state.lastAdvanceTransition = ParseTransitionTimestamp(
                ReadQword(m_settingsPath, LastAdvanceNotificationTransitionName));
            state.lastArrivedTransition = ParseTransitionTimestamp(
                ReadQword(m_settingsPath, LastArrivedNotificationTransitionName));
        }
        catch (...)
        {
            state = {};
        }
        return state;
    }

    bool SettingsService::SaveNotificationDeliveryState(
        notifications::NotificationDeliveryState const& state) const noexcept
    {
        try
        {
            HKEY key{};
            if (::RegCreateKeyExW(
                HKEY_CURRENT_USER,
                m_settingsPath.c_str(),
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

            bool saved = true;
            if (state.lastAdvanceTransition)
            {
                saved = WriteQword(
                    key,
                    LastAdvanceNotificationTransitionName,
                    static_cast<ULONGLONG>(
                        state.lastAdvanceTransition->time_since_epoch().count())) && saved;
            }
            if (state.lastArrivedTransition)
            {
                saved = WriteQword(
                    key,
                    LastArrivedNotificationTransitionName,
                    static_cast<ULONGLONG>(
                        state.lastArrivedTransition->time_since_epoch().count())) && saved;
            }
            ::RegCloseKey(key);
            return saved;
        }
        catch (...)
        {
            return false;
        }
    }

    bool SettingsService::DeleteAllSettings() const noexcept
    {
        const auto result = ::RegDeleteTreeW(HKEY_CURRENT_USER, m_settingsPath.c_str());
        return result == ERROR_SUCCESS
            || result == ERROR_FILE_NOT_FOUND
            || result == ERROR_PATH_NOT_FOUND;
    }

    std::wstring const& SettingsService::RegistrySubkey() const noexcept
    {
        return m_settingsPath;
    }
}
