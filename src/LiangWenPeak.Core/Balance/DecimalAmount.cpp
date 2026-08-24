#include "DecimalAmount.h"

#include <algorithm>
#include <limits>

namespace liangwenpeak::balance
{
    std::optional<DecimalAmount> DecimalAmount::TryParse(std::string_view const text) noexcept
    {
        if (text.empty())
        {
            return std::nullopt;
        }

        size_t position{};
        bool negative = false;
        if (text[position] == '+' || text[position] == '-')
        {
            negative = text[position] == '-';
            ++position;
        }
        if (position == text.size())
        {
            return std::nullopt;
        }

        std::int64_t whole{};
        size_t wholeDigits{};
        while (position < text.size() && text[position] >= '0' && text[position] <= '9')
        {
            const auto digit = static_cast<std::int64_t>(text[position] - '0');
            if (whole > (std::numeric_limits<std::int64_t>::max() / Scale - digit) / 10)
            {
                return std::nullopt;
            }
            whole = whole * 10 + digit;
            ++wholeDigits;
            ++position;
        }
        if (wholeDigits == 0)
        {
            return std::nullopt;
        }

        std::int64_t fraction{};
        size_t fractionDigits{};
        if (position < text.size() && text[position] == '.')
        {
            ++position;
            while (position < text.size() && text[position] >= '0' && text[position] <= '9')
            {
                const auto digit = static_cast<std::int64_t>(text[position] - '0');
                if (fractionDigits < 8)
                {
                    fraction = fraction * 10 + digit;
                }
                else if (digit != 0)
                {
                    return std::nullopt;
                }
                ++fractionDigits;
                ++position;
            }
            if (fractionDigits == 0)
            {
                return std::nullopt;
            }
        }
        if (position != text.size())
        {
            return std::nullopt;
        }

        const auto retainedFractionDigits = std::min<size_t>(fractionDigits, 8);
        for (size_t index = retainedFractionDigits; index < 8; ++index)
        {
            fraction *= 10;
        }

        if (whole > (std::numeric_limits<std::int64_t>::max() - fraction) / Scale)
        {
            return std::nullopt;
        }
        auto scaled = whole * Scale + fraction;
        if (negative)
        {
            scaled = -scaled;
        }
        return DecimalAmount{ scaled };
    }

    std::string DecimalAmount::ToString() const
    {
        const bool negative = m_scaledValue < 0;
        const auto magnitude = negative
            ? static_cast<std::uint64_t>(-(m_scaledValue + 1)) + 1
            : static_cast<std::uint64_t>(m_scaledValue);
        const auto whole = magnitude / static_cast<std::uint64_t>(Scale);
        auto fraction = magnitude % static_cast<std::uint64_t>(Scale);

        std::string result = negative ? "-" : "";
        result += std::to_string(whole);
        result.push_back('.');
        auto fractionText = std::to_string(fraction);
        result.append(8 - fractionText.size(), '0');
        result += fractionText;
        return result;
    }

    long double DecimalAmount::ToMajorUnits() const noexcept
    {
        return static_cast<long double>(m_scaledValue) / static_cast<long double>(Scale);
    }
}
