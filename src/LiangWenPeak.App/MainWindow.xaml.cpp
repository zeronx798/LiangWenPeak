#include "pch.h"
#include "MainWindow.xaml.h"
#include "MainWindowLayout.h"

#if __has_include("MainWindow.g.cpp")
#include "MainWindow.g.cpp"
#endif

#include <dwmapi.h>
#include <shellscalingapi.h>
#include <winrt/Microsoft.UI.Xaml.Documents.h>

#include "Services/CredentialService.h"
#include "Services/DeepSeekClient.h"
#include "Services/DeploymentPathService.h"
#include "Services/HistoryIdentityService.h"
#include "Services/SettingsService.h"
#include "Balance/BalanceHistoryStore.h"
#include "Time/BalanceRefreshSchedule.h"
#include "Time/BeijingTime.h"

#include <string>

#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "shcore.lib")

namespace winrt::LiangWenPeak::implementation
{
    using namespace Microsoft::UI::Windowing;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Microsoft::UI::Xaml::Documents;
    using namespace Microsoft::UI::Xaml::Media;
    using namespace Windows::Foundation;

    namespace
    {
        constexpr wchar_t kApplicationVersion[] = LIANGWENPEAK_VERSION;

        liangwenpeak::ui::MainWindowLayoutState WindowLayoutState(
            liangwenpeak::viewmodels::MainViewState const& state) noexcept
        {
            return {
                state.apiFeatureEnabled,
                state.forecastEnabled,
                state.hasSuccessfulObservation };
        }

        int CurrentNonClientHeight(HWND const windowHandle) noexcept
        {
            RECT windowRectangle{};
            RECT clientRectangle{};
            if (::GetWindowRect(windowHandle, &windowRectangle) == FALSE
                || ::GetClientRect(windowHandle, &clientRectangle) == FALSE)
            {
                return 0;
            }

            const auto windowHeight = windowRectangle.bottom - windowRectangle.top;
            const auto clientHeight = clientRectangle.bottom - clientRectangle.top;
            return windowHeight > clientHeight ? windowHeight - clientHeight : 0;
        }

        int WindowHeightInPixels(
            HWND const windowHandle,
            liangwenpeak::viewmodels::MainViewState const& state,
            UINT const targetDpi) noexcept
        {
            const auto clientHeight = ::MulDiv(
                liangwenpeak::ui::MainWindowClientHeightDips(WindowLayoutState(state)),
                static_cast<int>(targetDpi),
                96);

            auto nonClientHeight = CurrentNonClientHeight(windowHandle);
            const auto currentDpi = ::GetDpiForWindow(windowHandle);
            if (currentDpi != 0 && currentDpi != targetDpi)
            {
                nonClientHeight = ::MulDiv(
                    nonClientHeight,
                    static_cast<int>(targetDpi),
                    static_cast<int>(currentDpi));
            }
            return clientHeight + nonClientHeight;
        }

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

        auto credentialService = std::make_shared<liangwenpeak::services::CredentialService>();
        auto settingsService = std::make_shared<liangwenpeak::services::SettingsService>();
        auto identityService = std::make_shared<liangwenpeak::services::HistoryIdentityService>();
        const liangwenpeak::services::DeploymentPathService deploymentPaths;
        auto historyStore = std::make_shared<liangwenpeak::balance::BalanceHistoryStore>(
            deploymentPaths.DataRoot());
        m_viewModel = std::make_shared<liangwenpeak::viewmodels::MainViewModel>(
            std::move(credentialService),
            std::make_shared<liangwenpeak::services::DeepSeekClient>(),
            std::move(settingsService),
            std::move(identityService),
            std::move(historyStore));

        ConfigureWindow();
        ApplyTheme();
        m_viewModel->Initialize();
        ApplyWindowPresentation();
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
        RefreshBalanceAsync(liangwenpeak::balance::BalanceRefreshReason::StartupObservation);
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

