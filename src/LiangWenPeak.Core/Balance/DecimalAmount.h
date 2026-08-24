#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace liangwenpeak::balance
{
    class DecimalAmount final
    {
    public:
        static constexpr std::int64_t Scale = 100'000'000;

        constexpr DecimalAmount() noexcept = default;

        [[nodiscard]] static constexpr DecimalAmount FromScaled(std::int64_t value) noexcept
        {
            return DecimalAmount{ value };
        }

        [[nodiscard]] static std::optional<DecimalAmount> TryParse(std::string_view text) noexcept;
        [[nodiscard]] std::string ToString() const;
        [[nodiscard]] constexpr std::int64_t ScaledValue() const noexcept
        {
            return m_scaledValue;
        }
        [[nodiscard]] long double ToMajorUnits() const noexcept;

        auto operator<=>(DecimalAmount const&) const = default;

    private:
        explicit constexpr DecimalAmount(std::int64_t value) noexcept
            : m_scaledValue(value)
        {
        }

        std::int64_t m_scaledValue{};
    };
}
