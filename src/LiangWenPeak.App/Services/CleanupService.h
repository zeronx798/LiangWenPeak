#pragma once

#include <memory>
#include <vector>
#include <winrt/base.h>

namespace liangwenpeak::services
{
    class CredentialService;
    class HistoryIdentityService;
    class NotificationService;
    class SettingsService;

    struct CleanupResult
    {
        bool succeeded = false;
        bool notificationHistoryCleared = false;
        std::vector<winrt::hstring> failedSteps;

        [[nodiscard]] winrt::hstring UserMessage() const;
    };

    class CleanupService final
    {
    public:
        CleanupService(
            std::shared_ptr<SettingsService> settingsService,
            std::shared_ptr<CredentialService> credentialService,
            std::shared_ptr<HistoryIdentityService> historyIdentityService,
            NotificationService& notificationService);

        [[nodiscard]] CleanupResult Execute() noexcept;

    private:
        std::shared_ptr<SettingsService> m_settingsService;
        std::shared_ptr<CredentialService> m_credentialService;
        std::shared_ptr<HistoryIdentityService> m_historyIdentityService;
        NotificationService& m_notificationService;
    };
}
