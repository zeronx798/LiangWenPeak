#pragma once

#include <filesystem>
#include <optional>

namespace liangwenpeak::balance
{
    [[nodiscard]] std::filesystem::path ResolveDeploymentRoot(
        std::filesystem::path const& executablePath);
    [[nodiscard]] std::filesystem::path ResolveDataRoot(
        std::filesystem::path const& executablePath,
        std::optional<std::filesystem::path> const& overrideRoot = std::nullopt);
    [[nodiscard]] std::filesystem::path CanonicalizeDataRoot(
        std::filesystem::path const& dataRoot);
    [[nodiscard]] std::wstring BuildDataRootInstanceKey(
        std::filesystem::path const& canonicalDataRoot);
}
