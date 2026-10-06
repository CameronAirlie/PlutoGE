#pragma once
#include "PlutoGE/render/GraphicsQuality.h"
#include "PlutoGE/render/RenderCommandView.h"

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/Camera.h"

#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>

namespace PlutoGE::scene
{
    class Scene;
    struct Light;
    struct ParticleCpuData;
}

namespace PlutoGE::render
{
    class RhiSkinningExecutor;
    class RhiSkinnedShadowBounds;
    struct RhiSkinningJob;
    class RhiDrawPreparationCache;
    struct RhiSceneTimingStats
    {
        float commandTranslationMs = 0.0f;
        float visiblePreparationMs = 0.0f;
        float shadowPreparationMs = 0.0f;
        float giPreparationMs = 0.0f;
        float batchingMs = 0.0f;
        std::size_t reusedDrawPackets = 0;
        std::size_t rebuiltDrawPackets = 0;
        float translationPreparationMs = 0.0f;
        float meshUploadMs = 0.0f;
        float skinningDeformationMs = 0.0f;
        float skinningUploadMs = 0.0f;
        std::size_t gpuSkinningDispatches = 0, gpuSkinningVertices = 0, gpuSkinningPaletteBytes = 0;
        float textureReadMs = 0.0f;
        float textureUploadMs = 0.0f;
        std::size_t meshUploadCount = 0;
        std::size_t skinningUpdateCount = 0;
        std::size_t skinningVertexCount = 0;
        std::size_t textureUploadCount = 0;
        float sceneSetupMs = 0.0f;
        float renderRecordingMs = 0.0f;
        float beginFrameMs = 0.0f;
        float shadowRecordingMs = 0.0f;
        float geometryRecordingMs = 0.0f;
        float postProcessRecordingMs = 0.0f;
        float temporalUpscalerMs = 0.0f;
        float submitMs = 0.0f;
        float totalMs = 0.0f;
        std::size_t visibleDrawCount = 0;
        std::size_t visibleInstanceCount = 0;
        std::size_t shadowCandidateCount = 0;
        std::size_t recordedGeometryDepthDrawCount = 0;
        std::uint32_t geometryColorOutputs = 0;
        std::size_t geometryParameterCreates = 0, geometryParameterReuses = 0;
        std::size_t recordedGeometryDrawCount = 0;
        std::size_t recordedGeometryInstanceCount = 0;
        std::size_t glassPanes = 0, glassSnapshots = 0;
        std::size_t glassDepthSnapshots = 0, glassSnapshotReuseHits = 0;
        std::size_t reusedOpaqueBatchGroups = 0, rebuiltOpaqueBatchGroups = 0;
        float glassBoundsCpuMs = 0, glassGroupingCpuMs = 0, glassDamageCpuMs = 0;
        float glassCopyRecordingCpuMs = 0, glassDrawRecordingCpuMs = 0;
        std::size_t sharedDrawPacketHits = 0;
        std::size_t glassFullFootprints = 0;
        std::uint64_t glassSnapshotPixels = 0;
        std::array<std::size_t, 5> glassBoundsReasons{}, glassGroupBoundaries{};
        std::size_t pointShadowAtlasUpdates = 0, pointShadowAtlasCacheHits = 0, pointShadowDraws = 0;
        std::size_t pointShadowFaceUpdates = 0, pointShadowFaceHits = 0, pointShadowObjectUploads = 0, pointShadowMaterialUploads = 0;
        std::array<std::size_t, 4> pointShadowInvalidations{};
        std::size_t materialPreparations = 0, materialPreparationHits = 0;
        std::size_t graphSpecializedDraws = 0, graphInterpretedDraws = 0;
        std::array<std::uint64_t, 4> geometryTriangles{};
        rhi::Extent2D renderSize{}, outputSize{};
        GeometryDiagnosticMode geometryDiagnosticMode = GeometryDiagnosticMode::None;
        OcclusionMode occlusionMode = OcclusionMode::Off;
        OcclusionStats occlusion;
        bool occlusionActive = false;
        float directionalShadowSoftness = 0.0f;
        unsigned skinningParticipants = 1;
        float skinningDispatchMs = 0, skinningCallerMs = 0, skinningWaitMs = 0, skinningMergeMs = 0;
        unsigned ssrSteps = 0, ssrRefinementSteps = 0;
        rhi::Extent2D ssrTraceSize{};
        std::size_t recordedShadowDrawCount = 0;
        std::size_t recordedShadowInstanceCount = 0;
        std::size_t shadowObjectUploadCount = 0;
        std::size_t shadowCascadeUpdateCount = 0;
        std::size_t shadowCascadeCacheHitCount = 0;
        std::size_t shadowCascadeTargetCount = 0;
        VirtualShadowStats virtualShadows;
        bool virtualShadowsActive = false;
        std::string directionalShadowStatus = "Disabled";
        std::array<std::size_t, 4> recordedShadowDrawsByCascade{};
    };

