#include "pch.h"
#include "SettingsService.h"

#include "Time/BalanceRefreshSchedule.h"

#pragma comment(lib, "advapi32.lib")

namespace liangwenpeak::services
{
    namespace
    {
        constexpr wchar_t SettingsPath[] = L"Software\\LiangWenPeak";
        constexpr wchar_t BalanceRefreshIntervalName[] = L"BalanceRefreshIntervalMinutes";
    }

    std::chrono::minutes SettingsService::LoadBalanceRefreshInterval() const noexcept
    {
        DWORD value{};
        DWORD valueSize = static_cast<DWORD>(sizeof(value));
        const auto status = ::RegGetValueW(
            HKEY_CURRENT_USER,
            SettingsPath,
            BalanceRefreshIntervalName,
            RRF_RT_REG_DWORD,
            nullptr,
            &value,
            &valueSize);
        if (status != ERROR_SUCCESS)
        {
            return time::DefaultBalanceRefreshInterval;
        }

        const auto interval = std::chrono::minutes{
            static_cast<std::chrono::minutes::rep>(value) };
        return time::IsSupportedBalanceRefreshInterval(interval)
            ? interval
            : time::DefaultBalanceRefreshInterval;
    }

    bool SettingsService::SaveBalanceRefreshInterval(std::chrono::minutes const interval) const noexcept
    {
        if (!time::IsSupportedBalanceRefreshInterval(interval))
        {
            return false;
        }

        HKEY settingsKey{};
        const auto createStatus = ::RegCreateKeyExW(
            HKEY_CURRENT_USER,
            SettingsPath,
            0,
            nullptr,
            REG_OPTION_NON_VOLATILE,
            KEY_SET_VALUE,
            nullptr,
            &settingsKey,
            nullptr);
        if (createStatus != ERROR_SUCCESS)
        {
            return false;
        }

        const auto value = static_cast<DWORD>(interval.count());
        const auto saveStatus = ::RegSetValueExW(
            settingsKey,
            BalanceRefreshIntervalName,
            0,
            REG_DWORD,
            reinterpret_cast<BYTE const*>(&value),
            static_cast<DWORD>(sizeof(value)));
        ::RegCloseKey(settingsKey);
        return saveStatus == ERROR_SUCCESS;
    }
}
