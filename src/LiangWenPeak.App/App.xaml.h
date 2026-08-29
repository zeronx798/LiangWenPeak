#pragma once

#include "App.xaml.g.h"
#include "Services/StateProfile.h"

namespace winrt::LiangWenPeak::implementation
{
    struct App : AppT<App>
    {
        App();
        void OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const& args);

    private:
        liangwenpeak::services::StateProfile m_stateProfile;
        Microsoft::UI::Xaml::Window m_window{ nullptr };
    };
}
