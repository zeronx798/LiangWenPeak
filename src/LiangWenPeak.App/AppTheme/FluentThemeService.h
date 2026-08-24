#pragma once

namespace liangwenpeak::apptheme
{
    class FluentThemeService final
    {
    public:
        FluentThemeService() noexcept;

        [[nodiscard]] bool IsAvailable() const noexcept;
        [[nodiscard]] bool IsEnabled() const noexcept;
        [[nodiscard]] bool SetEnabled(bool enabled) noexcept;

    private:
        bool m_available = false;
        bool m_enabled = false;
    };
}
