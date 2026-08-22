#include "pch.h"
#include "MainWindow.xaml.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <dwmapi.h>
#include <shellscalingapi.h>

#include "Services/CredentialService.h"
#include "Services/DeepSeekClient.h"
#include "Services/SettingsService.h"
#include "Time/BalanceRefreshSchedule.h"
#include "Time/BeijingTime.h"

#include <algorithm>
#include <string>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")

namespace winrt::LiangWenPeak::implementation
{
    using namespace Microsoft::UI::Windowing;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Media;
    using namespace Windows::Foundation;

    namespace
    {
        constexpr int kWindowWidth = 256;
        constexpr int kWindowHeight = 214;
        constexpr wchar_t kApplicationVersion[] = LIANGWENPEAK_VERSION;

        UINT GetInitialWindowDpi(HWND const windowHandle) noexcept
        {
            const auto monitor = ::MonitorFromWindow(windowHandle, MONITOR_DEFAULTTONEAREST);
            UINT dpiX{};
            UINT dpiY{};
            if (monitor != nullptr
                && SUCCEEDED(::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))
                && dpiX != 0)
            {
                return dpiX;
            }

            const auto systemDpi = ::GetDpiForSystem();
            return systemDpi == 0 ? USER_DEFAULT_SCREEN_DPI : systemDpi;
        }

        winrt::hstring FormatRefreshInterval(std::chrono::minutes const interval)
        {
            return winrt::hstring{ std::to_wstring(interval.count()) + L" \u5206\u949f" };
        }

        int32_t RefreshIntervalIndex(std::chrono::minutes const interval) noexcept
        {
            const auto found = std::find(
                liangwenpeak::time::SupportedBalanceRefreshIntervals.begin(),
                liangwenpeak::time::SupportedBalanceRefreshIntervals.end(),
                interval);
            if (found == liangwenpeak::time::SupportedBalanceRefreshIntervals.end())
            {
                return 0;
            }

            return static_cast<int32_t>(
                std::distance(liangwenpeak::time::SupportedBalanceRefreshIntervals.begin(), found));
        }

        std::chrono::minutes RefreshIntervalAt(int32_t const index) noexcept
        {
            if (index < 0
                || static_cast<size_t>(index) >= liangwenpeak::time::SupportedBalanceRefreshIntervals.size())
            {
                return liangwenpeak::time::DefaultBalanceRefreshInterval;
            }

            return liangwenpeak::time::SupportedBalanceRefreshIntervals[static_cast<size_t>(index)];
        }

