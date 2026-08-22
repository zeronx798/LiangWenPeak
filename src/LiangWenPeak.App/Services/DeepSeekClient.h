#pragma once

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Web.Http.h>

namespace liangwenpeak::services
{
    class DeepSeekClient final
    {
    public:
        [[nodiscard]] winrt::Windows::Foundation::IAsyncOperation<double> GetCnyBalanceAsync(winrt::hstring apiKey) const;

    private:
        winrt::Windows::Web::Http::HttpClient m_httpClient;
    };
}

