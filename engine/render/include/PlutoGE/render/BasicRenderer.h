#pragma once

#include "PlutoGE/render/RenderDebugView.h"

#include "PlutoGE/render/rhi/Resource.h"

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include <glm/glm.hpp>

namespace PlutoGE::render
{
    class PostProcessResourcePool;

    enum class BasicPostProcessEffectType : std::uint8_t
    {
        ToneMapping,
        GammaCorrection,
        FXAA,
        ColorGrading,
        ChromaticAberration,
        Bloom,
        LensFlare,
        MotionBlur,
        DepthOfField,
        AutoExposure,
        TAA,
        SSAO,
        SSGI,
        SSR,
        VolumetricFog,
        PhysicalSky,
        VolumetricCloud,
        SceneComposite,
        VCTGI,
        Count,
    };

    enum class BasicPostProcessInput : std::uint8_t
    {
        None = 0,
        Depth = 1u << 0u,
        Normal = 1u << 1u,
        Material = 1u << 2u,
        Motion = 1u << 3u,
        History = 1u << 4u,
    };

    enum class BasicPostProcessStage : std::uint8_t
    {
        LightingComposite,
        AmbientOcclusion,
        ScreenSpaceAtmosphere,
        TemporalResolve,
        CameraOptics,
        Exposure,
        ToneAndColor,
    };

    [[nodiscard]] constexpr BasicPostProcessStage StageFor(BasicPostProcessEffectType type) noexcept
    {
        switch (type)
        {
        case BasicPostProcessEffectType::SSGI:
        case BasicPostProcessEffectType::SceneComposite:
        case BasicPostProcessEffectType::VCTGI:
            return BasicPostProcessStage::LightingComposite;
        case BasicPostProcessEffectType::SSAO:
            return BasicPostProcessStage::AmbientOcclusion;
        case BasicPostProcessEffectType::SSR:
        case BasicPostProcessEffectType::VolumetricFog:
        case BasicPostProcessEffectType::PhysicalSky:
        case BasicPostProcessEffectType::VolumetricCloud:
            return BasicPostProcessStage::ScreenSpaceAtmosphere;
        case BasicPostProcessEffectType::TAA:
            return BasicPostProcessStage::TemporalResolve;
        case BasicPostProcessEffectType::MotionBlur:
        case BasicPostProcessEffectType::DepthOfField:
        case BasicPostProcessEffectType::Bloom:
        case BasicPostProcessEffectType::LensFlare:
            return BasicPostProcessStage::CameraOptics;
        case BasicPostProcessEffectType::AutoExposure:
            return BasicPostProcessStage::Exposure;
        default:
            return BasicPostProcessStage::ToneAndColor;
        }
    }

    [[nodiscard]] constexpr BasicPostProcessInput operator|(BasicPostProcessInput lhs,
                                                            BasicPostProcessInput rhs) noexcept
    {
        return static_cast<BasicPostProcessInput>(static_cast<std::uint8_t>(lhs) |
                                                  static_cast<std::uint8_t>(rhs));
    }

    [[nodiscard]] constexpr bool HasInput(BasicPostProcessInput inputs,
                                          BasicPostProcessInput input) noexcept
    {
        return (static_cast<std::uint8_t>(inputs) & static_cast<std::uint8_t>(input)) != 0;
    }

    [[nodiscard]] constexpr BasicPostProcessInput InputsFor(BasicPostProcessEffectType type) noexcept
    {
        switch (type)
        {
        case BasicPostProcessEffectType::MotionBlur:
            return BasicPostProcessInput::Motion;
        case BasicPostProcessEffectType::DepthOfField:
            return BasicPostProcessInput::Depth;
        case BasicPostProcessEffectType::TAA:
            return BasicPostProcessInput::Depth | BasicPostProcessInput::Normal |
                   BasicPostProcessInput::Motion | BasicPostProcessInput::History;
        case BasicPostProcessEffectType::SSAO:
        case BasicPostProcessEffectType::SSGI:
        case BasicPostProcessEffectType::VCTGI:
            return BasicPostProcessInput::Depth | BasicPostProcessInput::Normal;
        case BasicPostProcessEffectType::SSR:
            return BasicPostProcessInput::Depth | BasicPostProcessInput::Normal |
                   BasicPostProcessInput::Material;
        case BasicPostProcessEffectType::VolumetricFog:
        case BasicPostProcessEffectType::PhysicalSky:
        case BasicPostProcessEffectType::VolumetricCloud:
            return BasicPostProcessInput::Depth;
        case BasicPostProcessEffectType::SceneComposite:
            return BasicPostProcessInput::Depth | BasicPostProcessInput::Normal |
                   BasicPostProcessInput::Material | BasicPostProcessInput::Motion;
        default:
            return BasicPostProcessInput::None;
        }
    }

