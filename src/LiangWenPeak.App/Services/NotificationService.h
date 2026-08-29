#pragma once

#include "NotificationIdentityService.h"
#include "Notifications/NotificationScheduler.h"

#include <filesystem>

namespace liangwenpeak::services
{
    class NotificationService final
    {
    public:
        NotificationService(
            StateProfile const& profile,
            std::filesystem::path launcherPath);

        [[nodiscard]] static bool SetCurrentProcessIdentity(
            StateProfile const& profile,
            winrt::hstring& failureMessage) noexcept;

        [[nodiscard]] bool Initialize() noexcept;
        [[nodiscard]] bool EnsureIdentity() noexcept;
        void Shutdown() noexcept;

        [[nodiscard]] bool IsAvailable() const noexcept;
        [[nodiscard]] bool Show(
            winrt::hstring const& title,
            winrt::hstring const& body) noexcept;
        [[nodiscard]] bool ShowScheduled(
            notifications::NotificationEvent const& event) noexcept;
        [[nodiscard]] bool ShowTest() noexcept;

        [[nodiscard]] bool ClearNotificationHistory() noexcept;
        [[nodiscard]] bool RemoveOwnedShortcut() noexcept;
        [[nodiscard]] winrt::hstring LastFailureMessage() const;

    private:
        NotificationIdentityService m_identity;
        winrt::hstring m_lastFailureMessage;
        bool m_processIdentityReady = false;
        bool m_identityReady = false;
    };
}
