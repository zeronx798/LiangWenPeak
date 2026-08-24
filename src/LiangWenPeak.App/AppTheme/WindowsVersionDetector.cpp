#include "pch.h"
#include "WindowsVersionDetector.h"

#include <winternl.h>

namespace liangwenpeak::apptheme
{
    bool WindowsVersionDetector::IsWindows11OrGreater() noexcept
    {
        using RtlGetVersionFunction = NTSTATUS(WINAPI*)(PRTL_OSVERSIONINFOW);

        const auto module = ::GetModuleHandleW(L"ntdll.dll");
        if (module == nullptr)
        {
            return false;
        }

        const auto rtlGetVersion = reinterpret_cast<RtlGetVersionFunction>(
            ::GetProcAddress(module, "RtlGetVersion"));
        if (rtlGetVersion == nullptr)
        {
            return false;
        }

        RTL_OSVERSIONINFOW version{};
        version.dwOSVersionInfoSize = sizeof(version);
        if (rtlGetVersion(&version) < 0)
        {
            return false;
        }

        return IsWindows11Version(version.dwMajorVersion, version.dwBuildNumber);
    }
}
