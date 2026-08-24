#include "pch.h"
#include "FluentThemeService.h"

#include "WindowsVersionDetector.h"

#include <optional>

#pragma comment(lib, "advapi32.lib")

namespace liangwenpeak::apptheme
{
    namespace
    {
        constexpr wchar_t SettingsPath[] = L"Software\\LiangWenPeak";
        constexpr wchar_t FluentThemeEnabledName[] = L"FluentThemeEnabled";

        std::optional<bool> LoadPersistedState() noexcept
        {
            DWORD value{};
            DWORD size = sizeof(value);
            if (::RegGetValueW(
                HKEY_CURRENT_USER,
                SettingsPath,
                FluentThemeEnabledName,
                RRF_RT_REG_DWORD,
                nullptr,
                &value,
                &size) != ERROR_SUCCESS)
            {
                return std::nullopt;
            }
            return value != 0;
        }

        bool SavePersistedState(bool const enabled) noexcept
        {
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

            const DWORD value = enabled ? 1U : 0U;
            const auto result = ::RegSetValueExW(
                key,
                FluentThemeEnabledName,
                0,
                REG_DWORD,
                reinterpret_cast<BYTE const*>(&value),
                sizeof(value));
            ::RegCloseKey(key);
            return result == ERROR_SUCCESS;
        }
    }

    FluentThemeService::FluentThemeService() noexcept
        : m_available(WindowsVersionDetector::IsWindows11OrGreater()),
          m_enabled(m_available && LoadPersistedState().value_or(true))
    {
    }

    bool FluentThemeService::IsAvailable() const noexcept
    {
        return m_available;
    }

    bool FluentThemeService::IsEnabled() const noexcept
    {
        return m_available && m_enabled;
    }

    bool FluentThemeService::SetEnabled(bool const enabled) noexcept
    {
        if (!m_available)
        {
            return false;
        }
        if (m_enabled == enabled)
        {
            return true;
        }
        if (!SavePersistedState(enabled))
        {
            return false;
        }
        m_enabled = enabled;
        return true;
    }
}
