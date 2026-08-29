#include "pch.h"
#include "NotificationIdentityService.h"

#include <knownfolders.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <cwchar>
#include <iterator>
#include <system_error>
#include <utility>

#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "shell32.lib")

namespace liangwenpeak::services
{
    namespace
    {
        struct ShortcutDetails
        {
            std::filesystem::path target;
            std::wstring appUserModelId;
        };

        class CoTaskMemString final
        {
        public:
            explicit CoTaskMemString(PWSTR value) noexcept : m_value(value)
            {
            }

            ~CoTaskMemString()
            {
                ::CoTaskMemFree(m_value);
            }

            CoTaskMemString(CoTaskMemString const&) = delete;
            CoTaskMemString& operator=(CoTaskMemString const&) = delete;

        private:
            PWSTR m_value;
        };

        winrt::hstring FailureWithHresult(wchar_t const* const context, HRESULT const error)
        {
            wchar_t code[16]{};
            swprintf_s(code, L"0x%08X", static_cast<unsigned int>(error));
            std::wstring message = context;
            message += L"（";
            message += code;
            message += L"）";
            return winrt::hstring{ message };
        }

        winrt::com_ptr<IShellLinkW> CreateShellLink()
        {
            winrt::com_ptr<IShellLinkW> shellLink;
            winrt::check_hresult(::CoCreateInstance(
                CLSID_ShellLink,
                nullptr,
                CLSCTX_INPROC_SERVER,
                __uuidof(IShellLinkW),
                shellLink.put_void()));
            return shellLink;
        }

        bool EquivalentPath(
            std::filesystem::path const& left,
            std::filesystem::path const& right) noexcept
        {
            try
            {
                const auto normalizedLeft = std::filesystem::absolute(left).lexically_normal().wstring();
                const auto normalizedRight = std::filesystem::absolute(right).lexically_normal().wstring();
                return _wcsicmp(normalizedLeft.c_str(), normalizedRight.c_str()) == 0;
            }
            catch (...)
            {
                return false;
            }
        }

        bool ReadShortcut(
            std::filesystem::path const& shortcutPath,
            ShortcutDetails& details,
            HRESULT& error) noexcept
        {
            try
            {
                auto shellLink = CreateShellLink();
                auto persistFile = shellLink.as<IPersistFile>();
                error = persistFile->Load(shortcutPath.c_str(), STGM_READ);
                if (FAILED(error))
                {
                    return false;
                }

                wchar_t target[32768]{};
                WIN32_FIND_DATAW findData{};
                error = shellLink->GetPath(
                    target,
                    static_cast<int>(std::size(target)),
                    &findData,
                    SLGP_RAWPATH);
                if (FAILED(error) || target[0] == L'\0')
                {
                    return false;
                }
                details.target = target;

                auto propertyStore = shellLink.as<IPropertyStore>();
                PROPVARIANT value{};
                ::PropVariantInit(&value);
                error = propertyStore->GetValue(PKEY_AppUserModel_ID, &value);
                if (SUCCEEDED(error) && value.vt == VT_LPWSTR && value.pwszVal != nullptr)
                {
                    details.appUserModelId = value.pwszVal;
                }
                else if (SUCCEEDED(error))
                {
                    error = HRESULT_FROM_WIN32(ERROR_INVALID_DATA);
                }
                ::PropVariantClear(&value);
                return SUCCEEDED(error);
            }
            catch (winrt::hresult_error const& exception)
            {
                error = exception.code();
                return false;
            }
            catch (...)
            {
                error = E_FAIL;
                return false;
            }
        }

        HRESULT WriteShortcut(
            std::filesystem::path const& shortcutPath,
            std::filesystem::path const& launcherPath,
            std::wstring const& appUserModelId) noexcept
        {
            try
            {
                auto shellLink = CreateShellLink();
                winrt::check_hresult(shellLink->SetPath(launcherPath.c_str()));
                winrt::check_hresult(shellLink->SetArguments(L""));
                winrt::check_hresult(shellLink->SetWorkingDirectory(
                    launcherPath.parent_path().c_str()));
                winrt::check_hresult(shellLink->SetDescription(L"LiangWenPeak"));
                winrt::check_hresult(shellLink->SetIconLocation(launcherPath.c_str(), 0));

                auto propertyStore = shellLink.as<IPropertyStore>();
                PROPVARIANT value{};
                ::PropVariantInit(&value);
                winrt::check_hresult(::InitPropVariantFromString(appUserModelId.c_str(), &value));
                const auto setResult = propertyStore->SetValue(PKEY_AppUserModel_ID, value);
                ::PropVariantClear(&value);
                winrt::check_hresult(setResult);
                winrt::check_hresult(propertyStore->Commit());

                auto persistFile = shellLink.as<IPersistFile>();
                winrt::check_hresult(persistFile->Save(shortcutPath.c_str(), TRUE));
                return S_OK;
            }
            catch (winrt::hresult_error const& exception)
            {
                return exception.code();
            }
            catch (...)
            {
                return E_FAIL;
            }
        }
    }

    NotificationIdentityService::NotificationIdentityService(
        StateProfile profile,
        std::filesystem::path launcherPath)
        : m_profile(std::move(profile)),
          m_launcherPath(std::filesystem::absolute(launcherPath).lexically_normal())
    {
    }

