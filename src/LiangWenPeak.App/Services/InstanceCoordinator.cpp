#include "pch.h"
#include "InstanceCoordinator.h"

#include "Balance/DeploymentPaths.h"

#include <stdexcept>

namespace liangwenpeak::services
{
    namespace
    {
        constexpr wchar_t GlobalObjectPrefix[] = L"Global\\zeronx798.LiangWenPeak.";
        constexpr wchar_t LocalObjectPrefix[] = L"Local\\zeronx798.LiangWenPeak.";
        constexpr wchar_t ActivationMessageName[] =
            L"zeronx798.LiangWenPeak.ActivateDataRootInstance.v1";

        std::wstring ObjectName(
            wchar_t const* const prefix,
            wchar_t const* const kind,
            std::wstring const& suffix)
        {
            return std::wstring{ prefix } + kind + L'.' + suffix;
        }
    }

    InstanceCoordinator::~InstanceCoordinator() noexcept
    {
        Close();
    }

    bool InstanceCoordinator::TryAcquire(std::filesystem::path const& canonicalDataRoot)
    {
        Close();
        const auto key = balance::BuildDataRootInstanceKey(canonicalDataRoot);
        const auto separator = key.find_last_of(L'.');
        m_nameSuffix = separator == std::wstring::npos ? key : key.substr(separator + 1);

        const auto mutexName = ObjectName(GlobalObjectPrefix, L"Mutex", m_nameSuffix);
        m_mutex = ::CreateMutexW(nullptr, FALSE, mutexName.c_str());
        if (m_mutex == nullptr)
        {
            throw std::runtime_error("Unable to create logical-instance mutex");
        }
        if (::GetLastError() == ERROR_ALREADY_EXISTS)
        {
            m_primary = false;
            return false;
        }

        const auto mappingName = ObjectName(LocalObjectPrefix, L"State", m_nameSuffix);
        m_mapping = ::CreateFileMappingW(
            INVALID_HANDLE_VALUE,
            nullptr,
            PAGE_READWRITE,
            0,
            static_cast<DWORD>(sizeof(SharedState)),
            mappingName.c_str());
        if (m_mapping == nullptr)
        {
            throw std::runtime_error("Unable to create logical-instance state mapping");
        }
        m_sharedState = static_cast<SharedState*>(::MapViewOfFile(
            m_mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedState)));
        if (m_sharedState == nullptr)
        {
            throw std::runtime_error("Unable to map logical-instance state");
        }
        *m_sharedState = {};

        const auto eventName = ObjectName(LocalObjectPrefix, L"Ready", m_nameSuffix);
        m_readyEvent = ::CreateEventW(nullptr, TRUE, FALSE, eventName.c_str());
        if (m_readyEvent == nullptr)
        {
            throw std::runtime_error("Unable to create logical-instance ready event");
        }
        m_primary = true;
        return true;
    }

    bool InstanceCoordinator::RedirectToPrimary() const noexcept
    {
        if (m_primary || m_nameSuffix.empty())
        {
            return false;
        }

        const auto eventName = ObjectName(LocalObjectPrefix, L"Ready", m_nameSuffix);
        const HANDLE readyEvent = ::OpenEventW(SYNCHRONIZE, FALSE, eventName.c_str());
        if (readyEvent == nullptr)
        {
            return false;
        }
        const DWORD waitResult = ::WaitForSingleObject(readyEvent, 15000);
        ::CloseHandle(readyEvent);
        if (waitResult != WAIT_OBJECT_0)
        {
            return false;
        }

        const auto mappingName = ObjectName(LocalObjectPrefix, L"State", m_nameSuffix);
        const HANDLE mapping = ::OpenFileMappingW(FILE_MAP_READ, FALSE, mappingName.c_str());
        if (mapping == nullptr)
        {
            return false;
        }
        const auto state = static_cast<SharedState const*>(::MapViewOfFile(
            mapping, FILE_MAP_READ, 0, 0, sizeof(SharedState)));
        if (state == nullptr)
        {
            ::CloseHandle(mapping);
            return false;
        }
        const SharedState snapshot = *state;
        ::UnmapViewOfFile(state);
        ::CloseHandle(mapping);

        const auto windowHandle = reinterpret_cast<HWND>(
            static_cast<ULONG_PTR>(snapshot.windowHandle));
        DWORD windowProcessId{};
        static_cast<void>(::GetWindowThreadProcessId(windowHandle, &windowProcessId));
        if (snapshot.processId == 0 || windowHandle == nullptr
            || !::IsWindow(windowHandle) || windowProcessId != snapshot.processId)
        {
            return false;
        }

        static_cast<void>(::AllowSetForegroundWindow(snapshot.processId));
        return ::PostMessageW(windowHandle, ActivationMessage(), 0, 0) != FALSE;
    }

    void InstanceCoordinator::PublishWindow(HWND const windowHandle)
    {
        if (!m_primary || m_sharedState == nullptr || m_readyEvent == nullptr
            || windowHandle == nullptr)
        {
            throw std::logic_error("Logical-instance window cannot be published");
        }
        m_sharedState->processId = ::GetCurrentProcessId();
        m_sharedState->windowHandle = static_cast<UINT64>(
            reinterpret_cast<ULONG_PTR>(windowHandle));
        if (::SetEvent(m_readyEvent) == FALSE)
        {
            throw std::runtime_error("Unable to signal logical-instance readiness");
        }
    }

    UINT InstanceCoordinator::ActivationMessage() noexcept
    {
        static const UINT message = ::RegisterWindowMessageW(ActivationMessageName);
        return message;
    }

    void InstanceCoordinator::Close() noexcept
    {
        if (m_sharedState != nullptr)
        {
            ::UnmapViewOfFile(m_sharedState);
            m_sharedState = nullptr;
        }
        if (m_readyEvent != nullptr)
        {
            ::CloseHandle(m_readyEvent);
            m_readyEvent = nullptr;
        }
        if (m_mapping != nullptr)
        {
            ::CloseHandle(m_mapping);
            m_mapping = nullptr;
        }
        if (m_mutex != nullptr)
        {
            ::CloseHandle(m_mutex);
            m_mutex = nullptr;
        }
        m_primary = false;
        m_nameSuffix.clear();
    }
}