        void ConfigureCompactDialogLayout(ContentDialog const& dialog)
        {
            const auto resources = dialog.Resources();
            resources.Insert(
                winrt::box_value(L"ContentDialogMinWidth"),
                winrt::box_value(0.0));
            resources.Insert(
                winrt::box_value(L"ContentDialogMinHeight"),
                winrt::box_value(0.0));
            resources.Insert(
                winrt::box_value(L"ContentDialogPadding"),
                winrt::box_value(Thickness{ 16.0, 12.0, 16.0, 12.0 }));
        }
    }

    MainWindow::MainWindow()
    {
        InitializeComponent();

        m_viewModel = std::make_shared<liangwenpeak::viewmodels::MainViewModel>(
            std::make_shared<liangwenpeak::services::CredentialService>(),
            std::make_shared<liangwenpeak::services::DeepSeekClient>());
        m_settingsService = std::make_shared<liangwenpeak::services::SettingsService>();
        m_balanceRefreshInterval = m_settingsService->LoadBalanceRefreshInterval();

        ConfigureWindow();
        ApplyWindowPresentation();
        m_viewModel->Initialize();
        ApplyState();
        RootGrid().UpdateLayout();
        ArmFirstFrameReveal();
        ConfigureTimers();
    }

    void MainWindow::OnRootLoaded(IInspectable const&, RoutedEventArgs const&)
    {
        if (m_loaded)
        {
            return;
        }

        m_loaded = true;
        m_clockTimer.Start();
        ScheduleNextBalanceRefresh(std::chrono::system_clock::now());
        RefreshBalanceAsync();
    }

    void MainWindow::OnClockTick(IInspectable const&, IInspectable const&)
    {
        const auto now = std::chrono::system_clock::now();
        m_viewModel->UpdateClock(now);
        ApplyState();
        ReconcileBalanceRefreshSchedule(now);
    }

    void MainWindow::OnBalanceTimerTick(
        Microsoft::UI::Dispatching::DispatcherQueueTimer const&,
        IInspectable const&)
    {
        const auto now = std::chrono::system_clock::now();
        if (!m_nextBalanceRefresh)
        {
            ReconcileBalanceRefreshSchedule(now);
            return;
        }

        const auto currentSecond = std::chrono::floor<std::chrono::seconds>(now);
        if (currentSecond < *m_nextBalanceRefresh)
        {
            ArmBalanceRefreshTimer(*m_nextBalanceRefresh, now);
            return;
        }
        if (currentSecond > *m_nextBalanceRefresh)
        {
            ScheduleNextBalanceRefresh(now);
            return;
        }

        ScheduleNextBalanceRefresh(now);
        RefreshBalanceAsync();
    }

    void MainWindow::OnAlwaysOnTopClick(IInspectable const& sender, RoutedEventArgs const&)
    {
        const auto item = sender.as<ToggleMenuFlyoutItem>();
        m_presenter.IsAlwaysOnTop(item.IsChecked());
    }

    void MainWindow::OnRefreshBalanceClick(IInspectable const&, RoutedEventArgs const&)
    {
        RefreshBalanceAsync();
    }

    void MainWindow::OnSetApiKeyClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowApiKeyDialogAsync();
    }

    void MainWindow::OnAboutClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowAboutDialogAsync();
    }

    void MainWindow::OnExitClick(IInspectable const&, RoutedEventArgs const&)
    {
        Close();
    }

    void MainWindow::OnCloseClick(IInspectable const&, RoutedEventArgs const&)
    {
        Close();
    }

    void MainWindow::ConfigureWindow()
    {
        m_appWindow = AppWindow();
        m_appWindow.Title(L"\u6881\u6587\u5cf0\u65f6\u949f");
        m_presenter = m_appWindow.Presenter().as<OverlappedPresenter>();
        m_presenter.IsResizable(false);
        m_presenter.IsMaximizable(false);
        m_presenter.IsMinimizable(false);
        m_presenter.SetBorderAndTitleBar(true, false);

        ExtendsContentIntoTitleBar(true);
        SetTitleBar(DragRegion());

        const auto windowNative = this->try_as<::IWindowNative>();
        winrt::check_hresult(windowNative->get_WindowHandle(&m_windowHandle));

        const BOOL darkMode = TRUE;
        ::DwmSetWindowAttribute(m_windowHandle, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkMode, sizeof(darkMode));
        const DWM_WINDOW_CORNER_PREFERENCE corners = DWMWCP_ROUND;
        ::DwmSetWindowAttribute(m_windowHandle, DWMWA_WINDOW_CORNER_PREFERENCE, &corners, sizeof(corners));

        m_closingToken = m_appWindow.Closing({ this, &MainWindow::OnWindowClosing });
    }

    void MainWindow::ApplyWindowPresentation()
    {
        const auto dpi = GetInitialWindowDpi(m_windowHandle);
        m_appWindow.Resize({
            ::MulDiv(kWindowWidth, static_cast<int>(dpi), 96),
            ::MulDiv(kWindowHeight, static_cast<int>(dpi), 96) });
        m_presenter.IsAlwaysOnTop(true);
        AlwaysOnTopMenuItem().IsChecked(true);
    }

    void MainWindow::ArmFirstFrameReveal()
    {
        const BOOL cloak = TRUE;
        if (FAILED(::DwmSetWindowAttribute(m_windowHandle, DWMWA_CLOAK, &cloak, sizeof(cloak))))
        {
            return;
        }

        m_startupCloaked = true;
        m_firstFrameRenderedToken = CompositionTarget::Rendered({ this, &MainWindow::OnFirstFrameRendered });
        m_firstFrameWatchActive = true;
    }

    void MainWindow::RevealFirstFrame() noexcept
    {
        if (!m_startupCloaked || m_closing)
        {
            return;
        }

        ::DwmFlush();
        const BOOL cloak = FALSE;
        ::DwmSetWindowAttribute(m_windowHandle, DWMWA_CLOAK, &cloak, sizeof(cloak));
        m_startupCloaked = false;
    }

    void MainWindow::CancelFirstFrameReveal() noexcept
    {
        if (m_firstFrameWatchActive)
        {
            CompositionTarget::Rendered(m_firstFrameRenderedToken);
            m_firstFrameWatchActive = false;
        }
    }

    void MainWindow::ConfigureTimers()
    {
        m_clockTimer = DispatcherTimer();
        m_clockTimer.Interval(std::chrono::seconds{ 1 });
        m_clockTickToken = m_clockTimer.Tick({ this, &MainWindow::OnClockTick });

        m_balanceTimer = DispatcherQueue().CreateTimer();
        m_balanceTimer.IsRepeating(false);
        m_balanceTickToken = m_balanceTimer.Tick({ this, &MainWindow::OnBalanceTimerTick });
    }

    void MainWindow::ScheduleNextBalanceRefresh(std::chrono::system_clock::time_point const now)
    {
        m_balanceTimer.Stop();
        m_nextBalanceRefresh.reset();
        if (!m_loaded || m_closing || !m_viewModel->State().hasApiKey)
        {
            return;
        }

        const auto beijingNow = liangwenpeak::time::BeijingTime::FromUtc(now);
        const auto target = liangwenpeak::time::GetNextAlignedRefreshTime(
            beijingNow,
            m_balanceRefreshInterval);
        m_nextBalanceRefresh = target;
        ArmBalanceRefreshTimer(target, now);
    }

    void MainWindow::ArmBalanceRefreshTimer(
        std::chrono::sys_seconds const target,
        std::chrono::system_clock::time_point const now)
    {
        auto delay = std::chrono::ceil<std::chrono::milliseconds>(target - now);
        if (delay <= std::chrono::milliseconds::zero())
        {
            delay = std::chrono::milliseconds{ 1 };
        }

        m_balanceTimer.Stop();
        m_balanceTimer.Interval(delay);
        m_balanceTimer.Start();
    }

    void MainWindow::ReconcileBalanceRefreshSchedule(std::chrono::system_clock::time_point const now)
    {
        if (!m_loaded || m_closing)
        {
            return;
        }
        if (!m_viewModel->State().hasApiKey)
        {
            if (m_nextBalanceRefresh)
            {
                m_balanceTimer.Stop();
                m_nextBalanceRefresh.reset();
            }
            return;
        }

        const auto currentSecond = std::chrono::floor<std::chrono::seconds>(now);
        if (m_nextBalanceRefresh && currentSecond == *m_nextBalanceRefresh)
        {
            return;
        }

        const auto expected = liangwenpeak::time::GetNextAlignedRefreshTime(
            liangwenpeak::time::BeijingTime::FromUtc(now),
            m_balanceRefreshInterval);
        if (!m_nextBalanceRefresh || *m_nextBalanceRefresh != expected)
        {
            ScheduleNextBalanceRefresh(now);
        }
    }

    void MainWindow::ApplyState()
    {
        const auto& state = m_viewModel->State();
        CurrentTimeText().Text(state.currentTime);
        StatusText().Text(state.statusText);
        CountdownText().Text(state.countdownText);
        BalanceText().Text(state.balanceText);
        NextPeriodText().Text(state.nextPeriodText);
        UpdateStatusText().Text(state.updateStatusText);

        const auto brushKey = state.pricingPeriod == liangwenpeak::pricing::PricingPeriod::Peak
            ? L"PeakBrush"
            : L"ValleyBrush";
        const auto brush = Application::Current().Resources().Lookup(winrt::box_value(brushKey)).as<Brush>();
        StatusText().Foreground(brush);

        RefreshBalanceMenuItem().IsEnabled(state.hasApiKey && !state.isRefreshing);
    }

    void MainWindow::StopTimers() noexcept
    {
        if (m_clockTimer)
        {
            m_clockTimer.Stop();
            m_clockTimer.Tick(m_clockTickToken);
        }
        if (m_balanceTimer)
        {
            m_balanceTimer.Stop();
            m_balanceTimer.Tick(m_balanceTickToken);
            m_nextBalanceRefresh.reset();
        }
    }

    void MainWindow::OnFirstFrameRendered(
        IInspectable const&,
        Microsoft::UI::Xaml::Media::RenderedEventArgs const&)
    {
        CancelFirstFrameReveal();

        auto weak = get_weak();
        const auto queued = DispatcherQueue().TryEnqueue(
            Microsoft::UI::Dispatching::DispatcherQueuePriority::Low,
            [weak]() noexcept
            {
                if (auto strong = weak.get())
                {
                    strong->RevealFirstFrame();
                }
            });
        if (!queued)
        {
            RevealFirstFrame();
        }
    }

    winrt::fire_and_forget MainWindow::RefreshBalanceAsync()
    {
        auto weak = get_weak();
        auto viewModel = m_viewModel;
        try
        {
            auto refresh = viewModel->RefreshBalanceAsync();
            if (auto strong = weak.get(); strong && !strong->m_closing)
            {
                strong->ApplyState();
            }

            co_await refresh;
            if (auto strong = weak.get(); strong && !strong->m_closing)
            {
                strong->ApplyState();
            }
        }
        catch (...)
        {
            // The view model already converts expected failures into quiet UI state.
        }
    }

    winrt::fire_and_forget MainWindow::ShowApiKeyDialogAsync()
    {
        auto weak = get_weak();
        try
        {
            PasswordBox apiKeyBox;
            apiKeyBox.HorizontalAlignment(HorizontalAlignment::Stretch);
            apiKeyBox.PlaceholderText(m_viewModel->State().hasApiKey
                ? L"\u5df2\u914d\u7f6e\uff0c\u7559\u7a7a\u5219\u4fdd\u6301\u4e0d\u53d8"
                : L"sk-...");
            apiKeyBox.PasswordRevealMode(PasswordRevealMode::Peek);

            TextBlock apiKeyLabel;
            apiKeyLabel.Text(L"API Key");
            apiKeyLabel.FontSize(12);

            TextBlock refreshIntervalLabel;
            refreshIntervalLabel.Text(L"\u4f59\u989d\u81ea\u52a8\u5237\u65b0");
            refreshIntervalLabel.FontSize(12);

            ComboBox refreshIntervalBox;
            refreshIntervalBox.HorizontalAlignment(HorizontalAlignment::Stretch);
            for (auto const interval : liangwenpeak::time::SupportedBalanceRefreshIntervals)
            {
                refreshIntervalBox.Items().Append(winrt::box_value(FormatRefreshInterval(interval)));
            }
            refreshIntervalBox.SelectedIndex(RefreshIntervalIndex(m_balanceRefreshInterval));

            Grid content;
            content.HorizontalAlignment(HorizontalAlignment::Stretch);
            content.RowSpacing(6.0);
            for (int32_t index = 0; index < 4; ++index)
            {
                RowDefinition row;
                row.Height(GridLengthHelper::Auto());
                content.RowDefinitions().Append(row);
            }

            Grid::SetRow(apiKeyBox, 1);
            Grid::SetRow(refreshIntervalLabel, 2);
            Grid::SetRow(refreshIntervalBox, 3);
            content.Children().Append(apiKeyLabel);
            content.Children().Append(apiKeyBox);
            content.Children().Append(refreshIntervalLabel);
            content.Children().Append(refreshIntervalBox);

            ScrollViewer contentScroller;
            contentScroller.HorizontalContentAlignment(HorizontalAlignment::Stretch);
            contentScroller.HorizontalScrollBarVisibility(ScrollBarVisibility::Disabled);
            contentScroller.VerticalScrollBarVisibility(ScrollBarVisibility::Auto);
            contentScroller.Content(content);

            ContentDialog dialog;
            ConfigureCompactDialogLayout(dialog);
            dialog.XamlRoot(RootGrid().XamlRoot());
            dialog.Content(contentScroller);
            dialog.PrimaryButtonText(L"\u4fdd\u5b58");
            dialog.SecondaryButtonText(L"\u6e05\u9664");
            dialog.CloseButtonText(L"\u53d6\u6d88");
            dialog.DefaultButton(ContentDialogButton::Primary);

            const auto result = co_await dialog.ShowAsync();
            if (auto strong = weak.get(); strong && !strong->m_closing)
            {
                if (result == ContentDialogResult::Primary)
                {
                    const auto refreshInterval = RefreshIntervalAt(refreshIntervalBox.SelectedIndex());
                    if (!strong->m_settingsService->SaveBalanceRefreshInterval(refreshInterval))
                    {
                        strong->UpdateStatusText().Text(L"\u8bbe\u7f6e\u4fdd\u5b58\u5931\u8d25");
                        co_return;
                    }

                    strong->m_balanceRefreshInterval = refreshInterval;
                    const auto apiKey = apiKeyBox.Password();
                    const auto apiKeyChanged = !apiKey.empty();
                    if (apiKeyChanged)
                    {
                        strong->m_viewModel->SaveApiKey(apiKey);
                    }
                    strong->ApplyState();
                    strong->ScheduleNextBalanceRefresh(std::chrono::system_clock::now());
                    if (apiKeyChanged)
                    {
                        strong->RefreshBalanceAsync();
                    }
                }
                else if (result == ContentDialogResult::Secondary)
                {
                    strong->m_viewModel->SaveApiKey({});
                    strong->ApplyState();
                    strong->ScheduleNextBalanceRefresh(std::chrono::system_clock::now());
                }
            }
        }
        catch (...)
        {
            if (auto strong = weak.get(); strong && !strong->m_closing)
            {
                strong->UpdateStatusText().Text(L"\u8bbe\u7f6e\u64cd\u4f5c\u5931\u8d25");
            }
        }
    }

    winrt::fire_and_forget MainWindow::ShowAboutDialogAsync()
    {
        try
        {
            const auto resources = Application::Current().Resources();
            const auto primaryBrush = resources.Lookup(winrt::box_value(L"PrimaryTextBrush")).as<Brush>();
            const auto secondaryBrush = resources.Lookup(winrt::box_value(L"SecondaryTextBrush")).as<Brush>();
            const auto tertiaryBrush = resources.Lookup(winrt::box_value(L"TertiaryTextBrush")).as<Brush>();

            TextBlock applicationName;
            applicationName.Text(L"LiangWenPeak");
            applicationName.FontSize(17);
            applicationName.FontWeight(Windows::UI::Text::FontWeight{ 600 });
            applicationName.Foreground(primaryBrush);
            applicationName.TextWrapping(TextWrapping::Wrap);

            TextBlock chineseName;
            chineseName.Text(L"\u6881\u6587\u5cf0\u65f6\u949f");
            chineseName.FontSize(14);
            chineseName.Foreground(secondaryBrush);
            chineseName.TextWrapping(TextWrapping::Wrap);

            TextBlock description;
            description.Text(L"\u5317\u4eac\u65f6\u95f4\u5cf0\u8c37\u72b6\u6001\u4eea\u8868");
            description.FontSize(13);
            description.Foreground(secondaryBrush);
            description.TextWrapping(TextWrapping::Wrap);

            TextBlock version;
            version.Text(winrt::hstring{ std::wstring{ L"\u7248\u672c " } + kApplicationVersion });
            version.FontSize(12);
            version.Foreground(tertiaryBrush);
            version.TextWrapping(TextWrapping::Wrap);

            Grid content;
            content.HorizontalAlignment(HorizontalAlignment::Stretch);
            content.RowSpacing(4.0);
            for (int32_t index = 0; index < 4; ++index)
            {
                RowDefinition row;
                row.Height(GridLengthHelper::Auto());
                content.RowDefinitions().Append(row);
            }

            Grid::SetRow(chineseName, 1);
            Grid::SetRow(description, 2);
            Grid::SetRow(version, 3);
            content.Children().Append(applicationName);
            content.Children().Append(chineseName);
            content.Children().Append(description);
            content.Children().Append(version);

            ContentDialog dialog;
            ConfigureCompactDialogLayout(dialog);
            dialog.XamlRoot(RootGrid().XamlRoot());
            dialog.Title(winrt::box_value(L"\u5173\u4e8e"));
            dialog.Content(content);
            dialog.CloseButtonText(L"\u5173\u95ed");
            co_await dialog.ShowAsync();
        }
        catch (...)
        {
            // Closing the owner while the dialog is active requires no user-facing error.
        }
    }

    void MainWindow::OnWindowClosing(
        Microsoft::UI::Windowing::AppWindow const&,
        Microsoft::UI::Windowing::AppWindowClosingEventArgs const&)
    {
        m_closing = true;
        CancelFirstFrameReveal();
        m_startupCloaked = false;
        StopTimers();
        if (m_appWindow)
        {
            m_appWindow.Closing(m_closingToken);
        }
    }
}
