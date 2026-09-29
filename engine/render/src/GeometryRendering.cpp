#include "PlutoGE/render/BasicRenderer.h"
#include <cmath>
#include <algorithm>

namespace PlutoGE::render
{
    void BasicRenderer::EnsureGeometryTargets(GeometryOutputLayout layout)
    {
        const auto ensure = [&](rhi::Texture &target, rhi::Format format, const char *name)
        {
            if (!target) target = rhi::Texture(*m_device, m_device->CreateTexture(
                {m_width, m_height, format, rhi::TextureUsage::ColorAttachment, name, true}));
        };
        if (layout != GeometryOutputLayout::Color) ensure(m_motionTarget, rhi::Format::R32G32Float, "G-buffer motion");
        if (layout >= GeometryOutputLayout::Surface)
        {
            ensure(m_normalTarget, rhi::Format::R8G8B8A8Unorm, "G-buffer normals");
            ensure(m_materialTarget, rhi::Format::R8G8B8A8Unorm, "G-buffer material");
            ensure(m_albedoTarget, rhi::Format::R8G8B8A8Unorm, "G-buffer albedo");
        }
        if (layout == GeometryOutputLayout::Diagnostics) ensure(m_debugTarget, rhi::Format::R16G16B16A16Float, "G-buffer debug");
    }

    BasicRenderer::DepthResources BasicRenderer::GeometryDepthResources(const BasicDraw &draw, bool instanced) const
    {
        const auto available = [](const auto &code) { return !code.glsl.empty() || !code.spirv.empty(); };
        if (draw.shaderGraphProgram) return DepthResources::Full;
        const auto &opaque = m_opaqueDepth[instanced ? 1 : 0];
        if (draw.alphaMode == 0 && available(opaque.vertex) && available(opaque.fragment))
            return DepthResources::Opaque;
        return available(m_standardFragment) && available(m_standardColorVertices[instanced ? 1 : 0])
            ? DepthResources::Alpha : DepthResources::Full;
    }

    rhi::PipelineHandle BasicRenderer::GeometryPipeline(const BasicDraw &draw, bool instanced,
        GeometryOutputLayout layout, bool depthOnly, bool prepassed)
    {
        const bool standard = !draw.shaderGraphProgram &&
            (!m_standardFragment.glsl.empty() || !m_standardFragment.spirv.empty());
        auto cull = draw.outlinePass ? rhi::CullMode::Front : rhi::CullMode::None;
        if (!draw.outlinePass && m_materialCulling && !draw.twoSided && draw.alphaMode < 2)
        {
            // Mixed-winding instance batches and graph deformation are conservative
            // fallbacks. Rigid mirrored models invert which face is front-facing.
            if (!instanced && !(draw.shaderGraphProgram && draw.shaderGraphProgram->data.header.z != 0))
            {
                const auto &model = draw.instanceModels && draw.instanceModels->size() == 1
                    ? draw.instanceModels->front() : draw.model;
                const float determinant = glm::determinant(glm::mat3(model));
                if (std::isfinite(determinant) && std::abs(determinant) > 1.e-12f)
                    cull = determinant < 0 ? rhi::CullMode::Front : rhi::CullMode::Back;
            }
        }
        const auto depthResources = depthOnly ? GeometryDepthResources(draw, instanced) : DepthResources::Full;
        const unsigned key = static_cast<unsigned>(layout) | (unsigned(instanced) << 2) | (unsigned(standard) << 3) |
            (unsigned(cull) << 4) | (unsigned(depthOnly) << 6) | (unsigned(prepassed) << 7) | (unsigned(depthResources) << 8);
        auto found = m_geometryPipelines.find(key);
        if (found != m_geometryPipelines.end()) return found->second.Get();
        auto descriptor = m_geometryDescriptors[instanced ? 1 : 0];
        descriptor.cullMode = cull;
        const auto &standardVertex = m_standardVertices[instanced ? 1 : 0];
        if (standard && (!standardVertex.glsl.empty() || !standardVertex.spirv.empty()))
            descriptor.vertexShader = standardVertex;
        const auto &colorVertex = (standard ? m_standardColorVertices : m_colorVertices)[instanced ? 1 : 0];
        if ((depthOnly || layout == GeometryOutputLayout::Color) &&
            (!colorVertex.glsl.empty() || !colorVertex.spirv.empty())) descriptor.vertexShader = colorVertex;
        if (depthOnly)
        {
            descriptor.colorFormats.clear();
            descriptor.colorFormat = rhi::Format::Undefined;
            descriptor.fragmentShader = m_coverageFragments[standard ? 1 : 0];
            if (depthResources != DepthResources::Full)
            {
                if (depthResources == DepthResources::Opaque)
                {
                    descriptor.vertexShader = m_opaqueDepth[instanced ? 1 : 0].vertex;
                    descriptor.fragmentShader = m_opaqueDepth[instanced ? 1 : 0].fragment;
                }
                // The standard coverage shader only consumes camera, object,
                // material constants and base alpha. Graph coverage retains the
                // full layout because its fragment evaluation can use lighting.
                std::erase_if(descriptor.resourceBindings, [depthResources](const auto &binding) {
                    return !((binding.type == rhi::ResourceBindingType::UniformBuffer &&
                        (binding.slot == 0 || binding.slot == 16 || binding.slot == 17 ||
                            (depthResources == DepthResources::Alpha && binding.slot == 8))) ||
                        (depthResources == DepthResources::Alpha && binding.type == rhi::ResourceBindingType::SampledTexture && binding.slot == 9));
                });
            }
        }
        else
        {
            if (layout < GeometryOutputLayout::Surface)
            {
                descriptor.colorFormats = {rhi::Format::R16G16B16A16Float};
                if (layout == GeometryOutputLayout::ColorMotion) descriptor.colorFormats.push_back(rhi::Format::R32G32Float);
                descriptor.fragmentShader = m_compactFragments[standard ? 1 : 0][static_cast<unsigned>(layout)];
            }
            else
            {
                if (layout == GeometryOutputLayout::Surface) descriptor.colorFormats.pop_back();
                if (standard) descriptor.fragmentShader = m_standardFragment;
            }
            descriptor.depthWrite = !prepassed;
        }
        descriptor.debugName = depthOnly ? "Geometry coverage" : "Geometry required outputs";
        auto pipeline = rhi::GraphicsPipeline(*m_device, m_device->CreateGraphicsPipeline(descriptor));
        const auto handle = pipeline.Get();
        m_geometryPipelines.emplace(key, std::move(pipeline));
        return handle;
    }

}
