#pragma once

namespace liangwenpeak::ui
{
    inline constexpr int MainWindowWidthDips = 256;
    inline constexpr int MainWindowTitleBarHeightDips = 34;
    inline constexpr int MainWindowStatusGroupHeightDips = 109;
    inline constexpr int MainWindowInformationRowHeightDips = 23;
    inline constexpr int MainWindowUpdateTimeRowHeightDips = 13;
    inline constexpr int MainWindowBaseBottomPaddingDips = 7;
    inline constexpr int MainWindowApiBottomPaddingDips = 4;

    struct MainWindowLayoutState
    {
        bool apiFeatureEnabled = false;
        bool forecastEnabled = false;
        bool updateTimeVisible = false;
    };

    [[nodiscard]] constexpr int MainWindowVisibleContentBottomDips(
        MainWindowLayoutState const state) noexcept
    {
        auto bottom = MainWindowTitleBarHeightDips
            + MainWindowStatusGroupHeightDips
            + MainWindowInformationRowHeightDips;
        if (!state.apiFeatureEnabled)
        {
            return bottom;
        }

        bottom += MainWindowInformationRowHeightDips;
        if (state.forecastEnabled)
        {
            bottom += 2 * MainWindowInformationRowHeightDips;
        }
        if (state.updateTimeVisible)
        {
            bottom += MainWindowUpdateTimeRowHeightDips;
        }
        return bottom;
    }

    [[nodiscard]] constexpr int MainWindowBottomPaddingDips(
        MainWindowLayoutState const state) noexcept
    {
        return MainWindowBaseBottomPaddingDips
            + (state.apiFeatureEnabled ? MainWindowApiBottomPaddingDips : 0);
    }

    [[nodiscard]] constexpr int MainWindowClientHeightDips(
        MainWindowLayoutState const state) noexcept
    {
        return MainWindowVisibleContentBottomDips(state)
            + MainWindowBottomPaddingDips(state);
    }
}
