#include "pch.h"
#include "ApiSettingsWindow.xaml.h"

#if __has_include("ApiSettingsWindow.g.cpp")
#include "ApiSettingsWindow.g.cpp"
#endif

#include "Time/BalanceRefreshSchedule.h"

#include <algorithm>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace winrt::LiangWenPeak::implementation
{
    using namespace Microsoft::UI::Windowing;
    using namespace Microsoft::UI::Xaml;
    using namespace Microsoft::UI::Xaml::Controls;
    using namespace Windows::Foundation;

    namespace
    {
        constexpr int SettingsWindowWidth = 420;
        constexpr int SettingsWindowHeight = 680;
        constexpr int WorkAreaInset = 16;

        winrt::hstring FormatRefreshInterval(std::chrono::minutes const interval)
        {
            return winrt::hstring{ std::to_wstring(interval.count()) + L" \u5206\u949f" };
        }

        winrt::hstring FormatRateWindow(std::chrono::seconds const window)
        {
            if (window < std::chrono::hours{ 1 })
            {
                return winrt::hstring{
                    std::to_wstring(std::chrono::duration_cast<std::chrono::minutes>(window).count())
                    + L" \u5206\u949f" };
            }
            if (window < std::chrono::hours{ 24 })
            {
                return winrt::hstring{
                    std::to_wstring(std::chrono::duration_cast<std::chrono::hours>(window).count())
                    + L" \u5c0f\u65f6" };
            }
            return winrt::hstring{
                std::to_wstring(std::chrono::duration_cast<std::chrono::hours>(window).count() / 24)
                + L" \u5929" };
        }

        int32_t RefreshIndex(std::chrono::minutes const interval)
        {
            const auto found = std::find(
                liangwenpeak::time::SupportedBalanceRefreshIntervals.begin(),
                liangwenpeak::time::SupportedBalanceRefreshIntervals.end(),
                interval);
            return found == liangwenpeak::time::SupportedBalanceRefreshIntervals.end()
                ? 0
                : static_cast<int32_t>(std::distance(
                    liangwenpeak::time::SupportedBalanceRefreshIntervals.begin(), found));
        }

        int32_t AlgorithmIndex(liangwenpeak::balance::PredictionAlgorithm const algorithm) noexcept
        {
            switch (algorithm)
            {
            case liangwenpeak::balance::PredictionAlgorithm::SlidingAverage:
                return 0;
            case liangwenpeak::balance::PredictionAlgorithm::ExponentialAverage:
                return 1;
            case liangwenpeak::balance::PredictionAlgorithm::RobustTrend:
                return 2;
            case liangwenpeak::balance::PredictionAlgorithm::LastValidSample:
                return 0;
            }
            return 0;
        }

        liangwenpeak::balance::PredictionAlgorithm AlgorithmAt(int32_t const index) noexcept
        {
            switch (index)
            {
            case 1:
                return liangwenpeak::balance::PredictionAlgorithm::ExponentialAverage;
            case 2:
                return liangwenpeak::balance::PredictionAlgorithm::RobustTrend;
            default:
                return liangwenpeak::balance::PredictionAlgorithm::SlidingAverage;
            }
        }

        std::wstring WarningInputText(liangwenpeak::balance::DecimalAmount const amount)
        {
            auto text = amount.ToString();
            while (text.size() > 3 && text.back() == '0' && text[text.size() - 3] != '.')
            {
                text.pop_back();
            }
            return std::wstring(text.begin(), text.end());
        }
    }

    ApiSettingsWindow::ApiSettingsWindow()
    {
        InitializeComponent();
    }

    void ApiSettingsWindow::InitializeOwned(
        HWND const owner,
        liangwenpeak::balance::ApiSettingsDraft draft,
        SaveCallback saveCallback,
        ResetCallback resetCallback,
        ClosedCallback closedCallback)
    {
        m_owner = owner;
        m_draft.emplace(std::move(draft));
        m_saveCallback = std::move(saveCallback);
        m_resetCallback = std::move(resetCallback);
        m_closedCallback = std::move(closedCallback);
        ConfigureWindow();
        PopulateControls();
    }

    void ApiSettingsWindow::ShowOwned()
    {
        if (m_closed)
        {
            return;
        }
        if (!m_ownerDisabled && m_owner != nullptr && ::IsWindow(m_owner))
        {
            ::EnableWindow(m_owner, FALSE);
            m_ownerDisabled = true;
        }
        try
        {
            Activate();
            BringToFront();
        }
        catch (...)
        {
            if (m_ownerDisabled && m_owner != nullptr && ::IsWindow(m_owner))
            {
                ::EnableWindow(m_owner, TRUE);
            }
            m_ownerDisabled = false;
            throw;
        }
    }

    void ApiSettingsWindow::CloseFromOwner() noexcept
    {
        m_closingFromOwner = true;
        try
        {
            Close();
        }
        catch (...)
        {
        }
    }

    void ApiSettingsWindow::OnApiKeyPasswordChanged(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_suppressEvents && m_draft)
        {
            m_draft->OnApiKeyInputChanged(!ApiKeyBox().Password().empty());
            UpdateApiKeyClearPresentation();
        }
    }

    void ApiSettingsWindow::OnClearApiKeyClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_draft)
        {
            return;
        }
        m_suppressEvents = true;
        ApiKeyBox().Password({});
        m_suppressEvents = false;
        if (m_draft->KeyAction() == liangwenpeak::balance::ApiKeyDraftAction::Clear)
        {
            m_draft->UndoApiKeyClear();
        }
        else
        {
            m_draft->RequestApiKeyClear();
        }
        UpdateApiKeyClearPresentation();
    }

    void ApiSettingsWindow::OnCurrencyChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_draft || CurrencyBox().SelectedIndex() < 0)
        {
            return;
        }
        if (!CaptureCurrentWarning())
        {
            const auto current = std::find(
                m_draft->Settings().knownCurrencies.begin(),
                m_draft->Settings().knownCurrencies.end(),
                m_editingCurrency);
            m_suppressEvents = true;
            CurrencyBox().SelectedIndex(current == m_draft->Settings().knownCurrencies.end()
                ? 0
                : static_cast<int32_t>(std::distance(
                    m_draft->Settings().knownCurrencies.begin(), current)));
            m_suppressEvents = false;
            return;
        }
        m_editingCurrency = SelectedCurrency();
        m_draft->Settings().selectedCurrency = m_editingCurrency;
        LoadWarningForCurrency();
    }

    void ApiSettingsWindow::OnRefreshIntervalChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_draft || RefreshIntervalBox().SelectedIndex() < 0)
        {
            return;
        }
        m_draft->SetRefreshInterval(SelectedRefreshInterval());
        PopulateRateWindows();
        UpdateAlgorithmControl();
    }

    void ApiSettingsWindow::OnRateWindowChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_draft || RateWindowBox().SelectedIndex() < 0)
        {
            return;
        }
        m_draft->SetRateWindow(SelectedRateWindow());
        UpdateAlgorithmControl();
    }

    void ApiSettingsWindow::OnAlgorithmChanged(IInspectable const&, SelectionChangedEventArgs const&)
    {
        if (m_suppressEvents || !m_draft || !AlgorithmBox().IsEnabled() || AlgorithmBox().SelectedIndex() < 0)
        {
            return;
        }
        m_draft->Settings().preferredAlgorithm = AlgorithmAt(AlgorithmBox().SelectedIndex());
    }

    void ApiSettingsWindow::OnSaveClick(IInspectable const&, RoutedEventArgs const&)
    {
        if (!m_draft || !CaptureCurrentWarning())
        {
            return;
        }
        m_draft->Settings().apiFeatureEnabled = ApiFeatureToggle().IsOn();
        m_draft->Settings().forecastEnabled = ForecastEnabledBox().IsChecked().Value();
        m_draft->Settings().selectedCurrency = SelectedCurrency();
        if (!m_saveCallback
            || !m_saveCallback(m_draft->Settings(), m_draft->KeyAction(), ApiKeyBox().Password()))
        {
            SettingsStatusText().Text(L"\u8bbe\u7f6e\u4fdd\u5b58\u5931\u8d25");
            return;
        }
        Close();
    }

    void ApiSettingsWindow::OnCancelClick(IInspectable const&, RoutedEventArgs const&)
    {
        Close();
    }

    void ApiSettingsWindow::OnResetStatisticsClick(IInspectable const&, RoutedEventArgs const&)
    {
        ConfirmResetStatisticsAsync();
    }

    void ApiSettingsWindow::ConfigureWindow()
    {
        m_appWindow = AppWindow();
        m_appWindow.Title(L"API Key \u529f\u80fd");
        const auto presenter = m_appWindow.Presenter().as<OverlappedPresenter>();
        presenter.IsResizable(false);
        presenter.IsMaximizable(false);
        presenter.IsMinimizable(false);
        presenter.IsAlwaysOnTop(true);

        const auto nativeWindow = this->try_as<::IWindowNative>();
        winrt::check_hresult(nativeWindow->get_WindowHandle(&m_windowHandle));
        ::SetLastError(ERROR_SUCCESS);
        const auto previousOwner = ::SetWindowLongPtrW(
            m_windowHandle,
            GWLP_HWNDPARENT,
            reinterpret_cast<LONG_PTR>(m_owner));
        if (previousOwner == 0 && ::GetLastError() != ERROR_SUCCESS)
        {
            winrt::throw_last_error();
        }
        const auto extendedStyle = ::GetWindowLongPtrW(m_windowHandle, GWL_EXSTYLE);
        ::SetWindowLongPtrW(
            m_windowHandle,
            GWL_EXSTYLE,
            (extendedStyle | WS_EX_TOOLWINDOW) & ~static_cast<LONG_PTR>(WS_EX_APPWINDOW));
        ::SetWindowPos(
            m_windowHandle,
            nullptr,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);

        const auto dpi = m_owner != nullptr && ::IsWindow(m_owner)
            ? ::GetDpiForWindow(m_owner)
            : ::GetDpiForWindow(m_windowHandle);
        const auto effectiveDpi = dpi == 0 ? USER_DEFAULT_SCREEN_DPI : dpi;
        auto width = ::MulDiv(SettingsWindowWidth, static_cast<int>(effectiveDpi), 96);
        auto height = ::MulDiv(SettingsWindowHeight, static_cast<int>(effectiveDpi), 96);

        const auto monitor = ::MonitorFromWindow(
            m_owner != nullptr ? m_owner : m_windowHandle,
            MONITOR_DEFAULTTONEAREST);
        MONITORINFO monitorInfo{ sizeof(monitorInfo) };
        winrt::check_bool(::GetMonitorInfoW(monitor, &monitorInfo));
        const auto workWidth = monitorInfo.rcWork.right - monitorInfo.rcWork.left;
        const auto workHeight = monitorInfo.rcWork.bottom - monitorInfo.rcWork.top;
        width = (std::min)(width, static_cast<int>(workWidth - WorkAreaInset * 2));
        height = (std::min)(height, static_cast<int>(workHeight - WorkAreaInset * 2));

        RECT ownerBounds = monitorInfo.rcWork;
        if (m_owner != nullptr && ::IsWindow(m_owner))
        {
            static_cast<void>(::GetWindowRect(m_owner, &ownerBounds));
        }
        const auto centeredX = ownerBounds.left + (ownerBounds.right - ownerBounds.left - width) / 2;
        const auto centeredY = ownerBounds.top + (ownerBounds.bottom - ownerBounds.top - height) / 2;
        const auto x = std::clamp(
            centeredX,
            monitorInfo.rcWork.left + WorkAreaInset,
            monitorInfo.rcWork.right - WorkAreaInset - width);
        const auto y = std::clamp(
            centeredY,
            monitorInfo.rcWork.top + WorkAreaInset,
            monitorInfo.rcWork.bottom - WorkAreaInset - height);
        m_appWindow.Resize({ width, height });
        m_appWindow.Move({ x, y });
        m_closedToken = Closed({ this, &ApiSettingsWindow::OnWindowClosed });
    }

    void ApiSettingsWindow::BringToFront()
    {
        if (m_windowHandle == nullptr || !::IsWindow(m_windowHandle))
        {
            return;
        }

        winrt::check_bool(::SetWindowPos(
            m_windowHandle,
            HWND_TOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW));
        static_cast<void>(::BringWindowToTop(m_windowHandle));
        static_cast<void>(::SetActiveWindow(m_windowHandle));
        static_cast<void>(::SetForegroundWindow(m_windowHandle));
    }

    void ApiSettingsWindow::PopulateControls()
    {
        if (!m_draft)
        {
            return;
        }
        m_suppressEvents = true;
        ApiFeatureToggle().IsOn(m_draft->Settings().apiFeatureEnabled);
        ForecastEnabledBox().IsChecked(m_draft->Settings().forecastEnabled);
        ApiKeyBox().PlaceholderText(m_draft->PersistedHasApiKey()
            ? L"\u5df2\u914d\u7f6e\uff0c\u7559\u7a7a\u5219\u4fdd\u6301\u4e0d\u53d8"
            : L"sk-...");

        CurrencyBox().Items().Clear();
        for (auto const& currency : m_draft->Settings().knownCurrencies)
        {
            CurrencyBox().Items().Append(winrt::box_value(winrt::to_hstring(currency)));
        }
        const auto currency = std::find(
            m_draft->Settings().knownCurrencies.begin(),
            m_draft->Settings().knownCurrencies.end(),
            m_draft->Settings().selectedCurrency);
        CurrencyBox().SelectedIndex(currency == m_draft->Settings().knownCurrencies.end()
            ? 0
            : static_cast<int32_t>(std::distance(m_draft->Settings().knownCurrencies.begin(), currency)));
        m_editingCurrency = SelectedCurrency();

        RefreshIntervalBox().Items().Clear();
        for (auto const interval : liangwenpeak::time::SupportedBalanceRefreshIntervals)
        {
            RefreshIntervalBox().Items().Append(winrt::box_value(FormatRefreshInterval(interval)));
        }
        RefreshIntervalBox().SelectedIndex(RefreshIndex(m_draft->Settings().refreshInterval));
        m_suppressEvents = false;

        PopulateRateWindows();
        LoadWarningForCurrency();
        UpdateAlgorithmControl();
        UpdateApiKeyClearPresentation();
    }

    void ApiSettingsWindow::PopulateRateWindows()
    {
        if (!m_draft)
        {
            return;
        }
        m_availableRateWindows = liangwenpeak::balance::GetAvailableRateWindows(
            m_draft->Settings().refreshInterval);
        m_suppressEvents = true;
        RateWindowBox().Items().Clear();
        for (auto const window : m_availableRateWindows)
        {
            RateWindowBox().Items().Append(winrt::box_value(FormatRateWindow(window)));
        }
        const auto selected = std::find(
            m_availableRateWindows.begin(),
            m_availableRateWindows.end(),
            m_draft->Settings().rateWindow);
        RateWindowBox().SelectedIndex(selected == m_availableRateWindows.end()
            ? 0
            : static_cast<int32_t>(std::distance(m_availableRateWindows.begin(), selected)));
        m_suppressEvents = false;
    }

    void ApiSettingsWindow::UpdateAlgorithmControl()
    {
        if (!m_draft)
        {
            return;
        }
        m_suppressEvents = true;
        AlgorithmBox().Items().Clear();
        const bool forced = liangwenpeak::balance::GetEffectiveAlgorithm(m_draft->Settings())
            == liangwenpeak::balance::PredictionAlgorithm::LastValidSample;
        if (forced)
        {
            AlgorithmBox().Items().Append(winrt::box_value(L"\u6700\u8fd1\u6709\u6548\u91c7\u6837"));
            AlgorithmBox().SelectedIndex(0);
            AlgorithmBox().IsEnabled(false);
        }
        else
        {
            AlgorithmBox().Items().Append(winrt::box_value(L"\u6ed1\u52a8\u5e73\u5747\uff08\u63a8\u8350\uff09"));
            AlgorithmBox().Items().Append(winrt::box_value(L"\u6307\u6570\u5e73\u5747"));
            AlgorithmBox().Items().Append(winrt::box_value(L"\u7a33\u5065\u8d8b\u52bf"));
            AlgorithmBox().SelectedIndex(AlgorithmIndex(m_draft->Settings().preferredAlgorithm));
            AlgorithmBox().IsEnabled(true);
        }
        m_suppressEvents = false;
    }

    void ApiSettingsWindow::UpdateApiKeyClearPresentation()
    {
        if (!m_draft)
        {
            return;
        }
        const bool pendingClear = m_draft->KeyAction() == liangwenpeak::balance::ApiKeyDraftAction::Clear;
        ClearApiKeyButton().Content(winrt::box_value(pendingClear ? L"\u64a4\u9500" : L"\u6e05\u9664"));
        ApiKeyBox().PlaceholderText(pendingClear
            ? L"\u4fdd\u5b58\u540e\u5c06\u6e05\u9664 API Key"
            : (m_draft->PersistedHasApiKey()
                ? L"\u5df2\u914d\u7f6e\uff0c\u7559\u7a7a\u5219\u4fdd\u6301\u4e0d\u53d8"
                : L"sk-..."));
    }

    void ApiSettingsWindow::LoadWarningForCurrency()
    {
        if (m_draft && !m_editingCurrency.empty())
        {
            WarningBalanceBox().Text(winrt::hstring{ WarningInputText(
                liangwenpeak::balance::GetWarningBalance(m_draft->Settings(), m_editingCurrency)) });
        }
    }

    bool ApiSettingsWindow::CaptureCurrentWarning()
    {
        if (!m_draft || m_editingCurrency.empty())
        {
            return true;
        }
        const auto amount = liangwenpeak::balance::DecimalAmount::TryParse(
            winrt::to_string(WarningBalanceBox().Text()));
        if (!amount || amount->ScaledValue() < 0)
        {
            SettingsStatusText().Text(L"\u8bf7\u8f93\u5165\u6709\u6548\u7684\u975e\u8d1f\u9884\u8b66\u4f59\u989d");
            return false;
        }
        liangwenpeak::balance::SetWarningBalance(m_draft->Settings(), m_editingCurrency, *amount);
        SettingsStatusText().Text({});
        return true;
    }

    std::string ApiSettingsWindow::SelectedCurrency()
    {
        const auto index = CurrencyBox().SelectedIndex();
        if (!m_draft || index < 0
            || static_cast<size_t>(index) >= m_draft->Settings().knownCurrencies.size())
        {
            return "CNY";
        }
        return m_draft->Settings().knownCurrencies[static_cast<size_t>(index)];
    }

    std::chrono::minutes ApiSettingsWindow::SelectedRefreshInterval()
    {
        const auto index = RefreshIntervalBox().SelectedIndex();
        if (index < 0
            || static_cast<size_t>(index) >= liangwenpeak::time::SupportedBalanceRefreshIntervals.size())
        {
            return liangwenpeak::time::DefaultBalanceRefreshInterval;
        }
        return liangwenpeak::time::SupportedBalanceRefreshIntervals[static_cast<size_t>(index)];
    }

    std::chrono::seconds ApiSettingsWindow::SelectedRateWindow()
    {
        const auto index = RateWindowBox().SelectedIndex();
        if (index < 0 || static_cast<size_t>(index) >= m_availableRateWindows.size())
        {
            return liangwenpeak::balance::DefaultRateWindow;
        }
        return m_availableRateWindows[static_cast<size_t>(index)];
    }

    winrt::fire_and_forget ApiSettingsWindow::ConfirmResetStatisticsAsync()
    {
        auto lifetime = get_strong();
        try
        {
            TextBlock message;
            message.Text(L"\u5f53\u524d\u4f59\u989d\u5386\u53f2\u5c06\u88ab\u5f52\u6863\u4fdd\u7559\u3002\n\u65b0\u7684\u6d88\u8017\u901f\u7387\u5c06\u4ece\u540e\u7eed\u81ea\u52a8\u91c7\u6837\u91cd\u65b0\u8ba1\u7b97\u3002");
            message.TextWrapping(TextWrapping::Wrap);

            ContentDialog dialog;
            dialog.XamlRoot(RootGrid().XamlRoot());
            dialog.Title(winrt::box_value(L"\u91cd\u65b0\u5f00\u59cb\u7edf\u8ba1\uff1f"));
            dialog.Content(message);
            dialog.PrimaryButtonText(L"\u91cd\u65b0\u5f00\u59cb");
            dialog.CloseButtonText(L"\u53d6\u6d88");
            dialog.DefaultButton(ContentDialogButton::Close);
            if (co_await dialog.ShowAsync() == ContentDialogResult::Primary)
            {
                if (m_resetCallback && m_resetCallback())
                {
                    SettingsStatusText().Text(L"\u5df2\u91cd\u65b0\u5f00\u59cb\u7edf\u8ba1");
                }
                else
                {
                    SettingsStatusText().Text(L"\u5386\u53f2\u5f52\u6863\u5931\u8d25");
                }
            }
        }
        catch (...)
        {
            if (!m_closed)
            {
                SettingsStatusText().Text(L"\u5386\u53f2\u5f52\u6863\u5931\u8d25");
            }
        }
    }

    void ApiSettingsWindow::OnWindowClosed(
        IInspectable const&,
        WindowEventArgs const&)
    {
        if (m_closed)
        {
            return;
        }
        m_closed = true;
        Closed(m_closedToken);
        if (m_ownerDisabled && m_owner != nullptr && ::IsWindow(m_owner))
        {
            ::EnableWindow(m_owner, TRUE);
            if (!m_closingFromOwner)
            {
                ::SetForegroundWindow(m_owner);
            }
        }
        m_ownerDisabled = false;
        if (m_closedCallback)
        {
            auto callback = std::move(m_closedCallback);
            callback();
        }
    }
}
