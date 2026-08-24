#pragma once

#include "DecimalAmount.h"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace liangwenpeak::balance
{
    enum class BalanceRefreshReason
    {
        StartupObservation,
        ManualObservation,
        SavedKeyObservation,
        ApiReenabledObservation,
        ScheduledSample,
    };

    [[nodiscard]] constexpr bool WritesHistory(BalanceRefreshReason const reason) noexcept
    {
        return reason == BalanceRefreshReason::ScheduledSample;
    }

    struct BalanceValue
    {
        std::string currency;
        DecimalAmount balance;
    };

    enum class HistoryEntryKind
    {
        Sample,
        ApiOff,
        ApiOn,
    };

    struct BalanceHistoryEntry
    {
        HistoryEntryKind kind = HistoryEntryKind::Sample;
        std::chrono::sys_seconds timestamp{};
        std::string seriesId;
        std::string currency;
        DecimalAmount balance;

        [[nodiscard]] static BalanceHistoryEntry Sample(
            std::string series,
            std::chrono::sys_seconds timestamp,
            std::string currency,
            DecimalAmount balance);
        [[nodiscard]] static BalanceHistoryEntry Marker(
            HistoryEntryKind kind,
            std::chrono::sys_seconds timestamp);
    };

    struct BalanceInterval
    {
        std::chrono::sys_seconds start{};
        std::chrono::sys_seconds end{};
        DecimalAmount consumption;
    };

    [[nodiscard]] std::vector<BalanceHistoryEntry> CreateHistorySampleBatch(
        BalanceRefreshReason reason,
        std::optional<std::chrono::sys_seconds> scheduledTimestamp,
        std::string const& seriesId,
        std::vector<BalanceValue> const& balances);

    [[nodiscard]] std::vector<BalanceInterval> BuildValidIntervals(
        std::vector<BalanceHistoryEntry> const& entries,
        std::string const& currency);
}
