#pragma once

#include <functional>
#include <string_view>

void VerifyBalanceStatistics(
    std::function<void(bool, std::string_view)> const& expect);
