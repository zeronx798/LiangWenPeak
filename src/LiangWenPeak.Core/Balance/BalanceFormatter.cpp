#include "BalanceFormatter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>

namespace liangwenpeak::balance
{
    namespace
    {
        std::wstring CurrencyPrefix(std::string const& currency)
        {
            if (currency == "CNY")
            {
                return L"\u00a5";
            }
            if (currency == "USD")
            {
                return L"$";
            }
            return std::wstring(currency.begin(), currency.end());
        }

        std::wstring FormatTwoDecimalsFromScaled(std::int64_t const scaled)
        {
            constexpr auto unitsPerCent = DecimalAmount::Scale / 100;
            const auto roundedCents = (scaled + unitsPerCent / 2) / unitsPerCent;
            std::wostringstream output;
            output.imbue(std::locale::classic());
            output << roundedCents / 100 << L'.' << std::setfill(L'0') << std::setw(2)
                   << roundedCents % 100;
            return output.str();
        }

        std::wstring FormatTwoDecimals(long double const value)
        {
            const auto nonNegative = std::max(0.0L, value);
            const auto cents = static_cast<std::int64_t>(std::llround(nonNegative * 100.0L));
            std::wostringstream output;
            output.imbue(std::locale::classic());
            output << cents / 100 << L'.' << std::setfill(L'0') << std::setw(2) << cents % 100;
            return output.str();
        }
    }

    std::wstring FormatCurrencyAmount(
        std::string const& currency,
        DecimalAmount const amount)
    {
        return CurrencyPrefix(currency) + L" " + FormatTwoDecimalsFromScaled(amount.ScaledValue());
    }

    std::wstring FormatBurnRate(
        std::string const& currency,
        long double const burnPerHour)
    {
        return CurrencyPrefix(currency) + L" " + FormatTwoDecimals(burnPerHour) + L"/\u65f6";
    }

    EtaResult CalculateEta(
        DecimalAmount const currentBalance,
        DecimalAmount const warningBalance,
        BalanceForecast const& forecast) noexcept
    {
        if (currentBalance <= warningBalance)
        {
            return EtaResult{ EtaState::AtThreshold, {} };
        }
        if (!forecast.hasValidInterval)
        {
            return EtaResult{ EtaState::InsufficientData, {} };
        }
        if (forecast.burnPerHour <= std::numeric_limits<long double>::epsilon())
        {
            return EtaResult{ EtaState::NoConsumption, {} };
        }

        const auto available = (currentBalance.ScaledValue() - warningBalance.ScaledValue())
            / static_cast<long double>(DecimalAmount::Scale);
        const auto rawSeconds = available / forecast.burnPerHour * 3600.0L;
        const auto maximum = static_cast<long double>(std::numeric_limits<std::int64_t>::max());
        const auto seconds = static_cast<std::int64_t>(std::clamp(rawSeconds, 0.0L, maximum));
        return EtaResult{ EtaState::Estimated, std::chrono::seconds{ seconds } };
    }

    std::wstring FormatEta(EtaResult const& eta)
    {
        switch (eta.state)
        {
        case EtaState::AtThreshold:
            return L"\u5df2\u89e6\u5e95";
        case EtaState::InsufficientData:
            return L"\u83b7\u53d6\u4e2d";
        case EtaState::NoConsumption:
            return L"\u2014\u2014";
        case EtaState::Estimated:
            break;
        }

        constexpr auto year = std::chrono::hours{ 24 * 365 };
        if (eta.remaining >= year)
        {
            return L"\u5927\u4e8e1\u5e74";
        }
        if (eta.remaining < std::chrono::minutes{ 1 })
        {
            return L"\u5c0f\u4e8e1\u5206\u949f";
        }

        struct Unit
        {
            std::int64_t seconds;
            wchar_t const* label;
        };
        constexpr std::array<Unit, 5> units = {
            Unit{ 30LL * 24 * 60 * 60, L"\u6708" },
            Unit{ 24LL * 60 * 60, L"\u5929" },
            Unit{ 60LL * 60, L"\u65f6" },
            Unit{ 60, L"\u5206" },
            Unit{ 1, L"\u79d2" },
        };

        auto remaining = eta.remaining.count();
        std::wstring result = L"\u7ea6 ";
        int emitted{};
        for (auto const& unit : units)
        {
            const auto value = remaining / unit.seconds;
            remaining %= unit.seconds;
            if (value == 0)
            {
                continue;
            }
            if (emitted > 0)
            {
                result.push_back(L' ');
            }
            result += std::to_wstring(value);
            result.push_back(L' ');
            result += unit.label;
            if (++emitted == 2)
            {
                break;
            }
        }
        return result;
    }
}
