#pragma once

#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

#include <array>
#include <cstdint>
#include <memory>

#include <glm/glm.hpp>

namespace PlutoGE::render
{
    class Shader;

    class VoxelConeTracingEffect : public ShaderPostProcessEffect
    {
    public:
        ~VoxelConeTracingEffect() override;

        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        std::string GetTypeName() const override { return "VCTGI"; }
        std::string GetDisplayName() const override { return "Voxel Cone Traced GI"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        RenderTarget *GenerateResolvedIndirectLighting(const PostProcessContext &context, int width, int height);
        bool OutputsIndirectOnly() const { return m_indirectOnly; }

    private:
        void EnsureResources(int width, int height);
        void Voxelize(const PostProcessContext &context, const glm::vec3 &volumeOrigin);
        void ReleaseVolume();
        void ResetHistory();

        Shader *m_voxelizationShader = nullptr;
        Shader *m_coneTraceShader = nullptr;
        Shader *m_temporalResolveShader = nullptr;
        Shader *m_historyMetadataShader = nullptr;
        std::unique_ptr<RenderTarget> m_indirectTarget;
        std::array<std::unique_ptr<RenderTarget>, 2> m_historyColorTargets;
        std::array<std::unique_ptr<RenderTarget>, 2> m_historyMetadataTargets;
        unsigned int m_radianceVolume = 0;
        unsigned int m_voxelFramebuffer = 0;
        int m_allocatedResolution = 0;
        int m_resolution = 64;
        int m_coneCount = 5;
        int m_voxelizationLodBias = 2;
        int m_updateInterval = 1;
        float m_volumeSize = 48.0f;
        float m_intensity = 1.0f;
        float m_aperture = 0.55f;
        float m_maxDistance = 24.0f;
        float m_normalBias = 1.5f;
        float m_temporalBlend = 0.92f;
        float m_historyDepthThreshold = 0.03f;
        float m_historyNormalThreshold = 0.9f;
        glm::vec3 m_volumeOrigin{0.0f};
        glm::mat4 m_previousView{1.0f};
        std::size_t m_lastSceneSignature = 0;
        std::size_t m_lastLightSignature = 0;
        unsigned long long m_lastVoxelizedFrame = ~0ull;
        bool m_hasVoxelVolume = false;
        std::uint8_t m_historyIndex = 0;
        bool m_hasHistory = false;
        bool m_indirectOnly = false;
    };
}
