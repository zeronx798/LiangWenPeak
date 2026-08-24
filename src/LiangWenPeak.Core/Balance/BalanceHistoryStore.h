#pragma once

#include "BalanceModels.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <optional>
#include <vector>

namespace liangwenpeak::balance
{
    struct HistoryLoadResult
    {
        bool archivedInvalidHistory = false;
        std::optional<std::filesystem::path> archivedPath;
    };

    class BalanceHistoryStore final
    {
    public:
        using ActiveFileCreator = std::function<void(std::filesystem::path const&)>;

        explicit BalanceHistoryStore(
            std::filesystem::path dataRoot,
            ActiveFileCreator activeFileCreator = {});

        [[nodiscard]] HistoryLoadResult Load(
            std::chrono::sys_seconds now = std::chrono::floor<std::chrono::seconds>(
                std::chrono::system_clock::now()));
        void AppendSamples(std::vector<BalanceHistoryEntry> const& samples);
        void AppendMarker(HistoryEntryKind marker, std::chrono::sys_seconds timestamp);
        [[nodiscard]] std::filesystem::path Rollover(
            std::chrono::sys_seconds now = std::chrono::floor<std::chrono::seconds>(
                std::chrono::system_clock::now()));

        [[nodiscard]] std::vector<BalanceHistoryEntry> const& Entries() const noexcept;
        [[nodiscard]] std::filesystem::path const& DataRoot() const noexcept;
        [[nodiscard]] std::filesystem::path const& ActivePath() const noexcept;
        [[nodiscard]] std::filesystem::path const& ArchiveDirectory() const noexcept;

        static constexpr char Header[] = "series_id,timestamp,currency,balance";

    private:
        [[nodiscard]] std::filesystem::path ArchiveActive(
            std::string_view prefix,
            std::chrono::sys_seconds now);
        void EnsureDirectories();
        void EnsureActiveFile();
        void CreateActiveFile(std::filesystem::path const& path) const;

        std::filesystem::path m_dataRoot;
        std::filesystem::path m_activePath;
        std::filesystem::path m_archiveDirectory;
        ActiveFileCreator m_activeFileCreator;
        std::vector<BalanceHistoryEntry> m_entries;
    };
}
