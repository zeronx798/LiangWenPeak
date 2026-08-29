#include "pch.h"
#include "NotificationService.h"

namespace
{
    winrt::hstring FailureWithHresult(wchar_t const* const context, HRESULT const error)
    {
        wchar_t code[16]{};
        swprintf_s(code, L"0x%08X", static_cast<unsigned int>(error));
        std::wstring message = L"测试通知发送失败：";
        message += context;
        message += L"（";
        message += code;
        message += L"）";
        return winrt::hstring{ message };
    }

}

namespace liangwenpeak::services
{
    using winrt::Windows::UI::Notifications::ToastNotification;
    using winrt::Windows::UI::Notifications::ToastNotificationManager;
    using winrt::Windows::UI::Notifications::ToastTemplateType;

    NotificationService::NotificationService(
        StateProfile const& profile,
        std::filesystem::path launcherPath)
        : m_identity(profile, std::move(launcherPath))
    {
    }

    bool NotificationService::SetCurrentProcessIdentity(
        StateProfile const& profile,
        winrt::hstring& failureMessage) noexcept
    {
        return NotificationIdentityService::SetCurrentProcessIdentity(profile, failureMessage);
    }

    bool NotificationService::Initialize() noexcept
    {
        m_processIdentityReady = NotificationIdentityService::SetCurrentProcessIdentity(
            m_identity.Profile(),
            m_lastFailureMessage);
        m_identityReady = false;
        return m_processIdentityReady;
    }

    bool NotificationService::EnsureIdentity() noexcept
    {
        if (!m_processIdentityReady && !Initialize())
        {
            return false;
        }
        if (!m_identity.EnsureShortcut())
        {
            m_lastFailureMessage = m_identity.LastFailureMessage();
            m_identityReady = false;
            return false;
        }
        m_identityReady = true;
        m_lastFailureMessage = {};
        return true;
    }

    void NotificationService::Shutdown() noexcept
    {
        m_identityReady = false;
    }

    bool NotificationService::IsAvailable() const noexcept
    {
        return m_identityReady;
    }

    bool NotificationService::Show(
        winrt::hstring const& title,
        winrt::hstring const& body) noexcept
    {
        if (!EnsureIdentity())
        {
            return false;
        }

        wchar_t const* failureContext = L"Windows Toast notifier 创建失败";
        try
        {
            auto notifier = ToastNotificationManager::CreateToastNotifier(
                winrt::hstring{ m_identity.Profile().AppUserModelId() });
            failureContext = L"Windows Toast 内容创建失败";
            auto document = ToastNotificationManager::GetTemplateContent(
                ToastTemplateType::ToastText02);
            const auto textNodes = document.GetElementsByTagName(L"text");
            if (textNodes.Length() < 2)
            {
                m_lastFailureMessage =
                    L"测试通知发送失败：Windows Toast 模板不完整";
                return false;
            }
            textNodes.Item(0).AppendChild(document.CreateTextNode(title));
            textNodes.Item(1).AppendChild(document.CreateTextNode(body));

            ToastNotification notification{ document };
            // Classic desktop Toast requires an in-process activation handler
            // even though LiangWenPeak 1.1.2 intentionally has no activation UI.
            [[maybe_unused]] const auto activatedToken = notification.Activated(
                [](ToastNotification const&, winrt::Windows::Foundation::IInspectable const&) noexcept
                {
                });
            failureContext = L"Windows Toast 过期时间设置失败";
            notification.ExpirationTime(
                winrt::Windows::Foundation::DateTime::clock::now() + std::chrono::minutes{ 15 });
            failureContext = L"Windows Toast 平台拒绝投递";
            notifier.Show(notification);
            m_lastFailureMessage = {};
            return true;
        }
        catch (winrt::hresult_error const& error)
        {
            m_lastFailureMessage = FailureWithHresult(
                failureContext,
                error.code());
            return false;
        }
        catch (...)
        {
            m_lastFailureMessage =
                L"测试通知发送失败：Windows Toast 平台拒绝投递";
            return false;
        }
    }

    bool NotificationService::ShowScheduled(
        notifications::NotificationEvent const& event) noexcept
    {
        try
        {
            const auto content = notifications::GetNotificationContent(event);
            return Show(winrt::hstring{ content.title }, winrt::hstring{ content.body });
        }
        catch (...)
        {
            return false;
        }
    }

    bool NotificationService::ShowTest() noexcept
    {
        return Show(
            L"LiangWenPeak 通知测试",
            L"通知功能工作正常。");
    }

    bool NotificationService::ClearNotificationHistory() noexcept
    {
        return m_identity.ClearNotificationHistory();
    }

    bool NotificationService::RemoveOwnedShortcut() noexcept
    {
        m_identityReady = false;
        const bool removed = m_identity.RemoveOwnedShortcut();
        if (!removed)
        {
            m_lastFailureMessage = m_identity.LastFailureMessage();
        }
        return removed;
    }

    winrt::hstring NotificationService::LastFailureMessage() const
    {
        return m_lastFailureMessage;
    }
}
