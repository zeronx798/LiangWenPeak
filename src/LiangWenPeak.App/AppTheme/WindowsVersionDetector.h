#pragma once

#include <cstdint>

namespace liangwenpeak::apptheme
{
    class WindowsVersionDetector final
    {
    public:
        static constexpr std::uint32_t Windows11MinimumBuild = 22000;

        [[nodiscard]] static constexpr bool IsWindows11Version(
            std::uint32_t const majorVersion,
            std::uint32_t const buildNumber) noexcept
        {
            return majorVersion > 10
                || (majorVersion == 10 && buildNumber >= Windows11MinimumBuild);
        }

        [[nodiscard]] static bool IsWindows11OrGreater() noexcept;
    };
}
