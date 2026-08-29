#pragma once

#include "ApiSettingsWindow.g.h"
#include "Balance/BalanceSettings.h"

#include <functional>
#include <optional>
#include <vector>

namespace winrt::LiangWenPeak::implementation
{
    struct ApiSettingsWindow : ApiSettingsWindowT<ApiSettingsWindow>
    {
        struct CleanupResetState
        {
            liangwenpeak::balance::SettingsDraft draft;
            bool fluentThemeEnabled = false;
            bool succeeded = false;
            winrt::hstring statusMessage;
        };

        using SaveCallback = std::function<bool(
            liangwenpeak::balance::BalanceSettings,
            liangwenpeak::balance::ApiKeyDraftAction,
            winrt::hstring const&)>;
        using ResetCallback = std::function<bool()>;
        using TestNotificationCallback = std::function<winrt::hstring()>;
        using CleanupCallback = std::function<std::optional<CleanupResetState>()>;
        using ClosedCallback = std::function<void()>;

        ApiSettingsWindow();

        void InitializeOwned(
            HWND owner,
            liangwenpeak::balance::SettingsDraft draft,
            bool fluentThemeEnabled,
            SaveCallback saveCallback,
            ResetCallback resetCallback,
            TestNotificationCallback testNotificationCallback,
            CleanupCallback cleanupCallback,
            ClosedCallback closedCallback);
        void ShowOwned();
        void CloseFromOwner() noexcept;

        void OnApiKeyPasswordChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnClearApiKeyClick(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnCurrencyChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnRefreshIntervalChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnRateWindowChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnAlgorithmChanged(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
        void OnNotificationEnabledToggled(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnAdvanceReminderToggled(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnTestNotificationClick(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnSaveClick(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnCancelClick(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnResetStatisticsClick(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnCleanupClick(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        void ConfigureWindow();
        void ApplyTheme();
        void BringToFront();
        void PopulateControls();
        void PopulateRateWindows();
        void UpdateAlgorithmControl();
        void UpdateApiKeyClearPresentation();
        void UpdateNotificationControlState();
        void LoadWarningForCurrency();
        [[nodiscard]] bool CaptureCurrentWarning();
        [[nodiscard]] bool CaptureNotificationSettings();
        [[nodiscard]] std::string SelectedCurrency();
        [[nodiscard]] std::chrono::minutes SelectedRefreshInterval();
        [[nodiscard]] std::chrono::seconds SelectedRateWindow();
        winrt::fire_and_forget ConfirmResetStatisticsAsync();
        winrt::fire_and_forget ConfirmCleanupAsync();
        void OnWindowClosed(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::WindowEventArgs const& args);

        std::optional<liangwenpeak::balance::SettingsDraft> m_draft;
        SaveCallback m_saveCallback;
        ResetCallback m_resetCallback;
        TestNotificationCallback m_testNotificationCallback;
        CleanupCallback m_cleanupCallback;
        ClosedCallback m_closedCallback;
        std::vector<std::chrono::seconds> m_availableRateWindows;
        std::string m_editingCurrency;
        HWND m_owner{};
        HWND m_windowHandle{};
        Microsoft::UI::Windowing::AppWindow m_appWindow{ nullptr };
        winrt::event_token m_closedToken{};
        bool m_suppressEvents = false;
        bool m_fluentThemeEnabled = false;
        bool m_ownerDisabled = false;
        bool m_closingFromOwner = false;
        bool m_closed = false;
    };
}

namespace winrt::LiangWenPeak::factory_implementation
{
    struct ApiSettingsWindow : ApiSettingsWindowT<ApiSettingsWindow, implementation::ApiSettingsWindow>
    {
    };
}