        const auto target = *m_nextBalanceRefresh;
        const auto currentSecond = std::chrono::floor<std::chrono::seconds>(now);
        if (currentSecond < target)
        {
            ArmBalanceRefreshTimer(target, now);
            return;
        }

        const bool targetIsCurrent = liangwenpeak::time::IsCurrentRefreshTarget(
            currentSecond,
            target,
            m_viewModel->Settings().refreshInterval);
        ScheduleNextBalanceRefresh(now);
        if (targetIsCurrent)
        {
            RefreshBalanceAsync(
                liangwenpeak::balance::BalanceRefreshReason::ScheduledSample,
                target);
        }
    }

    void MainWindow::OnAlwaysOnTopClick(IInspectable const& sender, RoutedEventArgs const&)
    {
        const auto item = sender.as<ToggleMenuFlyoutItem>();
        m_presenter.IsAlwaysOnTop(item.IsChecked());
    }

    void MainWindow::OnFluentThemeClick(IInspectable const& sender, RoutedEventArgs const&)
    {
        const auto item = sender.as<ToggleMenuFlyoutItem>();
        if (!m_themeManager.IsFluentThemeAvailable())
        {
            item.Visibility(Visibility::Collapsed);
            return;
        }

        if (!m_themeManager.SetFluentThemeEnabled(item.IsChecked()))
        {
            item.IsChecked(m_themeManager.IsFluentThemeEnabled());
            return;
        }
        ApplyTheme();
    }

    void MainWindow::OnRefreshBalanceClick(IInspectable const&, RoutedEventArgs const&)
    {
        RefreshBalanceAsync(liangwenpeak::balance::BalanceRefreshReason::ManualObservation);
    }

    void MainWindow::OnForecastToggleClick(IInspectable const& sender, RoutedEventArgs const&)
    {
        const auto item = sender.as<ToggleMenuFlyoutItem>();
        static_cast<void>(m_viewModel->SetForecastEnabled(item.IsChecked()));
        ApplyState();
    }

    void MainWindow::OnSetApiKeyClick(IInspectable const&, RoutedEventArgs const&)
    {
        ShowApiSettingsWindow();
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

        m_closingToken = m_appWindow.Closing({ this, &MainWindow::OnWindowClosing });
    }

    void MainWindow::ApplyTheme()
    {
        const bool available = m_themeManager.IsFluentThemeAvailable();
        const bool enabled = m_themeManager.IsFluentThemeEnabled();
        FluentThemeMenuItem().Visibility(available ? Visibility::Visible : Visibility::Collapsed);
        FluentThemeMenuItem().IsChecked(enabled);

        m_themeManager.ApplyToWindow(RootGrid(), m_windowHandle);
        liangwenpeak::apptheme::ThemeManager::ApplyControlPresentation(
            MoreButton(),
            enabled);
        liangwenpeak::apptheme::ThemeManager::ApplyControlPresentation(
            CloseButton(),
            enabled);
        MainMenuFlyout().MenuFlyoutPresenterStyle(
            liangwenpeak::apptheme::ThemeManager::CreateMenuFlyoutPresenterStyle(
                enabled));
    }

