#include "pch.h"
#include "DeploymentPathService.h"

#include "Balance/DeploymentPaths.h"

#include <stdexcept>
#include <vector>

namespace liangwenpeak::services
{
    DeploymentPathService::DeploymentPathService(
        std::optional<std::filesystem::path> dataRootOverride)
        : m_dataRootOverride(std::move(dataRootOverride))
    {
    }

    std::filesystem::path DeploymentPathService::ExecutablePath() const
    {
        std::vector<wchar_t> buffer(512);
        for (;;)
        {
            const auto length = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
            if (length == 0)
            {
                throw std::runtime_error("Unable to resolve application executable path");
            }
            if (length < buffer.size() - 1)
            {
                return std::filesystem::path{ std::wstring_view{ buffer.data(), length } };
            }
            buffer.resize(buffer.size() * 2);
        }
    }

    std::filesystem::path DeploymentPathService::DataRoot() const
    {
        return balance::ResolveDataRoot(ExecutablePath(), m_dataRootOverride);
    }
}
