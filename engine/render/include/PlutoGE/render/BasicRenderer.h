#pragma once

#include "PlutoGE/render/AmbientProbeConfig.h"
#include "PlutoGE/render/CloudOcclusion.h"
#include "PlutoGE/render/GeometryDiagnostics.h"
#include "PlutoGE/render/ShadowCasterLods.h"
#include "PlutoGE/render/ShadowGeometry.h"
#include "PlutoGE/render/ShadowFaceCache.h"
#include <optional>
#include "PlutoGE/render/FoliageWind.h"
#include "PlutoGE/render/ImpostorLayout.h"
#include "PlutoGE/render/LocalLight.h"
#include "PlutoGE/render/LodDistanceScale.h"
#include "PlutoGE/render/Outline.h"
#include "PlutoGE/render/ShaderGraph.h"
#include "PlutoGE/render/TerrainLayerLimits.h"
#include "PlutoGE/render/TextureCompression.h"
#include "PlutoGE/render/DeferredLocalLightPolicy.h"
#include "PlutoGE/render/FogFroxels.h"
#include "PlutoGE/render/OceanParameters.h"
#include "PlutoGE/render/PhysicalSkyLayout.h"
#include "PlutoGE/render/SkyIrradiance.h"

#include "PlutoGE/render/VctProbeCache.h"
#include "PlutoGE/render/VctRelighting.h"
#include "PlutoGE/render/RenderDebugView.h"
#include "PlutoGE/render/ShadowMethod.h"
#include "PlutoGE/render/VirtualShadowMaps.h"
#include "PlutoGE/render/OcclusionCulling.h"

