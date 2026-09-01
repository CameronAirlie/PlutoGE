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

    BasicRendererShaderPackage ShaderArtifactLibrary::LoadBasicRendererPackage() const
    {
        BasicRendererShaderPackage result{
            .vertex = Load("BasicLit", "vertex"),
            .fragment = Load("BasicLit", "fragment"),
            .shadowVertex = Load("DirectionalShadow", "vertex"),
            .shadowFragment = Load("DirectionalShadow", "fragment")};
        const auto addPostProcess = [&](BasicPostProcessEffectType type, std::string_view module)
        {
            result.postProcess[static_cast<std::size_t>(type)] = {
                .vertex = Load(module, "vertex"), .fragment = Load(module, "fragment")};
        };
        addPostProcess(BasicPostProcessEffectType::ToneMapping, "ToneMapping");
        addPostProcess(BasicPostProcessEffectType::GammaCorrection, "GammaCorrection");
        addPostProcess(BasicPostProcessEffectType::FXAA, "FXAA");
        addPostProcess(BasicPostProcessEffectType::ColorGrading, "ColorGrading");
        addPostProcess(BasicPostProcessEffectType::ChromaticAberration, "ChromaticAberration");
        addPostProcess(BasicPostProcessEffectType::LensFlare, "LensFlare");
        addPostProcess(BasicPostProcessEffectType::MotionBlur, "MotionBlur");
        addPostProcess(BasicPostProcessEffectType::DepthOfField, "DepthOfField");
        constexpr std::array<std::string_view, 4> bloomModules{
            "BloomPrefilter", "BloomDownsample", "BloomUpsample", "BloomComposite"};
        for (std::size_t index = 0; index < bloomModules.size(); ++index)
            result.bloom[index] = {.vertex = Load(bloomModules[index], "vertex"),
                                   .fragment = Load(bloomModules[index], "fragment")};
        return result;
    }

    std::filesystem::path ShaderArtifactLibrary::DefaultRoot()
    {
        return PLUTO_RHI_SHADER_DIR;
    }
}
