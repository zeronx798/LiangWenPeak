#include "BalanceForecastService.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <optional>
#include <utility>

namespace liangwenpeak::balance
{
    namespace
    {
        struct WindowInterval
        {
            long double durationSeconds{};
            long double consumption{};
            long double midpointAgeSeconds{};
            std::chrono::sys_seconds end{};
        };

        std::vector<WindowInterval> ClipToWindow(
            std::vector<BalanceInterval> const& intervals,
            std::chrono::sys_seconds const windowEnd,
            std::chrono::seconds const rateWindow)
        {
            const auto windowStart = windowEnd - rateWindow;
            std::vector<WindowInterval> result;
            for (auto const& interval : intervals)
            {
                const auto clippedStart = std::max(interval.start, windowStart);
                const auto clippedEnd = std::min(interval.end, windowEnd);
                if (clippedStart >= clippedEnd)
                {
                    continue;
                }

                const auto fullDuration = std::chrono::duration<long double>(interval.end - interval.start).count();
                const auto clippedDuration = std::chrono::duration<long double>(clippedEnd - clippedStart).count();
                const auto fraction = clippedDuration / fullDuration;
                const auto midpoint = clippedStart + std::chrono::duration_cast<std::chrono::seconds>(
                    (clippedEnd - clippedStart) / 2);
                result.push_back(WindowInterval{
                    clippedDuration,
                    interval.consumption.ToMajorUnits() * fraction,
                    std::chrono::duration<long double>(windowEnd - midpoint).count(),
                    clippedEnd });
            }
            return result;
        }

        long double SlidingAverage(std::vector<WindowInterval> const& intervals)
        {
            const auto duration = std::accumulate(
                intervals.begin(), intervals.end(), 0.0L,
                [](long double const total, WindowInterval const& interval)
                {
                    return total + interval.durationSeconds;
                });
            const auto consumption = std::accumulate(
                intervals.begin(), intervals.end(), 0.0L,
                [](long double const total, WindowInterval const& interval)
                {
                    return total + interval.consumption;
                });
            return duration > 0.0L ? consumption * 3600.0L / duration : 0.0L;
        }

        long double ExponentialAverage(
            std::vector<WindowInterval> const& intervals,
            std::chrono::seconds const rateWindow)
        {
            const auto halfLife = std::chrono::duration<long double>(rateWindow).count() / 2.0L;
            const auto decay = std::log(2.0L) / halfLife;
            long double weightedRate{};
            long double totalWeight{};
            for (auto const& interval : intervals)
            {
                const auto intervalRate = interval.consumption * 3600.0L / interval.durationSeconds;
                const auto weight = interval.durationSeconds * std::exp(-decay * interval.midpointAgeSeconds);
                weightedRate += intervalRate * weight;
                totalWeight += weight;
            }
            return totalWeight > 0.0L ? weightedRate / totalWeight : 0.0L;
        }

        std::pair<long double, long double> WeightedLine(
            std::vector<long double> const& x,
            std::vector<long double> const& y,
            std::vector<long double> const& weights)
        {
            long double totalWeight{};
            long double meanX{};
            long double meanY{};
            for (size_t index = 0; index < x.size(); ++index)
            {
                totalWeight += weights[index];
                meanX += weights[index] * x[index];
                meanY += weights[index] * y[index];
            }
            if (totalWeight <= 0.0L)
            {
                return { 0.0L, 0.0L };
            }
            meanX /= totalWeight;
            meanY /= totalWeight;

            long double numerator{};
            long double denominator{};
            for (size_t index = 0; index < x.size(); ++index)
            {
                const auto centeredX = x[index] - meanX;
                numerator += weights[index] * centeredX * (y[index] - meanY);
                denominator += weights[index] * centeredX * centeredX;
            }
            const auto slope = denominator > 0.0L ? numerator / denominator : 0.0L;
            return { meanY - slope * meanX, slope };
        }

        long double Median(std::vector<long double> values)
        {
            if (values.empty())
            {
                return 0.0L;
            }
            const auto middle = values.begin() + static_cast<std::ptrdiff_t>(values.size() / 2);
            std::nth_element(values.begin(), middle, values.end());
            if (values.size() % 2 != 0)
            {
                return *middle;
            }
            const auto lower = std::max_element(values.begin(), middle);
            return (*lower + *middle) / 2.0L;
        }

