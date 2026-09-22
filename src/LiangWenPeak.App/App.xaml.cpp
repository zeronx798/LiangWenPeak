#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"
#include "Services/DeploymentPathService.h"
#include "Services/NotificationService.h"
#include "Balance/DeploymentPaths.h"

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
        const liangwenpeak::services::DeploymentPathService deploymentPaths{ m_stateProfile };
        if (!m_instanceCoordinator.TryAcquire(deploymentPaths.CanonicalDataRoot()))
        {
            const bool redirected = m_instanceCoordinator.RedirectToPrimary();
            ::ExitProcess(redirected ? 0U : 1U);
        }

        m_mainWindow = winrt::make_self<MainWindow>(m_stateProfile);
        m_window = *m_mainWindow;
        m_window.Activate();
        m_instanceCoordinator.PublishWindow(m_mainWindow->WindowHandle());
    }
}
