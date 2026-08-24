#pragma once

#include "Balance/BalanceModels.h"

#include <vector>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>

namespace liangwenpeak::services
{
    class DeepSeekClient final
    {
    public:
        [[nodiscard]] winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> GetBalanceResponseAsync(
            winrt::hstring apiKey) const;
        [[nodiscard]] static std::vector<balance::BalanceValue> ParseBalanceResponse(
            winrt::hstring const& responseText);
        [[nodiscard]] winrt::Windows::Foundation::IAsyncOperation<double> GetCnyBalanceAsync(winrt::hstring apiKey) const;

    private:
        winrt::Windows::Web::Http::HttpClient m_httpClient;
    };
}
