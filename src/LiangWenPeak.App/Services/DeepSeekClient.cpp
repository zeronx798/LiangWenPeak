#include "pch.h"
#include "DeepSeekClient.h"

#include <charconv>
#include <string>
#include <system_error>

namespace liangwenpeak::services
{
    namespace
    {
        constexpr wchar_t BalanceEndpoint[] = L"https://api.deepseek.com/user/balance";

        double ParseBalance(winrt::hstring const& text)
        {
            const std::string utf8 = winrt::to_string(text);
            double value = 0.0;
            const auto [end, error] = std::from_chars(utf8.data(), utf8.data() + utf8.size(), value);
            if (error != std::errc{} || end != utf8.data() + utf8.size())
            {
                throw winrt::hresult_error{ E_UNEXPECTED, L"DeepSeek balance response is invalid." };
            }
            return value;
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<double> DeepSeekClient::GetCnyBalanceAsync(winrt::hstring apiKey) const
    {
        using namespace winrt::Windows::Data::Json;
        using namespace winrt::Windows::Foundation;
        using namespace winrt::Windows::Web::Http;
        using namespace winrt::Windows::Web::Http::Headers;

        HttpRequestMessage request{ HttpMethod::Get(), Uri{ BalanceEndpoint } };
        request.Headers().Authorization(HttpCredentialsHeaderValue{ L"Bearer", apiKey });
        request.Headers().Accept().Append(HttpMediaTypeWithQualityHeaderValue{ L"application/json" });

        const auto response = co_await m_httpClient.SendRequestAsync(request);
        if (!response.IsSuccessStatusCode())
        {
            throw winrt::hresult_error{ E_FAIL, L"DeepSeek balance request failed." };
        }

        const auto responseText = co_await response.Content().ReadAsStringAsync();
        const auto root = JsonObject::Parse(responseText);
        const auto balances = root.GetNamedArray(L"balance_infos");
        for (auto const& item : balances)
        {
            const auto balance = item.GetObject();
            if (balance.GetNamedString(L"currency") == L"CNY")
            {
                co_return ParseBalance(balance.GetNamedString(L"total_balance"));
            }
        }

        throw winrt::hresult_error{ E_UNEXPECTED, L"DeepSeek did not return a CNY balance." };
    }
}

