#include "PlutoGE/render/BasicRenderer.h"
#include <cmath>

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
        const unsigned key = static_cast<unsigned>(layout) | (unsigned(instanced) << 2) | (unsigned(standard) << 3) |
            (unsigned(cull) << 4) | (unsigned(depthOnly) << 6) | (unsigned(prepassed) << 7);
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
