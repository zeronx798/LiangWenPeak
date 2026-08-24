#include "pch.h"
#include "DeepSeekClient.h"

#include <algorithm>
#include <set>
#include <string>

namespace liangwenpeak::services
{
    namespace
    {
        constexpr wchar_t BalanceEndpoint[] = L"https://api.deepseek.com/user/balance";

        bool IsValidCurrency(std::string const& currency) noexcept
        {
            return !currency.empty() && currency.size() <= 16
                && std::all_of(currency.begin(), currency.end(), [](char const character)
                {
                    return (character >= 'A' && character <= 'Z')
                        || (character >= 'a' && character <= 'z')
                        || (character >= '0' && character <= '9')
                        || character == '_' || character == '-' || character == '.';
                });
        }
    }

    winrt::Windows::Foundation::IAsyncOperation<winrt::hstring> DeepSeekClient::GetBalanceResponseAsync(
        winrt::hstring apiKey) const
    {
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
        co_return co_await response.Content().ReadAsStringAsync();
    }

    std::vector<balance::BalanceValue> DeepSeekClient::ParseBalanceResponse(
        winrt::hstring const& responseText)
    {
        using namespace winrt::Windows::Data::Json;

        const auto root = JsonObject::Parse(responseText);
        const auto balances = root.GetNamedArray(L"balance_infos");
        std::vector<balance::BalanceValue> result;
        std::set<std::string> currencies;
        result.reserve(balances.Size());
        for (auto const& item : balances)
        {
            const auto itemObject = item.GetObject();
            auto currency = winrt::to_string(itemObject.GetNamedString(L"currency"));
            const auto parsed = balance::DecimalAmount::TryParse(
                winrt::to_string(itemObject.GetNamedString(L"total_balance")));
            if (!IsValidCurrency(currency)
                || !parsed
                || parsed->ScaledValue() < 0
                || !currencies.insert(currency).second)
            {
                throw winrt::hresult_error{ E_UNEXPECTED, L"DeepSeek balance response is invalid." };
            }
            result.push_back(balance::BalanceValue{ std::move(currency), *parsed });
        }
        if (result.empty())
        {
            throw winrt::hresult_error{ E_UNEXPECTED, L"DeepSeek did not return any balances." };
        }
        return result;
    }

    winrt::Windows::Foundation::IAsyncOperation<double> DeepSeekClient::GetCnyBalanceAsync(winrt::hstring apiKey) const
    {
        const auto values = ParseBalanceResponse(co_await GetBalanceResponseAsync(std::move(apiKey)));
        for (auto const& value : values)
        {
            if (value.currency == "CNY")
            {
                co_return static_cast<double>(value.balance.ToMajorUnits());
            }
        }

        throw winrt::hresult_error{ E_UNEXPECTED, L"DeepSeek did not return a CNY balance." };
    }
}