        long double RobustTrend(std::vector<WindowInterval> const& intervals)
        {
            std::vector<long double> x{ 0.0L };
            std::vector<long double> y{ 0.0L };
            long double elapsedHours{};
            long double cumulativeConsumption{};
            for (auto const& interval : intervals)
            {
                elapsedHours += interval.durationSeconds / 3600.0L;
                cumulativeConsumption += interval.consumption;
                x.push_back(elapsedHours);
                y.push_back(cumulativeConsumption);
            }
            if (x.size() == 2)
            {
                return elapsedHours > 0.0L ? cumulativeConsumption / elapsedHours : 0.0L;
            }

            std::vector<long double> weights(x.size(), 1.0L);
            auto [intercept, slope] = WeightedLine(x, y, weights);
            for (int iteration = 0; iteration < 30; ++iteration)
            {
                std::vector<long double> absoluteResiduals;
                absoluteResiduals.reserve(x.size());
                for (size_t index = 0; index < x.size(); ++index)
                {
                    absoluteResiduals.push_back(std::abs(y[index] - (intercept + slope * x[index])));
                }
                const auto scale = 1.4826L * Median(std::move(absoluteResiduals));
                if (scale <= std::numeric_limits<long double>::epsilon())
                {
                    break;
                }

                for (size_t index = 0; index < x.size(); ++index)
                {
                    const auto residual = std::abs(y[index] - (intercept + slope * x[index]));
                    const auto threshold = HuberTuningConstant * scale;
                    weights[index] = residual <= threshold ? 1.0L : threshold / residual;
                }
                const auto [nextIntercept, nextSlope] = WeightedLine(x, y, weights);
                if (std::abs(nextSlope - slope) <= 1e-12L)
                {
                    intercept = nextIntercept;
                    slope = nextSlope;
                    break;
                }
                intercept = nextIntercept;
                slope = nextSlope;
            }
            return std::max(0.0L, slope);
        }
    }

    BalanceForecast BalanceForecastService::Forecast(
        std::vector<BalanceHistoryEntry> const& entries,
        std::string const& currency,
        std::chrono::seconds const rateWindow,
        PredictionAlgorithm const algorithm) const
    {
        BalanceForecast result;
        result.algorithm = algorithm;
        if (rateWindow <= std::chrono::seconds::zero())
        {
            return result;
        }

        const auto intervals = BuildValidIntervals(entries, currency);
        if (intervals.empty())
        {
            return result;
        }

        const auto latestSample = std::find_if(entries.rbegin(), entries.rend(), [&currency](auto const& entry)
        {
            return entry.kind == HistoryEntryKind::Sample && entry.currency == currency;
        });
        if (latestSample == entries.rend())
        {
            return result;
        }

        if (algorithm == PredictionAlgorithm::LastValidSample)
        {
            auto const& interval = intervals.back();
            const auto duration = std::chrono::duration<long double>(interval.end - interval.start).count();
            result.hasValidInterval = duration > 0.0L;
            result.intervalCount = result.hasValidInterval ? 1 : 0;
            result.burnPerHour = result.hasValidInterval
                ? interval.consumption.ToMajorUnits() * 3600.0L / duration
                : 0.0L;
            return result;
        }

        const auto windowIntervals = ClipToWindow(intervals, latestSample->timestamp, rateWindow);
        if (windowIntervals.empty())
        {
            return result;
        }

        result.hasValidInterval = true;
        result.intervalCount = windowIntervals.size();
        switch (algorithm)
        {
        case PredictionAlgorithm::SlidingAverage:
            result.burnPerHour = SlidingAverage(windowIntervals);
            break;
        case PredictionAlgorithm::ExponentialAverage:
            result.burnPerHour = ExponentialAverage(windowIntervals, rateWindow);
            break;
        case PredictionAlgorithm::RobustTrend:
            result.burnPerHour = RobustTrend(windowIntervals);
            break;
        case PredictionAlgorithm::LastValidSample:
            break;
        }
        result.burnPerHour = std::max(0.0L, result.burnPerHour);
        return result;
    }
}
