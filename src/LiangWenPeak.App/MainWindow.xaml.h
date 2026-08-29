#pragma once

#include "ApiSettingsWindow.xaml.h"
#include "AppTheme/ThemeManager.h"
#include "MainWindow.g.h"
#include "Balance/BalanceModels.h"
#include "Notifications/NotificationScheduler.h"
#include "Services/CleanupService.h"
#include "Services/CredentialService.h"
#include "Services/DeploymentPathService.h"
#include "Services/HistoryIdentityService.h"
#include "Services/NotificationService.h"
#include "Services/SettingsService.h"
#include "Services/StateProfile.h"
#include "ViewModels/MainViewModel.h"

#include <chrono>
#include <memory>
#include <optional>

namespace winrt::LiangWenPeak::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();
        explicit MainWindow(liangwenpeak::services::StateProfile profile);

        void OnRootLoaded(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnClockTick(Windows::Foundation::IInspectable const& sender, Windows::Foundation::IInspectable const& args);
        void OnBalanceTimerTick(Microsoft::UI::Dispatching::DispatcherQueueTimer const& sender, Windows::Foundation::IInspectable const& args);
        void OnNotificationTimerTick(Microsoft::UI::Dispatching::DispatcherQueueTimer const& sender, Windows::Foundation::IInspectable const& args);
        void OnSystemSuspendStatusChanged(
            Windows::Foundation::IInspectable const& sender,
            Windows::Foundation::IInspectable const& args);
        void OnAlwaysOnTopClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnFluentThemeClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRefreshBalanceClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnForecastToggleClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnNotificationToggleClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnSettingsClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnAboutClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnExitClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnCloseClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        void ConfigureWindow();
        void ApplyTheme();
        void ApplyWindowPresentation();
        void ArmFirstFrameReveal();
        void RevealFirstFrame() noexcept;
        void CancelFirstFrameReveal() noexcept;
        void ConfigureTimers();
        void ConfigurePowerNotifications() noexcept;
        void ScheduleNextBalanceRefresh(std::chrono::system_clock::time_point now);
        void ArmBalanceRefreshTimer(
            std::chrono::sys_seconds target,
            std::chrono::system_clock::time_point now);
        void ReconcileBalanceRefreshSchedule(std::chrono::system_clock::time_point now);
        void ScheduleNextNotification(std::chrono::system_clock::time_point now);
        void ArmNotificationTimer(
            std::chrono::sys_seconds target,
            std::chrono::system_clock::time_point now);
        void ReconcileNotificationSchedule(std::chrono::system_clock::time_point now);
        void DeliverDueNotifications(std::chrono::sys_seconds now);
        void ResizeForCurrentState();
        void ApplyState();
        void StopTimers() noexcept;
        winrt::fire_and_forget RefreshBalanceAsync(
            liangwenpeak::balance::BalanceRefreshReason reason,
            std::optional<std::chrono::sys_seconds> scheduledTimestamp = std::nullopt);
        void ShowSettingsWindow();
        winrt::fire_and_forget ShowAboutDialogAsync();
        void OnFirstFrameRendered(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Media::RenderedEventArgs const& args);
        void OnWindowClosing(Microsoft::UI::Windowing::AppWindow const& sender, Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args);

        liangwenpeak::services::StateProfile m_stateProfile;
        liangwenpeak::services::DeploymentPathService m_deploymentPaths;
        std::shared_ptr<liangwenpeak::services::CredentialService> m_credentialService;
        std::shared_ptr<liangwenpeak::services::SettingsService> m_settingsService;
        std::shared_ptr<liangwenpeak::services::HistoryIdentityService> m_historyIdentityService;
        std::shared_ptr<liangwenpeak::viewmodels::MainViewModel> m_viewModel;
        liangwenpeak::services::NotificationService m_notificationService;
        liangwenpeak::services::CleanupService m_cleanupService;
        liangwenpeak::notifications::NotificationScheduler m_notificationScheduler;
        liangwenpeak::notifications::NotificationDeliveryState m_notificationDeliveryState;
        liangwenpeak::apptheme::ThemeManager m_themeManager;
        winrt::com_ptr<ApiSettingsWindow> m_settingsWindow;
        HWND m_windowHandle{};
        Microsoft::UI::Windowing::AppWindow m_appWindow{ nullptr };
        Microsoft::UI::Windowing::OverlappedPresenter m_presenter{ nullptr };
        Microsoft::UI::Xaml::DispatcherTimer m_clockTimer{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_balanceTimer{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_notificationTimer{ nullptr };
        std::optional<std::chrono::sys_seconds> m_nextBalanceRefresh;
        std::optional<std::chrono::sys_seconds> m_nextNotificationWake;
        winrt::event_token m_clockTickToken{};
        winrt::event_token m_balanceTickToken{};
        winrt::event_token m_notificationTickToken{};
        winrt::event_token m_suspendStatusToken{};
        winrt::event_token m_closingToken{};
        winrt::event_token m_firstFrameRenderedToken{};
        int m_appliedClientHeightDips = 0;
        UINT m_appliedWindowDpi = 0;
        bool m_loaded = false;
        bool m_closing = false;
        bool m_firstFrameWatchActive = false;
        bool m_startupCloaked = false;
        bool m_suspendStatusHandlerAttached = false;
    };
}

namespace winrt::LiangWenPeak::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