    struct TemporalUpscalerStatus
    {
        rhi::TemporalUpscalerOptions options;
        rhi::Extent2D renderSize;
        rhi::Extent2D outputSize;
        bool requested = false;
        bool active = false;
        // The upscaler is active but receives the full output resolution, so
        // it provides temporal reconstruction without a raster cost saving.
        bool nativeInput = false;
        std::string reason;
    };

    class Mesh;
    class Texture;
    class IPostProcessEffect;
    struct RenderCommand;

    // Backend-neutral scene translation and GPU asset cache shared by editor
    // and runtime hosts. Pixel acquisition is injected so this layer never
    // depends on OpenGL readback or a particular asset decoder.
    class RhiSceneRenderer
    {
    public:
        bool SetGraphicsQuality(const GraphicsQuality &quality) noexcept
        {
            if (!quality.IsValid()) return false;
            if (m_graphicsQuality != quality) ResetTemporalHistory();
            m_graphicsQuality = quality;
            return true;
        }
        RhiSceneRenderer();
        ~RhiSceneRenderer();
        using TexturePixelReader = std::function<std::vector<std::byte>(const Texture &)>;

        bool Initialize(rhi::IRenderDevice &device, const BasicRendererShaderPackage &shaders);
        void Shutdown();
        void SetImmediateTextureUploads(bool enabled) noexcept
        {
            m_immediateTextureUploads = enabled;
        }
        void InvalidateAssetCache();
        void SetSubmissionLabel(std::string label);
        // Camera-stack layers render the same immutable scene pose. Reuse the
        // base view's vertex streams and previous-frame positions for one Render.
        // Camera/depth/temporal reconstruction histories remain view-local.
        void ReuseSkinningForFrame(const RhiSceneRenderer &source);
        // Scene-wide effects (oceans and particle systems) belong to the main
        // view. Secondary views disable them but keep the scene's lights.
        void SetSceneEffectsEnabled(bool enabled) noexcept
        { m_sceneEffectsEnabled = enabled; m_particleEffectsEnabled = enabled; }
        void SetParticleEffectsEnabled(bool enabled) noexcept { m_particleEffectsEnabled = enabled; }
        void SetTransparentBackground(bool enabled) noexcept { m_transparentBackground = enabled; }
        void SetTemporalUpscalerOptions(rhi::TemporalUpscalerOptions options) noexcept
        {
            if (m_upscalerOptions == options)
                return;
            const bool reconstructionChanged =
                m_upscalerOptions.technology != options.technology ||
                m_upscalerOptions.quality != options.quality ||
                m_upscalerOptions.hdr != options.hdr ||
                m_upscalerOptions.autoExposure != options.autoExposure;
            if (reconstructionChanged && m_device &&
                m_upscalerOptions.technology != rhi::TemporalUpscaler::None)
                m_device->ReleaseTemporalUpscalerContext(m_upscalerContextId);
            m_upscalerOptions = options;
            if (reconstructionChanged)
            {
                m_upscalerStatus = {};
                ResetTemporalHistory();
            }
        }
        void ResetTemporalHistory() noexcept
        {
            if (m_renderer)
                m_renderer->ResetTemporalHistory();
            ++m_skinningHistoryEpoch;
            m_upscalerHistoryValid = false;
            m_temporalFrameIndex = 0;
            m_previousTemporalJitterNdc = glm::vec2(0.0f);
        }
        bool Render(std::uint32_t width, std::uint32_t height, const CameraData &cameraData,
                    const BasicLighting &lighting, RenderCommandView commands,
                    RenderCommandView shadowCommands,
                    std::span<IPostProcessEffect *const> postProcessEffects = {},
                    std::span<const BasicPostProcessEffect> atmosphereEffects = {},
                    const TexturePixelReader &texturePixelReader = {},
                    PostProcessDebugView debugView = PostProcessDebugView::None, bool submit = true,
                    const scene::Scene *scene = nullptr,
                    // When set, only these lights illuminate the view instead of every scene light.
                    std::optional<std::span<scene::Light *const>> lights = std::nullopt,
                    const BasicRenderer::BeforeTemporalResolve &beforeTemporalResolve = {}, bool linearOutput = false,
                    std::optional<glm::vec2> sharedClipJitter = std::nullopt);

