#include "pch.h"
#include "HistoryIdentityService.h"

#include "Balance/SeriesIdentity.h"

#include <algorithm>

namespace liangwenpeak::services
{
    std::vector<std::uint8_t> HistoryIdentityService::GetOrCreateSecret() const
    {
        const winrt::Windows::Security::Credentials::PasswordVault vault;
        try
        {
            auto credential = vault.Retrieve(ResourceName, UserName);
            credential.RetrievePassword();
            auto secret = balance::DecodeHex(winrt::to_string(credential.Password()));
            if (secret.size() == balance::HistoryIdentitySecretSize)
            {
                return secret;
            }
            vault.Remove(credential);
        }
        catch (...)
        {
            // Missing or invalid identity material is replaced with a new local secret.
        }

        auto secret = balance::GenerateHistoryIdentitySecret();
        const auto encoded = winrt::to_hstring(balance::EncodeHex(secret));
        vault.Add(winrt::Windows::Security::Credentials::PasswordCredential{
            ResourceName,
            UserName,
            encoded });
        return secret;
    }

    std::string HistoryIdentityService::GetSeriesId(winrt::hstring const& apiKey) const
    {
        auto secret = GetOrCreateSecret();
        auto key = winrt::to_string(apiKey);
        try
        {
            auto result = balance::ComputeSeriesId(secret, key);
            std::fill(secret.begin(), secret.end(), std::uint8_t{ 0 });
            std::fill(key.begin(), key.end(), '\0');
            return result;
        }
        catch (...)
        {
            std::fill(secret.begin(), secret.end(), std::uint8_t{ 0 });
            std::fill(key.begin(), key.end(), '\0');
            throw;
        }
    }
}