    void MainWindow::ApplyWindowPresentation()
    {
        const auto dpi = GetInitialWindowDpi(m_windowHandle);
        const auto clientHeightDips = liangwenpeak::ui::MainWindowClientHeightDips(
            WindowLayoutState(m_viewModel->State()));
        m_appWindow.Resize({
            ::MulDiv(liangwenpeak::ui::MainWindowWidthDips, static_cast<int>(dpi), 96),
            WindowHeightInPixels(m_windowHandle, m_viewModel->State(), dpi) });
        m_appliedClientHeightDips = clientHeightDips;
        m_appliedWindowDpi = dpi;
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
        const auto& state = m_viewModel->State();
        if (!m_loaded || m_closing || !state.apiFeatureEnabled || !state.hasApiKey)
        {
            return;
        }

        const auto beijingNow = liangwenpeak::time::BeijingTime::FromUtc(now);
        const auto target = liangwenpeak::time::GetNextAlignedRefreshTime(
            beijingNow,
            m_viewModel->Settings().refreshInterval);
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
        const auto& state = m_viewModel->State();
        if (!state.apiFeatureEnabled || !state.hasApiKey)
        {
            if (m_nextBalanceRefresh)
            {
                m_balanceTimer.Stop();
                m_nextBalanceRefresh.reset();
            }
            return;
        }

        const auto currentSecond = std::chrono::floor<std::chrono::seconds>(now);
        if (m_nextBalanceRefresh
            && liangwenpeak::time::IsCurrentRefreshTarget(
                currentSecond,
                *m_nextBalanceRefresh,
                m_viewModel->Settings().refreshInterval))
        {
            return;
        }

        const auto expected = liangwenpeak::time::GetNextAlignedRefreshTime(
            liangwenpeak::time::BeijingTime::FromUtc(now),
            m_viewModel->Settings().refreshInterval);
        if (!m_nextBalanceRefresh || *m_nextBalanceRefresh != expected)
        {
            ScheduleNextBalanceRefresh(now);
        }
    }

    void MainWindow::ResizeForCurrentState()
    {
        if (!m_appWindow)
        {
            return;
        }
        const auto dpi = ::GetDpiForWindow(m_windowHandle);
        const auto effectiveDpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
        const auto clientHeightDips = liangwenpeak::ui::MainWindowClientHeightDips(
            WindowLayoutState(m_viewModel->State()));
        if (m_appliedClientHeightDips == clientHeightDips
            && m_appliedWindowDpi == effectiveDpi)
        {
            return;
        }

        const auto currentSize = m_appWindow.Size();
        const auto targetHeight = WindowHeightInPixels(
            m_windowHandle,
            m_viewModel->State(),
            effectiveDpi);
        if (currentSize.Height != targetHeight)
        {
            m_appWindow.Resize({ currentSize.Width, targetHeight });
        }
        m_appliedClientHeightDips = clientHeightDips;
        m_appliedWindowDpi = effectiveDpi;
    }

