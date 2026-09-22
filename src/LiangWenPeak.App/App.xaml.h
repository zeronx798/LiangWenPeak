#pragma once

#include "App.xaml.g.h"
#include "Services/InstanceCoordinator.h"
#include "Services/StateProfile.h"

namespace winrt::LiangWenPeak::implementation
{
    struct MainWindow;

    struct App : AppT<App>
    {
        App();
        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const& args);

    private:
        liangwenpeak::services::StateProfile m_stateProfile;
        liangwenpeak::services::InstanceCoordinator m_instanceCoordinator;
        Microsoft::UI::Xaml::Window m_window{ nullptr };
        winrt::com_ptr<MainWindow> m_mainWindow;
    };
}
