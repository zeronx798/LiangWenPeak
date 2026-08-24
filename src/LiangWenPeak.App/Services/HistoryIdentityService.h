#pragma once

#include <string>
#include <vector>
#include <winrt/base.h>

namespace liangwenpeak::services
{
    class HistoryIdentityService final
    {
    public:
        [[nodiscard]] std::vector<std::uint8_t> GetOrCreateSecret() const;
        [[nodiscard]] std::string GetSeriesId(winrt::hstring const& apiKey) const;

    private:
        static constexpr wchar_t ResourceName[] = L"LiangWenPeak.DeepSeekApi";
        static constexpr wchar_t UserName[] = L"history-identity-secret";
    };
}