#include "PlutoGE/render/rhi/Resource.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace PlutoGE::render
{
    class RhiGpuSkinning;
    class DeferredProbeIrradiance;
    class BackgroundMotion;
    class UpscalerSharpening;
    class CloudTemporalFilter;
    class FogFroxelVolume;
    class InstanceStorage;
    class InstanceBinning;
    class CloudNoiseVolume;
    class ScreenSpaceGuide;
    class ScreenSpaceAmbientOcclusion;
    class ScreenSpaceReflections;
    class ScreenSpaceGlobalIllumination;
    class ScreenSpaceContactShadows;
    class DirectionalShadowMask;
    class DeferredLocalLights;
    class OutlinePass;
    class DdgiVolume;
    class VoxelDdgiRaySource;
    class RayQueryDdgiRaySource;
    class RayTracingScene;
    struct ScreenSpaceView;
    struct RhiGpuSkinningSource;
    struct RhiGpuSkinningState;
    struct RhiGpuSkinningDispatch;
    class Mesh;
    class PostProcessResourcePool;
    class PersistentParameterCache;
    class MaterialPreparationCache;
    class GeometryObjectStream;
    class ShaderGraphVariantCache;
    class OpaqueShadowBatches;
    class GeometryArena;
    class CascadedShadowLayers;
    class PointShadowLayers;
    class LightClusterGrid;
    class GpuDrivenOpaque;
    class GpuInstanceCulling;
    struct BindlessMaterialFallbacks;

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
        Ocean,
        DDGI,
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
        Albedo = 1u << 5u,
        Indirect = 1u << 6u,
        EnvironmentSpecular = 1u << 7u,
    };

    // Reference spiral remains available for controlled AO comparisons.
    enum class BasicSsaoEstimator : std::uint8_t { Spiral, GTAO };

    enum class GeometryOutputLayout : std::uint8_t
    {
        Color, ColorMotion, Surface, SurfaceIndirect, SurfaceAllIndirect, SurfaceReflections, Diagnostics
    };

    enum class BasicPostProcessStage : std::uint8_t
    {
        // Replaces the geometry pass's ambient diffuse light; screen-space GI and AO build on the result.
        ProbeLighting,
        LightingComposite,
        AmbientOcclusion,
        ScreenSpaceAtmosphere,
        WaterComposite,
        CloudComposite,
        FogComposite,
        TemporalResolve,
        CameraOptics,
        Exposure,
        ToneAndColor,
    };

    [[nodiscard]] constexpr BasicPostProcessStage StageFor(BasicPostProcessEffectType type) noexcept
    {
        switch (type)
        {
        case BasicPostProcessEffectType::DDGI:
            return BasicPostProcessStage::ProbeLighting;
        case BasicPostProcessEffectType::SSGI:
        case BasicPostProcessEffectType::SceneComposite:
        case BasicPostProcessEffectType::VCTGI:
            return BasicPostProcessStage::LightingComposite;
        case BasicPostProcessEffectType::SSAO:
            return BasicPostProcessStage::AmbientOcclusion;
        case BasicPostProcessEffectType::SSR:
        case BasicPostProcessEffectType::PhysicalSky:
            return BasicPostProcessStage::ScreenSpaceAtmosphere;
        case BasicPostProcessEffectType::Ocean:
            return BasicPostProcessStage::WaterComposite;
        case BasicPostProcessEffectType::VolumetricCloud:
            return BasicPostProcessStage::CloudComposite;
        case BasicPostProcessEffectType::VolumetricFog:
            return BasicPostProcessStage::FogComposite;
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
        case BasicPostProcessEffectType::Ocean:
            return BasicPostProcessInput::Depth | BasicPostProcessInput::Normal;
        case BasicPostProcessEffectType::DepthOfField:
            return BasicPostProcessInput::Depth;
        case BasicPostProcessEffectType::TAA:
            return BasicPostProcessInput::Depth | BasicPostProcessInput::Normal |
                   BasicPostProcessInput::Motion | BasicPostProcessInput::History;
        case BasicPostProcessEffectType::SSAO:
            // Material roughness and the environment reflection, for specular occlusion (GeometryInputsFor drops
            // them when it is off).
            return BasicPostProcessInput::Depth | BasicPostProcessInput::Normal | BasicPostProcessInput::Motion |
                   BasicPostProcessInput::Indirect | BasicPostProcessInput::Material |
                   BasicPostProcessInput::EnvironmentSpecular;
        case BasicPostProcessEffectType::SSGI:
            return BasicPostProcessInput::Depth | BasicPostProcessInput::Normal | BasicPostProcessInput::Material |
                   BasicPostProcessInput::Albedo | BasicPostProcessInput::Motion | BasicPostProcessInput::Indirect;
        case BasicPostProcessEffectType::DDGI:
            // The environment reflection, whose sky share DDGI occludes.
            return BasicPostProcessInput::Depth | BasicPostProcessInput::Normal | BasicPostProcessInput::Material |
                   BasicPostProcessInput::Albedo | BasicPostProcessInput::Indirect |
                   BasicPostProcessInput::EnvironmentSpecular;
        case BasicPostProcessEffectType::VCTGI:
            return BasicPostProcessInput::Depth | BasicPostProcessInput::Normal;
        case BasicPostProcessEffectType::SSR:
            return BasicPostProcessInput::Depth | BasicPostProcessInput::Normal | BasicPostProcessInput::Motion |
                   BasicPostProcessInput::Material | BasicPostProcessInput::Albedo | BasicPostProcessInput::EnvironmentSpecular;
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
        // w=1 carries an explicitly deformed previous position. Static mesh
        // aggregates keep w=0 and use position for object-only motion.
        std::array<float, 4> previousPosition{};
        std::array<float, 2> uv2{};
    };

    struct BasicMeshData
    {
        std::span<const BasicVertex> vertices;
        std::span<const std::uint32_t> indices;
    };

    // The BasicLitTerrain variant: the standard material path with terrain layer blending. Draws carrying
    // BasicDraw::terrainLayers use it for every geometry layout; without it they fall back to the standard path.
    struct BasicTerrainShaderPackage
    {
        std::array<rhi::GraphicsPipelineDescriptor::ShaderCode, 2> vertices, colorVertices; // plain, instanced
        rhi::GraphicsPipelineDescriptor::ShaderCode fragment, coverageFragment, indirectFragment;
        std::array<rhi::GraphicsPipelineDescriptor::ShaderCode, 2> compactFragments;        // colour, colour+motion
        [[nodiscard]] bool Available() const
        {
            return (!fragment.glsl.empty() || !fragment.spirv.empty()) && (!vertices[0].glsl.empty() || !vertices[0].spirv.empty());
        }
    };

    // Impostor cards (BasicLitImpostor.slang): vertex stages that place a camera-facing card per instance and pick its
    // atlas frame, the second for colour-only layouts and coverage depth. They pair with the standard fragments, so
    // impostors shade exactly as standard materials do. Without them draws carrying BasicDraw::impostor are skipped.
    struct BasicImpostorShaderPackage
    {
        rhi::GraphicsPipelineDescriptor::ShaderCode vertex, colorVertex;
        [[nodiscard]] bool Available() const
        {
            return (!vertex.glsl.empty() || !vertex.spirv.empty()) && (!colorVertex.glsl.empty() || !colorVertex.spirv.empty());
        }
    };

    // GPU instance culling (InstanceCulling.slang, GpuInstanceCulling): count, scan and scatter passes that bin instanced
    // draws' instances by LOD on the GPU. Without them the renderer bins on the CPU.
    struct BasicInstanceCullingShaderPackage
    {
        rhi::ComputePipelineDescriptor::ShaderCode count, scan, scatter;
        [[nodiscard]] bool Available() const
        {
            const auto present = [](const auto &code) { return !code.glsl.empty() || !code.spirv.empty(); };
            return present(count) && present(scan) && present(scatter);
        }
    };

    // GPU-driven opaque draws (BasicLitBindless.slang, GpuDrivenOpaque): the vertex stage for every geometry layout
    // but Color, and Color's; fragments for Surface and wider layouts, Color, ColorMotion and SurfaceIndirect; and
    // the pass copying occlusion results into the multi-draw commands. Without them every draw binds its material.
    struct BasicBindlessShaderPackage
    {
        rhi::GraphicsPipelineDescriptor::ShaderCode vertex, colorVertex;
        std::array<rhi::GraphicsPipelineDescriptor::ShaderCode, 4> fragments;
        rhi::ComputePipelineDescriptor::ShaderCode visibility;
        [[nodiscard]] bool Available() const noexcept;
    };

    // Dynamic diffuse GI (src/gi): the voxel ray source's trace, the probe update stages (DDGIProbeUpdate.slang), the
    // screen-space apply and the debug view's probe impostors (DDGIApply.slang). Without them DDGI leaves frames as
    // they are.
    struct BasicDdgiShaderPackage
    {
        rhi::ComputePipelineDescriptor::ShaderCode trace, visibility, reset, state, blendIrradiance, blendDistance;
        BasicPostProcessShaderPackage apply, probes;
        // Half-resolution gather and interpolating apply (DdgiSettings::applyResolutionDivisor); optional.
        BasicPostProcessShaderPackage gather, upsampledApply;
        // The hardware ray tracing source (DDGIRayQueryTrace.slang): SPIR-V only.
        rhi::ComputePipelineDescriptor::ShaderCode rayQueryTrace, rayQueryVisibility;
    };


    struct BasicRendererShaderPackage
    {
        rhi::GraphicsPipelineDescriptor::ShaderCode vertex;
        rhi::GraphicsPipelineDescriptor::ShaderCode instancedVertex;
        rhi::GraphicsPipelineDescriptor::ShaderCode fragment;
        rhi::GraphicsPipelineDescriptor::ShaderCode standardFragment;
        std::array<rhi::GraphicsPipelineDescriptor::ShaderCode, 2> indirectFragments;
        rhi::GraphicsPipelineDescriptor::ShaderCode colorVertex, colorInstancedVertex;
        std::array<rhi::GraphicsPipelineDescriptor::ShaderCode, 2> standardVertices, standardColorVertices;
        // Color-only and color+motion entry points, for graph and standard materials.
        std::array<std::array<rhi::GraphicsPipelineDescriptor::ShaderCode, 2>, 2> compactFragments;
        std::array<rhi::GraphicsPipelineDescriptor::ShaderCode, 2> coverageFragments;
        std::array<BasicPostProcessShaderPackage, 2> opaqueDepth;
        BasicTerrainShaderPackage terrain;
        BasicImpostorShaderPackage impostor;
        BasicInstanceCullingShaderPackage instanceCulling;
        BasicBindlessShaderPackage bindless;
        // Optional per-graph specialisations of fragment, compactFragments[0]
        // and coverageFragments[0]. Null keeps graphs interpreted.
        std::shared_ptr<ShaderGraphVariantCache> graphVariants;
        rhi::GraphicsPipelineDescriptor::ShaderCode transparentFragment, particleBoundaryFragment;
        BasicPostProcessShaderPackage glassSceneCopy;
        BasicPostProcessShaderPackage glassColorCopy, glassDepthCopy;
        BasicPostProcessShaderPackage decals, decalSnapshot;
        BasicPostProcessShaderPackage skyQuadrature;
        BasicPostProcessShaderPackage cloudShadow;
        BasicPostProcessShaderPackage cloudTemporal;
        // Reference SSR: trace, upsample and composite, horizontal filter, the trace for fewer than 16 rays per
        // pixel, and the vertical filter. The filters and traces run at trace resolution.
        std::array<BasicPostProcessShaderPackage, 5> ssrStages;
        // Stochastic SSR (SSRStochastic.slang): trace (compute), ray reuse, temporal accumulation and the guided
        // composite. Without them SSR uses the reference technique.
        rhi::ComputePipelineDescriptor::ShaderCode ssrTrace;
        BasicPostProcessShaderPackage ssrReuse, ssrTemporal, ssrComposite;
        // Surface guide shared by screen-space effects, and its variant that also builds a depth hierarchy.
        rhi::ComputePipelineDescriptor::ShaderCode screenSpaceGuide, screenSpaceGuideHierarchy;
        rhi::GraphicsPipelineDescriptor::ShaderCode shadowVertex;
        rhi::GraphicsPipelineDescriptor::ShaderCode shadowInstancedVertex;
        rhi::GraphicsPipelineDescriptor::ShaderCode shadowFragment;
        rhi::GraphicsPipelineDescriptor::ShaderCode maskedShadowFragment;
        BasicPostProcessShaderPackage opaqueShadow;
        BasicPostProcessShaderPackage shadowDepthSeed;
        VirtualShadowShaders virtualShadows;
        OcclusionShaders occlusion;
        // Clustered local-light culling (LightClusterCulling.slang). Without it every light is evaluated.
        rhi::ComputePipelineDescriptor::ShaderCode lightClusterCulling;
        BasicPostProcessShaderPackage displayOutput;
        // Optional screen-space outlines composited over the display output (SelectionOutline.slang, OutlinePass).
        BasicPostProcessShaderPackage outline;
        std::array<BasicPostProcessShaderPackage,
                   static_cast<std::size_t>(BasicPostProcessEffectType::Count)>
            postProcess;
        std::array<BasicPostProcessShaderPackage, 4> bloom;
        std::array<BasicPostProcessShaderPackage, 2> autoExposure;
        // Raw AO (spiral), denoise + temporal resolve, raw AO (horizons); then the extra denoise passes and the
        // full-resolution upsample and composite.
        std::array<BasicPostProcessShaderPackage, 3> ssao;
        BasicPostProcessShaderPackage ssaoDenoise, ssaoUpsample;
        // Directional contact shadows traced after the depth prepass (ContactShadows.slang).
        BasicPostProcessShaderPackage contactShadows;
        // The directional shadow mask (DirectionalShadowMask.slang, Vulkan); without it surfaces filter in place.
        BasicPostProcessShaderPackage directionalShadowMask;
        // Deferred local lights (DeferredLocalLights.slang, Vulkan); without it surfaces light local lights in place.
        BasicPostProcessShaderPackage deferredLocalLights;
        // Reduced-resolution trace and full-resolution composite of deferred probe irradiance.
        BasicPostProcessShaderPackage diffuseProbeTrace, diffuseProbeComposite;
        // Optional: the resolve that also applies baked reflection terms (DiffuseProbeResolve.slang).
        BasicPostProcessShaderPackage diffuseProbeTraceReflections, diffuseProbeCompositeReflections;
        // Temporal reconstruction inputs/outputs: background camera motion and upscaler sharpening.
        BasicPostProcessShaderPackage backgroundMotion, upscalerSharpening;
        std::array<rhi::ComputePipelineDescriptor::ShaderCode, 5> vctCompute;
        rhi::GraphicsPipelineDescriptor particles;
        rhi::GraphicsPipelineDescriptor::ShaderCode particleInstancedVertex;
        std::array<BasicPostProcessShaderPackage, 2> volumetricTrace; // fog, clouds
        BasicPostProcessShaderPackage volumetricComposite;
        // Optional: the fog's froxel lighting volume (FogFroxels.slang): its injection, and the march reading it at
        // full resolution and as the reduced-resolution trace. Without them fog evaluates its lighting per step.
        BasicPostProcessShaderPackage fogFroxels;
        std::array<BasicPostProcessShaderPackage, 2> fogFroxelMarch; // full resolution, trace
        BasicPostProcessShaderPackage fusedColor;
        rhi::GraphicsPipelineDescriptor vctVoxelization;
        std::array<BasicPostProcessShaderPackage, 2> vctPostProcess; // cone trace, temporal resolve
        // Screen-space GI (SSGI.slang): trace (compute), edge-aware filter, temporal accumulation and composite.
        rhi::ComputePipelineDescriptor::ShaderCode ssgiTrace;
        BasicPostProcessShaderPackage ssgiFilter, ssgiTemporal, ssgiComposite;
        BasicDdgiShaderPackage ddgi;
        rhi::ComputePipelineDescriptor::ShaderCode skinning;
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
        [[nodiscard]] std::uint32_t GetIndexCount() const noexcept { return m_indexCount; }
        [[nodiscard]] std::uint64_t GetRevision() const noexcept { return m_revision; }
        [[nodiscard]] std::span<const ShadowGeometryCluster> GetShadowClusters() const noexcept { return m_shadowClusters; }
        // Where the geometry lives: buffers of the mesh's own, or a range of a geometry arena page shared with other
        // meshes. Either way indices are stored rebased by the mesh's first vertex, so draws use a vertex offset of
        // zero, and the mesh's indices start at BaseIndex in IndexBuffer: bind the index buffer at that offset to draw
        // with mesh-relative first indices, or at zero and add BaseIndex to batch draws of several meshes.
        [[nodiscard]] rhi::BufferHandle VertexBuffer() const noexcept { return m_vertexBuffer; }
        [[nodiscard]] rhi::BufferHandle IndexBuffer() const noexcept { return m_indexBuffer; }
        [[nodiscard]] std::uint32_t BaseIndex() const noexcept { return m_baseIndex; }
        // The mesh's first vertex in VertexBuffer, and how many it has.
        [[nodiscard]] std::uint32_t BaseVertex() const noexcept { return m_baseVertex; }
        [[nodiscard]] std::size_t VertexCount() const noexcept { return m_vertexCount; }
        // Whether the GPU vertices hold the mesh's geometry and keep it: uploaded, and not rewritten by skinning.
        [[nodiscard]] bool HasStaticGpuVertices() const noexcept
        {
            return m_pendingVertices.empty() && !m_pendingSharedVertices && !m_gpuSkinningState;
        }

    private:
        friend class BasicRenderer;
        // Owned buffers, or the arena range (kept alive here) for meshes in a geometry arena.
        rhi::Buffer m_ownedVertexBuffer, m_ownedIndexBuffer;
        std::shared_ptr<const void> m_arenaRange;
        rhi::BufferHandle m_vertexBuffer, m_indexBuffer;
        std::uint32_t m_baseVertex = 0, m_baseIndex = 0;
        std::uint32_t m_indexCount = 0;
        std::size_t m_vertexCount = 0;
        std::uint64_t m_revision = 0;
        mutable std::vector<BasicVertex> m_pendingVertices;
        mutable std::shared_ptr<const std::vector<BasicVertex>> m_pendingSharedVertices;
        std::shared_ptr<RhiGpuSkinningState> m_gpuSkinningState;
        std::vector<std::uint32_t> m_shadowIndices;
        std::vector<ShadowGeometryCluster> m_shadowClusters;
    };

    // A terrain draw's layers on the GPU (RhiSceneRenderer builds them from a TerrainLayerSet): two weight maps
    // over the terrain's UVs and two 2D arrays with one layer per terrain layer (see TerrainLayerArrayPixels).
    struct BasicTerrainLayers
    {
        std::array<rhi::TextureHandle, kTerrainWeightMaps> weights{};
        rhi::TextureHandle albedo, normal;
        std::uint32_t layerCount = 0;
        float heightBlend = 0.0f;
        float detailDistance = 0.0f; // 0 = layers at every distance
        float detailFade = 0.0f;     // band beyond detailDistance over which the layers fade out
        std::array<glm::vec4, kMaxTerrainLayers> tiling{}; // x 1 / tile size, y normal strength, z roughness, w metallic
        std::array<glm::vec4, kMaxTerrainLayers> tint{};   // rgb tint, w anti-tiling (0 or 1)
        // Owns the arrays (RhiTerrainLayerCache) while any draw refers to them.
        std::shared_ptr<const void> owner;

        bool operator==(const BasicTerrainLayers &) const = default;
    };

    // Optional material maps beyond the core set (base colour, normal, metallic, roughness, emission). They bind
    // as a group from BasicRenderer::SurfaceTextureSlot (set 1 bindings from 26 in BasicLit.slang); the material
    // parameters record which are present and which channel each samples. Append new maps before Count.
    enum class BasicSurfaceTexture : std::uint8_t
    {
        Occlusion,
        ClearCoat,
        ClearCoatRoughness,
        SheenColor,
        SheenRoughness,
        DiffuseTransmission,
        Count,
    };
    inline constexpr std::size_t BasicSurfaceTextureCount = static_cast<std::size_t>(BasicSurfaceTexture::Count);

    // An instanced draw's LOD chain (foliage), so each instance chunk takes the LOD its nearest instance needs
    // rather than the LOD of the whole cluster: a 64 m foliage cell beside the camera would otherwise draw every
    // tree in it at full detail. Index ranges are in the draw's mesh; thresholds follow Mesh::SelectSubmeshLod.
    struct BasicInstanceLod
    {
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
        float maxScreenRadiusPixels = 0.0f;
    };
    struct BasicInstanceLodChain
    {
        std::vector<BasicInstanceLod> lods;
        glm::vec3 boundsCenter{0.0f}; // the mesh's bounding sphere, in its own space
        float boundsRadius = 0.0f;
        std::uint32_t minimumLod = 0;
        // Multiplies every projected radius before selection (LodDistanceScale.h). All of this chain's radii and its
        // triangle budget are compared with the scaled radius, so they all move with it. A mesh chain and its impostor's
        // must share it for their hand-over to meet (MakeImpostorDraw).
        float distanceScale = DefaultLodDistanceScale;
        // The projected radii this draw covers, [minimum, maximum): a mesh with an impostor hands instances smaller
        // than the impostor's threshold to its impostor draw, whose chain covers just those.
        float minScreenRadiusPixels = 0.0f;
        float maxScreenRadiusPixels = std::numeric_limits<float>::infinity();
        // Most triangles a LOD may spend per pixel of the instance's projected bounding disc. The importer's
        // screen-radius thresholds are shared by every mesh of a profile, so they suit no mesh in particular: a
        // multi-million-triangle scan would keep tens of thousands of triangles on a tree a few dozen pixels across,
        // while a 60k-triangle tree would leave full detail while still filling much of the screen. 0.15 keeps Verdant's
        // canopy close to full detail at about a third of its triangles (BasicLighting::foliageDetail scales it).
        static constexpr float MaxTrianglesPerPixel = 0.15f;
        // Replaces the mesh's switch radii with this density's: each LOD is kept until it would spend more than
        // MaxTrianglesPerPixel, so a mesh changes detail where its own triangle counts say it should. `triangles[lod]`
        // is the whole mesh's at that LOD, so a tree's bark and leaf submeshes switch together. Select, SelectBlend and
        // GPU binning all read the result.
        void ApplyTriangleDensity(std::span<const float> triangles)
        {
            for (std::size_t lod = 1; lod < lods.size() && lod <= triangles.size(); ++lod)
            {
                const float density = std::sqrt(triangles[lod - 1] / (MaxTrianglesPerPixel * std::numbers::pi_v<float>));
                lods[lod].maxScreenRadiusPixels = std::min(density, lods[lod - 1].maxScreenRadiusPixels);
            }
        }
        // The LOD index (into lods) for an instance whose bounding sphere projects to `pixels` in radius, or
        // NotDrawn() when this draw does not cover that size.
        [[nodiscard]] std::uint32_t NotDrawn() const { return static_cast<std::uint32_t>(lods.size()); }
        [[nodiscard]] std::uint32_t Select(float pixels) const
        {
            pixels *= distanceScale;
            if (lods.empty() || pixels < minScreenRadiusPixels || pixels >= maxScreenRadiusPixels) return NotDrawn();
            const auto last = static_cast<std::uint32_t>(lods.size() - 1);
            std::uint32_t selected = 0;
            for (std::uint32_t lod = 0; lod < lods.size(); ++lod)
                if (pixels <= lods[lod].maxScreenRadiusPixels) selected = lod;
            selected = std::min(std::max(selected, minimumLod), last);
            const float budget = MaxTrianglesPerPixel * std::numbers::pi_v<float> * pixels * pixels;
            while (selected < last && static_cast<float>(lods[selected].indexCount / 3) > budget) ++selected;
            return selected;
        }

        // Instances within this fraction of a switch radius draw both sides of the switch, dithered against each
        // other (BasicLit.slang lodDitherDiscards), so a LOD or impostor change fades in instead of popping.
        static constexpr float TransitionWidth = 0.15f;
        // Dither codes in an instance bin entry's top byte (InstanceStorage::StageBins): the low seven bits are the
        // fade (1..126 of 127), and the top bit marks the coarser side, which draws where the finer one does not.
        static constexpr std::uint32_t DitherShift = 24, IndexMask = (1u << DitherShift) - 1;
        static constexpr std::uint8_t DitherCoarser = 0x80, DitherSteps = 127;
        struct Blend
        {
            std::array<std::uint32_t, 2> lods{};
            std::array<std::uint8_t, 2> codes{};
            std::uint32_t count = 0;
        };
        // Where this draw's LODs switch, as SelectBlend fades between them: switch k sits between levels k - 1 and k
        // (levels run from -1, above the draw's range, through the LODs to lods.size(), below it) at the projected
        // radius below which level k is taken (Select's rules, MaxTrianglesPerPixel included), clamped into the draw's
        // range and non-increasing; equal switches enclose empty levels and act as one, so `coarser[k]` is the
        // coarsest level switch k leads to. GPU instance culling uploads these to fade exactly as the CPU does.
        static constexpr std::uint32_t MaxSwitches = 16;
        struct Switches
        {
            std::array<float, MaxSwitches> radius{};
            std::array<std::uint32_t, MaxSwitches> coarser{};
            // lods.size() + 1, or 0 when the chain has too many LODs to fade (SelectBlend then uses Select).
            std::uint32_t count = 0;
            // The finest LOD drawn (minimumLod, within the chain).
            std::uint32_t first = 0;
        };
        [[nodiscard]] Switches BlendSwitches() const
        {
            Switches result;
            const auto count = static_cast<std::uint32_t>(lods.size());
            if (count == 0 || count + 1 > MaxSwitches) return result;
            result.count = count + 1;
            result.first = std::min(minimumLod, count - 1);
            float bound = maxScreenRadiusPixels;
            for (std::uint32_t k = 0; k <= count; ++k)
            {
                float radius = maxScreenRadiusPixels;
                if (k == count) radius = minScreenRadiusPixels;
                else if (k > result.first)
                    // The finer level also needs enough pixels for its triangles (MaxTrianglesPerPixel).
                    radius = std::max(lods[k].maxScreenRadiusPixels,
                                      std::sqrt(static_cast<float>(lods[k - 1].indexCount / 3) /
                                                (MaxTrianglesPerPixel * std::numbers::pi_v<float>)));
                bound = std::min(bound, std::max(radius, minScreenRadiusPixels));
                result.radius[k] = bound;
            }
            for (std::uint32_t k = 0; k <= count; ++k)
            {
                auto level = k;
                while (level < count && result.radius[level + 1] == result.radius[level]) ++level;
                result.coarser[k] = level;
            }
            return result;
        }
        // The LODs (at most two) this draw bins an instance of radius `pixels` into, with their dither codes; 0 is
        // drawn whole. Sides another draw covers (a mesh's impostor-sized instances) are left out: both draws derive
        // the same fade from the same switch radius, so their dithers complement each other.
        [[nodiscard]] Blend SelectBlend(float pixels) const
        {
            Blend result;
            if (lods.empty() || !(pixels > 0.0f)) return result;
            const auto switches = BlendSwitches();
            if (switches.count == 0)
            {
                if (const auto lod = Select(pixels); lod != NotDrawn()) result = {{lod, 0}, {0, 0}, 1};
                return result;
            }
            pixels *= distanceScale;
            const auto count = static_cast<std::uint32_t>(lods.size());
            const auto add = [&](std::int64_t level, std::uint8_t code)
            {
                if (level < 0 || level >= count) return;
                result.lods[result.count] = static_cast<std::uint32_t>(std::max<std::int64_t>(level, switches.first));
                result.codes[result.count++] = code;
            };
            for (std::uint32_t k = 0; k < switches.count; ++k)
            {
                const float radius = switches.radius[k];
                if (!std::isfinite(radius) || !(radius > 0.0f)) continue;
                const float upper = radius * (1.0f + TransitionWidth);
                if (pixels > upper || pixels < radius * (1.0f - TransitionWidth)) continue;
                const float fade = std::clamp((upper - pixels) / (2.0f * TransitionWidth * radius), 0.0f, 1.0f);
                const auto steps = static_cast<std::uint32_t>(std::lround(fade * DitherSteps));
                const auto finer = static_cast<std::int64_t>(k) - 1;
                const auto coarser = static_cast<std::int64_t>(switches.coarser[k]);
                if (steps == 0) add(finer, 0);
                else if (steps >= DitherSteps) add(coarser, 0);
                else
                {
                    add(finer, static_cast<std::uint8_t>(steps));
                    add(coarser, static_cast<std::uint8_t>(DitherCoarser | steps));
                }
                return result;
            }
            // Outside every band: the last level whose switch the radius is below.
            std::int64_t level = -1;
            for (std::uint32_t k = 0; k < switches.count; ++k)
                if (pixels < switches.radius[k]) level = k;
            add(level, 0);
            return result;
        }
    };

    // An instanced draw's octahedral impostor (ImpostorLayout.h): the layout and the uploaded atlases. A draw that
    // carries one as BasicDraw::impostor draws its instances as camera-facing cards (BasicLitImpostor.slang) through
    // the standard surface shading, with the atlases bound as its base colour and normal maps.
    struct BasicImpostor
    {
        impostor::Parameters parameters;
        rhi::TextureHandle albedo;
        rhi::TextureHandle normalDepth;
    };

    struct BasicDraw
    {
        const BasicMesh *mesh = nullptr;
        glm::mat4 model{1.0f};
        glm::vec4 baseColor{1.0f};
        glm::vec2 uvScale{1.0f};
        rhi::TextureHandle baseColorTexture;
        rhi::TextureHandle normalTexture;
        // How normalTexture's channels hold the normal (Xy for block-compressed BC5 normals).
        NormalTextureEncoding normalEncoding = NormalTextureEncoding::Rgb;
        rhi::TextureHandle metallicTexture;
        rhi::TextureHandle roughnessTexture;
        rhi::TextureHandle emissionTexture;
        std::array<rhi::TextureHandle, BasicSurfaceTextureCount> surfaceTextures{};
        std::array<std::uint32_t, BasicSurfaceTextureCount> surfaceTextureChannels{};
        float occlusionStrength = 1.0f;
        // Layers over the base (SurfaceLayers.slang): none while clearCoat is 0 and sheenColor black.
        float clearCoat = 0.0f;
        float clearCoatRoughness = 0.0f;
        glm::vec3 sheenColor{0.0f};
        float sheenRoughness = 0.5f;
        int emissionTexCoord = 0;
        bool emissionChannelMask = false;
        // RGB selects output colour; W is independent channel intensity.
        std::array<glm::vec4, 3> emissionChannels{{{1,0,0,1}, {0,1,0,1}, {0,0,1,1}}};
        std::array<rhi::TextureHandle,4> graphTextures{};
        std::array<std::uint32_t,4> graphSamplers{};
        float metallic = 0.0f;
        float roughness = 1.0f;
        glm::vec3 emission{0.0f};
        float subsurface = 0.0f;
        glm::vec3 subsurfaceColor{1.0f, 0.35f, 0.2f};
        float subsurfaceRadius = 1.0f;
        // MaterialConfig's diffuse transmission. Above zero the surface is also foliage to GI: partly see-through to
        // voxel and hardware rays (docs/FOLIAGE_TRANSMISSION.md).
        float diffuseTransmission = 0.0f;
        glm::vec3 diffuseTransmissionColor{1.0f};
        float diffuseTransmissionAlbedoTint = 0.0f;
        std::uint32_t surfaceType = 0;
        float transmission = 0.0f;
        float ior = 1.45f;
        float thickness = 0.01f;
        glm::vec3 attenuationColor{1.0f};
        float attenuationDistance = 1.0f;
        bool twoSided = false;
        float outlineWidth = 0.0f;
        glm::vec2 probeDebug{0};
        glm::vec3 outlineColor{0.0f};
        bool outlinePass = false;
        std::shared_ptr<const ShaderGraphProgram> shaderGraphProgram;
        unsigned graphPassOrder = 0;
        float alphaCutoff = 0.5f;
        std::uint32_t alphaMode = 0;
        std::uint32_t metallicChannel = 0;
        std::uint32_t roughnessChannel = 0;
        bool flipNormalY = false;
        bool castsShadow = true;
        glm::vec3 shadowBoundsCenter{0.0f};
        float shadowBoundsRadius = -1.0f;
        // World bounding-sphere radius of the draw's largest instance (of the draw itself when not instanced), for
        // size-based culling: casters too small for a shadow cascade's texels, instances too small to matter to
        // ray-traced GI. Negative when unknown, which never culls.
        float maxInstanceRadius = -1.0f;
        std::uint32_t firstIndex = 0;
        std::uint32_t indexCount = 0;
        bool contributesToGi = true;
        // The surface's mean diffuse reflectance and emitted radiance over its textures (base colour times the albedo
        // texture's mean, less the metallic share; emission times the emission texture's mean), for consumers that
        // shade without sampling textures, such as ray-traced GI hits. Negative when unknown: the textures' pixels
        // were not read, and the constant factors are all there is.
        glm::vec3 meanDiffuse{-1.0f};
        glm::vec3 meanEmission{-1.0f};
        // The mean of the base colour's alpha (masked surfaces' open share of a card); negative when unknown.
        float meanCoverage = -1.0f;
        float normalizedLod = 0.0f;
        std::shared_ptr<const std::vector<glm::mat4>> instanceModels;
        std::shared_ptr<const std::vector<glm::mat4>> previousInstanceModels;
        // With instanceModels: per-chunk LOD selection in the camera passes (shadows keep firstIndex/indexCount).
        std::shared_ptr<const BasicInstanceLodChain> instanceLods;
        // On an instanced shadow caster with LODs: the mesh's LOD chain, and the LOD of it that firstIndex/indexCount
        // draw. Virtual shadow maps draw coarser LODs into the clipmap levels too coarse for this one's detail
        // (SelectShadowCasterLodVariants); no other pass reads them.
        std::shared_ptr<const ShadowCasterLodChain> shadowCasterLods;
        std::uint32_t shadowCasterLod = 0;
        // With instanceLods: the camera passes skip instances whose bounds lie wholly beyond this distance (the
        // producer's draw distance, RenderCommand::maxDrawDistance, which otherwise only culls whole clusters).
        float instanceDrawDistance = std::numeric_limits<float>::max();
        // Set on an impostor draw: its instances draw as this impostor's cards rather than as the mesh.
        std::shared_ptr<const BasicImpostor> impostor;
        // Set on the mesh draw that owns a model's impostor; RhiSceneRenderer adds the impostor draw over the same
        // instances (ImpostorDraws.h). Other renderer passes ignore it.
        std::shared_ptr<const BasicImpostor> spawnsImpostor;
        // Authoritative object history survives visibility and draw-order changes.
        std::optional<glm::mat4> previousModel;
        // Optional current world-space AABB. Negative extents select the
        // existing conservative sphere fallback; never reuse for deformed meshes.
        glm::vec3 occlusionBoundsCenter{0.0f};
        glm::vec3 occlusionBoundsExtents{-1.0f};
        // Renderer-owned immutable preparation token; zero for externally
        // authored/mutable packets. Consumers must retain mesh-revision checks.
        std::uint64_t preparationRevision = 0;
        std::size_t preparedMaterialHash = 0;
        // Set for terrains with material layers: drawn with the BasicLitTerrain variant.
        std::shared_ptr<const BasicTerrainLayers> terrainLayers;
        // Wind response (RenderCommand::foliageWind). Every pass deforms enabled draws the same way, with the
        // scene's wind from BasicLighting::wind (FoliageWindDraws.h).
        FoliageWindResponse foliageWind{};
    };

    // One outline style and the draws whose silhouette it outlines (BasicRenderer::SetOutlines).
    struct BasicOutlineLayer
    {
        OutlineStyle style;
        std::span<const BasicDraw> draws;
    };

    struct BasicParticleVertex
    {
        glm::vec3 position{0};
        glm::vec4 color{1};
        glm::vec2 uv{0};
        // Age (seconds), volume density scale (1 = authored), random, world size.
        glm::vec4 ageDensityRandomSize{0};
        glm::vec3 center{0};
    };
    struct BasicParticleParameters
    {
        glm::mat4 viewProjection{1};
        glm::mat4 inverseProjection{1};
        glm::mat4 view{1};
        // Lanes: material 0-1, shape/depth 2, flipbook 3, smoke 4-6,
        // volume 7-8, local positions/ranges 9-12, colors/types 13-16,
        // local directions 17-20, trail 21, spot cone cosines 22, viewport / transparency clipping 23,
        // volumetric fog 24-27 (the VolumetricFog effect's parameters[0-3], filled by BasicRenderer; zero disables).
        std::array<glm::vec4, 28> values{};
    };
    // Matches the structured buffer in Particles.slang. One record per billboard.
    struct alignas(16) BasicParticleInstance
    {
        glm::vec4 centerRotation{0};
        glm::vec4 color{1};
        // Age (seconds), volume density scale (1 = authored), random, world size.
        glm::vec4 ageDensityRandomSize{0};
    };
    static_assert(sizeof(BasicParticleInstance) == 48);
    struct BasicParticleDraw
    {
        std::vector<BasicParticleVertex> vertices;
        std::vector<BasicParticleInstance> instances;
        BasicParticleParameters parameters;
        rhi::TextureHandle texture;
    };

    struct alignas(16) BasicDecalParameters
    {
        glm::mat4 inverseViewProjection{1}, inverseModel{1};
        glm::vec4 color{1}, projectorNormal{0, 0, 1, 0};
        glm::vec4 material{1, 1, 0, 0.5f}; // UV scale, alpha mode, cutoff.
        glm::vec4 viewport{0}; // inverse size, clip convention bits, texture present.
    };
    static_assert(sizeof(BasicDecalParameters) == 192);
    struct BasicDecalDraw
    {
        BasicDecalParameters parameters;
        rhi::TextureHandle texture;
    };

    struct BasicPointLight
    {
        glm::vec3 position{0.0f};
        float range = 10.0f; // Internal culling/shadow cutoff; scene lights derive this from intensity.
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        bool castsShadows = false;
        // Emitter shape (scene::Light): sphere radius, and for point lights a tube of `sourceLength` along
        // `sourceAxis`. Specular only (SurfaceBrdf.slang).
        float sourceRadius = 0.0f;
        float sourceLength = 0.0f;
        glm::vec3 sourceAxis{0.0f, -1.0f, 0.0f};
        // World length of screen-space contact shadows towards the light (ScreenSpaceContactShadows); 0 disables
        // them. Lights compete for the pass's slots (BasicRenderer::ContactShadowLights).
        float contactShadowLength = 0.0f;
    };

    // A one-sided rectangle emitting along `direction` (Lambertian), shaded with linearly transformed cosines
    // (LtcAreaLights.slang). `light.intensity` is its luminous intensity along the normal, so from afar it lights
    // like a point with a cosine falloff; its radiance is that over its area. Sphere and tube shape fields are unused.
    struct BasicRectLight
    {
        BasicPointLight light;
        glm::vec3 direction{0.0f, -1.0f, 0.0f};
        glm::vec3 right{1.0f, 0.0f, 0.0f};
        float width = 1.0f, height = 1.0f;
    };


    struct BasicIblCapture
    {
        rhi::TextureHandle atlas;
        glm::vec3 origin{0}, size{1};
        float intensity = 0, blendDistance = 0, resolution = 1;
        std::array<glm::vec4, 9> irradiance{};
        // Scale reflections by the ambient volume's irradiance relative to the capture's own.
        bool relightFromAmbient = false;
    };

    struct alignas(16) AmbientProbeVolumeParameters
    {
        glm::vec4 origin{}, size{}, resolutionBase{};
        // xyz world-to-grid rows. w: [0] live sky scale (BakedProbeVolume::skyIrradianceScale),
        // [1] reflection occlusion strength (0 without sky visibility), [2] unused.
        std::array<glm::vec4, 3> worldToGridRows{};
    };
    static_assert(sizeof(AmbientProbeVolumeParameters) == 96);

    // The range of BasicLighting::foliageDetail.
    inline constexpr float MinFoliageDetail = 0.25f, MaxFoliageDetail = 2.0f;

    struct BasicLighting
    {
        // Declare external surface-buffer consumers (for example readback tools).
        BasicPostProcessInput requiredGeometryInputs = BasicPostProcessInput::None;
        GeometryDiagnosticMode geometryDiagnosticMode = GeometryDiagnosticMode::None;
        OcclusionMode occlusionMode = OcclusionMode::Off;
        // Scales instanced draws' projected sizes before their per-instance LOD selection (BasicInstanceLodChain), as
        // foliage detail: below 1, instances take coarser LODs, and hand over to their impostors, from nearer. Clamped
        // to [MinFoliageDetail, MaxFoliageDetail]. GraphicsQuality caps it per player tier.
        float foliageDetail = 1.0f;
        // The scene's wind (WindComponent, SceneEnvironment.cpp). Calm by default: draws with a foliage wind response
        // then render exactly as rigid geometry.
        SceneWindSettings wind{};
        std::vector<BasicPointLight> pointLights;
        // Spot sources for voxel GI; direct surface lighting has its own light path.
        struct SpotLight { BasicPointLight light; glm::vec3 direction{0,-1,0}; SpotCone cone; };
        std::vector<SpotLight> spotLights;
        std::vector<BasicRectLight> rectLights;
        glm::vec3 cameraPosition{0.0f};
        glm::mat4 view{1.0f};
        float ambientIntensity = 0.3f;
        std::array<BasicIblCapture, 4> iblCaptures{};
        bool localIblEnabled = true;
        rhi::TextureHandle diffuseProbeAtlas;
        glm::vec3 diffuseProbeOrigin{0}, diffuseProbeSize{1};
        glm::ivec3 diffuseProbeResolution{0};
        int diffuseProbeVolumeCount = 0, diffuseProbeAtlasWidth = 0;
        std::array<AmbientProbeVolumeParameters, PLUTO_AMBIENT_PROBE_VOLUMES> diffuseProbeVolumes{};
        int diffuseProbeHitPointRow = 0, diffuseProbeSummaryRow = 0;
        // Prepared atlases provide typed volume descriptors.
        bool diffuseProbeVolumesPrepared = false;
        // A volume occludes sky reflections or a capture relights from volumes
        // (BakedEnvironment.slang); a deferred grid then needs the reflection target.
        bool diffuseProbeReflections = false;
        // Directional environment used by the RHI PBR path. Physical sky
        // parameters share the layout used by the sky post-process pass so
        // the background and surface lighting cannot drift apart.
        bool physicalSkyEnabled = false;
        float physicalSkyExposure = 1.0f;
        std::array<glm::vec4, PLUTO_SKY_LANE_COUNT> physicalSkyParameters{};
        // The physical sky's current irradiance, for ambient volumes baked with a live sky
        // (SkyIrradiance.h). Unset without a physical sky: such volumes fall back to flat ambient.
        std::optional<SkyIrradianceSH> skyIrradiance;
        // A physical sky may hand the directional light to the moon at night.
        // Atmosphere passes still need the sun: zero means the directional
        // light is the sun, so sunDirection is -directionalDirection.
        glm::vec3 sunDirection{0.0f};
        // The scene's directional light before the physical sky attenuates it,
        // so raised media (clouds) can resolve their own sunlight. Zero intensity: unknown.
        glm::vec3 sunLightColor{1.0f};
        float sunLightIntensity = 0.0f;
        bool directionalIsMoon = false;
        glm::vec3 directionalDirection{0.4f, -0.8f, 0.3f};
        float directionalIntensity = 1.0f;
        glm::vec3 directionalColor{1.0f};
        bool shadowsEnabled = false;
        ShadowMethod shadowMethod = ShadowMethod::Virtual;
        std::uint32_t virtualShadowPoolPages = PLUTO_VSM_DEFAULT_POOL_PAGES;
        std::uint32_t virtualShadowSpotResolution = 2048;
        std::uint32_t virtualShadowMaxPageAge = 8;
        // An aged page may exceed the triangle budget once, alone, to guarantee progress.
        bool virtualShadowAllowOversizedPages = true;
        float virtualShadowCoarseMinCasterTexels = 0.5f;
        bool virtualShadowClusterCulling = true;
        std::uint32_t virtualShadowPageBudget = 64;
        std::uint32_t virtualShadowTriangleBudget = 1000000;
        bool virtualShadowBatching = true;
        bool virtualShadowAdaptiveBudget = false;
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
        // Cascaded shadow maps skip, per cascade, casters whose largest instance spans fewer texels across than this
        // (BasicDraw::maxInstanceRadius): grass contributes nothing but cost to a far cascade. 0 draws every caster.
        float shadowCasterMinimumTexels = 0.0f;
        // Ray-traced GI leaves out draws whose largest instance's bounding radius is below this, in world units
        // (RayTracingScene). 0 traces every instance.
        float rayTracedMinimumInstanceRadius = 0.0f;
        // World length of the screen-space contact shadow rays towards the directional light; 0 disables them.
        // Contact shadows (local lights' too: BasicPointLight::contactShadowLength) need the depth prepass and
        // shadowsEnabled (ScreenSpaceContactShadows.h).
        float contactShadowLength = 0.0f;
    };

    struct BasicPostProcessEffect
    {
        BasicPostProcessEffectType type = BasicPostProcessEffectType::ToneMapping;
        float exposure = 1.0f;
        float gamma = 2.2f;
        std::uint32_t quality = 0;
        // Effect-specific values are deliberately grouped into aligned lanes.
        // This keeps the GPU ABI stable while new single-input passes are added.
        // SSR reserves lanes 4.xyz, 5 and 6 for its internal passes; lane 3.y
        // selects the technique (0 stochastic, 1 reference) and lane 4.w = 1
        // the full-resolution reference used by GPU benchmarks.
        // VCT uses lane 2.z for trace divisor and lane 4.w for local injection.
        // Lane 5.x is local gain minus one; 5.y is secondary bounce (0 disables).
        // VolumetricFog uses lanes 0-3 for its medium and haze, 4.y/4.z for
        // noise strength/frequency and 6.xyz for wind velocity; the renderer
        // writes 4.x (ocean depth) and 5.xyz (wind offset) per frame.
        // TAA: 0 = history weights + RCAS sharpness, 1 = rejection thresholds +
        // jitter strength, 2 = current/previous jitter (UV), 3.x jitter debug;
        // the renderer writes 3.y (current-sample weight), 4.w and 5.w.
        std::array<glm::vec4, PLUTO_POST_PROCESS_LANES> parameters{};
        glm::mat4 worldToLocal{1.0f};
        const void *historyOwner = nullptr; // CPU-only identity for persistent effect resources
        // Volumetric trace resolution; 1 retains the full-resolution reference.
        std::uint32_t volumetricResolutionDivisor = 2;
        std::shared_ptr<const OceanParameters> ocean;
    };

    // The geometry outputs one effect needs with its settings; InputsFor its type covers every setting.
    [[nodiscard]] inline BasicPostProcessInput GeometryInputsFor(const BasicPostProcessEffect &effect) noexcept
    {
        auto inputs = static_cast<std::uint8_t>(InputsFor(effect.type));
        // SSAO without specular occlusion (lane 2.z) keeps the narrower indirect-only layout.
        if (effect.type == BasicPostProcessEffectType::SSAO && !(effect.parameters[2].z > 0.0f))
            inputs &= static_cast<std::uint8_t>(~static_cast<std::uint8_t>(BasicPostProcessInput::Material |
                                                                           BasicPostProcessInput::EnvironmentSpecular));
        return static_cast<BasicPostProcessInput>(inputs);
    }

    struct BasicRendererFrameStats
    {
        std::size_t gpuSkinningDispatches = 0, gpuSkinningVertices = 0, gpuSkinningPaletteBytes = 0;
        GeometryDiagnosticMode geometryDiagnosticMode = GeometryDiagnosticMode::None;
        std::uint64_t vctVoxelizedTriangles = 0, vctRelitVoxels = 0;
        std::uint32_t vctGeometryBuilds = 0, vctRelightDispatches = 0, vctPublications = 0;
        std::uint64_t vctPublishedVoxels = 0;
        std::uint32_t vctSecondarySlices = 0, vctSecondaryPublications = 0;
        // DDGI probes traced and reset this frame, the rays scheduled for them (inactive probes trace fewer), and the
        // frames one sweep of the whole volume takes.
        std::uint32_t ddgiProbesTraced = 0, ddgiProbesReset = 0, ddgiRaysScheduled = 0, ddgiFramesPerSweep = 0;
        // True when DDGI traced with hardware ray queries this frame, with this many instances; those left out as too
        // small (BasicLighting::rayTracedMinimumInstanceRadius) or beyond the probe volume's reach; whether the top
        // level was rebuilt this frame.
        bool ddgiHardwareRays = false;
        std::size_t rayTracedInstances = 0, rayTracedSkippedInstances = 0, rayTracedOutOfRangeInstances = 0;
        bool rayTracedSceneRebuilt = false;
        std::size_t geometryDepthDraws = 0;
        std::uint32_t geometryColorOutputs = 0;
        std::size_t geometryParameterCreates = 0, geometryParameterReuses = 0;
        std::size_t geometryDraws = 0;
        std::size_t geometryInstances = 0;
        // Instances of LOD-binned draws left out because their bounds lie outside the camera's view or beyond the
        // draw's instanceDrawDistance.
        std::size_t viewCulledInstances = 0;
        // Instance records uploaded this frame and resident across frames (InstanceStorage).
        std::size_t instanceRecordUploads = 0;
        std::size_t residentInstanceRecords = 0;
        std::size_t glassPanes = 0, glassSnapshots = 0;
        std::size_t glassDepthSnapshots = 0, glassSnapshotReuseHits = 0;
        float glassBoundsCpuMs = 0, glassGroupingCpuMs = 0, glassDamageCpuMs = 0;
        float glassCopyRecordingCpuMs = 0, glassDrawRecordingCpuMs = 0;
        std::size_t glassFullFootprints = 0;
        std::uint64_t glassSnapshotPixels = 0;
        std::array<std::size_t, 5> glassBoundsReasons{}, glassGroupBoundaries{};
        std::size_t materialPreparations = 0, materialPreparationHits = 0;
        // Graph draws using a generated variant vs the bytecode interpreter.
        std::size_t graphSpecializedDraws = 0, graphInterpretedDraws = 0;
        // Submitted triangles including instances: opaque, alpha-tested, transparent, outline.
        std::array<std::uint64_t, 4> geometryTriangles{};
        std::size_t shadowCandidates = 0;
        VirtualShadowStats virtualShadows;
        OcclusionStats occlusion;
        bool occlusionActive = false;
        // Local lights shading culled through the cluster grid; zero when every fragment evaluated every light.
        std::size_t clusteredLocalLights = 0;
        // Light-cluster culling dispatches; unchanged views and lights reuse the previous masks.
        std::uint32_t lightClusterUpdates = 0;
        // Opaque draws submitted through GPU-driven multi-draw indirect calls, those calls, and live bindless materials.
        std::size_t gpuDrivenDraws = 0, gpuDrivenBatches = 0, gpuDrivenMaterials = 0;
        // Instanced draws binned by GpuInstanceCulling (their triangles and instances are not counted).
        std::size_t gpuBinnedDraws = 0;
        // Whether GPU instance culling ran its second occlusion phase (GpuInstanceCulling::TwoPhase), and its latest
        // diagnostics (an earlier frame's).
        bool gpuCullingSecondPhase = false;
        InstanceCullingStats instanceCulling;
        // Frame-graph sections run on the async compute queue (zero when its work ran inline or none was needed).
        std::uint32_t asyncComputeSections = 0;
        // The lighting paths this frame's geometry pipelines kept (SurfaceFeature bits, src/SurfaceFeatures.h).
        std::uint32_t surfaceFeatures = 0;
        bool virtualShadowsActive = false;
        // Prepassed surfaces read the directional shadow from the per-pixel mask (DirectionalShadowMask.h).
        bool directionalShadowMask = false;
        // Eligible opaque surfaces took their local lights from the deferred pass (DeferredLocalLights.h).
        bool deferredLocalLights = false;
        std::string directionalShadowStatus = "Disabled";
        std::size_t shadowObjectUploads = 0;
        // Distinct cascade caster materials this frame (each shared by every draw using it).
        std::size_t shadowMaterialUploads = 0;
        std::size_t shadowInstances = 0;
        std::size_t shadowCascadeUpdates = 0;
        std::size_t shadowCascadeCacheHits = 0;
        // Casters the redrawn cascades skipped as narrower than BasicLighting::shadowCasterMinimumTexels.
        std::size_t shadowCastersBelowTexelSize = 0;
        std::size_t pointShadowAtlasUpdates = 0, pointShadowAtlasCacheHits = 0, pointShadowDraws = 0;
        std::size_t pointShadowFaceUpdates = 0, pointShadowFaceHits = 0, pointShadowObjectUploads = 0, pointShadowMaterialUploads = 0;
        std::size_t pointShadowStaticFaceUpdates = 0; // Static layer faces redrawn (PointShadowLayers).
        std::array<std::size_t, 4> pointShadowInvalidations{}; // initial, projection, caster set/range, content
        std::size_t shadowCascadeTargets = 0;
        std::array<std::size_t, 4> shadowDrawsByCascade{};

        [[nodiscard]] std::size_t ShadowDraws() const noexcept
        {
            std::size_t total = 0;
            for (const auto count : shadowDrawsByCascade)
                total += count;
            return virtualShadows.submittedIndirectCommands + total;
        }
    };

    struct BasicRendererTimingStats
    {
        float beginFrameMs = 0.0f;
        float shadowRecordingMs = 0.0f;
        // Instance record preparation (storage residency and uploads), then per-instance culling and LOD binning.
        float instancePreparationMs = 0.0f;
        float instanceBinningMs = 0.0f;
        float geometryRecordingMs = 0.0f;
        float postProcessRecordingMs = 0.0f;
        float cameraCompositionMs = 0.0f;
        float temporalUpscalerMs = 0.0f;
        float submitMs = 0.0f;
        // Shadows and geometry recorded on worker threads this frame.
        bool parallelRecording = false;
    };

    class BasicRenderer
    {
    public:
        // Terrain layer resources (TerrainLayers.slang): weight maps 0..1 and the albedo and normal arrays in four
        // texture slots from TerrainTextureSlot, the layer parameters in a uniform slot.
        static constexpr std::uint32_t TerrainTextureSlot = 3;
        static constexpr std::uint32_t TerrainParameterSlot = 4;
        // The contact shadow mask (BasicLit.slang contactShadowMask, set 0 binding 7).
        static constexpr std::uint32_t ContactShadowSlot = 7;
        // contactShadowParameters' light id for the directional light (BasicLit.slang ContactShadowDirectional).
        static constexpr float ContactShadowDirectional = 32.0f;
        // The first optional surface texture (BasicSurfaceTexture); each further one takes the next slot. On OpenGL
        // these are the texture units after the glass reflection's 32 (NormalizeOpenGLShader.cmake).
        static constexpr std::uint32_t SurfaceTextureSlot = 33;
        // The rectangle-light LTC table (LtcAreaLights.slang ltcTable, set 0 binding 8).
        static constexpr std::uint32_t LtcTableSlot = 8;
        // Instanced draws' records (BasicLit.slang instanceRecords, set 3 binding 1; OpenGL storage binding 1).
        static constexpr std::uint32_t InstanceRecordSlot = 1;
        // Instanced draws' per-instance LOD bins (BasicLit.slang instanceIndices, set 3 binding 2; OpenGL binding 2).
        static constexpr std::uint32_t InstanceIndexSlot = 2;
        // Instanced draws' storage parameters (BasicLit.slang InstanceStorageParameters, set 3 binding 3; OpenGL 18).
        static constexpr std::uint32_t InstanceStorageSlot = 18;
        // The sky lighting atlas in volumetric fog, for the cloud shadow map (VolumetricFog.slang cloudShadowAtlas).
        static constexpr std::uint32_t CloudShadowAtlasSlot = 16;
        // The fog's froxel volume (FogFroxels.slang): this frame's, read by the march, and last frame's, read by the
        // injection. Fog has no normal or material input, so these slots are free.
        static constexpr std::uint32_t FogFroxelSlot = 3, FogFroxelHistorySlot = 4;
        static constexpr std::uint32_t SurfaceTextureBinding = 26;

        // Runs outside a rendering pass with command recording open. The
        // callback may append camera work, but must not begin or submit a frame.
        using BeforeTemporalResolve = std::function<void(BasicRenderer &, glm::vec2)>;
        // Append requires an open command recording, outside a rendering pass.
        // The caller owns its final submission; camera-local resources stay independent.
        enum class RecordingMode { Begin, Append };
        BasicRenderer();
        ~BasicRenderer();
        BasicRenderer(const BasicRenderer &) = delete;
        BasicRenderer &operator=(const BasicRenderer &) = delete;

        bool Initialize(rhi::IRenderDevice &device, const BasicRendererShaderPackage &shaders);
        void Shutdown();
        [[nodiscard]] BasicMesh CreateMesh(const BasicMeshData &data);
        [[nodiscard]] bool SupportsGpuSkinning() const noexcept { return m_gpuSkinning != nullptr; }
        // Orders the mesh's vertices coarsest-LOD-first so distant actors can
        // deform only the prefix their drawn LODs reference.
        std::shared_ptr<RhiGpuSkinningSource> CreateGpuSkinningSource(const Mesh &);
        // Draws use the source's layout-ordered index buffer.
        BasicMesh CreateGpuSkinnedMesh(std::shared_ptr<const RhiGpuSkinningSource>, std::span<const ShadowGeometryCluster> bounds);
        // Deforms layout slots [firstVertex, endVertex). hasHistory means the
        // mesh still holds this actor's previous-frame deformation for all of
        // those slots; otherwise motion history restarts at the current pose.
        void UpdateGpuSkinnedMesh(BasicMesh &, std::span<const glm::mat4> palette, std::uint32_t firstVertex, std::uint32_t endVertex,
                                  bool hasHistory, bool geometryChanged, std::span<const ShadowGeometryCluster> bounds);
        // Stage CPU-deformed vertices; upload after BeginFrame, before any pass
        // reads the buffer. Topology and index buffers remain unchanged.
        // Supplied clusters must conservatively bound the updated vertices and
        // retain the mesh's contiguous index ranges. Empty uses exact CPU bounds.
        void UpdateMeshVertices(BasicMesh &mesh, std::span<const BasicVertex> vertices, bool geometryChanged = true,
                                std::span<const ShadowGeometryCluster> shadowClusters = {});
        // Retains the completed stream until Render records its upload. The
        // caller must not mutate it before then; no CPU staging copy is made.
        void UpdateSharedMeshVertices(BasicMesh &mesh, std::shared_ptr<const std::vector<BasicVertex>> vertices,
                                      bool geometryChanged = true, std::span<const ShadowGeometryCluster> shadowClusters = {});
        bool Resize(std::uint32_t width, std::uint32_t height,
                    std::uint32_t outputWidth = 0, std::uint32_t outputHeight = 0);
        void SetTemporalUpscalerOptions(rhi::TemporalUpscalerOptions options) noexcept;
        // Device timing statistics are published per submission label. Secondary
        // renderers use their own label so they never replace the main scene's.
        void SetSubmissionLabel(std::string label) { m_submissionLabel = std::move(label); }
        // Reference switches for controlled rendering/performance comparisons.
        // Bins instanced draws' instances on the GPU (GpuInstanceCulling) when its shaders are available; off (the
        // default), they bin on the CPU. The images agree. The GPU path adds occlusion against the previous frame's
        // depth, but issues one indirect draw per LOD whether or not its bin is empty (the RHI has no indirect draw
        // count to compact them), which costs more than CPU binning saves on integrated GPUs (docs/FOLIAGE.md). It
        // leaves triangle and instance statistics of the draws it bins uncounted (it never reads its bins back).
        void SetGpuInstanceCulling(bool enabled) noexcept { m_gpuInstanceCullingEnabled = enabled; }
        // Whether the device and shaders allow it (Initialize).
        [[nodiscard]] bool GpuInstanceCullingAvailable() const noexcept { return static_cast<bool>(m_gpuInstanceCulling); }
        void SetGeometryOptimizations(bool compactOutputs, bool depthPrepass, bool materialCulling) noexcept
        { m_compactGeometry = compactOutputs; m_depthPrepass = depthPrepass; m_materialCulling = materialCulling; }
        // Specialises Vulkan geometry pipelines to the lighting paths the frame and material use (src/SurfaceFeatures.h).
        // On by default; the images agree, so switching it off is for comparisons. OpenGL always draws the general shader.
        void SetSurfaceSpecialization(bool enabled) noexcept { m_surfaceSpecialization = enabled; }
        // Instanced shadow-cascade casters with a LOD chain draw each instance chunk at the camera passes' LOD (on by
        // default; off casts whole draws at their own LOD, as virtual shadow maps still do).
        void SetShadowCasterLods(bool enabled) noexcept { m_shadowLods = enabled; }
        // Volumetric clouds with temporal reconstruction trace a quarter of their pixels a frame once their history
        // reprojects (CloudTemporalFilter). On by default; off traces every pixel every frame, for comparisons.
        void SetCloudTraceInterleave(bool enabled) noexcept { m_cloudTraceInterleave = enabled; }
        // Filters the directional shadow once per pixel into a mask that prepassed surfaces read (src/
        // DirectionalShadowMask.h, Vulkan). On by default; off filters in each surface shader, for comparisons.
        void SetDirectionalShadowMask(bool enabled) noexcept { m_shadowMaskEnabled = enabled; }
        // Lights point, spot and rectangle lights once per pixel from the G-buffer for eligible opaque surfaces
        // (src/DeferredLocalLights.h, Vulkan). The policy decides per frame; enabled = false lights them in each
        // surface shader, for comparisons.
        void SetDeferredLocalLights(const DeferredLocalLightPolicy &policy) noexcept { m_deferredLocalLightPolicy = policy; }
        // Prepassed alpha-tested standard surfaces shade with an equal depth test and no discard, so depth is tested
        // before shading (kSurfacePrepassedCoverage, Vulkan). On by default; off keeps the discard, for comparisons.
        void SetPrepassedCoverage(bool enabled) noexcept { m_prepassedCoverage = enabled; }
        // Volumetric fog lighting through a froxel volume (FogFroxels.h); on by default where its shaders exist.
        void SetFogFroxels(const FogFroxelSettings &settings);
        [[nodiscard]] const FogFroxelSettings &GetFogFroxels() const noexcept { return m_fogFroxelSettings; }
        [[nodiscard]] const DeferredLocalLightPolicy &GetDeferredLocalLights() const noexcept { return m_deferredLocalLightPolicy; }
        // Records shadows and geometry on worker threads when the device supports it. The output is identical;
        // switching it off is for comparisons and debugging.
        void SetParallelRecordingEnabled(bool enabled) noexcept { m_parallelRecording = enabled; }
        // Single objects read their constants from a per-frame stream indexed by first instance (devices that
        // support it). The output is identical; switching it off is for comparisons and debugging.
        void SetObjectStreamingEnabled(bool enabled) noexcept { m_objectStreaming = enabled; }
        // Local lights are culled per view cluster before shading. The image is identical; switching it off
        // evaluates every light per fragment, for comparisons.
        void SetClusteredLightingEnabled(bool enabled) noexcept { m_clusteredLighting = enabled; }
        // Standard opaque draws read bindless materials and draw through multi-draw indirect calls (Vulkan with
        // bindless textures). The image is identical; switching it off binds each draw's material, for comparisons.
        void SetGpuDrivenOpaqueEnabled(bool enabled) noexcept { m_gpuDrivenOpaqueEnabled = enabled; }
        // Rigid meshes created afterwards share geometry arena buffers (Vulkan), so shadow-page draws of different
        // meshes batch into one indirect draw. The output is identical; switching it off is for comparisons.
        void SetGeometryArenaEnabled(bool enabled);
        // Binds a mesh's vertex and index buffers for draws with mesh-relative first indices.
        static void BindMeshBuffers(rhi::ICommandContext &commands, const BasicMesh &mesh);
        void SetOpaqueShadowBatchingEnabled(bool enabled) noexcept
        {
            if (m_opaqueShadowBatchingEnabled != enabled)
            {
                m_opaqueShadowBatchingEnabled = enabled;
                m_shadowCacheValid.fill(false);
            }
        }
        void SetStaticShadowCachingEnabled(bool enabled) noexcept;
        // `sceneDepthPlansShadows`: VSM planning reads the coverage depth prepass, so no receiver depth is drawn.
        [[nodiscard]] const char *VirtualShadowUnavailableReason(std::span<const BasicDraw> draws,
                                                               std::span<const BasicDraw> shadowDraws = {},
                                                               bool sceneDepthPlansShadows = false) const;
        void Render(const glm::mat4 &viewProjection, std::span<const BasicDraw> draws);
        void Render(const glm::mat4 &viewProjection, const BasicLighting &lighting, std::span<const BasicDraw> draws,
                    std::span<const BasicPostProcessEffect> postProcessEffects = {},
                    std::span<const BasicDraw> shadowDraws = {},
                    PostProcessDebugView debugView = PostProcessDebugView::None,
                    const rhi::TemporalUpscalerFrame *upscalerFrame = nullptr,
                    const glm::mat4 *motionViewProjection = nullptr,
                    // Allows a caller to append overlays before one final submit.
                    bool submit = true,
                    // Full scene with GI materials, independent of camera visibility.
                    std::span<const BasicDraw> giDraws = {}, std::span<const BasicParticleDraw> particles = {},
                    const BeforeTemporalResolve &beforeTemporalResolve = {}, bool linearOutput = false,
                    std::optional<glm::vec2> sharedClipJitter = std::nullopt,
                    std::span<const BasicDecalDraw> decals = {},
                    RecordingMode recordingMode = RecordingMode::Begin);

        // Camera composition runs on HDR color and geometry inputs before TAA.
        void SetTemporalMetadata(rhi::TextureHandle metadata) noexcept { m_temporalMetadata = metadata; }
        void ResetTemporalHistory() noexcept;
        [[nodiscard]] const glm::mat4 &GetInverseViewProjection() const noexcept { return m_inverseViewProjection; }
        [[nodiscard]] const glm::mat4 &GetPreviousViewProjection() const noexcept { return m_framePreviousViewProjection; }
        // Directional VSM recorded by the Render in progress (or the last one),
        // empty when this frame used another method. Valid for the same recording.
        [[nodiscard]] VirtualShadowReceivers GetDirectionalShadowReceivers() const noexcept { return m_directionalShadowReceivers; }
        // The next Render samples these directional shadows instead of preparing
        // its own. It records no directional casters and no local-light VSM.
        void ReuseDirectionalShadowsForFrame(VirtualShadowReceivers receivers) noexcept { m_reusedShadowReceivers = receivers; }

        [[nodiscard]] rhi::TextureHandle GetColorTexture() const noexcept { return m_outputColor; }
        [[nodiscard]] rhi::TextureHandle GetDepthTexture() const noexcept { return m_depthTarget.Get(); }
        [[nodiscard]] rhi::TextureHandle GetNormalTexture() const noexcept { return m_normalTarget.Get(); }
        [[nodiscard]] rhi::TextureHandle GetMaterialTexture() const noexcept { return m_materialTarget.Get(); }
        [[nodiscard]] rhi::TextureHandle GetMotionTexture() const noexcept { return m_motionTarget.Get(); }
        [[nodiscard]] rhi::TextureHandle GetCoverageTexture() const noexcept { return m_outputCoverage; }
        [[nodiscard]] std::uint32_t GetWidth() const noexcept { return m_width; }
        [[nodiscard]] std::uint32_t GetHeight() const noexcept { return m_height; }
        [[nodiscard]] std::uint32_t GetOutputWidth() const noexcept { return m_outputWidth; }
        [[nodiscard]] std::uint32_t GetOutputHeight() const noexcept { return m_outputHeight; }
        [[nodiscard]] bool IsInitialized() const noexcept { return m_device != nullptr; }
        [[nodiscard]] const BasicRendererFrameStats &GetFrameStats() const noexcept { return m_frameStats; }
        // Diagnostic reference: same scheduling and lighting, full-volume publication.
        void SetIncrementalVctPublicationEnabled(bool enabled) noexcept { m_incrementalVctPublication = enabled; }
        void SetTransparentBackground(bool enabled) noexcept { m_transparentBackground = enabled; }
        // Outlines drawn on the display output of the next Render, in order (later layers on top); layers past
        // MaxOutlineLayers are dropped. The layers and their draws are borrowed and must stay valid until then.
        void SetOutlines(std::span<const BasicOutlineLayer> layers) noexcept { m_outlineLayers = layers; }
        void SetVirtualShadowMembershipCachingEnabled(bool enabled) noexcept
        { if (m_virtualShadows) m_virtualShadows->SetMembershipCachingEnabled(enabled); }
        [[nodiscard]] const BasicRendererTimingStats &GetTimingStats() const noexcept { return m_timingStats; }
        [[nodiscard]] bool WasTemporalUpscalerEvaluated() const noexcept
        {
            return m_temporalUpscalerEvaluatedLastFrame;
        }

    private:
        void PrepareMeshVertexUpdate(BasicMesh &mesh, std::span<const BasicVertex> vertices, bool geometryChanged,
                                     std::span<const ShadowGeometryCluster> shadowClusters);
        [[nodiscard]] rhi::PipelineHandle GeometryPipeline(const BasicDraw &draw, bool instanced,
            GeometryOutputLayout layout, bool depthOnly, bool prepassed);
        // The face culling a surface draw uses: material culling with the model's handedness, or none.
        [[nodiscard]] rhi::CullMode GeometryCullMode(const BasicDraw &draw) const;
        // The GPU-driven (bindless) standard opaque pipeline matching GeometryPipeline for a shading draw.
        [[nodiscard]] rhi::PipelineHandle BindlessGeometryPipeline(const BasicDraw &draw, GeometryOutputLayout layout,
                                                                   bool prepassed);
        // The fallback maps and sampler bound-path materials use, for bindless materials.
        [[nodiscard]] BindlessMaterialFallbacks MaterialFallbacks() const;
        // The material texture LOD bias this frame's temporal reconstruction needs (src/TemporalSampling.h).
        [[nodiscard]] float TemporalMipLodBias(std::span<const BasicPostProcessEffect> postProcessEffects) const noexcept;
        // Recreates the material sampler when its LOD bias changes; bindless materials re-register with it.
        void ApplyMaterialMipLodBias(float bias);
        // Binds a terrain draw's weight maps, layer arrays and layer parameters (terrain pipelines only).
        void BindTerrainLayers(rhi::ICommandContext &commands, const BasicTerrainLayers &terrain);
        void EnsureGeometryTargets(GeometryOutputLayout layout);
        bool m_compactGeometry = true, m_depthPrepass = true, m_materialCulling = true;
        bool m_surfaceSpecialization = true;
        bool m_shadowLods = true;
        bool m_cloudTraceInterleave = true;
        bool m_shadowMaskEnabled = true;
        // This frame's SurfaceFeature bits (src/SurfaceFeatures.h); all set is the general shader.
        std::uint32_t m_surfaceFeatures = ~0u;
        std::array<rhi::GraphicsPipelineDescriptor, 2> m_geometryDescriptors;
        std::array<std::array<rhi::GraphicsPipelineDescriptor::ShaderCode, 2>, 2> m_compactFragments;
        std::array<rhi::GraphicsPipelineDescriptor::ShaderCode, 2> m_coverageFragments;
        std::array<BasicPostProcessShaderPackage, 2> m_opaqueDepth;
        enum class DepthResources { Full, Alpha, Opaque };
        DepthResources GeometryDepthResources(const BasicDraw &draw, bool instanced) const;
        rhi::GraphicsPipelineDescriptor::ShaderCode m_standardFragment;
        std::array<rhi::GraphicsPipelineDescriptor::ShaderCode, 2> m_indirectFragments;
        std::array<rhi::GraphicsPipelineDescriptor::ShaderCode, 2> m_colorVertices, m_standardVertices, m_standardColorVertices;
        BasicTerrainShaderPackage m_terrainShaders;
        BasicImpostorShaderPackage m_impostorShaders;
        // Pipeline state flags plus the specialised graph structure (0 = interpreter or standard).
        struct GeometryPipelineKey
        {
            unsigned state = 0;
            std::uint64_t graphStructure = 0;
            bool operator==(const GeometryPipelineKey &) const = default;
        };
        struct GeometryPipelineKeyHash
        {
            std::size_t operator()(const GeometryPipelineKey &key) const noexcept
            { return std::hash<std::uint64_t>{}(key.graphStructure ^ (std::uint64_t(key.state) * 0x9E3779B97F4A7C15ull)); }
        };
        std::unordered_map<GeometryPipelineKey, rhi::GraphicsPipeline, GeometryPipelineKeyHash> m_geometryPipelines;
        BasicBindlessShaderPackage m_bindlessShaders;
        std::unordered_map<unsigned, rhi::GraphicsPipeline> m_bindlessGeometryPipelines;
        std::unique_ptr<GpuDrivenOpaque> m_gpuDriven;
        bool m_gpuDrivenOpaqueEnabled = true;
        std::unique_ptr<GpuInstanceCulling> m_gpuInstanceCulling;
        bool m_gpuInstanceCullingEnabled = false;
        // Renders so far, and the one whose occlusion hierarchy the next may cull against (GpuInstanceCulling), with
        // its camera position.
        std::uint64_t m_renderCount = 0, m_occlusionHierarchyRender = 0;
        glm::vec3 m_occlusionHierarchyCamera{0.0f};
        std::shared_ptr<ShaderGraphVariantCache> m_graphVariants;
        [[nodiscard]] rhi::TextureHandle RenderFusedColor(rhi::TextureHandle source,
            std::span<const BasicPostProcessEffect> effects, rhi::ICommandContext &commands,
            std::size_t bufferIndex, std::size_t &targetIndex);
        [[nodiscard]] rhi::TextureHandle CompositeVolumetric(rhi::TextureHandle source,
            rhi::TextureHandle trace, std::uint32_t traceWidth, std::uint32_t traceHeight,
            rhi::ICommandContext &commands, std::size_t &targetIndex, rhi::TextureHandle oceanVolumeDepth = {});
        [[nodiscard]] rhi::TextureHandle RenderBloom(rhi::TextureHandle source,
                                                     const BasicPostProcessEffect &effect);
        [[nodiscard]] rhi::TextureHandle RenderAutoExposure(rhi::TextureHandle source,
                                                            const BasicPostProcessEffect &effect,
                                                            rhi::ICommandContext &commands, std::size_t &targetIndex);
        // Screen-space state for this frame's camera and G-buffer (see ScreenSpacePass.h).
        [[nodiscard]] ScreenSpaceView MakeScreenSpaceView(std::uint32_t width, std::uint32_t height) const;
        [[nodiscard]] rhi::TextureHandle RenderSsao(rhi::TextureHandle source,
                                                    const BasicPostProcessEffect &effect,
                                                    rhi::ICommandContext &commands);
        // Builds the guide for `divisor` this frame (once), with any depth hierarchy SSAO needs. Null if unavailable.
        ScreenSpaceGuide *PrepareScreenSpaceGuide(std::uint32_t divisor, std::uint32_t width, std::uint32_t height,
                                                  rhi::ICommandContext &commands);
        [[nodiscard]] rhi::TextureHandle RenderSsr(rhi::TextureHandle source,
                                                   const BasicPostProcessEffect &effect,
                                                   bool temporalResolve,
                                                   rhi::ICommandContext &commands);
        [[nodiscard]] rhi::TextureHandle RenderSsgi(rhi::TextureHandle source,
                                                    const BasicPostProcessEffect &effect,
                                                    rhi::ICommandContext &commands);
        [[nodiscard]] rhi::TextureHandle RenderDdgi(rhi::TextureHandle source,
                                                    const BasicPostProcessEffect &effect,
                                                    const BasicLighting &lighting,
                                                    std::span<const BasicDraw> draws,
                                                    rhi::ICommandContext &commands);
        [[nodiscard]] rhi::TextureHandle RenderVctgi(rhi::TextureHandle source,
                                                     const BasicPostProcessEffect &effect,
                                                     const BasicLighting &lighting,
                                                     std::span<const BasicDraw> draws,
                                                     rhi::ICommandContext &commands);
        // The voxel scene (VCT's progressively voxelized, relit and bounced cascades) that VCT GI cone-traces and DDGI
        // probes ray-march. One effect configures it per frame: VCT GI when present, otherwise DDGI.
        struct VoxelSceneSettings
        {
            std::uint32_t resolution = 64, cascades = 2, updateInterval = 1, commandBudget = 8;
            float volumeSize = 48.0f, updateSpeed = 1.0f, localLightBounce = 1.0f, secondaryBounce = 1.0f;
            // Reserves the last cascade for VCT's stationary world-cache source.
            bool worldCache = false;
            float cacheSize = 432.0f;
            bool injectLocalLights = false;
            const void *owner = nullptr;
        };
        [[nodiscard]] VoxelSceneSettings VoxelSceneFor(const BasicPostProcessEffect &effect) const;
        // Advances the voxel scene at most once per frame; returns how many cascades, finest first, hold a published
        // field. Records its own GPU scopes.
        std::uint32_t UpdateVoxelScene(const VoxelSceneSettings &settings, const BasicLighting &lighting,
                                       std::span<const BasicDraw> draws, rhi::ICommandContext &commands);
        [[nodiscard]] rhi::Buffer &AcquireVctBuffer(std::size_t index);
        void ResetVctResources();
        [[nodiscard]] rhi::Buffer &AcquirePostProcessBuffer(std::size_t index);
        [[nodiscard]] rhi::Texture &AcquirePostProcessTarget(std::size_t index,
                                                            std::uint32_t width,
                                                            std::uint32_t height,
                                                            rhi::Format format = rhi::Format::R16G16B16A16Float);
        void EnsurePointShadowAtlas();
        void EnsureShadowTargets(const BasicLighting &lighting);
        std::unique_ptr<VirtualShadowMaps> m_virtualShadows;
        VirtualShadowReceivers m_directionalShadowReceivers, m_reusedShadowReceivers;
        std::unique_ptr<OcclusionCulling> m_occlusion;
        std::unique_ptr<LightClusterGrid> m_lightClusters;
        bool m_clusteredLighting = true;
        std::unique_ptr<RhiGpuSkinning> m_gpuSkinning;
        std::unique_ptr<DeferredProbeIrradiance> m_deferredProbeIrradiance;
        std::unique_ptr<BackgroundMotion> m_backgroundMotion;
        std::unique_ptr<UpscalerSharpening> m_upscalerSharpening;
        std::unique_ptr<CloudTemporalFilter> m_cloudTemporal;
        std::unique_ptr<InstanceStorage> m_instanceStorage;
        std::unique_ptr<InstanceBinning> m_instanceBinning;
        rhi::Buffer m_instanceStoragePlaceholder;
        std::unique_ptr<CloudNoiseVolume> m_cloudNoise;
        // Surface guides by resolution divisor (full, half), shared by SSAO and SSR.
        std::array<std::unique_ptr<ScreenSpaceGuide>, 2> m_screenSpaceGuides;
        std::unique_ptr<ScreenSpaceAmbientOcclusion> m_ambientOcclusion;
        std::unique_ptr<ScreenSpaceReflections> m_reflections;
        std::unique_ptr<ScreenSpaceGlobalIllumination> m_globalIllumination;
        std::unique_ptr<ScreenSpaceContactShadows> m_contactShadows;
        std::unique_ptr<DirectionalShadowMask> m_shadowMask;
        std::unique_ptr<DeferredLocalLights> m_deferredLocalLights;
        DeferredLocalLightPolicy m_deferredLocalLightPolicy;
        bool m_deferredLocalLightsActive = false;
        bool m_prepassedCoverage = true;
        FogFroxelSettings m_fogFroxelSettings;
        std::unique_ptr<FogFroxelVolume> m_fogFroxels;
        rhi::GraphicsPipeline m_fogFroxelPipeline;
        std::array<rhi::GraphicsPipeline, 2> m_fogFroxelMarchPipelines; // full resolution, trace
        // Whether this frame's prepassed shading pipelines read m_shadowMask (BasicRenderer::Render).
        bool m_shadowMaskActive = false;
        std::unique_ptr<DdgiVolume> m_ddgi;
        std::unique_ptr<VoxelDdgiRaySource> m_ddgiRays;
        // Hardware ray tracing (DdgiRaySource::Hardware): the traced scene and its DDGI source, when the device has it.
        std::unique_ptr<RayTracingScene> m_rayTracingScene;
        std::unique_ptr<RayQueryDdgiRaySource> m_rayQueryDdgiRays;
        const void *m_ddgiOwner = nullptr;
        // The ray source DDGI's probe history was traced with; another one starts it again.
        const void *m_ddgiHistoryRays = nullptr;
        // This frame's scene colour and the ambient light it holds, as DDGI and SSGI leave them; SSAO attenuates
        // that indirect term and SSR replaces the environment reflection where it finds hits. Starts as the geometry
        // pass's colour, indirect and environment specular targets.
        rhi::TextureHandle m_lightingColor, m_lightingIndirect, m_lightingEnvironmentSpecular;
        bool m_ambientRelightingTargets = false;
        // The voxel scene this frame's effects share, and the frame it was last advanced.
        std::optional<VoxelSceneSettings> m_frameVoxelScene;
        std::uint64_t m_voxelSceneFrame = ~0ull;
        std::uint32_t m_voxelSceneCascades = 0;
        // This frame's SSAO hierarchy falloff per guide divisor; zero when no SSAO uses that guide.
        std::array<float, 2> m_screenSpaceHierarchyFalloff{};
        std::vector<RhiGpuSkinningDispatch> m_gpuSkinningDispatches;
        std::uint64_t m_geometrySweepFrame = 0;
        VirtualShadowShaders m_virtualShadowShaders;
        rhi::Texture m_emptyVirtualShadowPageTable;
        std::uint64_t m_nextMeshRevision = 1;
        rhi::Buffer m_emptyVirtualShadowTable;

        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_transparentPipeline;
        rhi::GraphicsPipeline m_transparentTwoSidedPipeline;
        rhi::GraphicsPipeline m_glassSceneCopyPipeline;
        rhi::GraphicsPipeline m_glassColorCopyPipeline, m_glassDepthCopyPipeline;
        rhi::GraphicsPipeline m_skyQuadraturePipeline;
        rhi::Buffer m_skyQuadratureBuffer;
        rhi::Texture m_skyQuadratureTexture;
        // Traces the cloud shadow map into its region of m_skyQuadratureTexture (CloudShadowMap.h).
        rhi::GraphicsPipeline m_cloudShadowPipeline;
        rhi::Buffer m_cloudShadowBuffer;
        // This frame's cloud lighting state (CloudOcclusion.h), shared by every pass that lights with the sky.
        CloudOcclusionParameters m_frameCloudOcclusion{};
        // lighting with the directional light dimmed by the clouds over the camera, for GI ray sources.
        [[nodiscard]] BasicLighting CloudShadedGiLighting(const BasicLighting &lighting) const;
        std::array<glm::vec4, PLUTO_SKY_LANE_COUNT + 1> m_skyEnvironmentParameters{};
        bool m_skyEnvironmentValid = false;
        rhi::Texture m_glassDepthCopy;
        rhi::Buffer m_glassReflectionBuffer;
        rhi::GraphicsPipeline m_pipeline;
        rhi::GraphicsPipeline m_shadowPipeline;
        rhi::GraphicsPipeline m_shadowInstancedPipeline;
        rhi::GraphicsPipeline m_maskedShadowPipeline, m_maskedShadowInstancedPipeline;
        // Cascade casters' materials: this frame's handle per draw, and the buffers animated materials upload into.
        std::vector<rhi::BufferHandle> m_shadowMaterials;
        std::vector<rhi::Buffer> m_shadowMaterialBuffers;
        rhi::GraphicsPipeline m_displayPipeline;
        std::unique_ptr<OutlinePass> m_outlines;
        std::span<const BasicOutlineLayer> m_outlineLayers;
        std::array<rhi::GraphicsPipeline, static_cast<std::size_t>(BasicPostProcessEffectType::Count)> m_postProcessPipelines;
        std::array<rhi::GraphicsPipeline, 2> m_volumetricTracePipelines;
        rhi::GraphicsPipeline m_volumetricCompositePipeline;
        rhi::GraphicsPipeline m_oceanVolumeDepthPipeline;
        rhi::GraphicsPipeline m_fusedColorPipeline;
        std::vector<rhi::Buffer> m_fusedColorBuffers;
        rhi::Buffer m_cameraBuffer;
        rhi::Buffer m_debugViewBuffer;
        std::array<rhi::Buffer, 4> m_shadowCameraBuffers;
        // Shadow object transforms are cascade-independent and are uploaded
        // once per caster, then referenced by every intersecting cascade.
        std::vector<rhi::Buffer> m_shadowObjectBuffers;
        std::vector<rhi::Buffer> m_shadowInstanceBuffers;
        rhi::GraphicsPipeline m_opaqueShadowPipeline;
        std::unique_ptr<OpaqueShadowBatches> m_opaqueShadowBatches;
        std::vector<rhi::Buffer> m_opaqueShadowInstanceBuffers;
        bool m_opaqueShadowBatchingEnabled = true;
        bool m_parallelRecording = true;
        std::unique_ptr<GeometryArena> m_geometryArena;
        bool m_geometryArenaEnabled = true;
        bool m_objectStreaming = true;
        rhi::GraphicsPipeline m_shadowDepthSeedPipeline;
        std::unique_ptr<CascadedShadowLayers> m_shadowLayers;
        // Opt-in: full-resolution depth seeding can cost more than rerasterizing.
        bool m_staticShadowCachingEnabled = false;
        // Reused visibility scratch keeps cascade filtering out of the command-recording
        // loop without introducing per-frame allocations.
        std::array<std::vector<std::size_t>, 4> m_shadowCascadeDrawIndices;
        std::vector<std::uint8_t> m_shadowVisibleInAnyCascade;
        std::vector<std::uint64_t> m_shadowDrawSignatures;
        // The same casters' signatures without the wind's per-frame parameters: what a swaying caster's rest pose
        // casts (VirtualShadowCompute's swayingMask). Equal to m_shadowDrawSignatures for casters that do not sway.
        std::vector<std::uint64_t> m_shadowStillSignatures;
        // Shadow draw signatures by the immutable packet they describe (BasicDraw::preparationRevision), not by list
        // position: the caster list gains and loses draws as the camera moves, and a positional cache then misses for
        // every draw after the change, rehashing each one's instance matrices. Entries unused for a while are dropped.
        struct ShadowSignatureEntry
        {
            std::uint64_t meshRevision = 0, signature = 0, lastUsed = 0;
        };
        std::unordered_map<std::uint64_t, ShadowSignatureEntry> m_shadowSignatureCache;
        std::uint64_t m_shadowSignatureFrame = 0;
        // Each recorded draw owns stable parameters until backend submission.
        // Reusing one buffer causes every Vulkan draw to observe the last upload.
        std::vector<rhi::Buffer> m_postProcessBuffers;
        std::vector<rhi::Buffer> m_oceanBuffers;
        std::size_t m_oceanBufferCursor = 0;
        // Vulkan records the complete frame before execution, so every draw
        // needs stable object data until submission completes.
        std::unique_ptr<PersistentParameterCache> m_geometryParameters;
        std::unique_ptr<GeometryObjectStream> m_objectStream;
        std::unique_ptr<MaterialPreparationCache> m_materialPreparation;

        rhi::Texture m_fallbackTexture;
        rhi::Texture m_fallbackNormalTexture;
        rhi::Texture m_fallbackDataTexture;
        rhi::Texture m_ltcTable;
        rhi::Sampler m_fallbackSampler;
        std::array<rhi::Sampler,4> m_graphSamplers;
        rhi::Sampler m_screenSampler;
        rhi::Sampler m_shadowSampler;
        rhi::Sampler m_vctVolumeSampler;
        rhi::Texture m_colorTarget;
        rhi::Texture m_displayTarget;
        rhi::Texture m_normalTarget;
        rhi::Texture m_materialTarget;
        rhi::Texture m_motionTarget;
        rhi::Texture m_albedoTarget;
        rhi::Texture m_indirectTarget;
        rhi::Texture m_environmentSpecularTarget;
        rhi::Texture m_debugTarget;
        // Reusing ping-pong attachments within a recorded Vulkan chain produced
        // screen-tile corruption. Keep one stable output per ordinary pass.
        std::vector<rhi::Texture> m_postProcessPassTargets;
        struct PostProcessTargetDescription
        {
            rhi::Extent2D size;
            rhi::Format format = rhi::Format::Undefined;
            bool operator==(const PostProcessTargetDescription &) const = default;
        };
        std::vector<PostProcessTargetDescription> m_postProcessPassTargetDescriptions;
        std::array<rhi::Texture, 2> m_taaHistoryTargets;
        std::array<rhi::Texture, 2> m_taaCoverageHistoryTargets;
        std::array<rhi::Texture, 2> m_taaDepthHistoryTargets; // Layered NDC depth beside RGBA16F colour history.
        rhi::TextureHandle m_outputCoverage;
        std::array<rhi::GraphicsPipeline, 4> m_bloomPipelines;
        std::array<rhi::GraphicsPipeline, 2> m_autoExposurePipelines;
        std::array<rhi::Texture, 2> m_exposureHistoryTargets;
        rhi::GraphicsPipeline m_vctProbePipeline;
        rhi::Texture m_vctProbeRadiance, m_vctProbeVisibility;
        glm::vec4 m_vctCacheOriginSize{0.0f}, m_vctCacheConfiguration{0.0f};
        VctProbeSchedule m_vctProbeSchedule;
        std::uint32_t m_vctNextCascade = 0;
        const void *m_vctHistoryOwner = nullptr;
        rhi::GraphicsPipeline m_vctRelightPipeline;
        void RelightVctCascade(std::uint32_t index, const BasicLighting &lighting, float localLightBounce, rhi::ICommandContext &commands);
        void PublishVctCascade(std::uint32_t index, float secondaryGain, rhi::ICommandContext &commands, bool staging = false);
        std::uint32_t AdvanceVctSecondary(std::uint32_t sliceBudget, float gain, rhi::ICommandContext &commands);
        rhi::GraphicsPipeline m_vctResolvePipeline;
        rhi::GraphicsPipeline m_vctBouncePipeline;
        rhi::GraphicsPipeline m_vctDirectionalMipPipeline;
        rhi::GraphicsPipeline m_vctVoxelizationPipeline;
        std::array<rhi::GraphicsPipeline, 2> m_vctPostProcessPipelines;
        struct VctCascade
        {
            std::array<rhi::Texture, 4> accumulation;
            rhi::Texture surfaceRecord, secondaryVolume, directVolume, shadowDepth, shadowColor;
            bool geometryReady = false, relightPending = false, fullRelight = true;
            VctRelightRegion publicationDirty{};
            bool fullPublication = true;
            bool deferredPublication = false;
            std::uint64_t lightingSignature = 0, directionalSignature = 0;
            BasicLighting publishedLighting;
            bool publishedInjectLocalLights = false;
            std::uint32_t nextBounceSlice = 0;
            bool secondaryReady = false, secondaryDirty = false;
            float appliedSecondaryBounce = 0.0f;
            glm::vec3 origin{0.0f};
            float size = 0.0f;
            glm::vec3 pendingOrigin{0.0f};
            float pendingSize = 0.0f;
            std::size_t nextDraw = 0;
            std::uint32_t nextVoxelIndex = 0;
            std::uint64_t contentSignature = 0;
            std::uint64_t pendingSignature = 0;
            std::uint64_t lastUpdateFrame = 0;
            std::vector<BasicDraw> pendingDraws;
            std::vector<std::pair<const BasicMesh *, std::uint64_t>> pendingMeshes;
            // The textures pendingDraws bind (packed handles, VctTextureKey): like the meshes, borrowed from the scene.
            std::vector<std::uint64_t> pendingTextures;
            BasicLighting pendingLighting;
            float pendingGraphTime=0;
            bool pendingInjectLocalLights = false;
            float pendingSecondaryBounce = 0.0f;
            bool secondaryPass = false;
            glm::mat4 pendingShadowMatrix{1.0f};
            std::size_t nextShadowDraw = 0;
            std::uint32_t nextShadowIndex = 0;
            bool shadowReady = false;
            bool valid = false;
            bool rebuilding = false;
        };
        std::array<VctCascade, 3> m_vctCascades;
        bool m_incrementalVctPublication = true;
        std::array<rhi::Texture, 6> m_vctRadianceAtlases;
        std::array<rhi::Texture, 6> m_vctInjectionAtlases;
        // One immutable direct-light snapshot and scratch output are shared by
        // all cascades. Completed secondary volumes remain readable during work.
        rhi::Texture m_vctSecondaryScratch;
        std::uint32_t m_vctBounceCascade = 3, m_vctNextBounceCascade = 0;
        std::uint64_t m_vctBounceLightingSignature = 0;
        std::array<rhi::Texture, 2> m_vctTraceTargets; // Checkerboard halves of the trace grid, half width each.
        std::uint32_t m_vctTraceWidth = 0, m_vctTraceHeight = 0;
        rhi::Texture m_vctCompositeTarget;
        std::array<rhi::Texture, 2> m_vctHistoryTargets;
        std::array<rhi::Texture, 2> m_vctMetadataTargets;
        std::vector<rhi::Buffer> m_vctBuffers;
        std::size_t m_vctBufferCursor = 0;
        std::uint32_t m_vctResolution = 0;
        std::uint32_t m_vctCascadeCount = 0;
        std::uint8_t m_vctHistoryIndex = 0;
        bool m_vctHistoryValid = false;
        std::uint32_t m_vctTraceParity = 0;
        // Settings of the last trace; the untraced checkerboard half is only reusable while they hold.
        std::array<glm::vec4, PLUTO_POST_PROCESS_LANES> m_vctTraceSettings{};
        std::uint32_t m_vctTraceQuality = 0;
        std::uint32_t m_vctReactiveFrames = 0;
        glm::mat4 m_vctPreviousView{1.0f};
        std::unique_ptr<PostProcessResourcePool> m_postProcessResourcePool;
        std::size_t m_postProcessBufferCursor = 0;
        std::uint8_t m_taaHistoryIndex = 0;
        bool m_taaHistoryValid = false;
        bool m_hadCameraComposition = false;
        rhi::TextureHandle m_temporalMetadata;
        glm::mat4 m_framePreviousViewProjection{1.0f};
        std::uint8_t m_exposureHistoryIndex = 0;
        bool m_exposureHistoryValid = false;
        rhi::Texture m_depthTarget;
        rhi::Texture m_temporalUpscalerOutput;
        // Render-resolution reactive mask an upscaler derives from translucent composition.
        rhi::Texture m_upscalerReactiveMask;
        rhi::Extent2D m_upscalerReactiveMaskSize;
        rhi::GraphicsPipeline m_particlePipeline;
        rhi::GraphicsPipeline m_particleBoundaryPipeline, m_particleBoundaryTwoSidedPipeline;
        std::array<rhi::Texture, 2> m_particleTransparencyDepth;
        rhi::GraphicsPipeline m_decalPipeline, m_decalSnapshotPipeline;
        std::vector<rhi::Buffer> m_decalParameters;
        rhi::Texture m_decalColorSnapshot, m_decalAlbedoSnapshot;
        rhi::Extent2D m_decalSnapshotSize;
        rhi::GraphicsPipeline m_particleInstancedPipeline;
        rhi::Buffer m_particleQuadIndices;
        rhi::Buffer m_impostorCardIndices;
        std::vector<rhi::Buffer> m_particleInstances;
        std::vector<std::size_t> m_particleInstanceCapacities;
        rhi::Texture m_particleDepthCopy;
        rhi::Extent2D m_particleDepthSize;
        std::vector<rhi::Buffer> m_particleVertices;
        std::vector<std::size_t> m_particleVertexCapacities;
        std::vector<rhi::Buffer> m_particleParameters;
        std::uint64_t m_pointShadowSignature = 0;
        bool m_pointShadowCacheValid = false;
        rhi::Texture m_pointShadowColor;
        rhi::Texture m_pointShadowDepth;
        std::array<rhi::Buffer, 24> m_pointShadowCameras;
        std::array<ShadowFaceCache, 24> m_pointShadowFaces;
        std::array<std::vector<std::size_t>, 24> m_pointShadowFaceDraws;
        // Stationary casters of each face, kept in a static copy of the atlas
        // and seeded in when a moving caster changes the face (PointShadowLayers).
        std::unique_ptr<PointShadowLayers> m_pointShadowLayers;
        rhi::Texture m_pointShadowStaticColor;
        rhi::Texture m_pointShadowStaticDepth;
        struct PointShadowResources
        {
            rhi::Buffer material;
            std::vector<std::byte> materialBytes;
            std::vector<rhi::Buffer> objects;
            // Instanced draws: one block of up to 64 models per DrawIndexedInstanced.
            std::vector<rhi::Buffer> instanceChunks;
            std::vector<glm::mat4> models;
        };
        std::vector<PointShadowResources> m_pointShadowResources;
        std::array<rhi::Texture, 4> m_shadowColorTargets;
        std::array<rhi::Texture, 4> m_shadowDepthTargets;
        bool m_transparentBackground = false;
        std::array<std::uint32_t, 4> m_shadowResolutions{};
        std::array<std::uint64_t, 4> m_shadowContentSignatures{};
        std::array<std::uint64_t, 4> m_shadowInputSignatures{};
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
        glm::mat4 m_previousMotionViewProjection{1.0f};
        std::vector<glm::mat4> m_previousModels;
        bool m_hasPreviousFrame = false;
        float m_previousGraphTime = 0.0f;
        // This frame's wind (BasicLighting::wind at the shader-graph clock), set before any pass records.
        FoliageWindFrame m_foliageWind{};
        rhi::TextureHandle m_outputColor;
        std::string m_submissionLabel = "Scene";
        BasicRendererFrameStats m_frameStats;
        BasicRendererTimingStats m_timingStats;
    };
}
