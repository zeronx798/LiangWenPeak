#pragma once

#include "BalanceForecastService.h"
#include "DecimalAmount.h"

#include <chrono>
#include <string>

namespace liangwenpeak::balance
{
    enum class EtaState
    {
        AtThreshold,
        InsufficientData,
        NoConsumption,
        Estimated,
    };

    struct EtaResult
    {
        EtaState state = EtaState::InsufficientData;
        std::chrono::seconds remaining{};
    };

    [[nodiscard]] std::wstring FormatCurrencyAmount(
        std::string const& currency,
        DecimalAmount amount);
    [[nodiscard]] std::wstring FormatBurnRate(
        std::string const& currency,
        long double burnPerHour);
    [[nodiscard]] EtaResult CalculateEta(
        DecimalAmount currentBalance,
        DecimalAmount warningBalance,
        BalanceForecast const& forecast) noexcept;
    [[nodiscard]] std::wstring FormatEta(EtaResult const& eta);
}
