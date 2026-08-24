#pragma once

#include <filesystem>
#include <optional>

namespace liangwenpeak::services
{
    class DeploymentPathService final
    {
    public:
        explicit DeploymentPathService(
            std::optional<std::filesystem::path> dataRootOverride = std::nullopt);

        [[nodiscard]] std::filesystem::path ExecutablePath() const;
        [[nodiscard]] std::filesystem::path DataRoot() const;

    private:
        std::optional<std::filesystem::path> m_dataRootOverride;
    };
}
