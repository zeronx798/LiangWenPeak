#include "pch.h"
#include "StateProfile.h"

#include <algorithm>
#include <cwctype>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace liangwenpeak::services
{
    namespace
    {
        constexpr wchar_t TestRunIdVariable[] = L"LIANGWENPEAK_TEST_RUN_ID";
        constexpr wchar_t TestPortableRootVariable[] = L"LIANGWENPEAK_TEST_PORTABLE_ROOT";

        std::optional<std::wstring> ReadEnvironmentVariable(wchar_t const* const name)
        {
            ::SetLastError(ERROR_SUCCESS);
            const auto required = ::GetEnvironmentVariableW(name, nullptr, 0);
            if (required == 0)
            {
                if (::GetLastError() == ERROR_ENVVAR_NOT_FOUND)
                {
                    return std::nullopt;
                }
                return std::wstring{};
            }

            std::vector<wchar_t> value(required);
            const auto written = ::GetEnvironmentVariableW(
                name,
                value.data(),
                static_cast<DWORD>(value.size()));
            if (written == 0 || written >= value.size())
            {
                throw std::runtime_error("Unable to read LiangWenPeak test profile environment");
            }
            return std::wstring{ value.data(), written };
        }

        bool IsCanonicalGuid(std::wstring const& value) noexcept
        {
            if (value.size() != 36)
            {
                return false;
            }
            for (std::size_t index = 0; index < value.size(); ++index)
            {
                const bool separator = index == 8 || index == 13 || index == 18 || index == 23;
                if (separator ? value[index] != L'-' : std::iswxdigit(value[index]) == 0)
                {
                    return false;
                }
            }
            return true;
        }

        std::wstring NormalizeGuid(std::wstring value)
        {
            if (!IsCanonicalGuid(value))
            {
                throw std::invalid_argument(
                    "LIANGWENPEAK_TEST_RUN_ID must be a canonical GUID");
            }
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t const character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
            return value;
        }

        std::wstring Lowercase(std::wstring value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](wchar_t const character)
            {
                return static_cast<wchar_t>(std::towlower(character));
            });
            return value;
        }
    }

    StateProfile StateProfile::FromEnvironment()
    {
        const auto runId = ReadEnvironmentVariable(TestRunIdVariable);
        const auto portableRoot = ReadEnvironmentVariable(TestPortableRootVariable);
        if (!runId && !portableRoot)
        {
            return StateProfile{};
        }
        if (!runId || !portableRoot || runId->empty() || portableRoot->empty())
        {
            throw std::invalid_argument(
                "LiangWenPeak test profile requires both run ID and portable root");
        }
        return StateProfile{ NormalizeGuid(*runId), std::filesystem::path{ *portableRoot } };
    }

    StateProfile::StateProfile(
        std::wstring testRunId,
        std::filesystem::path testPortableRoot)
        : m_isTest(true),
          m_testRunId(std::move(testRunId)),
          m_testPortableRoot(std::move(testPortableRoot))
    {
        if (!m_testPortableRoot.is_absolute())
        {
            throw std::invalid_argument("LiangWenPeak test portable root must be absolute");
        }
        m_testPortableRoot = std::filesystem::absolute(m_testPortableRoot).lexically_normal();

        const auto normalizedRoot = Lowercase(m_testPortableRoot.wstring());
        if (normalizedRoot.find(m_testRunId) == std::wstring::npos)
        {
            throw std::invalid_argument(
                "LiangWenPeak test portable root must contain its test run GUID");
        }

        m_registrySubkey = L"Software\\LiangWenPeak.Tests\\" + m_testRunId;
        m_apiCredentialResource = L"LiangWenPeak.Test." + m_testRunId + L".ApiKey";
        m_apiCredentialUserName = L"api-key." + m_testRunId;
        m_historyCredentialResource =
            L"LiangWenPeak.Test." + m_testRunId + L".HistoryIdentity";
        m_historyCredentialUserName = L"history-identity-secret." + m_testRunId;
        m_appUserModelId = L"zeronx798.LiangWenPeak.Test." + m_testRunId;
        m_shortcutFileName = L"LiangWenPeak Test " + m_testRunId + L".lnk";
    }

    bool StateProfile::IsTest() const noexcept
    {
        return m_isTest;
    }

    std::wstring const& StateProfile::TestRunId() const noexcept
    {
        return m_testRunId;
    }

    std::wstring const& StateProfile::RegistrySubkey() const noexcept
    {
        return m_registrySubkey;
    }

    std::wstring const& StateProfile::ApiCredentialResource() const noexcept
    {
        return m_apiCredentialResource;
    }

    std::wstring const& StateProfile::ApiCredentialUserName() const noexcept
    {
        return m_apiCredentialUserName;
    }

    std::wstring const& StateProfile::HistoryCredentialResource() const noexcept
    {
        return m_historyCredentialResource;
    }

    std::wstring const& StateProfile::HistoryCredentialUserName() const noexcept
    {
        return m_historyCredentialUserName;
    }

    std::wstring const& StateProfile::AppUserModelId() const noexcept
    {
        return m_appUserModelId;
    }

    std::wstring const& StateProfile::ShortcutFileName() const noexcept
    {
        return m_shortcutFileName;
    }

    std::filesystem::path const& StateProfile::TestPortableRoot() const noexcept
    {
        return m_testPortableRoot;
    }
}
