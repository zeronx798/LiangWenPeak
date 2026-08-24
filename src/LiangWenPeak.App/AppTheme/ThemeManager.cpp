#include "pch.h"
#include "ThemeManager.h"

#include "WindowsVersionDetector.h"

#include <dwmapi.h>
#include <winrt/Windows.UI.Xaml.Interop.h>

#pragma comment(lib, "dwmapi.lib")

namespace liangwenpeak::apptheme
{
    bool ThemeManager::IsFluentThemeAvailable() const noexcept
    {
        return m_service.IsAvailable();
    }

    bool ThemeManager::IsFluentThemeEnabled() const noexcept
    {
        return m_service.IsEnabled();
    }

    bool ThemeManager::SetFluentThemeEnabled(bool const enabled) noexcept
    {
        return m_service.SetEnabled(enabled);
    }

    void ThemeManager::ApplyToWindow(
        winrt::Microsoft::UI::Xaml::FrameworkElement const& root,
        HWND const windowHandle) const noexcept
    {
        ApplyWindowPresentation(root, windowHandle, IsFluentThemeEnabled());
    }

    void ThemeManager::ApplyWindowPresentation(
        winrt::Microsoft::UI::Xaml::FrameworkElement const& root,
        HWND const windowHandle,
        bool const fluentThemeEnabled) noexcept
    {
        try
        {
            root.RequestedTheme(fluentThemeEnabled
                ? winrt::Microsoft::UI::Xaml::ElementTheme::Dark
                : winrt::Microsoft::UI::Xaml::ElementTheme::Default);
        }
        catch (...)
        {
            // A closing XAML root needs no theme update.
        }

        if (windowHandle == nullptr || !::IsWindow(windowHandle))
        {
            return;
        }

        const BOOL darkMode = TRUE;
        static_cast<void>(::DwmSetWindowAttribute(
            windowHandle,
            DWMWA_USE_IMMERSIVE_DARK_MODE,
            &darkMode,
            sizeof(darkMode)));

        if (!WindowsVersionDetector::IsWindows11OrGreater())
        {
            return;
        }

        const DWM_WINDOW_CORNER_PREFERENCE corners = fluentThemeEnabled
            ? DWMWCP_ROUND
            : DWMWCP_DONOTROUND;
        static_cast<void>(::DwmSetWindowAttribute(
            windowHandle,
            DWMWA_WINDOW_CORNER_PREFERENCE,
            &corners,
            sizeof(corners)));

        const COLORREF borderColor = fluentThemeEnabled
            ? RGB(32, 50, 74)
            : static_cast<COLORREF>(0xFFFFFFFF);
        static_cast<void>(::DwmSetWindowAttribute(
            windowHandle,
            DWMWA_BORDER_COLOR,
            &borderColor,
            sizeof(borderColor)));
    }

    void ThemeManager::ApplyControlPresentation(
        winrt::Microsoft::UI::Xaml::Controls::Control const& control,
        bool const fluentThemeEnabled) noexcept
    {
        try
        {
            const auto radius = fluentThemeEnabled
                ? winrt::Microsoft::UI::Xaml::CornerRadius{ 4.0, 4.0, 4.0, 4.0 }
                : winrt::Microsoft::UI::Xaml::CornerRadius{};
            const auto focusMargin = fluentThemeEnabled
                ? winrt::Microsoft::UI::Xaml::Thickness{ -2.0, -2.0, -2.0, -2.0 }
                : winrt::Microsoft::UI::Xaml::Thickness{};
            control.CornerRadius(radius);
            control.UseSystemFocusVisuals(true);
            control.FocusVisualMargin(focusMargin);
        }
        catch (...)
        {
            // A closing control needs no visual update.
        }
    }

    winrt::Microsoft::UI::Xaml::Style ThemeManager::CreateMenuFlyoutPresenterStyle(
        bool const fluentThemeEnabled)
    {
        if (fluentThemeEnabled)
        {
            return nullptr;
        }

        winrt::Microsoft::UI::Xaml::Style style;
        style.TargetType(winrt::xaml_typename<
            winrt::Microsoft::UI::Xaml::Controls::MenuFlyoutPresenter>());

        winrt::Microsoft::UI::Xaml::Setter cornerRadius;
        cornerRadius.Property(
            winrt::Microsoft::UI::Xaml::Controls::Control::CornerRadiusProperty());
        cornerRadius.Value(winrt::box_value(winrt::Microsoft::UI::Xaml::CornerRadius{}));
        style.Setters().Append(cornerRadius);
        return style;
    }

}
