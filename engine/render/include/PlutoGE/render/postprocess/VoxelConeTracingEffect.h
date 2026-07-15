#pragma once

#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

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
        static constexpr std::size_t kCascadeCount = 3;
        static constexpr std::size_t kDirectionCount = 6;

        struct VoxelizationJob
        {
            RenderCommand command;
            std::size_t voxelLod = 0;
        };

        struct VoxelCascade
        {
            std::array<unsigned int, kDirectionCount> radianceVolumes{};
            unsigned int pendingBaseVolume = 0;
            unsigned int accumulationRG = 0;
            unsigned int accumulationBCount = 0;
            unsigned int accumulationOpacity = 0;
            unsigned int framebuffer = 0;
            glm::vec3 origin{0.0f};
            glm::vec3 pendingOrigin{0.0f};
            float size = 0.0f;
            std::vector<VoxelizationJob> jobs;
            std::size_t jobIndex = 0;
            std::size_t pendingSceneSignature = 0;
            std::size_t pendingLightSignature = 0;
            std::size_t lastSceneSignature = 0;
            std::size_t lastLightSignature = 0;
            unsigned long long lastVoxelizedFrame = ~0ull;
            bool hasVolume = false;
            bool rebuildInProgress = false;
        };

        void EnsureResources(int width, int height);
        void BeginVoxelization(std::size_t cascadeIndex, const glm::vec3 &volumeOrigin,
                               std::size_t sceneSignature, std::size_t lightSignature,
                               const std::vector<RenderCommand> &commands);
        bool VoxelizeChunk(std::size_t cascadeIndex, const PostProcessContext &context);
        void GenerateDirectionalMips(VoxelCascade &cascade);
        void ReleaseVolume();
        void ResetHistory();

        Shader *m_voxelizationShader = nullptr;
        Shader *m_voxelResolveShader = nullptr;
        Shader *m_directionalMipShader = nullptr;
        Shader *m_coneTraceShader = nullptr;
        Shader *m_temporalResolveShader = nullptr;
        Shader *m_historyMetadataShader = nullptr;
        std::unique_ptr<RenderTarget> m_indirectTarget;
        std::array<std::unique_ptr<RenderTarget>, 2> m_historyColorTargets;
        std::array<std::unique_ptr<RenderTarget>, 2> m_historyMetadataTargets;
        std::array<VoxelCascade, kCascadeCount> m_cascades;
        int m_allocatedResolution = 0;
        std::size_t m_activeCascadeCount = kCascadeCount;
        int m_resolution = 64;
        int m_coneCount = 5;
        int m_voxelizationLodBias = 2;
        int m_voxelizationCommandBudget = 8;
        int m_updateInterval = 1;
        float m_volumeSize = 48.0f;
        float m_intensity = 1.0f;
        float m_aperture = 0.55f;
        float m_maxDistance = 24.0f;
        float m_normalBias = 1.5f;
        float m_temporalBlend = 0.92f;
        float m_historyDepthThreshold = 0.03f;
        float m_historyNormalThreshold = 0.9f;
        glm::mat4 m_previousView{1.0f};
        glm::vec3 m_previousCameraPosition{0.0f};
        std::size_t m_nextCascadeToUpdate = 0;
        std::uint8_t m_historyIndex = 0;
        bool m_hasHistory = false;
        bool m_hasPreviousCameraPosition = false;
        bool m_indirectOnly = false;
    };
}
