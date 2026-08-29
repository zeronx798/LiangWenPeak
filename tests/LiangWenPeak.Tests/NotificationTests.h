#pragma once

#include <functional>
#include <string_view>

void VerifyNotifications(
    std::function<void(bool, std::string_view)> const& expect);
