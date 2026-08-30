#pragma once

#include "PlutoGE/render/rhi/Types.h"

#include <filesystem>
#include <string_view>

namespace PlutoGE::render
{
    // Loads the backend artifacts generated from a single Slang module. Keeping
    // artifact discovery here prevents editor/runtime shader paths diverging.
    class ShaderArtifactLibrary
    {
    public:
        explicit ShaderArtifactLibrary(std::filesystem::path root = DefaultRoot());

        [[nodiscard]] rhi::GraphicsPipelineDescriptor::ShaderCode Load(
            std::string_view module, std::string_view stage) const;
        [[nodiscard]] static std::filesystem::path DefaultRoot();

    private:
        std::filesystem::path m_root;
    };
}
