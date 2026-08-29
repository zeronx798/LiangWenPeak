#pragma once

#include "Services/StateProfile.h"

#include <string>

namespace liangwenpeak::apptheme
{
    class FluentThemeService final
    {
    public:
        explicit FluentThemeService(services::StateProfile const& profile) noexcept;

        [[nodiscard]] bool IsAvailable() const noexcept;
        [[nodiscard]] bool IsEnabled() const noexcept;
        [[nodiscard]] bool SetEnabled(bool enabled) noexcept;
        void Reload() noexcept;

    private:
        std::wstring m_settingsPath;
        bool m_available = false;
        bool m_enabled = false;
    };
}
