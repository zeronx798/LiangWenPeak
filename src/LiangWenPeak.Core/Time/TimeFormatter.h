#pragma once

#include "BeijingTime.h"
#include "../Pricing/PricingScheduleService.h"

#include <chrono>
#include <string>

namespace liangwenpeak::time
{
    [[nodiscard]] std::wstring FormatClock(BeijingTime const& beijingTime);
    [[nodiscard]] std::wstring FormatDuration(std::chrono::seconds duration);
    [[nodiscard]] std::wstring FormatPeriodRange(pricing::PeriodRange const& range);
    [[nodiscard]] std::wstring FormatCnyBalance(double balance);
    [[nodiscard]] std::wstring FormatLastUpdated(std::chrono::seconds elapsed);
}

