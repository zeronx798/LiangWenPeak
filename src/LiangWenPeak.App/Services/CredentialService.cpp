#include "pch.h"
#include "CredentialService.h"

#include <string_view>

namespace liangwenpeak::services
{
    namespace
    {
        winrt::hstring Trim(winrt::hstring const& value)
        {
            const std::wstring_view source{ value };
            const auto first = source.find_first_not_of(L" \t\r\n");
            if (first == std::wstring_view::npos)
            {
                return {};
            }
            const auto last = source.find_last_not_of(L" \t\r\n");
            return winrt::hstring{ source.substr(first, last - first + 1) };
        }
    }

    std::optional<winrt::hstring> CredentialService::TryGetApiKey() const noexcept
    {
        try
        {
            const winrt::Windows::Security::Credentials::PasswordVault vault;
            auto credential = vault.Retrieve(ResourceName, UserName);
            credential.RetrievePassword();
            auto password = credential.Password();
            if (!password.empty())
            {
                return password;
            }
        }
        catch (...)
        {
            // Missing or unavailable credentials are treated as an unconfigured app.
        }
        return std::nullopt;
    }

    bool CredentialService::HasApiKey() const noexcept
    {
        return TryGetApiKey().has_value();
    }

    void CredentialService::SaveApiKey(winrt::hstring const& apiKey) const
    {
        const auto trimmed = Trim(apiKey);
        if (trimmed.empty())
        {
            ClearApiKey();
            return;
        }

        ClearApiKey();
        const winrt::Windows::Security::Credentials::PasswordVault vault;
        vault.Add(winrt::Windows::Security::Credentials::PasswordCredential{ ResourceName, UserName, trimmed });
    }

    void CredentialService::ClearApiKey() const noexcept
    {
        try
        {
            const winrt::Windows::Security::Credentials::PasswordVault vault;
            auto credential = vault.Retrieve(ResourceName, UserName);
            vault.Remove(credential);
        }
        catch (...)
        {
            // Removing a credential that does not exist is already the desired state.
        }
    }
}

