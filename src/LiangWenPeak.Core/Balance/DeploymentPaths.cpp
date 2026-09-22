#include "DeploymentPaths.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>

namespace liangwenpeak::balance
{
    std::filesystem::path ResolveDeploymentRoot(std::filesystem::path const& executablePath)
    {
        auto directory = std::filesystem::absolute(executablePath).lexically_normal().parent_path();
        const auto directoryName = directory.filename().wstring();
        if (directoryName.starts_with(L"app-") && directoryName.size() > 4)
        {
            return directory.parent_path();
        }

        auto candidate = directory;
        while (!candidate.empty())
        {
            if (std::filesystem::exists(candidate / "Version.props"))
            {
                return candidate;
            }
            const auto parent = candidate.parent_path();
            if (parent == candidate)
            {
                break;
            }
            candidate = parent;
        }
        return directory;
    }

    std::filesystem::path ResolveDataRoot(
        std::filesystem::path const& executablePath,
        std::optional<std::filesystem::path> const& overrideRoot)
    {
        if (overrideRoot)
        {
            return std::filesystem::absolute(*overrideRoot).lexically_normal();
        }
        return ResolveDeploymentRoot(executablePath) / "data";
    }

    std::filesystem::path CanonicalizeDataRoot(std::filesystem::path const& dataRoot)
    {
        std::error_code error;
        auto canonical = std::filesystem::weakly_canonical(
            std::filesystem::absolute(dataRoot), error);
        if (error)
        {
            canonical = std::filesystem::absolute(dataRoot).lexically_normal();
        }

        auto normalized = canonical.lexically_normal().wstring();
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](wchar_t value)
        {
            if (value == L'/')
            {
                return L'\\';
            }
            return static_cast<wchar_t>(std::towlower(value));
        });
        while (normalized.size() > 3 && normalized.back() == L'\\')
        {
            normalized.pop_back();
        }
        return std::filesystem::path{ normalized };
    }

    std::wstring BuildDataRootInstanceKey(std::filesystem::path const& canonicalDataRoot)
    {
        constexpr std::array<std::uint64_t, 2> offsets{
            14695981039346656037ULL,
            1099511628211ULL ^ 0x9e3779b97f4a7c15ULL,
        };
        constexpr std::uint64_t prime = 1099511628211ULL;
        auto hashes = offsets;
        const auto identity = std::wstring{ L"zeronx798.LiangWenPeak\0", 23 }
            + canonicalDataRoot.wstring();
        for (wchar_t const value : identity)
        {
            const auto unsignedValue = static_cast<std::uint16_t>(value);
            for (size_t byteIndex = 0; byteIndex < sizeof(unsignedValue); ++byteIndex)
            {
                const auto byte = static_cast<std::uint8_t>(unsignedValue >> (byteIndex * 8U));
                hashes[0] = (hashes[0] ^ byte) * prime;
                hashes[1] = (hashes[1] ^ static_cast<std::uint8_t>(byte + 0x5bU)) * prime;
            }
        }

        std::wostringstream result;
        result.imbue(std::locale::classic());
        result << L"zeronx798.LiangWenPeak.DataRoot."
               << std::hex << std::setfill(L'0')
               << std::setw(16) << hashes[0]
               << std::setw(16) << hashes[1];
        return result.str();
    }
}
