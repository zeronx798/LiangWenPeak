#pragma once

#include "MainWindow.g.h"
#include "Services/SettingsService.h"
#include "ViewModels/MainViewModel.h"

#include <chrono>
#include <memory>
#include <optional>

namespace winrt::LiangWenPeak::implementation
{
    struct MainWindow : MainWindowT<MainWindow>
    {
        MainWindow();

        void OnRootLoaded(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnClockTick(Windows::Foundation::IInspectable const& sender, Windows::Foundation::IInspectable const& args);
        void OnBalanceTimerTick(Microsoft::UI::Dispatching::DispatcherQueueTimer const& sender, Windows::Foundation::IInspectable const& args);
        void OnAlwaysOnTopClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnRefreshBalanceClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnSetApiKeyClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnAboutClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnExitClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);
        void OnCloseClick(Windows::Foundation::IInspectable const& sender, Microsoft::UI::Xaml::RoutedEventArgs const& args);

    private:
        void ConfigureWindow();
        void ApplyWindowPresentation();
        void ArmFirstFrameReveal();
        void RevealFirstFrame() noexcept;
        void CancelFirstFrameReveal() noexcept;
        void ConfigureTimers();
        void ScheduleNextBalanceRefresh(std::chrono::system_clock::time_point now);
        void ArmBalanceRefreshTimer(
            std::chrono::sys_seconds target,
            std::chrono::system_clock::time_point now);
        void ReconcileBalanceRefreshSchedule(std::chrono::system_clock::time_point now);
        void ApplyState();
        void StopTimers() noexcept;
        winrt::fire_and_forget RefreshBalanceAsync();
        winrt::fire_and_forget ShowApiKeyDialogAsync();
        winrt::fire_and_forget ShowAboutDialogAsync();
        void OnFirstFrameRendered(
            Windows::Foundation::IInspectable const& sender,
            Microsoft::UI::Xaml::Media::RenderedEventArgs const& args);
        void OnWindowClosing(Microsoft::UI::Windowing::AppWindow const& sender, Microsoft::UI::Windowing::AppWindowClosingEventArgs const& args);

        std::shared_ptr<liangwenpeak::viewmodels::MainViewModel> m_viewModel;
        std::shared_ptr<liangwenpeak::services::SettingsService> m_settingsService;
        HWND m_windowHandle{};
        Microsoft::UI::Windowing::AppWindow m_appWindow{ nullptr };
        Microsoft::UI::Windowing::OverlappedPresenter m_presenter{ nullptr };
        Microsoft::UI::Xaml::DispatcherTimer m_clockTimer{ nullptr };
        Microsoft::UI::Dispatching::DispatcherQueueTimer m_balanceTimer{ nullptr };
        std::optional<std::chrono::sys_seconds> m_nextBalanceRefresh;
        std::chrono::minutes m_balanceRefreshInterval{ 1 };
        winrt::event_token m_clockTickToken{};
        winrt::event_token m_balanceTickToken{};
        winrt::event_token m_closingToken{};
        winrt::event_token m_firstFrameRenderedToken{};
        bool m_loaded = false;
        bool m_closing = false;
        bool m_firstFrameWatchActive = false;
        bool m_startupCloaked = false;
    };
}

namespace winrt::LiangWenPeak::factory_implementation
{
    struct MainWindow : MainWindowT<MainWindow, implementation::MainWindow>
    {
    };
}
