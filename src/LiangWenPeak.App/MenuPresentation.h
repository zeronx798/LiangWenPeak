#pragma once

#include <string_view>

namespace liangwenpeak::ui
{
    struct NotificationMenuPresentation
    {
        bool checked;
        std::wstring_view text;
        std::wstring_view glyph;
    };

    [[nodiscard]] constexpr NotificationMenuPresentation GetNotificationMenuPresentation(
        bool const enabled) noexcept
    {
        return enabled
            ? NotificationMenuPresentation{ true, L"\u5173\u95ed\u901a\u77e5", L"\uEA8F" }
            : NotificationMenuPresentation{ false, L"\u542f\u7528\u901a\u77e5", L"\uE7ED" };
    }
}
