#pragma once

#include "StateProfile.h"

#include <filesystem>

namespace liangwenpeak::services
{
    class NotificationIdentityService final
    {
    public:
        NotificationIdentityService(
            StateProfile profile,
            std::filesystem::path launcherPath);

        [[nodiscard]] static bool SetCurrentProcessIdentity(
            StateProfile const& profile,
            winrt::hstring& failureMessage) noexcept;

        [[nodiscard]] bool EnsureShortcut() noexcept;
        [[nodiscard]] bool RemoveOwnedShortcut() noexcept;
        [[nodiscard]] bool ClearNotificationHistory() noexcept;

        [[nodiscard]] StateProfile const& Profile() const noexcept;
        [[nodiscard]] std::filesystem::path const& LauncherPath() const noexcept;
        [[nodiscard]] std::filesystem::path ShortcutPath() const;
        [[nodiscard]] winrt::hstring LastFailureMessage() const;

    private:
        [[nodiscard]] std::filesystem::path ResolveShortcutPath() const;

        StateProfile m_profile;
        std::filesystem::path m_launcherPath;
        mutable std::filesystem::path m_shortcutPath;
        mutable winrt::hstring m_lastFailureMessage;
    };
}
