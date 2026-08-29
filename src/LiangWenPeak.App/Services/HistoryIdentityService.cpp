#include "pch.h"
#include "HistoryIdentityService.h"

#include "Balance/SeriesIdentity.h"

#include <algorithm>

namespace liangwenpeak::services
{
    HistoryIdentityService::HistoryIdentityService(StateProfile const& profile)
        : m_resourceName(profile.HistoryCredentialResource()),
          m_userName(profile.HistoryCredentialUserName())
    {
    }

    std::vector<std::uint8_t> HistoryIdentityService::GetOrCreateSecret() const
    {
        const winrt::Windows::Security::Credentials::PasswordVault vault;
        try
        {
            auto credential = vault.Retrieve(m_resourceName, m_userName);
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
            m_resourceName,
            m_userName,
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

    bool HistoryIdentityService::ClearSecret() const noexcept
    {
        try
        {
            const winrt::Windows::Security::Credentials::PasswordVault vault;
            auto credential = vault.Retrieve(m_resourceName, m_userName);
            vault.Remove(credential);
            return true;
        }
        catch (winrt::hresult_error const& error)
        {
            return error.code() == HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
        }
        catch (...)
        {
            return false;
        }
    }
}
