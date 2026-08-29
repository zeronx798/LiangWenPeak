#pragma once

#include "StateProfile.h"

#include <filesystem>
#include <optional>

namespace liangwenpeak::services
{
    class DeploymentPathService final
    {
    public:
        explicit DeploymentPathService(
            std::optional<std::filesystem::path> dataRootOverride = std::nullopt);
        explicit DeploymentPathService(StateProfile const& profile);

        [[nodiscard]] std::filesystem::path ExecutablePath() const;
        [[nodiscard]] std::filesystem::path DeploymentRoot() const;
        [[nodiscard]] std::filesystem::path LauncherPath() const;
        [[nodiscard]] std::filesystem::path DataRoot() const;

    private:
        std::optional<std::filesystem::path> m_dataRootOverride;
        std::optional<std::filesystem::path> m_testPortableRoot;
    };
}
