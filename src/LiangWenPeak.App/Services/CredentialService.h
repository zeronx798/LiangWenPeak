#pragma once

#include "StateProfile.h"

#include <optional>
#include <winrt/base.h>

namespace liangwenpeak::services
{
    class CredentialService final
    {
    public:
        explicit CredentialService(StateProfile const& profile);

        [[nodiscard]] std::optional<winrt::hstring> TryGetApiKey() const noexcept;
        [[nodiscard]] bool HasApiKey() const noexcept;
        void SaveApiKey(winrt::hstring const& apiKey) const;
        bool ClearApiKey() const noexcept;

    private:
        winrt::hstring m_resourceName;
        winrt::hstring m_userName;
    };
}
