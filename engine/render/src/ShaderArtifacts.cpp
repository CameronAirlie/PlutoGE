#include "PlutoGE/render/ShaderArtifacts.h"

#include <fstream>
#include <iterator>

namespace PlutoGE::render
{
    ShaderArtifactLibrary::ShaderArtifactLibrary(std::filesystem::path root)
        : m_root(std::move(root))
    {
    }

    rhi::GraphicsPipelineDescriptor::ShaderCode ShaderArtifactLibrary::Load(
        std::string_view module, std::string_view stage) const
    {
        const std::string stem = std::string(module) + "." + std::string(stage);
        rhi::GraphicsPipelineDescriptor::ShaderCode result;

        std::ifstream text(m_root / (stem + ".glsl"), std::ios::binary);
        if (text)
            result.glsl.assign(std::istreambuf_iterator<char>(text), {});

        std::ifstream binary(m_root / (stem + ".spv"), std::ios::binary | std::ios::ate);
        if (!binary)
            return result;
        const auto byteSize = binary.tellg();
        if (byteSize <= 0 || byteSize % static_cast<std::streamoff>(sizeof(std::uint32_t)) != 0)
            return result;
        result.spirv.resize(static_cast<std::size_t>(byteSize) / sizeof(std::uint32_t));
        binary.seekg(0);
        binary.read(reinterpret_cast<char *>(result.spirv.data()), byteSize);
        if (!binary)
            result.spirv.clear();
        return result;
    }

    std::filesystem::path ShaderArtifactLibrary::DefaultRoot()
    {
        return PLUTO_RHI_SHADER_DIR;
    }
}
