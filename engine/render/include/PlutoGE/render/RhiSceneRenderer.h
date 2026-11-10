#pragma once
#include "PlutoGE/render/RhiTerrainLayerCache.h"
#include "PlutoGE/render/GraphicsQuality.h"
#include "PlutoGE/render/RenderingSettings.h"
#include "PlutoGE/render/RenderCommandView.h"

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/ShaderGraph.h"
#include "PlutoGE/render/TextureCompression.h"
#include "PlutoGE/render/DynamicResolution.h"

#include <algorithm>
#include <compare>
#include <deque>
#include <functional>
#include <future>
#include <map>
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
    class RhiTextureCompression;
    class DirectionalShadowCascadeRetention;
    struct ParticleInstanceKey;
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
        // Material textures replaced by block-compressed uploads this frame, and encodes still running.
        std::size_t compressedTextureUploads = 0;
        std::size_t pendingTextureCompressions = 0;
        // The dynamic resolution scale this frame rendered at (1 when disabled), and its render size.
        float dynamicResolutionScale = 1.0f;
        std::uint32_t renderWidth = 0, renderHeight = 0;
        float sceneSetupMs = 0.0f;
        float renderRecordingMs = 0.0f;
        float beginFrameMs = 0.0f;
        float shadowRecordingMs = 0.0f;
        float instancePreparationMs = 0.0f;
        float instanceBinningMs = 0.0f;
        float geometryRecordingMs = 0.0f;
        bool parallelRecording = false;   // Shadows and geometry recorded on worker threads.
        float postProcessRecordingMs = 0.0f;
        float cameraCompositionMs = 0.0f;
        float temporalUpscalerMs = 0.0f;
        float submitMs = 0.0f;
        float totalMs = 0.0f;
        std::size_t visibleDrawCount = 0;
        std::size_t visibleInstanceCount = 0;
        std::size_t shadowCandidateCount = 0;
        // Skinned shadow-only casters skipped (not deformed) because they cannot
        // shadow any receiver in this view.
        std::size_t skinnedShadowCastersOutOfReach = 0;
        // Rigid casters dropped because the full list exceeded the VSM caster capacity.
        std::size_t rigidShadowCastersOutOfReach = 0;
        // Casters dropped beyond their producer's own shadow distance (RenderCommand::maxShadowDistance).
        std::size_t shadowCastersBeyondShadowDistance = 0;
        // Cascades that kept last frame's projection (DirectionalShadowCascadeRetention).
        std::size_t retainedShadowCascades = 0;
        // Casters skipped by cascades too coarse to resolve them (BasicLighting::shadowCasterMinimumTexels).
        std::size_t shadowCastersBelowTexelSize = 0;
        // Hardware ray-traced GI: instances in the top level, and whether it was rebuilt this frame.
        std::size_t rayTracedInstances = 0, rayTracedSkippedInstances = 0, rayTracedOutOfRangeInstances = 0;
        bool rayTracedSceneRebuilt = false;
        std::size_t recordedGeometryDepthDrawCount = 0;
        std::uint32_t geometryColorOutputs = 0;
        std::size_t geometryParameterCreates = 0, geometryParameterReuses = 0;
        std::size_t recordedGeometryDrawCount = 0;
        std::size_t recordedGeometryInstanceCount = 0;
        std::size_t viewCulledInstanceCount = 0;
        std::size_t impostorDrawCount = 0;
        std::size_t glassPanes = 0, glassSnapshots = 0;
        std::size_t glassDepthSnapshots = 0, glassSnapshotReuseHits = 0;
        std::size_t reusedOpaqueBatchGroups = 0, rebuiltOpaqueBatchGroups = 0, rebuiltOpaqueBatchDraws = 0;
        float glassBoundsCpuMs = 0, glassGroupingCpuMs = 0, glassDamageCpuMs = 0;
        float glassCopyRecordingCpuMs = 0, glassDrawRecordingCpuMs = 0;
        std::size_t sharedDrawPacketHits = 0;
        std::size_t glassFullFootprints = 0;
        std::uint64_t glassSnapshotPixels = 0;
        std::array<std::size_t, 5> glassBoundsReasons{}, glassGroupBoundaries{};
        std::size_t pointShadowAtlasUpdates = 0, pointShadowAtlasCacheHits = 0, pointShadowDraws = 0;
        std::size_t pointShadowFaceUpdates = 0, pointShadowFaceHits = 0, pointShadowObjectUploads = 0, pointShadowMaterialUploads = 0;
        std::size_t pointShadowStaticFaceUpdates = 0; // Static layer faces redrawn (PointShadowLayers).
        std::array<std::size_t, 4> pointShadowInvalidations{};
        std::size_t materialPreparations = 0, materialPreparationHits = 0;
        std::size_t graphSpecializedDraws = 0, graphInterpretedDraws = 0;
        std::array<std::uint64_t, 4> geometryTriangles{};
        rhi::Extent2D renderSize{}, outputSize{};
        GeometryDiagnosticMode geometryDiagnosticMode = GeometryDiagnosticMode::None;
        OcclusionMode occlusionMode = OcclusionMode::Off;
        OcclusionStats occlusion;
        bool occlusionActive = false;
        // GPU instance culling's latest diagnostics, when it binned anything (BasicRenderer::FrameStats::instanceCulling).
        InstanceCullingStats instanceCulling;
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
        // Replaces the matching BasicLighting fields (occlusion culling) of every view rendered. The image is
        // unchanged, so temporal history survives a change.
        void SetRenderingSettings(const RenderingSettings &settings) noexcept { m_renderingSettings = settings; }
        RhiSceneRenderer();
        ~RhiSceneRenderer();
        using TexturePixelReader = std::function<std::vector<std::byte>(const Texture &)>;

        bool Initialize(rhi::IRenderDevice &device, const BasicRendererShaderPackage &shaders);
        void Shutdown();
        void SetImmediateTextureUploads(bool enabled) noexcept
        {
            m_immediateTextureUploads = enabled;
        }
        // Block compression of material textures (TextureCompression.h). Renderers on one device share their
        // texture caches, so this applies to all of them; changing whether it is enabled re-uploads every texture.
        // Defaults to TextureCompressionSettings::FromEnvironment(). With immediate texture uploads, textures are
        // encoded before their first use instead of in the background.
        void SetTextureCompression(const TextureCompressionSettings &settings);
        [[nodiscard]] const TextureCompressionSettings &GetTextureCompression() const noexcept
        {
            return m_textureCompressionSettings;
        }
        // Automatic render resolution (DynamicResolution.h): the render size the view would otherwise use (native,
        // or the temporal upscaler's quality mode) is scaled each frame from completed GPU timings of this
        // renderer's submissions. Render targets are rebuilt when the scale changes level; a temporal upscaler
        // keeps its history across levels. Needs device GPU timing (Vulkan); without it the scale stays at its
        // maximum. Replace the decision with SetDynamicResolutionPolicy.
        void SetDynamicResolution(const DynamicResolutionSettings &settings);
        void SetDynamicResolutionPolicy(std::unique_ptr<IDynamicResolutionPolicy> policy);
        [[nodiscard]] const DynamicResolutionSettings &GetDynamicResolution() const noexcept
        {
            return m_dynamicResolutionSettings;
        }
        void InvalidateAssetCache();
        void SetSubmissionLabel(std::string label);
        // BasicRenderer::SetGpuInstanceCulling, kept across Initialize.
        void SetGpuInstanceCulling(bool enabled);
        // Camera-stack layers render the same immutable scene pose. Reuse the
        // base view's vertex streams and previous-frame positions for one Render.
        // Camera/depth/temporal reconstruction histories remain view-local.
        void ReuseSkinningForFrame(const RhiSceneRenderer &source);
        // Camera-stack layers share the base view's viewpoint. The next Render
        // samples the base view's directional VSM and skips its own directional
        // caster preparation. Local-light shadows remain view-local.
        void ReuseDirectionalShadowsForFrame(VirtualShadowReceivers receivers) noexcept
        { m_reusedShadowReceivers = receivers; }
        // Scene-wide effects (oceans and particle systems) belong to the main
        // view. Secondary views disable them but keep the scene's lights.
        void SetSceneEffectsEnabled(bool enabled) noexcept
        { m_sceneEffectsEnabled = enabled; m_particleEffectsEnabled = enabled; }
        void SetParticleEffectsEnabled(bool enabled) noexcept { m_particleEffectsEnabled = enabled; }
        void SetTransparentBackground(bool enabled) noexcept { m_transparentBackground = enabled; }
        // Screen-space outlines for the following frames: visible commands owned by each layer's entities are
        // outlined in that layer's style, later layers on top. Layers without entities and past MaxOutlineLayers are
        // dropped; an empty span turns outlines off. The layers are copied.
        void SetOutlines(std::span<const OutlineLayer> layers);
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
                    std::optional<glm::vec2> sharedClipJitter = std::nullopt,
                    BasicRenderer::RecordingMode recordingMode = BasicRenderer::RecordingMode::Begin);

        // Render in two phases, so a render thread can record a frame while the game simulates the next one.
        // Prepare does everything that reads the scene, assets and render commands (translation, visibility, GI
        // selection, particles, decals, uploads of new resources) and returns what recording needs. Record records
        // and, when the frame asked for it, submits; it reads only the PreparedFrame, this renderer and its device
        // resources. A PreparedFrame borrows this renderer's preparation caches: record it before the next Prepare,
        // and do not call this renderer from another thread while Record runs. Render is Prepare then Record.
        struct PreparedFrame
        {
            glm::mat4 viewProjection{1.0f};
            BasicLighting lighting;
            std::span<const BasicDraw> draws, shadowDraws, giDraws;
            // The first outlineCount entries: each outline layer's style and prepared draws.
            std::array<BasicOutlineLayer, MaxOutlineLayers> outlines{};
            std::size_t outlineCount = 0;
            std::span<const BasicParticleDraw> particles;
            std::vector<BasicPostProcessEffect> effects;
            std::vector<BasicDecalDraw> decals;
            PostProcessDebugView debugView = PostProcessDebugView::None;
            bool useTemporalUpscaler = false;
            std::optional<rhi::TemporalUpscalerFrame> upscalerFrame;
            glm::mat4 unjitteredViewProjection{1.0f};
            bool submit = true;
            BasicRenderer::BeforeTemporalResolve beforeTemporalResolve;
            bool linearOutput = false;
            std::optional<glm::vec2> sharedClipJitter;
            BasicRenderer::RecordingMode recordingMode = BasicRenderer::RecordingMode::Begin;
            rhi::Extent2D renderSize{}, outputSize{};
            bool virtualShadowCapacityFallback = false;
            // The shader-graph clock the frame was prepared with; Record pins it on its thread.
            ScopedShaderGraphClock::Values clock;
            float preparationMs = 0.0f, recordingSetupMs = 0.0f;
        };
        [[nodiscard]] std::optional<PreparedFrame> Prepare(
            std::uint32_t width, std::uint32_t height, const CameraData &cameraData, const BasicLighting &lighting,
            RenderCommandView commands, RenderCommandView shadowCommands,
            std::span<IPostProcessEffect *const> postProcessEffects = {},
            std::span<const BasicPostProcessEffect> atmosphereEffects = {}, const TexturePixelReader &texturePixelReader = {},
            PostProcessDebugView debugView = PostProcessDebugView::None, bool submit = true,
            const scene::Scene *scene = nullptr, std::optional<std::span<scene::Light *const>> lights = std::nullopt,
            const BasicRenderer::BeforeTemporalResolve &beforeTemporalResolve = {}, bool linearOutput = false,
            std::optional<glm::vec2> sharedClipJitter = std::nullopt,
            BasicRenderer::RecordingMode recordingMode = BasicRenderer::RecordingMode::Begin);
        void Record(PreparedFrame &frame);

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
        RenderingSettings m_renderingSettings;
      bool m_immediateTextureUploads = false;
      bool m_sceneEffectsEnabled = true;
      bool m_particleEffectsEnabled = true;
      bool m_transparentBackground = false;
      // SetOutlines' layers, each with its entities sorted and unique.
      std::vector<OutlineLayer> m_outlineLayers;
      std::vector<const RenderCommand *> m_outlineCommands;
      std::string m_submissionLabel = "Scene";
      bool m_gpuInstanceCulling = true;
      rhi::IRenderDevice *m_device = nullptr;
      std::unique_ptr<BasicRenderer> m_renderer;
      std::unique_ptr<RhiSkinningExecutor> m_skinningExecutor;
      std::unique_ptr<RhiDrawPreparationCache> m_drawPreparation;
      // Producers' impostors with their atlases uploaded, by albedo atlas (cleared with the texture caches).
      std::unordered_map<const Texture *, std::shared_ptr<const BasicImpostor>> m_impostors;
      struct CachedMesh
      {
          std::weak_ptr<const void> lifetime;
          BasicMesh mesh;
          std::unordered_map<std::uint64_t, std::uint32_t> canonicalGeometry;
          std::unordered_map<std::uint64_t, ShadowGeometryCluster> localBounds;
          std::uint64_t contentRevision = 0;
          // Everything that distinguishes this mesh's instanced LOD chains (BasicDraw::instanceLods).
          struct InstanceLodKey
          {
              std::uint32_t submesh = 0, minimumLod = 0;
              bool handsOverToImpostor = false;
              float distanceScale = DefaultLodDistanceScale;
              auto operator<=>(const InstanceLodKey &) const = default;
          };
          std::map<InstanceLodKey, std::shared_ptr<const BasicInstanceLodChain>> instanceLods;
          // Each submesh's shadow caster LOD chain (BasicDraw::shadowCasterLods), by submesh.
          std::unordered_map<std::size_t, std::shared_ptr<const ShadowCasterLodChain>> shadowCasterLods;
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
          // Finest LOD level any draw of this pose needs this frame, and the
          // GPU layout prefix (with its finest fully deformed level) last written.
          std::uint32_t requiredLevel = 0;
          std::uint32_t deformedVertexCount = 0;
          std::uint32_t deformedLevel = 0;
          std::uint64_t growthFrame = 0;
          bool wasMoving = false;
          glm::vec3 boundsCenter{0};
          glm::vec3 boundsExtents{-1};
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
      // Indices into m_pendingSkinning whose shadow bounds refit this flush.
      std::vector<std::size_t> m_skinningRefits;
      // Entries deformed earlier this frame that a later list or view needs at a finer LOD.
      std::vector<SkinnedMesh *> m_skinningGrowth;
      std::vector<RhiSkinningJob> m_skinningJobs;
      std::vector<const RenderCommand *> m_directionalShadowCommands;
      std::unique_ptr<DirectionalShadowCascadeRetention> m_cascadeRetention;
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
      VirtualShadowReceivers m_reusedShadowReceivers;
      bool m_borrowedSkinningCache = false;
      std::uint64_t m_skinningFrame = 0;
      // Per-view material/packet aging must not follow a borrowed camera's clock.
      std::uint64_t m_preparationFrame = 0;
      std::uint64_t m_skinningHistoryEpoch = 0;
      struct TextureVersion { std::weak_ptr<const void> lifetime; std::uint64_t identity, revision; };
      // Immutable material images belong to the device, not an individual camera.
      // Keep one allocation across editor/game/secondary views on that device.
      struct CachedTexture
      {
          rhi::Texture texture;
          rhi::Format format = rhi::Format::R8G8B8A8Unorm;
          // Some use needs the exact RGBA8 texels (shader graphs, impostors, painted maps): never compress it.
          bool keepUncompressed = false;
          // Compression was queued, or decided against, for this upload; otherwise the next compressible use asks.
          bool compressionDecided = false;
          [[nodiscard]] rhi::TextureHandle Get() const noexcept { return texture.Get(); }
      };
      struct TextureCache
      {
          std::unordered_map<const Texture *, CachedTexture> srgb, linear, normal;
          // Shared by every renderer on the device, like the caches it fills.
          std::shared_ptr<RhiTextureCompression> compression;
          // Uploads replaced while renderers on the device may still hold them. Renderers sharing the cache prepare
          // and record on their own schedules (render textures, camera layers), and keep prepared draws between
          // Prepares, so a replaced texture lives until every renderer has prepared its materials since the
          // replacement (`residency` versus each renderer's `preparedResidency`), and the device has completed the
          // submission pending at that point.
          struct RetiredTexture
          {
              rhi::Texture texture;
              std::uint64_t residency = 0;
              std::optional<std::uint64_t> releaseSubmission;
          };
          std::vector<RetiredTexture> retired;
          // Each live renderer's residencyRevision as of its last material preparation.
          std::vector<std::weak_ptr<const std::uint64_t>> preparedResidency;
          std::unordered_map<const Texture *, TextureVersion> versions;
          // Each read texture's mean: raw channels (linear data) and its colour decoded from sRGB (alpha raw).
          struct Mean { glm::vec4 raw{0.0f}, srgb{0.0f}; };
          std::unordered_map<const Texture *, Mean> means;
          std::uint64_t residencyRevision = 1;
          RhiTerrainLayerCache terrainLayers;
      };
      std::shared_ptr<TextureCache> m_textureCache;
      TextureCompressionSettings m_textureCompressionSettings = TextureCompressionSettings::FromEnvironment();
      // This renderer's TextureCache::preparedResidency entry.
      std::shared_ptr<std::uint64_t> m_preparedResidency = std::make_shared<std::uint64_t>(0);
      struct CachedIblCapture
      {
          TextureVersion version;
          rhi::Texture atlas;
          std::array<glm::vec4, 9> irradiance{};
      };
      std::unordered_map<const Texture *, CachedIblCapture> m_iblCaptures;
      rhi::Texture m_diffuseProbeAtlas;
      int m_diffuseProbeHitPointRow = 0, m_diffuseProbeSummaryRow = 0;
      const scene::Scene *m_diffuseProbeScene = nullptr;
      std::uint64_t m_diffuseProbeRevision = 0;
      // One CPU-only job bounds worker count and temporary image memory.
      std::future<std::vector<std::byte>> m_normalMipJob;
      const Texture *m_pendingNormalSource = nullptr;
      std::weak_ptr<const void> m_pendingNormalLifetime;
      std::uint64_t m_pendingNormalRevision = 0;
      std::uint32_t m_pendingNormalWidth = 0;
      std::uint32_t m_pendingNormalHeight = 0;
      std::size_t m_sceneCommandCount = 0;
      std::size_t m_drawCount = 0;
      // Per-emitter draws, then the same billboards in one global depth order.
      std::vector<BasicParticleDraw> m_particleDraws, m_orderedParticleDraws;
      std::vector<ParticleInstanceKey> m_particleInstanceKeys;
      std::vector<std::pair<const scene::ParticleCpuData *, float>> m_sortedParticles;
      RhiSceneTimingStats m_timingStats;
      std::uint64_t m_temporalFrameIndex = 0;
      glm::vec2 m_previousTemporalJitterNdc{0.0f};
      rhi::TemporalUpscalerOptions m_upscalerOptions;
      bool m_previousUpscalerOrthographic = false;
      glm::mat4 m_previousUpscalerViewProjection{1.0f};
      rhi::Extent2D m_previousRenderSize;
      rhi::Extent2D m_previousOutputSize;
      // The render size before dynamic resolution scaling, whose change resets every temporal history.
      rhi::Extent2D m_previousMaximumRenderSize;
      // The scale for the next frame, from GPU timings; null when disabled.
      [[nodiscard]] float UpdateDynamicResolution();
      DynamicResolutionSettings m_dynamicResolutionSettings;
      std::unique_ptr<IDynamicResolutionPolicy> m_dynamicResolution;
      // (pending submission when prepared, scale) of recent frames, to match GPU results with their scale.
      std::deque<std::pair<std::uint64_t, float>> m_dynamicResolutionFrames;
      std::uint64_t m_dynamicResolutionObservation = 0;
      bool m_upscalerHistoryValid = false;
      TemporalUpscalerStatus m_upscalerStatus;
      std::uint64_t m_upscalerContextId = 0;
    };
}