    struct BasicPostProcessShaderPackage
    {
        rhi::GraphicsPipelineDescriptor::ShaderCode vertex;
        rhi::GraphicsPipelineDescriptor::ShaderCode fragment;
    };

    struct BasicVertex
    {
        std::array<float, 3> position{};
        std::array<float, 3> normal{};
        std::array<float, 2> uv{};
        // A valid fallback avoids undefined normalization for procedural or legacy meshes
        // that do not provide tangent data. Imported meshes overwrite this value.
        std::array<float, 4> tangent{1.0f, 0.0f, 0.0f, 1.0f};
    };

    struct BasicMeshData
    {
        std::span<const BasicVertex> vertices;
        std::span<const std::uint32_t> indices;
    };

    struct BasicRendererShaderPackage
    {
        rhi::GraphicsPipelineDescriptor::ShaderCode vertex;
        rhi::GraphicsPipelineDescriptor::ShaderCode instancedVertex;
        rhi::GraphicsPipelineDescriptor::ShaderCode fragment;
        rhi::GraphicsPipelineDescriptor::ShaderCode shadowVertex;
        rhi::GraphicsPipelineDescriptor::ShaderCode shadowInstancedVertex;
        rhi::GraphicsPipelineDescriptor::ShaderCode shadowFragment;
        BasicPostProcessShaderPackage displayOutput;
        std::array<BasicPostProcessShaderPackage,
                   static_cast<std::size_t>(BasicPostProcessEffectType::Count)>
            postProcess;
        std::array<BasicPostProcessShaderPackage, 4> bloom;
        std::array<BasicPostProcessShaderPackage, 2> autoExposure;
        std::array<BasicPostProcessShaderPackage, 3> ssao;
        std::array<rhi::ComputePipelineDescriptor::ShaderCode, 2> vctCompute;
        rhi::GraphicsPipelineDescriptor vctVoxelization;
        std::array<BasicPostProcessShaderPackage, 3> vctPostProcess;
    };

    class BasicMesh
    {
    public:
        BasicMesh() = default;
        BasicMesh(BasicMesh &&) noexcept = default;
        BasicMesh &operator=(BasicMesh &&) noexcept = default;
        BasicMesh(const BasicMesh &) = delete;
        BasicMesh &operator=(const BasicMesh &) = delete;
        [[nodiscard]] bool IsValid() const noexcept { return m_vertexBuffer && m_indexBuffer && m_indexCount != 0; }

    private:
        friend class BasicRenderer;
        rhi::Buffer m_vertexBuffer;
        rhi::Buffer m_indexBuffer;
        std::uint32_t m_indexCount = 0;
    };

    struct BasicDraw
    {
        const BasicMesh *mesh = nullptr;
        glm::mat4 model{1.0f};
        glm::vec4 baseColor{1.0f};
        glm::vec2 uvScale{1.0f};
        rhi::TextureHandle baseColorTexture;
        rhi::TextureHandle normalTexture;
        rhi::TextureHandle metallicTexture;
        rhi::TextureHandle roughnessTexture;
        float metallic = 0.0f;
        float roughness = 1.0f;
        glm::vec3 emission{0.0f};
        float subsurface = 0.0f;
        glm::vec3 subsurfaceColor{1.0f, 0.35f, 0.2f};
        float subsurfaceRadius = 1.0f;
        float alphaCutoff = 0.5f;
        std::uint32_t alphaMode = 0;
        std::uint32_t metallicChannel = 0;
        std::uint32_t roughnessChannel = 0;
        bool flipNormalY = false;
        bool castsShadow = true;
        glm::vec3 shadowBoundsCenter{0.0f};
        float shadowBoundsRadius = -1.0f;
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
        bool contributesToGi = true;
        float normalizedLod = 0.0f;
        std::shared_ptr<const std::vector<glm::mat4>> instanceModels;
        std::shared_ptr<const std::vector<glm::mat4>> previousInstanceModels;
    };

