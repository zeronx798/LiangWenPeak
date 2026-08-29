#pragma once

#include "FluentThemeService.h"

namespace liangwenpeak::apptheme
{
    class ThemeManager final
    {
    public:
        explicit ThemeManager(services::StateProfile const& profile) noexcept;

        [[nodiscard]] bool IsFluentThemeAvailable() const noexcept;
        [[nodiscard]] bool IsFluentThemeEnabled() const noexcept;
        [[nodiscard]] bool SetFluentThemeEnabled(bool enabled) noexcept;
        void Reload() noexcept;

        void ApplyToWindow(
            winrt::Microsoft::UI::Xaml::FrameworkElement const& root,
            HWND windowHandle) const noexcept;

        static void ApplyWindowPresentation(
            winrt::Microsoft::UI::Xaml::FrameworkElement const& root,
            HWND windowHandle,
            bool fluentThemeEnabled) noexcept;

        static void ApplyControlPresentation(
            winrt::Microsoft::UI::Xaml::Controls::Control const& control,
            bool fluentThemeEnabled) noexcept;

        [[nodiscard]] static winrt::Microsoft::UI::Xaml::Style CreateMenuFlyoutPresenterStyle(
            bool fluentThemeEnabled);

    private:
        FluentThemeService m_service;
    };
}
