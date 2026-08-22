#pragma once

#include <optional>
#include <winrt/base.h>

namespace liangwenpeak::services
{
    class CredentialService final
    {
    public:
        [[nodiscard]] std::optional<winrt::hstring> TryGetApiKey() const noexcept;
        [[nodiscard]] bool HasApiKey() const noexcept;
        void SaveApiKey(winrt::hstring const& apiKey) const;
        void ClearApiKey() const noexcept;

    private:
        static constexpr wchar_t ResourceName[] = L"LiangWenPeak.DeepSeekApi";
        static constexpr wchar_t UserName[] = L"api-key";
    };
}

