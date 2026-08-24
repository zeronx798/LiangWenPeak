#include "BalanceHistoryStore.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace liangwenpeak::balance
{
    namespace
    {
        bool IsValidSeriesId(std::string_view const value) noexcept
        {
            return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char const character)
            {
                return (character >= '0' && character <= '9')
                    || (character >= 'a' && character <= 'f')
                    || (character >= 'A' && character <= 'F');
            });
        }

        bool IsValidCurrency(std::string_view const value) noexcept
        {
            return !value.empty() && value.size() <= 16
                && std::all_of(value.begin(), value.end(), [](char const character)
                {
                    return (character >= 'A' && character <= 'Z')
                        || (character >= 'a' && character <= 'z')
                        || (character >= '0' && character <= '9')
                        || character == '_' || character == '-' || character == '.';
                });
        }

        std::optional<std::int64_t> ParseInteger(std::string_view const value) noexcept
        {
            std::int64_t parsed{};
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
            if (error != std::errc{} || end != value.data() + value.size())
            {
                return std::nullopt;
            }
            return parsed;
        }

        std::array<std::string_view, 4> SplitCsvLine(std::string_view const line)
        {
            std::array<std::string_view, 4> fields;
            size_t start{};
            for (size_t index = 0; index < 3; ++index)
            {
                const auto comma = line.find(',', start);
                if (comma == std::string_view::npos)
                {
                    throw std::runtime_error("History row has too few columns");
                }
                fields[index] = line.substr(start, comma - start);
                start = comma + 1;
            }
            if (line.find(',', start) != std::string_view::npos)
            {
                throw std::runtime_error("History row has too many columns");
            }
            fields[3] = line.substr(start);
            return fields;
        }

        BalanceHistoryEntry ParseEntry(std::string_view const line)
        {
            const auto fields = SplitCsvLine(line);
            const auto timestampValue = ParseInteger(fields[1]);
            if (!timestampValue)
            {
                throw std::runtime_error("History timestamp is invalid");
            }
            const auto timestamp = std::chrono::sys_seconds{ std::chrono::seconds{ *timestampValue } };

            if (fields[0] == "@API_OFF" || fields[0] == "@API_ON")
            {
                if (!fields[2].empty() || !fields[3].empty())
                {
                    throw std::runtime_error("History marker payload is invalid");
                }
                return BalanceHistoryEntry::Marker(
                    fields[0] == "@API_OFF" ? HistoryEntryKind::ApiOff : HistoryEntryKind::ApiOn,
                    timestamp);
            }

            if (!IsValidSeriesId(fields[0]) || !IsValidCurrency(fields[2]))
            {
                throw std::runtime_error("History sample identity is invalid");
            }
            const auto amount = DecimalAmount::TryParse(fields[3]);
            if (!amount || amount->ScaledValue() < 0)
            {
                throw std::runtime_error("History balance is invalid");
            }
            return BalanceHistoryEntry::Sample(
                std::string{ fields[0] },
                timestamp,
                std::string{ fields[2] },
                *amount);
        }

        std::string MarkerName(HistoryEntryKind const kind)
        {
            switch (kind)
            {
            case HistoryEntryKind::ApiOff:
                return "@API_OFF";
            case HistoryEntryKind::ApiOn:
                return "@API_ON";
            case HistoryEntryKind::Sample:
                break;
            }
            throw std::invalid_argument("Sample is not a history marker");
        }

        std::string SerializeEntry(BalanceHistoryEntry const& entry)
        {
            const auto timestamp = entry.timestamp.time_since_epoch().count();
            if (entry.kind == HistoryEntryKind::Sample)
            {
                return entry.seriesId + ',' + std::to_string(timestamp) + ',' + entry.currency + ','
                    + entry.balance.ToString() + "\n";
            }
            return MarkerName(entry.kind) + ',' + std::to_string(timestamp) + ",,\n";
        }

        std::string ArchiveTimestamp(std::chrono::sys_seconds const timestamp)
        {
            return std::to_string(timestamp.time_since_epoch().count());
        }
    }

    BalanceHistoryStore::BalanceHistoryStore(
        std::filesystem::path dataRoot,
        ActiveFileCreator activeFileCreator)
        : m_dataRoot(std::filesystem::absolute(std::move(dataRoot)).lexically_normal()),
          m_activePath(m_dataRoot / "balance-history.csv"),
          m_archiveDirectory(m_dataRoot / "history"),
          m_activeFileCreator(std::move(activeFileCreator))
    {
    }

    HistoryLoadResult BalanceHistoryStore::Load(std::chrono::sys_seconds const now)
    {
        EnsureDirectories();
        EnsureActiveFile();

        std::string contents;
        {
            std::ifstream input(m_activePath, std::ios::binary);
            if (!input)
            {
                throw std::runtime_error("Unable to open active balance history");
            }
            contents.assign(
                std::istreambuf_iterator<char>{ input },
                std::istreambuf_iterator<char>{});
        }
        const bool endsWithNewline = !contents.empty() && contents.back() == '\n';

        try
        {
            std::vector<BalanceHistoryEntry> parsed;
            bool ignoredTruncatedFinalLine = false;
            std::istringstream lines(contents);
            std::string line;
            if (!std::getline(lines, line))
            {
                throw std::runtime_error("History header is missing");
            }
            if (!line.empty() && line.back() == '\r')
            {
                line.pop_back();
            }
            if (line != Header)
            {
                throw std::runtime_error("History header is invalid");
            }

            std::optional<std::chrono::sys_seconds> previousTimestamp;
            size_t lineNumber = 1;
            while (std::getline(lines, line))
            {
                ++lineNumber;
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }
                try
                {
                    auto entry = ParseEntry(line);
                    if (previousTimestamp && entry.timestamp < *previousTimestamp)
                    {
                        throw std::runtime_error("History timestamps are out of order");
                    }
                    previousTimestamp = entry.timestamp;
                    parsed.push_back(std::move(entry));
                }
                catch (...)
                {
                    const bool isFinalTruncatedLine = !endsWithNewline && lines.peek() == std::char_traits<char>::eof();
                    if (isFinalTruncatedLine)
                    {
                        ignoredTruncatedFinalLine = true;
                        break;
                    }
                    throw std::runtime_error("History contains an invalid record at line " + std::to_string(lineNumber));
                }
            }

            if (!endsWithNewline)
            {
                if (ignoredTruncatedFinalLine)
                {
                    const auto lastNewline = contents.find_last_of('\n');
                    contents.resize(lastNewline == std::string::npos ? 0 : lastNewline + 1);
                }
                else
                {
                    contents.push_back('\n');
                }
                std::ofstream repaired(m_activePath, std::ios::binary | std::ios::trunc);
                repaired.write(contents.data(), static_cast<std::streamsize>(contents.size()));
                repaired.flush();
                if (!repaired)
                {
                    throw std::runtime_error("Unable to repair active balance history tail");
                }
            }
            m_entries = std::move(parsed);
            return {};
        }
        catch (...)
        {
            const auto archived = ArchiveActive("balance-history-invalid-", now);
            m_entries.clear();
            return HistoryLoadResult{ true, archived };
        }
    }

    void BalanceHistoryStore::AppendSamples(std::vector<BalanceHistoryEntry> const& samples)
    {
        if (samples.empty())
        {
            return;
        }
        EnsureDirectories();
        EnsureActiveFile();

        std::set<std::string> currencies;
        std::string payload;
        auto const& first = samples.front();
        if (first.kind != HistoryEntryKind::Sample || !IsValidSeriesId(first.seriesId))
        {
            throw std::invalid_argument("History sample batch is invalid");
        }
        if (!m_entries.empty() && first.timestamp < m_entries.back().timestamp)
        {
            throw std::invalid_argument("History sample timestamp is out of order");
        }
        for (auto const& sample : samples)
        {
            if (sample.kind != HistoryEntryKind::Sample
                || sample.timestamp != first.timestamp
                || sample.seriesId != first.seriesId
                || !IsValidCurrency(sample.currency)
                || sample.balance.ScaledValue() < 0
                || !currencies.insert(sample.currency).second)
            {
                throw std::invalid_argument("History sample batch is inconsistent");
            }
            payload += SerializeEntry(sample);
        }

        std::ofstream output(m_activePath, std::ios::binary | std::ios::app);
        output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        output.flush();
        if (!output)
        {
            throw std::runtime_error("Unable to append balance history samples");
        }
        m_entries.insert(m_entries.end(), samples.begin(), samples.end());
    }

    void BalanceHistoryStore::AppendMarker(
        HistoryEntryKind const marker,
        std::chrono::sys_seconds const timestamp)
    {
        const auto entry = BalanceHistoryEntry::Marker(marker, timestamp);
        EnsureDirectories();
        EnsureActiveFile();
        if (!m_entries.empty() && timestamp < m_entries.back().timestamp)
        {
            throw std::invalid_argument("History marker timestamp is out of order");
        }
        const auto payload = SerializeEntry(entry);
        std::ofstream output(m_activePath, std::ios::binary | std::ios::app);
        output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        output.flush();
        if (!output)
        {
            throw std::runtime_error("Unable to append balance history marker");
        }
        m_entries.push_back(entry);
    }

    std::filesystem::path BalanceHistoryStore::Rollover(std::chrono::sys_seconds const now)
    {
        EnsureDirectories();
        EnsureActiveFile();
        const auto archived = ArchiveActive("balance-history-", now);
        m_entries.clear();
        return archived;
    }

    std::vector<BalanceHistoryEntry> const& BalanceHistoryStore::Entries() const noexcept
    {
        return m_entries;
    }

    std::filesystem::path const& BalanceHistoryStore::DataRoot() const noexcept
    {
        return m_dataRoot;
    }

    std::filesystem::path const& BalanceHistoryStore::ActivePath() const noexcept
    {
        return m_activePath;
    }

    std::filesystem::path const& BalanceHistoryStore::ArchiveDirectory() const noexcept
    {
        return m_archiveDirectory;
    }

    std::filesystem::path BalanceHistoryStore::ArchiveActive(
        std::string_view const prefix,
        std::chrono::sys_seconds const now)
    {
        EnsureDirectories();
        auto archivePath = m_archiveDirectory
            / (std::string{ prefix } + ArchiveTimestamp(now) + ".csv");
        for (int suffix = 1; std::filesystem::exists(archivePath); ++suffix)
        {
            archivePath = m_archiveDirectory
                / (std::string{ prefix } + ArchiveTimestamp(now) + '-' + std::to_string(suffix) + ".csv");
        }

        std::filesystem::rename(m_activePath, archivePath);
        try
        {
            CreateActiveFile(m_activePath);
        }
        catch (...)
        {
            std::error_code removeError;
            std::filesystem::remove(m_activePath, removeError);
            std::error_code rollbackError;
            std::filesystem::rename(archivePath, m_activePath, rollbackError);
            throw;
        }
        return archivePath;
    }

    void BalanceHistoryStore::EnsureDirectories()
    {
        std::filesystem::create_directories(m_archiveDirectory);
    }

    void BalanceHistoryStore::EnsureActiveFile()
    {
        if (!std::filesystem::exists(m_activePath))
        {
            CreateActiveFile(m_activePath);
        }
    }

    void BalanceHistoryStore::CreateActiveFile(std::filesystem::path const& path) const
    {
        if (m_activeFileCreator)
        {
            m_activeFileCreator(path);
            return;
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output << Header << '\n';
        output.flush();
        if (!output)
        {
            throw std::runtime_error("Unable to create active balance history");
        }
    }
}
