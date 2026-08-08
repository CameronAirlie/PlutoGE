#pragma once

#include "PlutoGE/render/postprocess/ShaderPostProcessEffect.h"
#include "PlutoGE/render/surfacecache/SurfaceCache.h"
#include "PlutoGE/render/surfacecache/SurfaceCacheAtlas.h"
#include "PlutoGE/render/surfacecache/SurfaceCardSpatialIndex.h"
#include "PlutoGE/render/surfacecache/SurfaceCardGpuIndex.h"
#include "PlutoGE/render/visibility/IWorldVisibilityProvider.h"

#include <memory>
#include <array>
#include <vector>

namespace PlutoGE::render
{
    class Shader;
    class Mesh;
    class Material;

    class SurfaceCacheGIEffect final : public ShaderPostProcessEffect
    {
    public:
        SurfaceCacheGIEffect();
        ~SurfaceCacheGIEffect() override;
        void Initialize() override;
        void Apply(const PostProcessContext &context) override;
        std::string GetTypeName() const override { return "SurfaceCacheGI"; }
        std::string GetDisplayName() const override { return "Surface Cache Global Illumination"; }
        std::vector<PostProcessParameter> GetParameters() const override;
        void SetParameters(const std::vector<PostProcessParameter> &parameters) override;
        const SurfaceCacheStats &GetStats() const { return m_stats; }

    private:
        struct ResidentCard
        {
            SurfaceCard card;
            Mesh *mesh = nullptr;
            Material *material = nullptr;
            glm::mat4 model{1.0f};
        };
        void RebuildCards(const PostProcessContext &context);
        void CapturePendingCards(const PostProcessContext &context);
        void ResolveAccumulatedRadiance();
        void UpdateVisibility(const PostProcessContext &context);
        void EnsureGatherTarget(int width, int height);
        void RenderGatherInputs(const PostProcessContext &context);
        void RenderIndirectGather(const PostProcessContext &context);
        void ResetGatherHistory();
        void ResolveGatherHistory(const PostProcessContext &context);
        std::size_t ComputeSceneSignature(const PostProcessContext &context) const;
        std::size_t ComputeLightingSignature(const PostProcessContext &context) const;

        std::unique_ptr<SurfaceCacheAtlas> m_atlas;
        std::unique_ptr<RenderTarget> m_gatherTarget;
        std::unique_ptr<RenderTarget> m_indirectGatherTarget;
        std::array<std::unique_ptr<RenderTarget>, 2> m_gatherHistoryTargets;
        std::array<std::unique_ptr<RenderTarget>, 2> m_gatherHistoryMetadataTargets;
        Shader *m_captureShader = nullptr;
        Shader *m_debugShader = nullptr;
        Shader *m_radianceResolveShader = nullptr;
        Shader *m_cardLookupDebugShader = nullptr;
        Shader *m_gatherShader = nullptr;
        Shader *m_indirectGatherShader = nullptr;
        Shader *m_gatherTemporalShader = nullptr;
        Shader *m_gatherMetadataShader = nullptr;
        std::vector<ResidentCard> m_cards;
        SurfaceCardSpatialIndex m_cardSpatialIndex;
        SurfaceCardGpuIndex m_cardGpuIndex;
        SurfaceCacheStats m_stats;
        WorldVisibilitySnapshot m_visibilitySnapshot;
        int m_visibilityStatus = 0;
        glm::vec2 m_debugAtlasExtent{1.0f};
        std::size_t m_sceneSignature = 0;
        std::size_t m_lightingSignature = 0;
        std::size_t m_nextCapture = 0;
        int m_atlasSize = 2048;
        int m_texelsPerUnit = 32;
        int m_minCardResolution = 16;
        int m_maxCardResolution = 256;
        int m_captureBudget = 12;
        int m_debugView = 0;
        int m_maxCaptureLights = 8;
        int m_visibilityCascadeCount = 3;
        int m_screenRayCount = 4;
        int m_screenTraceSteps = 16;
        float m_screenTraceDistance = 4.0f;
        float m_gatherTemporalBlend = 0.88f;
        float m_radianceIntensity = 1.0f;
        float m_environmentIntensity = 1.0f;
        float m_radianceClamp = 32.0f;
        float m_radianceHistoryBlend = 0.85f;
        bool m_directionalShadows = true;
        bool m_staticGeometryOnly = true;
        bool m_cacheLayoutDirty = true;
        bool m_hasRadianceHistory = false;
        std::uint8_t m_radianceHistoryIndex = 0;
        std::uint8_t m_gatherHistoryIndex = 0;
        bool m_hasGatherHistory = false;
        glm::mat4 m_previousGatherView{1.0f};
        glm::vec3 m_previousGatherCameraPosition{0.0f};
        bool m_hasPreviousGatherCamera = false;
    };
}
