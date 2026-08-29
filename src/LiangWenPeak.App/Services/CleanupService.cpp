#include "pch.h"
#include "CleanupService.h"

#include "CredentialService.h"
#include "HistoryIdentityService.h"
#include "NotificationService.h"
#include "SettingsService.h"

#include <utility>

namespace liangwenpeak::services
{
    winrt::hstring CleanupResult::UserMessage() const
    {
        if (succeeded)
        {
            return L"已彻底清理；本地余额历史 data/ 已保留";
        }

        std::wstring message = L"部分项目清理失败：";
        for (std::size_t index = 0; index < failedSteps.size(); ++index)
        {
            if (index != 0)
            {
                message += L"、";
            }
            message += failedSteps[index].c_str();
        }
        message += L"。已重新从实际状态加载，未恢复清理前内容。";
        return winrt::hstring{ message };
    }

    CleanupService::CleanupService(
        std::shared_ptr<SettingsService> settingsService,
        std::shared_ptr<CredentialService> credentialService,
        std::shared_ptr<HistoryIdentityService> historyIdentityService,
        NotificationService& notificationService)
        : m_settingsService(std::move(settingsService)),
          m_credentialService(std::move(credentialService)),
          m_historyIdentityService(std::move(historyIdentityService)),
          m_notificationService(notificationService)
    {
    }

    CleanupResult CleanupService::Execute() noexcept
    {
        CleanupResult result;

        m_notificationService.Shutdown();
        result.notificationHistoryCleared = m_notificationService.ClearNotificationHistory();

        if (!m_notificationService.RemoveOwnedShortcut())
        {
            result.failedSteps.emplace_back(L"通知系统集成");
        }
        if (!m_credentialService->ClearApiKey())
        {
            result.failedSteps.emplace_back(L"API Key");
        }
        if (!m_historyIdentityService->ClearSecret())
        {
            result.failedSteps.emplace_back(L"身份密钥");
        }
        if (!m_settingsService->DeleteAllSettings())
        {
            result.failedSteps.emplace_back(L"应用设置");
        }

        result.succeeded = result.failedSteps.empty();
        return result;
    }
}
