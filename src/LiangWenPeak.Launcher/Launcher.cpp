#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    constexpr wchar_t LauncherTitle[] = L"LiangWenPeak";
    constexpr std::int64_t MaximumCurrentFileSize = 4096;
    constexpr std::size_t MaximumVersionLength = 128;

    class UniqueHandle final
    {
    public:
        explicit UniqueHandle(HANDLE value = nullptr) noexcept : m_value(value)
        {
        }

        ~UniqueHandle()
        {
            if (m_value != nullptr && m_value != INVALID_HANDLE_VALUE)
            {
                CloseHandle(m_value);
            }
        }

        UniqueHandle(UniqueHandle const&) = delete;
        UniqueHandle& operator=(UniqueHandle const&) = delete;

        [[nodiscard]] HANDLE Get() const noexcept
        {
            return m_value;
        }

        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_value != nullptr && m_value != INVALID_HANDLE_VALUE;
        }

    private:
        HANDLE m_value;
    };

    [[nodiscard]] bool IsAsciiWhitespace(char value) noexcept
    {
        return value == ' ' || value == '\t' || value == '\r' || value == '\n' ||
            value == '\v' || value == '\f';
    }

    [[nodiscard]] bool IsAllowedVersionCharacter(char value) noexcept
    {
        return (value >= '0' && value <= '9') ||
            (value >= 'A' && value <= 'Z') ||
            (value >= 'a' && value <= 'z') ||
            value == '.' || value == '-' || value == '+';
    }

    [[nodiscard]] std::wstring TrimSystemMessage(std::wstring message)
    {
        while (!message.empty())
        {
            wchar_t const last = message.back();
            if (last != L' ' && last != L'\t' && last != L'\r' && last != L'\n')
            {
                break;
            }
            message.pop_back();
        }
        return message;
    }

    [[nodiscard]] std::wstring FormatSystemError(DWORD error)
    {
        wchar_t buffer[512]{};
        DWORD const length = FormatMessageW(
            FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr,
            error,
            0,
            buffer,
            static_cast<DWORD>(std::size(buffer)),
            nullptr);

        if (length == 0)
        {
            return L"Windows error " + std::to_wstring(error);
        }

        return TrimSystemMessage(std::wstring(buffer, length));
    }

    int ShowFailure(std::wstring_view detail, DWORD error = ERROR_SUCCESS)
    {
        std::wstring message = L"LiangWenPeak failed to start.\n\n";
        message.append(detail.data(), detail.size());

        if (error != ERROR_SUCCESS)
        {
            message += L"\n\n";
            message += FormatSystemError(error);
        }

        MessageBoxW(
            nullptr,
            message.c_str(),
            LauncherTitle,
            MB_OK | MB_ICONERROR | MB_SETFOREGROUND | MB_TASKMODAL);
        return 1;
    }

    [[nodiscard]] bool TryGetLauncherRoot(std::wstring& root, DWORD& error)
    {
        std::vector<wchar_t> path(32768);
        DWORD const length = GetModuleFileNameW(
            nullptr,
            path.data(),
            static_cast<DWORD>(path.size()));

        if (length == 0)
        {
            error = GetLastError();
            return false;
        }

        if (static_cast<std::size_t>(length) == path.size())
        {
            error = ERROR_INSUFFICIENT_BUFFER;
            return false;
        }

        std::wstring modulePath(path.data(), length);
        std::size_t const separator = modulePath.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
        {
            error = ERROR_BAD_PATHNAME;
            return false;
        }

        root.assign(modulePath, 0, separator);
        return true;
    }

    [[nodiscard]] std::wstring JoinPath(std::wstring_view parent, std::wstring_view child)
    {
        std::wstring result(parent);
        if (!result.empty() && result.back() != L'\\' && result.back() != L'/')
        {
            result.push_back(L'\\');
        }
        result.append(child.data(), child.size());
        return result;
    }

    [[nodiscard]] bool TryReadCurrentFile(
        std::wstring const& path,
        std::string& contents,
        DWORD& error,
        bool& tooLarge)
    {
        tooLarge = false;
        UniqueHandle const file(CreateFileW(
            path.c_str(),
            GENERIC_READ,
            FILE_SHARE_READ,
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr));

        if (!file.IsValid())
        {
            error = GetLastError();
            return false;
        }

        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.Get(), &size))
        {
            error = GetLastError();
            return false;
        }

        if (size.QuadPart < 0 || size.QuadPart > MaximumCurrentFileSize)
        {
            tooLarge = true;
            return false;
        }

        contents.resize(static_cast<std::size_t>(size.QuadPart));
        if (contents.empty())
        {
            return true;
        }

        DWORD bytesRead = 0;
        if (!ReadFile(
                file.Get(),
                contents.data(),
                static_cast<DWORD>(contents.size()),
                &bytesRead,
                nullptr))
        {
            error = GetLastError();
            return false;
        }

        if (static_cast<std::size_t>(bytesRead) != contents.size())
        {
            error = ERROR_READ_FAULT;
            return false;
        }

        return true;
    }

    [[nodiscard]] bool TryParseVersion(std::string const& contents, std::wstring& version)
    {
        std::size_t begin = 0;
        if (contents.size() >= 3 &&
            static_cast<unsigned char>(contents[0]) == 0xEF &&
            static_cast<unsigned char>(contents[1]) == 0xBB &&
            static_cast<unsigned char>(contents[2]) == 0xBF)
        {
            begin = 3;
        }

        while (begin < contents.size() && IsAsciiWhitespace(contents[begin]))
        {
            ++begin;
        }

        std::size_t end = contents.size();
        while (end > begin && IsAsciiWhitespace(contents[end - 1]))
        {
            --end;
        }

        std::size_t const length = end - begin;
        if (length == 0 || length > MaximumVersionLength)
        {
            return false;
        }

        for (std::size_t index = begin; index < end; ++index)
        {
            if (!IsAllowedVersionCharacter(contents[index]))
            {
                return false;
            }
        }

        version.clear();
        version.reserve(length);
        for (std::size_t index = begin; index < end; ++index)
        {
            version.push_back(static_cast<wchar_t>(contents[index]));
        }
        return true;
    }

    [[nodiscard]] bool IsDirectory(std::wstring const& path) noexcept
    {
        DWORD const attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    }

    [[nodiscard]] bool IsFile(std::wstring const& path) noexcept
    {
        DWORD const attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) == 0;
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX);

    std::wstring launcherRoot;
    DWORD error = ERROR_SUCCESS;
    if (!TryGetLauncherRoot(launcherRoot, error))
    {
        return ShowFailure(L"Could not determine the launcher directory.", error);
    }

    std::wstring const currentPath = JoinPath(launcherRoot, L"current.txt");
    std::string currentContents;
    bool tooLarge = false;
    if (!TryReadCurrentFile(currentPath, currentContents, error, tooLarge))
    {
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
        {
            return ShowFailure(L"current.txt was not found.");
        }
        if (tooLarge)
        {
            return ShowFailure(L"current.txt is too large.");
        }
        return ShowFailure(L"Could not read current.txt.", error);
    }

    std::wstring version;
    if (!TryParseVersion(currentContents, version))
    {
        return ShowFailure(L"current.txt contains an invalid version.");
    }

    std::wstring const applicationDirectoryName = L"app-" + version;
    std::wstring const applicationDirectory = JoinPath(launcherRoot, applicationDirectoryName);
    if (!IsDirectory(applicationDirectory))
    {
        return ShowFailure(L"Application version directory not found:\n" + applicationDirectoryName);
    }

    std::wstring const applicationRelativePath =
        applicationDirectoryName + L"\\LiangWenPeak.App.exe";
    std::wstring const applicationPath = JoinPath(applicationDirectory, L"LiangWenPeak.App.exe");
    if (!IsFile(applicationPath))
    {
        return ShowFailure(L"Application executable not found:\n" + applicationRelativePath);
    }

    std::wstring commandLine = L"\"" + applicationPath + L"\"";
    std::vector<wchar_t> mutableCommandLine(commandLine.begin(), commandLine.end());
    mutableCommandLine.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = static_cast<DWORD>(sizeof(startupInfo));
    PROCESS_INFORMATION processInfo{};
    if (!CreateProcessW(
            applicationPath.c_str(),
            mutableCommandLine.data(),
            nullptr,
            nullptr,
            FALSE,
            0,
            nullptr,
            applicationDirectory.c_str(),
            &startupInfo,
            &processInfo))
    {
        return ShowFailure(L"Could not start LiangWenPeak.App.exe.", GetLastError());
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return 0;
}