    bool NotificationIdentityService::SetCurrentProcessIdentity(
        StateProfile const& profile,
        winrt::hstring& failureMessage) noexcept
    {
        const auto result = ::SetCurrentProcessExplicitAppUserModelID(
            profile.AppUserModelId().c_str());
        if (FAILED(result))
        {
            failureMessage = FailureWithHresult(
                L"通知进程身份设置失败",
                result);
            return false;
        }
        failureMessage = {};
        return true;
    }

    bool NotificationIdentityService::EnsureShortcut() noexcept
    {
        try
        {
            if (!std::filesystem::is_regular_file(m_launcherPath))
            {
                m_lastFailureMessage = L"通知系统集成创建失败：Launcher 不存在";
                return false;
            }

            const auto shortcutPath = ResolveShortcutPath();
            const bool exists = std::filesystem::exists(shortcutPath);
            if (exists)
            {
                ShortcutDetails current;
                HRESULT readResult{};
                if (!ReadShortcut(shortcutPath, current, readResult))
                {
                    m_lastFailureMessage = FailureWithHresult(
                        L"通知系统集成检查失败",
                        readResult);
                    return false;
                }
                if (current.appUserModelId != m_profile.AppUserModelId())
                {
                    m_lastFailureMessage =
                        L"通知系统集成创建失败：同名快捷方式不属于 LiangWenPeak";
                    return false;
                }
                if (_wcsicmp(
                        current.target.filename().c_str(),
                        m_launcherPath.filename().c_str()) != 0)
                {
                    m_lastFailureMessage =
                        L"通知系统集成修复失败：快捷方式目标不属于 LiangWenPeak";
                    return false;
                }
                if (EquivalentPath(current.target, m_launcherPath))
                {
                    m_lastFailureMessage = {};
                    return true;
                }
            }

            const auto writeResult = WriteShortcut(
                shortcutPath,
                m_launcherPath,
                m_profile.AppUserModelId());
            if (FAILED(writeResult))
            {
                m_lastFailureMessage = FailureWithHresult(
                    L"通知系统集成创建或修复失败",
                    writeResult);
                return false;
            }

            ::SHChangeNotify(
                exists ? SHCNE_UPDATEITEM : SHCNE_CREATE,
                SHCNF_PATHW,
                shortcutPath.c_str(),
                nullptr);
            m_lastFailureMessage = {};
            return true;
        }
        catch (winrt::hresult_error const& error)
        {
            m_lastFailureMessage = FailureWithHresult(
                L"通知系统集成创建或修复失败",
                error.code());
            return false;
        }
        catch (...)
        {
            m_lastFailureMessage = L"通知系统集成创建或修复失败";
            return false;
        }
    }

    bool NotificationIdentityService::RemoveOwnedShortcut() noexcept
    {
        try
        {
            const auto shortcutPath = ResolveShortcutPath();
            if (!std::filesystem::exists(shortcutPath))
            {
                m_lastFailureMessage = {};
                return true;
            }

            ShortcutDetails current;
            HRESULT readResult{};
            if (!ReadShortcut(shortcutPath, current, readResult))
            {
                m_lastFailureMessage = FailureWithHresult(
                    L"通知系统集成清理失败",
                    readResult);
                return false;
            }
            if (current.appUserModelId != m_profile.AppUserModelId()
                || _wcsicmp(
                    current.target.filename().c_str(),
                    m_launcherPath.filename().c_str()) != 0)
            {
                m_lastFailureMessage =
                    L"通知系统集成清理失败：同名快捷方式不属于 LiangWenPeak";
                return false;
            }

            std::error_code error;
            const bool removed = std::filesystem::remove(shortcutPath, error);
            if (!removed || error)
            {
                m_lastFailureMessage = L"通知系统集成清理失败";
                return false;
            }
            ::SHChangeNotify(SHCNE_DELETE, SHCNF_PATHW, shortcutPath.c_str(), nullptr);
            m_lastFailureMessage = {};
            return true;
        }
        catch (...)
        {
            m_lastFailureMessage = L"通知系统集成清理失败";
            return false;
        }
    }

    bool NotificationIdentityService::ClearNotificationHistory() noexcept
    {
        try
        {
            winrt::Windows::UI::Notifications::ToastNotificationManager::History().Clear(
                winrt::hstring{ m_profile.AppUserModelId() });
            return true;
        }
        catch (...)
        {
            // History cleanup is best effort for an unpackaged desktop AUMID.
            return false;
        }
    }

    StateProfile const& NotificationIdentityService::Profile() const noexcept
    {
        return m_profile;
    }

    std::filesystem::path const& NotificationIdentityService::LauncherPath() const noexcept
    {
        return m_launcherPath;
    }

    std::filesystem::path NotificationIdentityService::ShortcutPath() const
    {
        return ResolveShortcutPath();
    }

    winrt::hstring NotificationIdentityService::LastFailureMessage() const
    {
        return m_lastFailureMessage;
    }

    std::filesystem::path NotificationIdentityService::ResolveShortcutPath() const
    {
        if (!m_shortcutPath.empty())
        {
            return m_shortcutPath;
        }

        PWSTR programsPath{};
        winrt::check_hresult(::SHGetKnownFolderPath(
            FOLDERID_Programs,
            KF_FLAG_DEFAULT,
            nullptr,
            &programsPath));
        [[maybe_unused]] const CoTaskMemString release{ programsPath };
        m_shortcutPath = std::filesystem::path{ programsPath } / m_profile.ShortcutFileName();
        return m_shortcutPath;
    }
}