    void MainWindow::ApplyState()
    {
        const auto& state = m_viewModel->State();
        CurrentTimeText().Text(state.currentTime);
        StatusText().Text(state.statusText);
        CountdownText().Text(state.countdownText);
        BalanceText().Text(state.balanceText);
        BurnRateText().Text(state.burnRateText);
        EtaText().Text(state.etaText);
        NextPeriodText().Text(state.nextPeriodText);
        UpdateStatusText().Text(state.updateStatusText);

        ApiSection().Visibility(state.apiFeatureEnabled ? Visibility::Visible : Visibility::Collapsed);
        ForecastSection().Visibility(
            state.apiFeatureEnabled && state.forecastEnabled
                ? Visibility::Visible
                : Visibility::Collapsed);
        UpdateStatusText().Visibility(
            state.apiFeatureEnabled && state.hasSuccessfulObservation
                ? Visibility::Visible
                : Visibility::Collapsed);

        const auto brushKey = state.pricingPeriod == liangwenpeak::pricing::PricingPeriod::Peak
            ? L"PeakBrush"
            : L"ValleyBrush";
        const auto brush = Application::Current().Resources().Lookup(winrt::box_value(brushKey)).as<Brush>();
        StatusText().Foreground(brush);

        RefreshBalanceMenuItem().IsEnabled(
            state.apiFeatureEnabled && state.hasApiKey && !state.isRefreshing);
        ForecastMenuItem().IsChecked(state.forecastEnabled);
        ResizeForCurrentState();
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

    winrt::fire_and_forget MainWindow::RefreshBalanceAsync(
        liangwenpeak::balance::BalanceRefreshReason const reason,
        std::optional<std::chrono::sys_seconds> const scheduledTimestamp)
    {
        auto weak = get_weak();
        auto viewModel = m_viewModel;
        try
        {
            auto refresh = viewModel->RefreshBalanceAsync(reason, scheduledTimestamp);
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

    void MainWindow::ShowApiSettingsWindow()
    {
        if (m_apiSettingsWindow)
        {
            m_apiSettingsWindow->ShowOwned();
            return;
        }

        auto weak = get_weak();
        auto window = winrt::make_self<ApiSettingsWindow>();
        window->InitializeOwned(
            m_windowHandle,
            m_viewModel->CreateSettingsDraft(),
            m_themeManager.IsFluentThemeEnabled(),
            [weak](
                liangwenpeak::balance::BalanceSettings settings,
                liangwenpeak::balance::ApiKeyDraftAction const keyAction,
                winrt::hstring const& replacementApiKey)
            {
                auto strong = weak.get();
                if (!strong || strong->m_closing)
                {
                    return false;
                }
                const auto result = strong->m_viewModel->CommitSettings(
                    std::move(settings),
                    keyAction,
                    replacementApiKey);
                if (!result.succeeded)
                {
                    return false;
                }
                strong->ApplyState();
                if (result.scheduleChanged)
                {
                    strong->ScheduleNextBalanceRefresh(std::chrono::system_clock::now());
                }
                if (result.immediateRefreshReason)
                {
                    strong->RefreshBalanceAsync(*result.immediateRefreshReason);
                }
                return true;
            },
            [weak]()
            {
                auto strong = weak.get();
                if (!strong || strong->m_closing)
                {
                    return false;
                }
                const bool reset = strong->m_viewModel->ResetStatistics();
                strong->ApplyState();
                return reset;
            },
            [weak]()
            {
                if (auto strong = weak.get())
                {
                    strong->m_apiSettingsWindow = nullptr;
                }
            });
        m_apiSettingsWindow = std::move(window);
        m_apiSettingsWindow->ShowOwned();
    }

    winrt::fire_and_forget MainWindow::ShowAboutDialogAsync()
    {
        try
        {
            const auto resources = Application::Current().Resources();
            const auto primaryBrush = resources.Lookup(winrt::box_value(L"PrimaryTextBrush")).as<Brush>();
            const auto tertiaryBrush = resources.Lookup(winrt::box_value(L"TertiaryTextBrush")).as<Brush>();

            TextBlock applicationName;
            applicationName.Text(L"LiangWenPeak");
            applicationName.FontSize(17);
            applicationName.FontWeight(Windows::UI::Text::FontWeight{ 600 });
            applicationName.Foreground(primaryBrush);
            applicationName.TextWrapping(TextWrapping::Wrap);

            TextBlock repository;
            repository.FontSize(12);
            repository.TextWrapping(TextWrapping::Wrap);

            Hyperlink repositoryLink;
            repositoryLink.NavigateUri(Uri{ L"https://github.com/zeronx798/LiangWenPeak" });
            Run repositoryName;
            repositoryName.Text(L"zeronx798/LiangWenPeak");
            repositoryLink.Inlines().Append(repositoryName);
            repository.Inlines().Append(repositoryLink);

            TextBlock license;
            license.Text(L"Licensed under Apache License 2.0");
            license.FontSize(12);
            license.Foreground(tertiaryBrush);
            license.TextWrapping(TextWrapping::Wrap);

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

            Grid::SetRow(repository, 1);
            Grid::SetRow(license, 2);
            Grid::SetRow(version, 3);
            content.Children().Append(applicationName);
            content.Children().Append(repository);
            content.Children().Append(license);
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
        if (m_apiSettingsWindow)
        {
            m_apiSettingsWindow->CloseFromOwner();
        }
        CancelFirstFrameReveal();
        m_startupCloaked = false;
        StopTimers();
        if (m_appWindow)
        {
            m_appWindow.Closing(m_closingToken);
        }
    }
}
