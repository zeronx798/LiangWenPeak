#include "DeploymentPaths.h"

#include <string>

namespace liangwenpeak::balance
{
    std::filesystem::path ResolveDeploymentRoot(std::filesystem::path const& executablePath)
    {
        auto directory = std::filesystem::absolute(executablePath).lexically_normal().parent_path();
        const auto directoryName = directory.filename().wstring();
        if (directoryName.starts_with(L"app-") && directoryName.size() > 4)
        {
            return directory.parent_path();
        }

        auto candidate = directory;
        while (!candidate.empty())
        {
            if (std::filesystem::exists(candidate / "Version.props"))
            {
                return candidate;
            }
            const auto parent = candidate.parent_path();
            if (parent == candidate)
            {
                break;
            }
            candidate = parent;
        }
        return directory;
    }

    std::filesystem::path ResolveDataRoot(
        std::filesystem::path const& executablePath,
        std::optional<std::filesystem::path> const& overrideRoot)
    {
        if (overrideRoot)
        {
            return std::filesystem::absolute(*overrideRoot).lexically_normal();
        }
        return ResolveDeploymentRoot(executablePath) / "data";
    }
}
