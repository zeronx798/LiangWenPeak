#include "pch.h"
#include "App.xaml.h"
#include "MainWindow.xaml.h"

namespace winrt::LiangWenPeak::implementation
{
    App::App()
    {
        RequestedTheme(Microsoft::UI::Xaml::ApplicationTheme::Dark);
        InitializeComponent();
    }

    void App::OnLaunched(Microsoft::UI::Xaml::LaunchActivatedEventArgs const&)
    {
        m_window = winrt::make<MainWindow>();
        m_window.Activate();
    }
}
