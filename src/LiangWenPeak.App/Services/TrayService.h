#pragma once

#include <shellapi.h>

namespace liangwenpeak::services
{
    class TrayService final
    {
    public:
        static constexpr UINT CallbackMessage = WM_APP + 42;
        TrayService() = default;
        ~TrayService()
        {
            Remove();
            if (m_icon) ::DestroyIcon(m_icon);
        }
        TrayService(TrayService const&) = delete;
        TrayService& operator=(TrayService const&) = delete;

        [[nodiscard]] bool Show(HWND window) noexcept
        {
            if (m_visible) return true;
            m_data = {};
            m_data.cbSize = sizeof(m_data);
            m_data.hWnd = window;
            m_data.uID = 1;
            m_data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
            m_data.uCallbackMessage = CallbackMessage;
            if (!m_icon)
            {
                const auto dpi = ::GetDpiForWindow(window);
                m_icon = static_cast<HICON>(::LoadImageW(::GetModuleHandleW(nullptr),
                    MAKEINTRESOURCEW(101), IMAGE_ICON,
                    ::GetSystemMetricsForDpi(SM_CXSMICON, dpi),
                    ::GetSystemMetricsForDpi(SM_CYSMICON, dpi), 0));
                if (!m_icon) return false;
            }
            m_data.hIcon = m_icon;
            wcscpy_s(m_data.szTip, L"LiangWenPeak");
            if (!::Shell_NotifyIconW(NIM_ADD, &m_data)) return false;
            m_data.uVersion = NOTIFYICON_VERSION_4;
            if (!::Shell_NotifyIconW(NIM_SETVERSION, &m_data))
            {
                static_cast<void>(::Shell_NotifyIconW(NIM_DELETE, &m_data));
                return false;
            }
            m_visible = true;
            return true;
        }

        void Remove() noexcept
        {
            if (m_visible)
            {
                static_cast<void>(::Shell_NotifyIconW(NIM_DELETE, &m_data));
                m_visible = false;
            }
        }

        [[nodiscard]] bool RecoverAfterExplorerRestart() noexcept
        {
            if (!m_visible) return true;
            m_visible = false;
            return Show(m_data.hWnd);
        }

    private:
        NOTIFYICONDATAW m_data{};
        HICON m_icon{};
        bool m_visible = false;
    };
}
