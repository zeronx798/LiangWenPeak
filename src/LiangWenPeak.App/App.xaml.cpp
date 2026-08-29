#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "Services/NotificationService.h"

namespace winrt::LiangWenPeak::implementation
{
    App::App()
        : m_stateProfile(liangwenpeak::services::StateProfile::FromEnvironment())
    {
        winrt::hstring ignoredFailure;
        static_cast<void>(liangwenpeak::services::NotificationService::SetCurrentProcessIdentity(
            m_stateProfile,
            ignoredFailure));
        RequestedTheme(Microsoft::UI::Xaml::ApplicationTheme::Dark);
        InitializeComponent();
    }

    void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
    {
        m_window = winrt::make<MainWindow>(m_stateProfile);
        m_window.Activate();
    }
}
