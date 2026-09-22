#pragma once

#include <filesystem>
#include <string>
#include <windows.h>

namespace liangwenpeak::services
{
    class InstanceCoordinator final
    {
    public:
        InstanceCoordinator() = default;
        ~InstanceCoordinator() noexcept;
        InstanceCoordinator(InstanceCoordinator const&) = delete;
        InstanceCoordinator& operator=(InstanceCoordinator const&) = delete;

        [[nodiscard]] bool TryAcquire(std::filesystem::path const& canonicalDataRoot);
        [[nodiscard]] bool RedirectToPrimary() const noexcept;
        void PublishWindow(HWND windowHandle);

        [[nodiscard]] static UINT ActivationMessage() noexcept;

    private:
        struct SharedState
        {
            DWORD processId;
            UINT64 windowHandle;
        };

        void Close() noexcept;

        std::wstring m_nameSuffix;
        HANDLE m_mutex{};
        HANDLE m_mapping{};
        HANDLE m_readyEvent{};
        SharedState* m_sharedState{};
        bool m_primary = false;
    };
}