    struct BasicLighting
    {
        glm::vec3 cameraPosition{0.0f};
        glm::mat4 view{1.0f};
        float ambientIntensity = 0.3f;
        // Directional environment used by the RHI PBR path. Physical sky
        // parameters share the layout used by the sky post-process pass so
        // the background and surface lighting cannot drift apart.
        bool physicalSkyEnabled = false;
        float physicalSkyExposure = 1.0f;
        std::array<glm::vec4, 6> physicalSkyParameters{};
        glm::vec3 directionalDirection{0.4f, -0.8f, 0.3f};
        float directionalIntensity = 1.0f;
        glm::vec3 directionalColor{1.0f};
        bool shadowsEnabled = false;
        std::array<glm::mat4, 4> shadowMatrices{
            glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f), glm::mat4(1.0f)};
        glm::vec4 shadowCascadeSplits{0.0f};
        std::array<glm::vec4, 4> shadowCascadeMetrics{}; // world texel size, depth range
        bool shadowFlipY = false;
        float shadowDepthScale = 1.0f;
        float shadowDepthBias = 0.0f;
        std::uint32_t shadowResolution = 2048;
        std::uint32_t shadowCascadeCount = 4;
        float shadowCascadeResolutionFalloff = 0.75f;
        float shadowNearCascadeDistance = 8.0f;
        float shadowSplitLambda = 0.9f;
        float shadowCascadeBlendDistance = 4.0f;
        float shadowSoftness = 1.5f;
        bool shadowFilterEnabled = true;
        float shadowFilterRenderScale = 0.5f;
        std::uint32_t shadowFilterRadius = 2;
        float shadowFilterDepthScale = 0.015f;
        float shadowFilterMinDepthScale = 0.05f;
        float shadowFilterNormalThreshold = 0.72f;
        float shadowFilterNormalSoftness = 0.26f;
        float shadowDistance = 150.0f;
        float shadowCasterDistance = 0.0f;
    };

    struct BasicPostProcessEffect
    {
        BasicPostProcessEffectType type = BasicPostProcessEffectType::ToneMapping;
        float exposure = 1.0f;
        float gamma = 2.2f;
        std::uint32_t quality = 0;
        // Effect-specific values are deliberately grouped into aligned lanes.
        // This keeps the GPU ABI stable while new single-input passes are added.
        std::array<glm::vec4, 6> parameters{};
        glm::mat4 worldToLocal{1.0f};
    };

    struct BasicRendererFrameStats
    {
        std::size_t geometryDraws = 0;
        std::size_t geometryInstances = 0;
        std::size_t shadowCandidates = 0;
        std::size_t shadowObjectUploads = 0;
        std::size_t shadowInstances = 0;
        std::size_t shadowCascadeUpdates = 0;
        std::size_t shadowCascadeCacheHits = 0;
        std::array<std::size_t, 4> shadowDrawsByCascade{};

        [[nodiscard]] std::size_t ShadowDraws() const noexcept
        {
            std::size_t total = 0;
            for (const auto count : shadowDrawsByCascade)
                total += count;
            return total;
        }
    };

    class BasicRenderer
    {
    public:
        BasicRenderer();
        ~BasicRenderer();
        BasicRenderer(const BasicRenderer &) = delete;
        BasicRenderer &operator=(const BasicRenderer &) = delete;

        bool Initialize(rhi::IRenderDevice &device, const BasicRendererShaderPackage &shaders);
        void Shutdown();
        [[nodiscard]] BasicMesh CreateMesh(const BasicMeshData &data);
        bool Resize(std::uint32_t width, std::uint32_t height,
                    std::uint32_t outputWidth = 0, std::uint32_t outputHeight = 0);
        void SetTemporalUpscalerOptions(rhi::TemporalUpscalerOptions options) noexcept;
        void Render(const glm::mat4 &viewProjection, std::span<const BasicDraw> draws);
        void Render(const glm::mat4 &viewProjection, const BasicLighting &lighting,
                    std::span<const BasicDraw> draws,
                    std::span<const BasicPostProcessEffect> postProcessEffects = {},
                    std::span<const BasicDraw> shadowDraws = {},
                    PostProcessDebugView debugView = PostProcessDebugView::None,
                    const rhi::TemporalUpscalerFrame *upscalerFrame = nullptr);

        [[nodiscard]] rhi::TextureHandle GetColorTexture() const noexcept { return m_outputColor; }
        [[nodiscard]] rhi::TextureHandle GetDepthTexture() const noexcept { return m_depthTarget.Get(); }
        [[nodiscard]] rhi::TextureHandle GetNormalTexture() const noexcept { return m_normalTarget.Get(); }
        [[nodiscard]] rhi::TextureHandle GetMaterialTexture() const noexcept { return m_materialTarget.Get(); }
        [[nodiscard]] rhi::TextureHandle GetMotionTexture() const noexcept { return m_motionTarget.Get(); }
        [[nodiscard]] std::uint32_t GetWidth() const noexcept { return m_width; }
        [[nodiscard]] std::uint32_t GetHeight() const noexcept { return m_height; }
        [[nodiscard]] std::uint32_t GetOutputWidth() const noexcept { return m_outputWidth; }
        [[nodiscard]] std::uint32_t GetOutputHeight() const noexcept { return m_outputHeight; }
        [[nodiscard]] bool IsInitialized() const noexcept { return m_device != nullptr; }
        [[nodiscard]] const BasicRendererFrameStats &GetFrameStats() const noexcept { return m_frameStats; }
        [[nodiscard]] bool WasTemporalUpscalerEvaluated() const noexcept
        {
            return m_temporalUpscalerEvaluatedLastFrame;
        }

    private:
        [[nodiscard]] rhi::TextureHandle RenderBloom(rhi::TextureHandle source,
                                                     const BasicPostProcessEffect &effect);
        [[nodiscard]] rhi::TextureHandle RenderAutoExposure(rhi::TextureHandle source,
                                                            const BasicPostProcessEffect &effect,
                                                            rhi::ICommandContext &commands);
        [[nodiscard]] rhi::TextureHandle RenderSsao(rhi::TextureHandle source,
                                                    const BasicPostProcessEffect &effect,
                                                    rhi::ICommandContext &commands);
        [[nodiscard]] rhi::TextureHandle RenderVctgi(rhi::TextureHandle source,
                                                     const BasicPostProcessEffect &effect,
                                                     const BasicLighting &lighting,
                                                     std::span<const BasicDraw> draws,
                                                     rhi::ICommandContext &commands);
        [[nodiscard]] rhi::Buffer &AcquireVctBuffer(std::size_t index);
        void ResetVctResources();
        [[nodiscard]] rhi::Buffer &AcquirePostProcessBuffer(std::size_t index);
        [[nodiscard]] rhi::Texture &AcquirePostProcessTarget(std::size_t index,
                                                            std::uint32_t width,
                                                            std::uint32_t height);
        void EnsureShadowTargets(const BasicLighting &lighting);

        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline;
        rhi::GraphicsPipeline m_instancedPipeline;
        rhi::GraphicsPipeline m_shadowPipeline;
        rhi::GraphicsPipeline m_shadowInstancedPipeline;
        rhi::GraphicsPipeline m_displayPipeline;
        std::array<rhi::GraphicsPipeline, static_cast<std::size_t>(BasicPostProcessEffectType::Count)> m_postProcessPipelines;
        rhi::Buffer m_cameraBuffer;
        rhi::Buffer m_debugViewBuffer;
        std::array<rhi::Buffer, 4> m_shadowCameraBuffers;
        // Shadow object transforms are cascade-independent and are uploaded
        // once per caster, then referenced by every intersecting cascade.
        std::vector<rhi::Buffer> m_shadowObjectBuffers;
        std::vector<rhi::Buffer> m_shadowInstanceBuffers;
        // Reused visibility scratch keeps cascade filtering out of the command-recording
        // loop without introducing per-frame allocations.
        std::array<std::vector<std::size_t>, 4> m_shadowCascadeDrawIndices;
        std::vector<std::uint8_t> m_shadowVisibleInAnyCascade;
        // Each recorded draw owns stable parameters until backend submission.
        // Reusing one buffer causes every Vulkan draw to observe the last upload.
        std::vector<rhi::Buffer> m_postProcessBuffers;
        // Vulkan records the complete frame before execution, so every draw
        // needs stable object data until submission completes.
        std::vector<rhi::Buffer> m_objectBuffers;
        std::vector<rhi::Buffer> m_instanceBuffers;
        std::vector<rhi::Buffer> m_materialBuffers;
        rhi::Texture m_fallbackTexture;
        rhi::Texture m_fallbackNormalTexture;
        rhi::Texture m_fallbackDataTexture;
        rhi::Sampler m_fallbackSampler;
        rhi::Sampler m_screenSampler;
        rhi::Sampler m_shadowSampler;
        rhi::Sampler m_vctVolumeSampler;
        rhi::Texture m_colorTarget;
        rhi::Texture m_displayTarget;
        rhi::Texture m_normalTarget;
        rhi::Texture m_materialTarget;
        rhi::Texture m_motionTarget;
        rhi::Texture m_albedoTarget;
        rhi::Texture m_debugTarget;
        std::array<rhi::Texture, 2> m_postProcessTargets;
        // Reusing ping-pong attachments within a recorded Vulkan chain produced
        // screen-tile corruption. Keep one stable output per ordinary pass.
        std::vector<rhi::Texture> m_postProcessPassTargets;
        std::vector<rhi::Extent2D> m_postProcessPassTargetSizes;
        std::array<rhi::Texture, 2> m_taaHistoryTargets;
        std::array<rhi::GraphicsPipeline, 4> m_bloomPipelines;
        std::array<rhi::GraphicsPipeline, 2> m_autoExposurePipelines;
        std::array<rhi::Texture, 2> m_exposureHistoryTargets;
        std::array<rhi::GraphicsPipeline, 3> m_ssaoPipelines;
        rhi::GraphicsPipeline m_vctResolvePipeline;
        rhi::GraphicsPipeline m_vctDirectionalMipPipeline;
        rhi::GraphicsPipeline m_vctVoxelizationPipeline;
        std::array<rhi::GraphicsPipeline, 3> m_vctPostProcessPipelines;
        struct VctCascade
        {
            std::array<rhi::Texture, 4> accumulation;
            glm::vec3 origin{0.0f};
            float size = 0.0f;
            glm::vec3 pendingOrigin{0.0f};
            float pendingSize = 0.0f;
            std::size_t nextDraw = 0;
            std::uint64_t contentSignature = 0;
            std::uint64_t pendingSignature = 0;
            std::uint64_t lastUpdateFrame = 0;
            bool valid = false;
            bool rebuilding = false;
        };
        std::array<VctCascade, 3> m_vctCascades;
        std::array<rhi::Texture, 6> m_vctRadianceAtlases;
        rhi::Texture m_vctTraceTarget;
        std::array<rhi::Texture, 2> m_vctHistoryTargets;
        std::array<rhi::Texture, 2> m_vctMetadataTargets;
        std::vector<rhi::Buffer> m_vctBuffers;
        std::size_t m_vctBufferCursor = 0;
        std::uint32_t m_vctResolution = 0;
        std::uint32_t m_vctCascadeCount = 0;
        std::uint8_t m_vctHistoryIndex = 0;
        bool m_vctHistoryValid = false;
        glm::mat4 m_vctPreviousView{1.0f};
        rhi::Texture m_ssaoRawTarget;
        std::array<rhi::Texture, 2> m_ssaoHistoryTargets;
        std::unique_ptr<PostProcessResourcePool> m_postProcessResourcePool;
        std::size_t m_postProcessBufferCursor = 0;
        std::uint8_t m_taaHistoryIndex = 0;
        bool m_taaHistoryValid = false;
        std::uint8_t m_exposureHistoryIndex = 0;
        bool m_exposureHistoryValid = false;
        std::uint8_t m_ssaoHistoryIndex = 0;
        bool m_ssaoHistoryValid = false;
        rhi::Texture m_depthTarget;
        rhi::Texture m_temporalUpscalerOutput;
        std::array<rhi::Texture, 4> m_shadowColorTargets;
        std::array<rhi::Texture, 4> m_shadowDepthTargets;
        std::array<std::uint32_t, 4> m_shadowResolutions{};
        std::array<std::uint64_t, 4> m_shadowContentSignatures{};
        std::array<bool, 4> m_shadowCacheValid{};
        std::uint32_t m_width = 0;
        std::uint32_t m_height = 0;
        std::uint32_t m_outputWidth = 0;
        std::uint32_t m_outputHeight = 0;
        std::uint32_t m_postProcessWidth = 0;
        std::uint32_t m_postProcessHeight = 0;
        rhi::TemporalUpscalerOptions m_upscalerOptions;
        float m_materialMipLodBias = 0.0f;
        bool m_temporalUpscalerEvaluatedLastFrame = false;
        std::uint64_t m_frameIndex = 0;
        glm::mat4 m_inverseViewProjection{1.0f};
        glm::mat4 m_postProcessView{1.0f};
        glm::mat4 m_postProcessProjection{1.0f};
        glm::vec4 m_postProcessCameraPosition{0.0f};
        glm::mat4 m_previousViewProjection{1.0f};
        std::vector<glm::mat4> m_previousModels;
        bool m_hasPreviousFrame = false;
        rhi::TextureHandle m_outputColor;
        BasicRendererFrameStats m_frameStats;
    };
}
