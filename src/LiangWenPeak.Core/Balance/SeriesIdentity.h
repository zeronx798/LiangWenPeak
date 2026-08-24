#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace liangwenpeak::balance
{
    inline constexpr size_t HistoryIdentitySecretSize = 32;

    [[nodiscard]] std::vector<std::uint8_t> GenerateHistoryIdentitySecret();
    [[nodiscard]] std::string ComputeSeriesId(
        std::span<std::uint8_t const> secret,
        std::string_view apiKey);
    [[nodiscard]] std::string EncodeHex(std::span<std::uint8_t const> bytes);
    [[nodiscard]] std::vector<std::uint8_t> DecodeHex(std::string_view text);
}
