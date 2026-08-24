#include "BalanceModels.h"

#include <optional>
#include <stdexcept>

namespace liangwenpeak::balance
{
    BalanceHistoryEntry BalanceHistoryEntry::Sample(
        std::string series,
        std::chrono::sys_seconds const timestamp,
        std::string currencyCode,
        DecimalAmount const amount)
    {
        BalanceHistoryEntry entry;
        entry.kind = HistoryEntryKind::Sample;
        entry.timestamp = timestamp;
        entry.seriesId = std::move(series);
        entry.currency = std::move(currencyCode);
        entry.balance = amount;
        return entry;
    }

    BalanceHistoryEntry BalanceHistoryEntry::Marker(
        HistoryEntryKind const markerKind,
        std::chrono::sys_seconds const timestamp)
    {
        if (markerKind == HistoryEntryKind::Sample)
        {
            throw std::invalid_argument("A history marker cannot use Sample kind");
        }

        BalanceHistoryEntry entry;
        entry.kind = markerKind;
        entry.timestamp = timestamp;
        return entry;
    }

    std::vector<BalanceHistoryEntry> CreateHistorySampleBatch(
        BalanceRefreshReason const reason,
        std::optional<std::chrono::sys_seconds> const scheduledTimestamp,
        std::string const& seriesId,
        std::vector<BalanceValue> const& balances)
    {
        if (!WritesHistory(reason))
        {
            return {};
        }
        if (!scheduledTimestamp)
        {
            throw std::invalid_argument("Scheduled balance refresh requires its target timestamp");
        }

        std::vector<BalanceHistoryEntry> samples;
        samples.reserve(balances.size());
        for (auto const& value : balances)
        {
            samples.push_back(BalanceHistoryEntry::Sample(
                seriesId,
                *scheduledTimestamp,
                value.currency,
                value.balance));
        }
        return samples;
    }

    std::vector<BalanceInterval> BuildValidIntervals(
        std::vector<BalanceHistoryEntry> const& entries,
        std::string const& currency)
    {
        std::vector<BalanceInterval> intervals;
        std::optional<BalanceHistoryEntry> previous;
        for (auto const& entry : entries)
        {
            if (entry.kind != HistoryEntryKind::Sample)
            {
                previous.reset();
                continue;
            }
            if (entry.currency != currency)
            {
                continue;
            }

            if (previous
                && previous->seriesId == entry.seriesId
                && previous->timestamp < entry.timestamp
                && entry.balance <= previous->balance)
            {
                intervals.push_back(BalanceInterval{
                    previous->timestamp,
                    entry.timestamp,
                    DecimalAmount::FromScaled(
                        previous->balance.ScaledValue() - entry.balance.ScaledValue()) });
            }
            previous = entry;
        }
        return intervals;
    }
}
