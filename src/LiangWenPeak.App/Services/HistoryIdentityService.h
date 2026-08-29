#pragma once

#include "StateProfile.h"

#include <string>
#include <vector>
#include <winrt/base.h>

namespace liangwenpeak::services
{
    class HistoryIdentityService final
    {
    public:
        explicit HistoryIdentityService(StateProfile const& profile);

        [[nodiscard]] std::vector<std::uint8_t> GetOrCreateSecret() const;
        [[nodiscard]] std::string GetSeriesId(winrt::hstring const& apiKey) const;
        bool ClearSecret() const noexcept;

    private:
        winrt::hstring m_resourceName;
        winrt::hstring m_userName;
    };
}
