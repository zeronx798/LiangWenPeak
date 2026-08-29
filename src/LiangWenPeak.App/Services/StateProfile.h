#pragma once

#include <filesystem>
#include <string>

namespace liangwenpeak::services
{
    class StateProfile final
    {
    public:
        [[nodiscard]] static StateProfile FromEnvironment();

        [[nodiscard]] bool IsTest() const noexcept;
        [[nodiscard]] std::wstring const& TestRunId() const noexcept;
        [[nodiscard]] std::wstring const& RegistrySubkey() const noexcept;
        [[nodiscard]] std::wstring const& ApiCredentialResource() const noexcept;
        [[nodiscard]] std::wstring const& ApiCredentialUserName() const noexcept;
        [[nodiscard]] std::wstring const& HistoryCredentialResource() const noexcept;
        [[nodiscard]] std::wstring const& HistoryCredentialUserName() const noexcept;
        [[nodiscard]] std::wstring const& AppUserModelId() const noexcept;
        [[nodiscard]] std::wstring const& ShortcutFileName() const noexcept;
        [[nodiscard]] std::filesystem::path const& TestPortableRoot() const noexcept;

    private:
        StateProfile() = default;
        StateProfile(std::wstring testRunId, std::filesystem::path testPortableRoot);

        bool m_isTest = false;
        std::wstring m_testRunId;
        std::wstring m_registrySubkey = L"Software\\LiangWenPeak";
        std::wstring m_apiCredentialResource = L"LiangWenPeak.DeepSeekApi";
        std::wstring m_apiCredentialUserName = L"api-key";
        std::wstring m_historyCredentialResource = L"LiangWenPeak.DeepSeekApi";
        std::wstring m_historyCredentialUserName = L"history-identity-secret";
        std::wstring m_appUserModelId = L"zeronx798.LiangWenPeak";
        std::wstring m_shortcutFileName = L"LiangWenPeak.lnk";
        std::filesystem::path m_testPortableRoot;
    };
}
