#pragma once

#include "BalanceModels.h"
#include "BalanceSettings.h"

#include <chrono>
#include <string>
#include <vector>

namespace liangwenpeak::balance
{
    inline constexpr long double HuberTuningConstant = 1.345L;

    struct BalanceForecast
    {
        bool hasValidInterval = false;
        long double burnPerHour = 0.0L;
        PredictionAlgorithm algorithm = PredictionAlgorithm::SlidingAverage;
        size_t intervalCount = 0;
    };

    class BalanceForecastService final
    {
    public:
        [[nodiscard]] BalanceForecast Forecast(
            std::vector<BalanceHistoryEntry> const& entries,
            std::string const& currency,
            std::chrono::seconds rateWindow,
            PredictionAlgorithm algorithm) const;
    };
}