        [[nodiscard]] const glm::mat4 &GetInverseViewProjection() const noexcept;
        [[nodiscard]] const glm::mat4 &GetPreviousViewProjection() const noexcept;

        [[nodiscard]] rhi::TextureHandle GetColorTexture() const noexcept;
        [[nodiscard]] rhi::TextureHandle GetDepthTexture() const noexcept;
        [[nodiscard]] rhi::TextureHandle GetNormalTexture() const noexcept;
        [[nodiscard]] rhi::TextureHandle GetMaterialTexture() const noexcept;
        [[nodiscard]] rhi::TextureHandle GetMotionTexture() const noexcept;
        [[nodiscard]] rhi::TextureHandle GetCoverageTexture() const noexcept;
        [[nodiscard]] std::size_t GetSceneCommandCount() const noexcept { return m_sceneCommandCount; }
        [[nodiscard]] std::size_t GetDrawCount() const noexcept { return m_drawCount; }
        [[nodiscard]] const RhiSceneTimingStats &GetTimingStats() const noexcept { return m_timingStats; }
        [[nodiscard]] const TemporalUpscalerStatus &GetTemporalUpscalerStatus() const noexcept
        {
            return m_upscalerStatus;
        }

    private:
        GraphicsQuality m_graphicsQuality;
      bool m_immediateTextureUploads = false;
      bool m_sceneEffectsEnabled = true;
      bool m_particleEffectsEnabled = true;
      bool m_transparentBackground = false;
      std::string m_submissionLabel = "Scene";
      rhi::IRenderDevice *m_device = nullptr;
      std::unique_ptr<BasicRenderer> m_renderer;
      std::unique_ptr<RhiSkinningExecutor> m_skinningExecutor;
      std::unique_ptr<RhiDrawPreparationCache> m_drawPreparation;
      struct CachedMesh
      {
          std::weak_ptr<const void> lifetime;
          BasicMesh mesh;
          std::unordered_map<std::uint64_t, std::uint32_t> canonicalGeometry;
          std::unordered_map<std::uint64_t, ShadowGeometryCluster> localBounds;
          std::uint64_t contentRevision = 0;
      };
      std::unordered_map<const Mesh *, CachedMesh> m_meshes;
      struct SkinnedMesh
      {
          std::weak_ptr<const void> lifetime;
          BasicMesh mesh;
          std::shared_ptr<std::vector<BasicVertex>> vertices = std::make_shared<std::vector<BasicVertex>>();
          std::size_t sourceVertexCount = 0;
          std::shared_ptr<RhiGpuSkinningSource> gpuSource;
          std::vector<glm::mat4> pose;
          std::shared_ptr<RhiSkinnedShadowBounds> shadowBounds;
          std::vector<ShadowGeometryCluster> shadowClusters;
          std::uint64_t lastFrame = 0;
          std::uint64_t queuedFrame = 0;
          std::uint64_t contentRevision = 0;
          std::uint64_t historyEpoch = 0;
          bool wasMoving = false;
          glm::vec3 boundsCenter{0};
          float boundsRadius = 0;
      };
      struct PendingSkinning
      {
          SkinnedMesh *entry;
          const Mesh *mesh;
          const std::vector<glm::mat4> *pose;
          bool changed, topologyChanged, upload, hasHistory;
          std::size_t jobIndex;
      };
      std::vector<PendingSkinning> m_pendingSkinning;
      std::vector<RhiSkinningJob> m_skinningJobs;
      // A shared model can have multiple independently animated owners.
      struct SkinningCache
      {
          struct ShadowBounds
          {
              std::weak_ptr<const void> lifetime;
              std::uint64_t contentRevision = 0;
              std::shared_ptr<RhiSkinnedShadowBounds> bounds;
              std::shared_ptr<RhiGpuSkinningSource> gpuSource;
          };
          std::unordered_map<const Mesh *, std::unordered_map<const std::vector<glm::mat4> *, SkinnedMesh>> meshes;
          std::unordered_map<const Mesh *, ShadowBounds> shadowBounds;
      };
      std::shared_ptr<SkinningCache> m_skinningCache = std::make_shared<SkinningCache>();
      std::optional<std::pair<std::uint64_t, std::uint64_t>> m_reusedSkinningFrame;
      bool m_borrowedSkinningCache = false;
      std::uint64_t m_skinningFrame = 0;
      // Per-view material/packet aging must not follow a borrowed camera's clock.
      std::uint64_t m_preparationFrame = 0;
      std::uint64_t m_skinningHistoryEpoch = 0;
      std::unordered_map<const Texture *, rhi::Texture> m_srgbTextures;
      std::unordered_map<const Texture *, rhi::Texture> m_linearTextures;
      std::unordered_map<const Texture *, rhi::Texture> m_normalTextures;
      // One CPU-only job bounds worker count and temporary image memory.
      std::future<std::vector<std::byte>> m_normalMipJob;
      const Texture *m_pendingNormalSource = nullptr;
      std::weak_ptr<const void> m_pendingNormalLifetime;
      std::uint64_t m_pendingNormalRevision = 0;
      struct TextureVersion { std::weak_ptr<const void> lifetime; std::uint64_t identity, revision; };
      std::unordered_map<const Texture *, TextureVersion> m_textureVersions;
      std::uint64_t m_textureResidencyRevision = 1;
      std::uint32_t m_pendingNormalWidth = 0;
      std::uint32_t m_pendingNormalHeight = 0;
      std::size_t m_sceneCommandCount = 0;
      std::size_t m_drawCount = 0;
      std::vector<BasicParticleDraw> m_particleDraws;
      std::vector<std::pair<const scene::ParticleCpuData *, float>> m_sortedParticles;
      RhiSceneTimingStats m_timingStats;
      std::uint64_t m_temporalFrameIndex = 0;
      glm::vec2 m_previousTemporalJitterNdc{0.0f};
      rhi::TemporalUpscalerOptions m_upscalerOptions;
      bool m_previousUpscalerOrthographic = false;
      glm::mat4 m_previousUpscalerViewProjection{1.0f};
      rhi::Extent2D m_previousRenderSize;
      rhi::Extent2D m_previousOutputSize;
      bool m_upscalerHistoryValid = false;
      TemporalUpscalerStatus m_upscalerStatus;
      std::uint64_t m_upscalerContextId = 0;
    };
}
