#include "PlutoGE/render/VirtualShadowMaps.h"
#include "PlutoGE/render/BasicRenderer.h"
#include "ChangedBufferRecords.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>

namespace PlutoGE::render
{
    namespace
    {
        template<class T> auto Bytes(const T &value) { return std::as_bytes(std::span(&value, 1)); }
        struct alignas(16) DrawParameters
        {
            std::array<glm::mat4, 64> models{};
            glm::uvec4 draw{};
            glm::vec4 alpha{};
            FoliageWindParameters foliageWind{};
        };
        struct alignas(16) RigidDrawParameters
        {
            glm::mat4 model{};
            glm::uvec4 draw{};
            glm::vec4 alpha{};
            FoliageWindParameters foliageWind{};
        };
        static_assert(sizeof(RigidDrawParameters) == 96 + sizeof(FoliageWindParameters));
        // One batched page draw's record (VirtualShadowRaster's BatchedDraw); parameters.w is its first model.
        struct alignas(16) BatchedDraw
        {
            glm::uvec4 parameters{};
            glm::vec4 alpha{};
            FoliageWindParameters foliageWind{};
        };
        static_assert(sizeof(BatchedDraw) == 32 + sizeof(FoliageWindParameters));
        // VirtualShadowCommon's VirtualShadowCaster.
        struct alignas(16) Caster { glm::vec4 bounds; glm::uvec4 draw, identity; glm::vec4 extents; glm::uvec4 still; };
        static_assert(sizeof(Caster) == 80 && sizeof(DrawParameters) == 4128 + sizeof(FoliageWindParameters));
        using InstanceBoundsRecord = std::array<glm::vec4, PLUTO_VSM_MASKED_INSTANCES>;
        static_assert(PLUTO_VSM_LEVELS <= 32, "caster level masks hold one bit per level");
        static_assert(PLUTO_VSM_MASKED_INSTANCES <= 32, "masks hold one bit per model");

        // How far `range` (mesh space) of a model sways from its rest pose under this frame's wind: the reach at the
        // range's height, scaled by the model's vertical scale. Valid while the wind's settings stay the same, which
        // preparation guarantees by rebuilding the chunks whenever they change.
        float WindPad(const FoliageWindFrame &wind, const BasicDraw &draw, const ShadowGeometryCluster &range,
                      const glm::mat4 &model)
        {
            const float top = range.extents.x < 0.0f ? std::numeric_limits<float>::infinity() : range.center.y + range.extents.y;
            const float reach = wind.MaxDisplacement(draw.foliageWind, top);
            return reach > 0.0f ? reach * glm::length(glm::vec3(model[1])) : 0.0f;
        }
        // Grows known world bounds of `range` under `models` by their largest wind reach, so swaying geometry stays
        // inside them.
        void PadForWind(ShadowWorldBounds &bounds, const FoliageWindFrame &wind, const BasicDraw &draw,
                        const ShadowGeometryCluster &range, std::span<const glm::mat4> models)
        {
            if (bounds.sphere.w < 0.0f) return;
            float pad = 0.0f;
            for (const auto &model : models) pad = std::max(pad, WindPad(wind, draw, range, model));
            bounds.sphere.w += pad;
            bounds.extents += glm::vec4(pad, pad, pad, 0.0f);
        }
        // Casters whose draws have no preparation revision may change behind the same packet every frame (skinned and
        // other transient draws). With a static layer, their pages restore the other casters' cached depth and redraw
        // only these.
        bool IsDynamicCaster(const BasicDraw &draw) noexcept { return draw.preparationRevision == 0; }
        // Pages updated per frame at most (parameters.limits.z); caster page lists are sized by it.
        std::uint32_t PageUpdateBudget(const BasicLighting &lighting, std::uint32_t poolCapacity) noexcept
        {
            return std::clamp(lighting.virtualShadowPageBudget, 1u, poolCapacity);
        }
        // Whether a chunk of `models` models plans and sways each model separately (VirtualShadowCommon's masked casters).
        constexpr bool MaskedModels(std::size_t models) noexcept { return models > 1 && models <= PLUTO_VSM_MASKED_INSTANCES; }
        // Subdivision buys little when the entire mesh fits within one page's
        // width. Coalesce those ranges to avoid multiplying CPU submissions.
        // The world bound of an instanced draw's chunk of `count` instances from `first`: its geometry (`range`, mesh
        // space) under each instance's model, padded by the wind's reach. Without it a chunk took the whole draw's
        // bound: one of a foliage cell's thousands of instances was planned into, and as it swayed dirtied, every page
        // the cell touched. Unknown when the draw is not instanced or the range has no bound.
        ShadowWorldBounds InstanceChunkBounds(const FoliageWindFrame &wind, const BasicDraw &draw,
                                              const ShadowGeometryCluster &range, std::size_t first, std::size_t count)
        {
            if (!draw.instanceModels || draw.instanceModels->size() <= 1 || range.extents.x < 0.0f) return {};
            const auto models = std::span<const glm::mat4>(*draw.instanceModels).subspan(first, count);
            auto bounds = ShadowClusterWorldBounds(range, models);
            PadForWind(bounds, wind, draw, range, models);
            return bounds;
        }
        // The world spheres of a masked chunk's models (`range` under each, padded by its wind reach); unused entries
        // and unknown bounds are negative, which pages treat as reaching everywhere.
        InstanceBoundsRecord ModelBounds(const FoliageWindFrame &wind, const BasicDraw &draw,
                                         const ShadowGeometryCluster &range, std::span<const glm::mat4> models)
        {
            InstanceBoundsRecord result;
            result.fill(glm::vec4(0, 0, 0, -1));
            for (std::size_t index = 0; index < std::min(models.size(), result.size()); ++index)
            {
                auto sphere = ShadowClusterWorldSphere(range, models.subspan(index, 1));
                if (sphere.w >= 0.0f) sphere.w += WindPad(wind, draw, range, models[index]);
                result[index] = sphere;
            }
            return result;
        }

        bool FitsShadowPage(const VirtualShadowParameters &parameters, glm::vec4 sphere)
        {
            if (sphere.w < 0) return false;
            if (parameters.camera.w > 0 && 2 * sphere.w > parameters.metrics[0].z) return false;
            for (int level = PLUTO_VSM_DIRECTIONAL_LEVELS; level < PLUTO_VSM_SPOT_ROOT_BASE; ++level)
            {
                if (parameters.origins[level].w == 0) continue;
                const auto &matrix = parameters.matrices[level];
                const auto clip = matrix * glm::vec4(glm::vec3(sphere), 1);
                if (clip.w + sphere.w <= 0) continue;
                const auto nearW = clip.w - sphere.w;
                if (nearW <= 0) return false;
                for (int axis = 0; axis < 2; ++axis)
                {
                    const float axisScale = glm::length(glm::vec3(matrix[0][axis], matrix[1][axis], matrix[2][axis]));
                    const float diameterUv = sphere.w * (axisScale + std::abs(clip[axis] / clip.w)) / nearW;
                    if (diameterUv * parameters.pool.w > 1) return false;
                }
            }
            return true;
        }
        std::uint32_t ProjectionEpoch(const glm::mat4 &view, float depthCentre, float depthRange, float span)
        {
            std::uint32_t hash = 2166136261u;
            const auto append = [&](auto value)
            {
                for (const auto byte : Bytes(value)) { hash ^= std::to_integer<unsigned char>(byte); hash *= 16777619u; }
            };
            append(view); append(depthCentre); append(depthRange); append(span);
            return hash;
        }
    }
    bool VirtualShadowShaders::Complete() const
    {
        const auto present = [](const auto &code) { return !code.glsl.empty() || !code.spirv.empty(); };
        return std::all_of(compute.begin(), compute.end(), present) && std::all_of(raster.begin(), raster.end(), present);
    }
    VirtualShadowParameters VirtualShadowMaps::BuildClipmaps(const BasicLighting &lighting,
                                                                  const VirtualShadowParameters *previous, float resolutionScale)
    {
        VirtualShadowParameters result;
        const auto tiles = VirtualShadowPoolTiles(lighting.virtualShadowPoolPages);
        result.pool = {tiles * tiles, tiles, tiles * PLUTO_VSM_PAGE_SIZE, VirtualShadowSpotGrid(lighting.virtualShadowSpotResolution)};
        result.scheduling = {std::clamp(lighting.virtualShadowMaxPageAge, 1u, 120u), lighting.virtualShadowAllowOversizedPages ? 1u : 0u, 0, 0};
        result.culling = {std::clamp(lighting.virtualShadowCoarseMinCasterTexels, 0.0f, 4.0f),
                          std::max(lighting.wind.shadowSwayDistance, 0.0f), 0, 0};
        glm::vec3 direction = lighting.directionalDirection;
        if (!std::isfinite(glm::dot(direction, direction)) || glm::dot(direction, direction) < 1.e-8f)
            direction = glm::vec3(0, -1, 0);
        direction = glm::normalize(direction);
        const glm::vec3 up = std::abs(direction.y) > 0.99f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
        const glm::mat4 view = glm::lookAt(glm::vec3(0), direction, up);
        const glm::vec3 centre = glm::vec3(view * glm::vec4(lighting.cameraPosition, 1));
        const float distance = std::max(lighting.shadowDistance, 8.0f);
        const float casterDistance = lighting.shadowCasterDistance > 0 ? lighting.shadowCasterDistance : distance;
        // A fixed depth envelope and coarse Z recenter preserve cached pages
        // during normal travel. The envelope includes receivers and upstream casters.
        const float depthRange = std::max(4.0f * (distance + casterDistance), 64.0f);
        const float depthStep = depthRange / 8.0f;
        float depthCentre = std::round(centre.z / depthStep) * depthStep;
        // The root's projection carries the depth centre (any level would; the root always exists).
        if (previous && previous->metrics[previous->levels.x].y == depthRange)
        {
            // A round-to-nearest centre avoids a global invalidation at world
            // zero. Hysteresis keeps the old projection across quantisation
            // boundaries while receivers/casters still have ample depth room.
            const float oldCentre = (previous->matrices[previous->levels.x][3][2] - 0.5f) * depthRange;
            if (std::abs(centre.z - oldCentre) < depthRange * 0.25f)
                depthCentre = oldCentre;
        }
        // Projection epochs hash float bits; -0 and +0 are the same centre.
        if (depthCentre == 0.0f) depthCentre = 0.0f;
        // Fine levels 0..root-1 double in span up to twice the distance; the root covers the outermost one coarsely.
        const int root = static_cast<int>(VirtualShadowFineLevels(distance));
        result.levels = {static_cast<std::uint32_t>(root), 0, 0, 0};
        for (int level = 0; level <= root; ++level)
        {
            // Keep the resident root projection unchanged while fine coverage
            // adapts, so refinement changes never discard the safety coverage.
            const float scale = level == root ? 1.0f : resolutionScale;
            const float span = std::ldexp(2.0f * distance * scale, std::min(level, root - 1) - (root - 1));
            const int grid = PLUTO_VSM_LEVEL_GRID(level, root);
            const float page = span / grid;
            const glm::ivec2 origin = glm::ivec2(glm::floor(glm::vec2(centre) / page)) - grid / 2;
            glm::mat4 projection(1);
            projection[0][0] = projection[1][1] = 2.0f / span;
            projection[2][2] = -1.0f / depthRange;
            projection[3][0] = -1.0f - 2.0f * origin.x / grid;
            projection[3][1] = -1.0f - 2.0f * origin.y / grid;
            projection[3][2] = 0.5f + depthCentre / depthRange;
            result.matrices[level] = projection * view;
            result.origins[level] = glm::ivec4(origin, static_cast<int>(ProjectionEpoch(view, depthCentre, depthRange, span)), 0);
            result.metrics[level] = {span / (grid * PLUTO_VSM_PAGE_SIZE), depthRange, page, span};
        }
        int spotShadowCount = 0;
        for (std::size_t index = 0; index < std::min(lighting.spotLights.size(), std::size_t{16}) && spotShadowCount < PLUTO_VSM_SPOT_COUNT; ++index)
        {
            const auto &spot = lighting.spotLights[index];
            if (!spot.light.castsShadows || spot.light.range <= 0.02f) continue;
            auto direction = spot.direction;
            const float lengthSquared = glm::dot(direction, direction);
            direction = std::isfinite(lengthSquared) && lengthSquared > 1.e-8f ? glm::normalize(direction) : glm::vec3(0,-1,0);
            const auto up = std::abs(direction.y) > 0.99f ? glm::vec3(0,0,1) : glm::vec3(0,1,0);
            const int level = PLUTO_VSM_DIRECTIONAL_LEVELS + spotShadowCount++;
            const auto matrix = glm::perspectiveRH_ZO(2.0f * std::acos(spot.cone.Cosines().x), 1.0f, 0.01f, spot.light.range) *
                glm::lookAt(spot.light.position, spot.light.position + direction, up);
            result.matrices[level] = matrix;
            result.origins[level] = {0, 0, static_cast<int>(ProjectionEpoch(matrix, 0, spot.light.range, float(result.pool.w))), 1};
            result.metrics[level] = {1.0f / (result.pool.w * PLUTO_VSM_PAGE_SIZE), spot.light.range, 0, 0};
            const auto root = level + PLUTO_VSM_SPOT_COUNT;
            result.matrices[root] = matrix;
            result.origins[root] = {0, 0, static_cast<int>(ProjectionEpoch(matrix, 0, spot.light.range, 0)), 1};
            result.metrics[root] = {1.0f / (PLUTO_VSM_SPOT_ROOT_GRID * PLUTO_VSM_PAGE_SIZE), spot.light.range, 0, 0};
        }
        result.camera = glm::vec4(lighting.cameraPosition, lighting.shadowsEnabled && lighting.shadowMethod == ShadowMethod::Virtual ? 1.0f : 0.0f);
        return result;
    }
    VirtualShadowMaps::VirtualShadowMaps()
        : m_batchedDraws(std::make_unique<GpuRecordTable>("VSM batched draw table")),
          m_batchedModels(std::make_unique<GpuRecordHeap>("VSM batched model heap", sizeof(glm::mat4)))
    {
    }

    VirtualShadowMaps::~VirtualShadowMaps() = default;

    void VirtualShadowMaps::Initialize(rhi::IRenderDevice &device, const VirtualShadowShaders &shaders)
    {
        if (!shaders.Complete() || !device.GetImmediateContext().SupportsGpuDrivenShadows())
            throw std::invalid_argument("GPU virtual shadows require complete shaders and storage/indirect/clip capabilities");
        using namespace rhi;
        std::vector<GraphicsPipelineDescriptor::ResourceBinding> bindings{
            {0, 0, 0, ResourceBindingType::UniformBuffer, ShaderStageMask::Compute},
            {1, 0, 1, ResourceBindingType::SampledTexture, ShaderStageMask::Compute}};
        for (std::uint32_t slot = 2; slot <= 7; ++slot)
            bindings.push_back({slot, 0, slot, ResourceBindingType::StorageBuffer, ShaderStageMask::Compute});
        for (std::uint32_t slot = 8; slot <= 9; ++slot)
            bindings.push_back({slot - 8, 0, slot, ResourceBindingType::StorageImage, ShaderStageMask::Compute});
        bindings.push_back({0, 0, 10, ResourceBindingType::StorageBuffer, ShaderStageMask::Compute});
        // OpenGL storage slot 1 (NormalizeOpenGLShader's VSM bindings); Vulkan binding 11.
        bindings.push_back({1, 0, 11, ResourceBindingType::StorageBuffer, ShaderStageMask::Compute});
        constexpr std::array<const char *, 7> names{"VSM reset", "VSM depth requests", "VSM residency", "VSM signatures", "VSM update budget", "VSM caster binning", "VSM publish"};
        for (std::size_t index = 0; index < m_compute.size(); ++index)
            m_compute[index] = GraphicsPipeline(device, device.CreateComputePipeline({shaders.compute[index], bindings, names[index]}));
        GraphicsPipelineDescriptor descriptor;
        descriptor.cullMode = CullMode::None;
        descriptor.vertexLayout = {sizeof(BasicVertex), {
            {0, Format::R32G32B32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, position))},
            {1, Format::R32G32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, uv))}}};
        const auto meshLayout = descriptor.vertexLayout;
        // Pipeline roles in RasterModules order: 0 receiver depth, 1 caster pages, 2 tile clears, 3 static layer restores.
        constexpr std::array<std::size_t, VirtualShadowShaders::RasterModules.size()> roles{0, 1, 2, 0, 1, 1, 0, 0, 3};
        for (std::size_t index = 0; index < m_raster.size(); ++index)
        {
            const auto role = roles[index];
            // Tile clears and restores draw generated quads over whole tiles.
            const bool tiles = role == 2 || role == 3;
            // Receivers only produce depth for page requests.
            descriptor.colorFormat = role == 0 ? Format::Undefined : Format::R32Float;
            descriptor.vertexLayout = tiles ? decltype(meshLayout){} : meshLayout;
            descriptor.vertexShader = shaders.raster[index * 2];
            descriptor.fragmentShader = shaders.raster[index * 2 + 1];
            descriptor.depthCompare = role == 0 ? CompareOperation::Greater : role == 1 ? CompareOperation::Less : CompareOperation::Always;
            descriptor.clipDistanceCount = role == 1 ? 4 : 0;
            descriptor.resourceBindings = {{0, 0, 0, ResourceBindingType::UniformBuffer, ShaderStageMask::AllGraphics}};
            if (!tiles)
            {
                if (index == 5)
                {
                    descriptor.resourceBindings.push_back({5, 0, 5, ResourceBindingType::StorageBuffer, ShaderStageMask::AllGraphics});
                    descriptor.resourceBindings.push_back({6, 0, 6, ResourceBindingType::StorageBuffer, ShaderStageMask::Vertex});
                }
                else descriptor.resourceBindings.push_back({1, 0, 1, ResourceBindingType::UniformBuffer, ShaderStageMask::AllGraphics});
                descriptor.resourceBindings.push_back({9, 1, 1, ResourceBindingType::SampledTexture, ShaderStageMask::Fragment});
            }
            if (role != 0) descriptor.resourceBindings.push_back({2, 0, 2, ResourceBindingType::StorageBuffer, ShaderStageMask::Vertex});
            if (role == 1) descriptor.resourceBindings.push_back({4, 0, 4, ResourceBindingType::StorageBuffer, ShaderStageMask::Vertex});
            if (role == 3) descriptor.resourceBindings.push_back({3, 0, 3, ResourceBindingType::SampledTexture, ShaderStageMask::Fragment});
            descriptor.debugName = role == 0 ? "VSM receiver depth" : role == 1 ? "VSM indirect pages"
                                 : role == 2 ? "VSM tile clears" : "VSM static layer restores";
            m_raster[index] = GraphicsPipeline(device, device.CreateGraphicsPipeline(descriptor));
        }
        m_table = Texture(device, device.CreateTexture({PLUTO_VSM_GRID, PLUTO_VSM_GRID * PLUTO_VSM_LEVELS, Format::R32Float,
            TextureUsage::Sampled, "VSM GPU page table", true, 1, true, 1}));
        m_requests = Texture(device, device.CreateTexture({PLUTO_VSM_GRID, PLUTO_VSM_GRID * PLUTO_VSM_LEVELS, Format::R32Uint,
            TextureUsage::Sampled, "VSM GPU request mask", false, 1, true, 1}));
        m_parameters = Buffer(device, device.CreateBuffer({sizeof(VirtualShadowParameters), BufferUsage::Uniform, "VSM clipmaps"}));
        m_requestList = Buffer(device, device.CreateBuffer({(PLUTO_VSM_UPDATE_LIST_OFFSET + PLUTO_VSM_CAPACITY) * 4,
            BufferUsage::Storage, "VSM compact GPU requests"}));
        m_counters = Buffer(device, device.CreateBuffer({PLUTO_VSM_COUNTER_COUNT * 4, BufferUsage::Storage, "VSM asynchronous counters"}));
        m_sampler = Sampler(device, device.CreateSampler({false, false, "VSM point sampler"}));
        m_materialSampler = Sampler(device, device.CreateSampler({true, true, "VSM material sampler"}));
        const std::array<std::uint8_t, 4> white{255, 255, 255, 255};
        m_white = Texture(device, device.CreateTexture({1, 1, Format::R8G8B8A8Unorm, TextureUsage::Sampled, "VSM neutral alpha"}, Bytes(white)));
    }
    void VirtualShadowMaps::ResizePool(rhi::IRenderDevice &device, std::uint32_t tiles)
    {
        using namespace rhi;
        const auto pixels = tiles * PLUTO_VSM_PAGE_SIZE;
        // Build replacements before releasing the current pool. Backend resource
        // retirement protects in-flight frames; a fresh stats owner rejects old callbacks.
        Texture depth(device, device.CreateTexture({pixels, pixels, Format::D32Float,
            TextureUsage::DepthStencilAttachment, "VSM physical depth", true}));
        Texture color(device, device.CreateTexture({pixels, pixels, Format::R32Float,
            TextureUsage::ColorAttachment, "VSM physical color", false}));
        Buffer pages(device, device.CreateBuffer({tiles * tiles * PLUTO_VSM_PAGE_METADATA_BYTES, BufferUsage::Storage, "VSM persistent physical metadata"}));
        m_depth = std::move(depth); m_color = std::move(color); m_pages = std::move(pages);
        // The static layer shares the pool's layout; the next frame with dynamic casters creates it at the new size.
        m_staticDepth.Reset();
        m_poolTiles = tiles;
        m_frame = 0; m_capacity = 0;
        m_uploadedCasters.clear();
        m_stats = std::make_shared<VirtualShadowStats>();
        m_resolutionPolicy = {};
        m_budgetPolicy = {};
    }
    void VirtualShadowMaps::CreateStaticLayer(rhi::IRenderDevice &device)
    {
        const auto pixels = m_poolTiles * PLUTO_VSM_PAGE_SIZE;
        m_staticDepth = rhi::Texture(device, device.CreateTexture({pixels, pixels, rhi::Format::D32Float,
            rhi::TextureUsage::DepthStencilAttachment, "VSM static layer depth", true}));
        // Generation zero means no layer; pages drawn under an older one redraw their static casters.
        if (++m_staticGeneration == 0) ++m_staticGeneration;
    }
    bool VirtualShadowMaps::CanPrepare(std::span<const BasicDraw> receivers, std::span<const BasicDraw> casters)
    {
        const auto chunkCount = [](std::span<const BasicDraw> draws)
        {
            std::size_t count = 0;
            for (const auto &draw : draws) count += draw.instanceModels && !draw.instanceModels->empty() ? (draw.instanceModels->size() + 63) / 64 : 1;
            return count;
        };
        return chunkCount(casters) <= PLUTO_VSM_MAX_DRAW_CHUNKS && chunkCount(receivers) <= PLUTO_VSM_MAX_DRAW_CHUNKS;
    }
    bool VirtualShadowMaps::IsReceiver(const BasicDraw &draw) noexcept
    {
        // Impostor cards have no mesh geometry to draw receiver depth with; the coverage prepass covers them.
        return draw.mesh && draw.mesh->IsValid() && draw.alphaMode != 2 && draw.surfaceType != 1 && !draw.impostor;
    }
    bool VirtualShadowMaps::Prepare(rhi::IRenderDevice &device, const BasicLighting &lighting, const glm::mat4 &viewProjection,
                                   std::span<const BasicDraw> receivers, std::span<const BasicDraw> casters,
                                   std::span<const std::uint64_t> signatures, std::span<const std::uint64_t> stillSignatures,
                                   std::uint32_t width, std::uint32_t height,
                                   rhi::TextureHandle sceneDepth, const FoliageWindFrame &wind)
    {
        if (signatures.size() != casters.size() || stillSignatures.size() != casters.size())
            throw std::invalid_argument("VSM signature count mismatch");
        const auto planningReceivers = sceneDepth ? std::span<const BasicDraw>{} : receivers;
        if (!CanPrepare(planningReceivers, casters)) return false;
        const auto poolTiles = VirtualShadowPoolTiles(lighting.virtualShadowPoolPages);
        if (poolTiles != m_poolTiles) ResizePool(device, poolTiles);
        const auto poolCapacity = m_poolTiles * m_poolTiles;
        // Epoch zero is reserved for uninitialised membership. Before wrapping,
        // retire the pair cache and reset page metadata/feedback together.
        if (m_frame == std::numeric_limits<std::uint32_t>::max())
        {
            m_frame = 0;
            m_capacity = 0;
            m_uploadedCasters.clear();
            m_stats = std::make_shared<VirtualShadowStats>();
            m_resolutionPolicy = {};
            m_budgetPolicy = {};
        }
        bool changed = m_frame == 0 || m_width != width || m_height != height;
        if (m_width != width || m_height != height)
        {
            m_receiverDepth.Reset();
            m_width = width; m_height = height;
        }
        // Created only while some frame renders its own receivers.
        if (!sceneDepth && !m_receiverDepth)
            m_receiverDepth = rhi::Texture(device, device.CreateTexture({width, height, rhi::Format::D32Float,
                rhi::TextureUsage::DepthStencilAttachment, "VSM receiver depth", true}));
        // Switching the depth source replans: the reused frame's requests came from the other depth.
        changed |= m_sceneDepth != static_cast<bool>(sceneDepth);
        m_sceneDepth = static_cast<bool>(sceneDepth);
        m_planningDepth = sceneDepth ? sceneDepth : m_receiverDepth.Get();
        // Directional adaptation uses its own demand and the capacity remaining
        // after coarse/local residency. Local lights cannot block its recovery.
        if (m_stats->gpuCountersAvailable)
            m_resolutionPolicy.Observe(m_stats->gpuFrame, m_frame,
                m_stats->directionalFineRequested, m_stats->directionalFineResident,
                // Do not create another refinement backlog while existing updates are deferred.
                m_stats->deferred == 0 ? m_stats->directionalFineCapacity : 0u);
        auto parameters = BuildClipmaps(lighting, m_frame != 0 ? &m_previousClipmaps : nullptr, m_resolutionPolicy.Scale());
        m_batchPages = lighting.virtualShadowBatching && device.GetImmediateContext().MaxIndexedIndirectBatchSize() > 0;
        parameters.scheduling.z = m_batchPages ? 1u : 0u;
        auto preparationProjection = parameters;
        preparationProjection.camera.x = preparationProjection.camera.y = preparationProjection.camera.z = 0;
        m_preparationScratch.clear();
        m_preparationScratch.push_back(lighting.virtualShadowClusterCulling);
        m_preparationScratch.push_back(m_batchPages);
        // Sizes the caster page lists, which preparation allocates.
        m_preparationScratch.push_back(PageUpdateBudget(lighting, poolCapacity));
        bool immutable = m_frame != 0 && m_cacheMembership && !changed;
        const auto appendKey = [&](std::span<const BasicDraw> draws, bool shadow)
        {
            m_preparationScratch.push_back(draws.size());
            for (std::size_t index = 0; index < draws.size(); ++index)
            {
                const auto &draw = draws[index];
                const bool eligible = IsReceiver(draw) && (!shadow || draw.castsShadow);
                // Fixed-width entries preserve section boundaries even when
                // filtering changes without changing the packet array sizes.
                if (!eligible)
                {
                    m_preparationScratch.insert(m_preparationScratch.end(), {index, 0, 0, 0, 0, 0});
                    continue;
                }
                immutable &= draw.preparationRevision != 0;
                m_preparationScratch.insert(m_preparationScratch.end(), {index, 1, draw.preparationRevision,
                    reinterpret_cast<std::uintptr_t>(draw.mesh), draw.mesh->GetRevision(), shadow ? signatures[index] : 0});
            }
        };
        // Planning that reads the scene's depth draws no receivers, so they neither prepare nor invalidate.
        appendKey(planningReceivers, false); appendKey(casters, true);
        const bool sameProjection = std::memcmp(&preparationProjection, &m_preparationProjection, sizeof(preparationProjection)) == 0;
        const bool reusePreparation = immutable && m_capacity != 0 && m_preparationScratch == m_preparationKey && sameProjection;
        m_stats->reusedPreparation = reusePreparation;
        m_stats->reusedPackets = m_stats->rebuiltPackets = 0;
        m_stats->clusterBoundsBuilds = m_stats->clusterBoundsCacheHits = 0;
        if (!sameProjection) ++m_projectionRevision;
        if (!reusePreparation)
        {
            const auto packetFrame = ++m_packetFrame;
            m_batchedModels->BeginFrame(packetFrame);
            // Draws without a preparation revision may change behind the same packet; they rebuild every frame under a
            // key no revision uses.
            constexpr std::uint64_t TransientPacket = 1ull << 63;
            constexpr std::uint64_t PacketRetainFrames = 240, PacketSweepInterval = 64;
            // Each level's texel size, which LOD variants are chosen for. Spot levels are perspective (no one size) and
            // keep their casters' own LODs, as do levels the directional clipmaps do not use.
            std::array<float, PLUTO_VSM_LEVELS> levelTexels{};
            if (parameters.camera.w > 0.0f)
                for (std::uint32_t level = 0; level <= parameters.levels.x; ++level) levelTexels[level] = parameters.metrics[level].x;
            // The chunks each chunking needs. The capacity contract is kept by stepping down to coarser chunking, and
            // whole draws always fit (CanPrepare), so a fragmented scene keeps its shadows.
            VirtualShadowChunkCounts chunkCounts;
            m_casterPacketSlots.assign(casters.size(), nullptr);
            for (std::size_t index = 0; index < casters.size(); ++index)
            {
                const auto &draw = casters[index];
                if (!IsReceiver(draw) || !draw.castsShadow) continue;
                const auto available = draw.firstIndex < draw.mesh->GetIndexCount() ? draw.mesh->GetIndexCount() - draw.firstIndex : 0;
                const auto count = std::min(draw.indexCount ? draw.indexCount : available, available);
                auto &packet = m_casterPackets[draw.preparationRevision ? draw.preparationRevision : TransientPacket | index];
                const auto *instances = draw.instanceModels.get();
                const auto instanceCount = instances ? instances->size() : 0;
                if (!draw.preparationRevision || packet.mesh != draw.mesh || packet.meshRevision != draw.mesh->GetRevision() ||
                    packet.firstIndex != draw.firstIndex || packet.indexCount != count || packet.instances != instances ||
                    packet.instanceCount != instanceCount || packet.model != draw.model)
                {
                    // An unrevised packet keeps its model records, rewritten in place below when they differ.
                    const auto models = packet.models, modelCount = packet.modelCount;
                    if (draw.preparationRevision) ReleaseCasterModels(packet);
                    packet = {};
                    if (!draw.preparationRevision) { packet.models = models; packet.modelCount = modelCount; }
                    packet.mesh = draw.mesh;
                    packet.meshRevision = draw.mesh->GetRevision();
                    packet.firstIndex = draw.firstIndex;
                    packet.indexCount = count;
                    packet.model = draw.model;
                    packet.instances = instances;
                    packet.instanceCount = instanceCount;
                }
                packet.lastFrame = packetFrame;
                m_casterPacketSlots[index] = &packet;
                const auto models = draw.instanceModels && !draw.instanceModels->empty() ? draw.instanceModels->size() : 1;
                const auto modelGroups = (models + PLUTO_VSM_MASKED_INSTANCES - 1) / PLUTO_VSM_MASKED_INSTANCES;
                chunkCounts.maskedModels += modelGroups;
                if (!lighting.virtualShadowClusterCulling) continue;
                // Ranges not starting on a triangle cannot be split into clusters; they keep whole-draw chunks.
                if (draw.firstIndex % 3 != 0)
                {
                    chunkCounts.clustered += (models + 63) / 64;
                    chunkCounts.clusteredLodVariants += (models + 63) / 64;
                    continue;
                }
                const auto ranges = SelectShadowGeometryClusters(draw.mesh->GetShadowClusters(), draw.firstIndex, count);
                auto rangeCount = std::max<std::size_t>(1, ranges.size());
                if (models == 1 && ranges.size() > 1)
                {
                    if (!packet.planned)
                    {
                        ++m_stats->clusterBoundsBuilds;
                        packet.planned = true;
                        packet.merged = MergeShadowGeometryClusters(ranges);
                        const auto &model = draw.instanceModels && !draw.instanceModels->empty() ? draw.instanceModels->front() : draw.model;
                        packet.worldBounds = ShadowClusterWorldSphere(packet.merged, std::span<const glm::mat4>(&model, 1));
                        packet.coalesceProjection = 0;
                    }
                    else ++m_stats->clusterBoundsCacheHits;
                    // Reevaluate projection-dependent fit when lights or clipmaps move.
                    if (packet.coalesceProjection != m_projectionRevision)
                    {
                        packet.coalesce = FitsShadowPage(parameters, packet.worldBounds);
                        packet.coalesceProjection = m_projectionRevision;
                    }
                    if (packet.coalesce) rangeCount = 1;
                }
                else packet.coalesce = false;
                chunkCounts.clustered += rangeCount * modelGroups;
                SelectCasterVariants(packet, draw, levelTexels);
                if (packet.variants.empty()) chunkCounts.clusteredLodVariants += rangeCount * modelGroups;
                else
                    // As BuildCasterChunks splits them: the draw's own LOD by clusters, coarser ones by models alone.
                    for (const auto &variant : packet.variants)
                        chunkCounts.clusteredLodVariants += variant.lod == draw.shadowCasterLod ? rangeCount * modelGroups : modelGroups;
            }
            const auto chunking = SelectVirtualShadowChunking(chunkCounts, lighting.virtualShadowClusterCulling);
            const bool useClusters = chunking == VirtualShadowChunking::ClusteredLodVariants || chunking == VirtualShadowChunking::Clustered;
            m_stats->chunking = chunking;
            m_stats->clusteredChunks = useClusters;
            m_stats->lodVariantChunks = 0;
            // Chunk bounds are padded for this wind only (WindPad); other settings may sway geometry further.
            if (!(wind.wind == m_chunkWind))
            {
                for (auto &entry : m_casterPackets) entry.second.chunksValid = false;
                m_chunkWind = wind.wind;
            }
            // Uploads a chunk's own parameters, for page draws that are not batched and for receivers.
            const auto uploadChunk = [&](Chunk &chunk, std::size_t cursor, const BasicDraw &draw, std::size_t first, std::uint32_t models,
                                         const FoliageWindParameters &chunkWind)
            {
                const glm::uvec4 drawParameters(cursor, models, draw.alphaMode, 0);
                const glm::vec4 alpha(draw.uvScale, draw.alphaCutoff, draw.baseColor.a);
                const auto upload = [&](const auto &values)
                {
                    const auto bytes = Bytes(values);
                    if (!chunk.uniform || chunk.uploaded.size() != bytes.size())
                        chunk.uniform = rhi::Buffer(device, device.CreateBuffer({bytes.size(), rhi::BufferUsage::Uniform, "VSM draw chunk"}));
                    if (chunk.uploaded.size() != bytes.size() || std::memcmp(chunk.uploaded.data(), bytes.data(), bytes.size()) != 0)
                    {
                        device.UpdateBuffer(chunk.uniform.Get(), 0, bytes);
                        chunk.uploaded.assign(bytes.begin(), bytes.end());
                        changed = true;
                    }
                };
                if (models == 1)
                {
                    const auto &model = draw.instanceModels && !draw.instanceModels->empty() ? (*draw.instanceModels)[first] : draw.model;
                    upload(RigidDrawParameters{model, drawParameters, alpha, chunkWind});
                }
                else
                {
                    DrawParameters values;
                    std::copy_n(draw.instanceModels->begin() + first, models, values.models.begin());
                    values.draw = drawParameters; values.alpha = alpha;
                    values.foliageWind = chunkWind;
                    upload(values);
                }
            };
            // Receivers draw depth only when planning has no scene depth. Their chunks keep list positions: a
            // receiver is reused while it stays at the same place in the visible list.
            std::size_t receiverCursor = 0;
            m_receiverPackets.resize(planningReceivers.size());
            for (std::size_t index = 0; index < planningReceivers.size(); ++index)
            {
                const auto &draw = planningReceivers[index];
                if (!IsReceiver(draw)) continue;
                auto &packet = m_receiverPackets[index];
                if (draw.preparationRevision && packet.revision == draw.preparationRevision &&
                    packet.mesh == draw.mesh && packet.meshRevision == draw.mesh->GetRevision() &&
                    packet.first == receiverCursor && receiverCursor <= m_receiverChunks.size() &&
                    packet.count <= m_receiverChunks.size() - receiverCursor &&
                    // Packets retain ranges in shared chunk storage, not ownership
                    // of those slots. Visibility changes can overwrite a range
                    // while its old packet remains cached at an inactive index.
                    std::all_of(m_receiverChunks.begin() + receiverCursor, m_receiverChunks.begin() + receiverCursor + packet.count,
                        [&](const Chunk &chunk) {
                            return chunk.sourceIndex == index && chunk.mesh == draw.mesh &&
                                chunk.meshRevision == draw.mesh->GetRevision() &&
                                chunk.preparationRevision == draw.preparationRevision;
                        }))
                {
                    ++m_stats->reusedPackets;
                    for (std::size_t part = 0; part < packet.count; ++part)
                    {
                        auto &chunk = m_receiverChunks[receiverCursor++];
                        chunk.submission.draw = &draw;
                        chunk.sourceIndex = index;
                    }
                    continue;
                }
                const auto packetFirst = receiverCursor;
                ++m_stats->rebuiltPackets;
                const auto available = draw.firstIndex < draw.mesh->GetIndexCount() ? draw.mesh->GetIndexCount() - draw.firstIndex : 0;
                const auto count = std::min(draw.indexCount ? draw.indexCount : available, available);
                if (count == 0) continue;
                // Submissions use absolute first indices (see BasicMesh::BaseIndex) so arena meshes batch together.
                const auto firstIndex = draw.mesh->BaseIndex() + draw.firstIndex;
                const std::size_t modelCount = draw.instanceModels && !draw.instanceModels->empty() ? draw.instanceModels->size() : 1;
                for (std::size_t first = 0; first < modelCount; first += 64)
                {
                    if (receiverCursor == m_receiverChunks.size()) m_receiverChunks.emplace_back();
                    auto &chunk = m_receiverChunks[receiverCursor];
                    const auto models = static_cast<std::uint32_t>(std::min<std::size_t>(64, modelCount - first));
                    uploadChunk(chunk, receiverCursor, draw, first, models, wind.Parameters(draw.foliageWind));
                    const auto texture = draw.baseColorTexture ? draw.baseColorTexture : m_white.Get();
                    changed |= chunk.mesh != draw.mesh || chunk.meshRevision != draw.mesh->GetRevision() ||
                        chunk.texture != texture || chunk.submission.indexCount != count || chunk.submission.firstIndex != firstIndex;
                    chunk.preparationRevision = draw.preparationRevision;
                    chunk.firstInstance = first;
                    chunk.sourceIndex = index;
                    chunk.mesh = draw.mesh; chunk.meshRevision = draw.mesh->GetRevision();
                    chunk.geometry = draw.mesh->VertexBuffer();
                    chunk.submission = {&draw, count, firstIndex, models, {}, receiverCursor * 20};
                    chunk.texture = texture;
                    ++receiverCursor;
                }
                packet = {draw.mesh, draw.preparationRevision, draw.mesh->GetRevision(), packetFirst, receiverCursor - packetFirst};
            }
            // Casters reuse their packets' chunks wherever they now sit in the list; only list positions (the page
            // lists and indirect commands they address) are rewritten.
            std::vector<Caster> inputs;
            inputs.reserve(m_casterCount);
            std::vector<InstanceBoundsRecord> instanceBounds;
            instanceBounds.reserve(m_casterCount);
            std::vector<BatchedDraw> batched;
            if (m_batchPages) batched.reserve(m_casterCount);
            // Static casters' chunks come first and dynamic ones after them, so the static layer and the main atlas
            // each draw one contiguous range of chunks (RecordPages).
            m_casterOrder.clear();
            for (const bool dynamic : {false, true})
                for (std::size_t index = 0; index < casters.size(); ++index)
                    if (m_casterPacketSlots[index] && IsDynamicCaster(casters[index]) == dynamic) m_casterOrder.push_back(index);
            bool hasDynamic = false;
            for (const auto index : m_casterOrder)
            {
                auto *packet = m_casterPacketSlots[index];
                const auto &draw = casters[index];
                hasDynamic |= IsDynamicCaster(draw);
                const bool coalesced = useClusters && packet->coalesce;
                const bool variants = chunking == VirtualShadowChunking::ClusteredLodVariants;
                if (packet->chunksValid && packet->chunking == chunking && packet->coalesced == coalesced &&
                    (variants ? packet->chunkVariants == packet->variants : packet->chunkVariants.empty()))
                    ++m_stats->reusedPackets;
                else
                {
                    ++m_stats->rebuiltPackets;
                    BuildCasterChunks(*packet, draw, chunking, coalesced, wind);
                }
                if (m_batchPages && !packet->chunks.empty())
                {
                    // A revised packet's models are immutable and upload once; an unrevised one's may change in place.
                    const auto models = draw.instanceModels && !draw.instanceModels->empty()
                        ? std::span<const glm::mat4>(*draw.instanceModels) : std::span<const glm::mat4>(&draw.model, 1);
                    if (packet->models != GpuRecordHeap::Invalid && packet->modelCount != models.size()) ReleaseCasterModels(*packet);
                    if (packet->models == GpuRecordHeap::Invalid)
                    {
                        packet->models = m_batchedModels->Allocate(std::as_bytes(models));
                        packet->modelCount = static_cast<std::uint32_t>(models.size());
                    }
                    else if (!draw.preparationRevision)
                        m_batchedModels->Write(packet->models, std::as_bytes(models));
                }
            }
            std::size_t casterCursor = 0, staticCasterCount = 0;
            const auto emitChunk = [&](std::size_t index, const CasterPacket &packet, const CasterChunk &part)
            {
                const auto &draw = casters[index];
                const bool dynamic = IsDynamicCaster(draw);
                const auto texture = draw.baseColorTexture ? draw.baseColorTexture : m_white.Get();
                const glm::vec4 alpha(draw.uvScale, draw.alphaCutoff, draw.baseColor.a);
                // Pages beyond the shadow sway distance zero these per entry (VirtualShadowCompute's swayingMask).
                const FoliageWindParameters chunkWind = wind.Parameters(draw.foliageWind);
                if (casterCursor == m_casterChunks.size()) m_casterChunks.emplace_back();
                auto &chunk = m_casterChunks[casterCursor];
                const auto firstIndex = draw.mesh->BaseIndex() + part.indexOffset;
                changed |= chunk.mesh != draw.mesh || chunk.meshRevision != draw.mesh->GetRevision() ||
                    chunk.texture != texture || chunk.submission.indexCount != part.indexCount ||
                    chunk.submission.firstIndex != firstIndex || chunk.submission.instances != part.instances;
                chunk.preparationRevision = draw.preparationRevision;
                chunk.firstInstance = part.firstInstance;
                if (part.levels != PLUTO_VSM_ALL_LEVELS) ++m_stats->lodVariantChunks;
                chunk.sourceIndex = index;
                chunk.mesh = draw.mesh; chunk.meshRevision = draw.mesh->GetRevision();
                chunk.geometry = draw.mesh->VertexBuffer();
                chunk.texture = texture;
                chunk.bounds = part.bounds; chunk.extents = part.extents;
                chunk.submission = {&draw, part.indexCount, firstIndex, part.instances, {}, casterCursor * 20};
                if (m_batchPages)
                {
                    // Batched draws read the shared tables; drop parameters left from unbatched recording.
                    if (chunk.uniform) { chunk.uniform.Reset(); chunk.uploaded = {}; }
                    batched.push_back({glm::uvec4(casterCursor, part.instances, draw.alphaMode, packet.models + part.firstInstance), alpha,
                                       chunkWind});
                }
                else uploadChunk(chunk, casterCursor, draw, part.firstInstance, part.instances, chunkWind);
                // One identity per chunk geometry, whether its packet was rebuilt or reused this frame. Pages choose
                // the swaying or the rest-pose one by their distance from the camera (VirtualShadowCompute's swayingMask).
                const auto geometry = std::uint64_t(part.indexOffset) * 0x9e3779b97f4a7c15ull;
                const auto signature = signatures[index] ^ geometry, still = stillSignatures[index] ^ geometry;
                inputs.push_back({part.bounds, {part.indexCount, firstIndex, part.instances, static_cast<std::uint32_t>(index)},
                    {std::uint32_t(signature), std::uint32_t(signature >> 32) ^ part.firstInstance, 0, part.levels}, part.extents,
                    {std::uint32_t(still), std::uint32_t(still >> 32) ^ part.firstInstance, chunkWind.Active() ? 1u : 0u, dynamic ? 1u : 0u}});
                instanceBounds.push_back(part.instanceBounds);
                ++casterCursor;
            };
            // Within each of those ranges, chunks drawing into any fine level precede those drawing only into coarse
            // ones (LOD variants for the root), so the waves that plan a page mostly skip whole blocks of chunks
            // that do not serve its level instead of diverging over interleaved ones.
            const std::uint32_t fineLevels = (1u << parameters.levels.x) - 1u;
            for (const bool dynamic : {false, true})
            {
                for (const bool coarseOnly : {false, true})
                    for (const auto index : m_casterOrder)
                    {
                        if (IsDynamicCaster(casters[index]) != dynamic) continue;
                        const auto &packet = *m_casterPacketSlots[index];
                        for (const auto &part : packet.chunks)
                            if (((part.levels & fineLevels) == 0) == coarseOnly) emitChunk(index, packet, part);
                    }
                if (!dynamic) staticCasterCount = casterCursor;
            }
            if (packetFrame % PacketSweepInterval == 0)
                std::erase_if(m_casterPackets, [&](auto &item)
                {
                    const bool stale = (item.first & TransientPacket) ? item.second.lastFrame != packetFrame
                                                                      : item.second.lastFrame + PacketRetainFrames < packetFrame;
                    if (stale) ReleaseCasterModels(item.second);
                    return stale;
                });
            changed |= receiverCursor != m_receiverCount || casterCursor != m_casterCount;
            m_receiverCount = receiverCursor; m_casterCount = casterCursor;
            m_staticCasterCount = staticCasterCount;
            // The static layer exists from the first frame with both static and dynamic casters until the pool is
            // replaced; with only one kind it would hold nothing worth restoring.
            if (hasDynamic && m_staticCasterCount != 0 && !m_staticDepth) CreateStaticLayer(device);
            // A caster lists at most one entry per updated page and model (VirtualShadowCommon's virtualListStride).
            const auto listEntries = PageUpdateBudget(lighting, poolCapacity) * PLUTO_VSM_MASKED_INSTANCES;
            const bool grow = m_capacity < std::max(m_casterCount, std::size_t{1});
            if (grow)
            {
                m_capacity = VirtualShadowCasterCapacity(m_casterCount);
                m_casters = rhi::Buffer(device, device.CreateBuffer({m_capacity * sizeof(Caster), rhi::BufferUsage::Storage, "VSM caster bounds/signatures"}));
                m_uploadedCasters.clear();
                m_instanceBounds = rhi::Buffer(device, device.CreateBuffer({m_capacity * sizeof(InstanceBoundsRecord),
                    rhi::BufferUsage::Storage, "VSM masked chunk model bounds"}));
                m_uploadedInstanceBounds.clear();
            }
            if (grow || m_listEntries != listEntries)
            {
                m_listEntries = listEntries;
                m_lists = rhi::Buffer(device, device.CreateBuffer({m_capacity * listEntries * 4, rhi::BufferUsage::Storage, "VSM compact caster page lists"}));
            }
            if (grow)
            {
                m_indirect = rhi::Buffer(device, device.CreateBuffer({m_capacity * 20, rhi::BufferUsage::Storage, "VSM indexed indirect commands"}));
                // Page-major records (VirtualShadowCompute membershipRecord): the frame each was evaluated, and its
                // conservative membership mask. Frame zero makes every pair invalid until evaluated.
                const std::vector<glm::uvec2> empty(m_capacity * poolCapacity, glm::uvec2(0));
                m_membership = rhi::Buffer(device, device.CreateBuffer({empty.size() * sizeof(glm::uvec2),
                    rhi::BufferUsage::Storage, "VSM persistent caster/page membership"}, std::as_bytes(std::span(empty))));
            }
            for (std::size_t index = 0; index < inputs.size(); ++index)
            {
                auto &input = inputs[index];
                input.identity.z = m_frame + 1;
                if ((index + 1) * sizeof(Caster) <= m_uploadedCasters.size())
                {
                    Caster previous;
                    std::memcpy(&previous, m_uploadedCasters.data() + index * sizeof(Caster), sizeof(Caster));
                    // Membership depends only on conservative bounds (and, for masked chunks, their models' bounds), not
                    // mesh identity. Reordering equal bounds is therefore safe as well.
                    const bool sameModels = previous.draw.z == input.draw.z &&
                        ((index + 1) * sizeof(InstanceBoundsRecord) <= m_uploadedInstanceBounds.size()) &&
                        std::memcmp(m_uploadedInstanceBounds.data() + index * sizeof(InstanceBoundsRecord),
                                    instanceBounds[index].data(), sizeof(InstanceBoundsRecord)) == 0;
                    if (m_cacheMembership && input.bounds == previous.bounds && input.extents == previous.extents && sameModels &&
                        input.identity.w == previous.identity.w)
                        input.identity.z = previous.identity.z;
                }
            }
            const auto inputBytes = std::as_bytes(std::span(inputs));
            changed |= UploadChangedRecords(device, m_casters.Get(), inputBytes, sizeof(Caster), m_uploadedCasters);
            changed |= UploadChangedRecords(device, m_instanceBounds.Get(), std::as_bytes(std::span(instanceBounds)),
                                            sizeof(InstanceBoundsRecord), m_uploadedInstanceBounds);
            for (std::size_t index = 0; index < m_casterCount; ++index) m_casterChunks[index].submission.indirect = m_indirect.Get();
            if (m_batchPages)
            {
                // Batched page draws read each caster chunk's parameters and models from two tables (VirtualShadowRaster's
                // BatchedDraw), so chunks of any model count batch.
                changed |= m_batchedDraws->Upload(device, std::as_bytes(std::span(batched)), sizeof(BatchedDraw));
                changed |= m_batchedModels->Flush(device);
            }
        }
        else
        {
            m_stats->reusedPackets = static_cast<std::uint32_t>(planningReceivers.size() + casters.size());
            // Borrowed packet addresses change even when immutable content does not.
            for (std::size_t index = 0; index < m_receiverCount; ++index)
                m_receiverChunks[index].submission.draw = &planningReceivers[m_receiverChunks[index].sourceIndex];
            for (std::size_t index = 0; index < m_casterCount; ++index)
                m_casterChunks[index].submission.draw = &casters[m_casterChunks[index].sourceIndex];
        }
        m_preparationKey.swap(m_preparationScratch);
        m_preparationProjection = preparationProjection;
        for (std::size_t level = 0; level < PLUTO_VSM_LEVELS; ++level)
        {
            const bool stable = m_frame != 0 && parameters.matrices[level] == m_previousClipmaps.matrices[level] &&
                parameters.metrics[level] == m_previousClipmaps.metrics[level] && parameters.origins[level] == m_previousClipmaps.origins[level] && parameters.culling == m_previousClipmaps.culling;
            parameters.membershipEpochs[level / 4][level % 4] = stable
                ? m_previousClipmaps.membershipEpochs[level / 4][level % 4] : m_frame + 1;
        }
        m_previousClipmaps = parameters;
        parameters.viewProjection = viewProjection;
        parameters.inverseViewProjection = glm::inverse(viewProjection);
        parameters.viewport = {width, height, device.GetApi() == rhi::GraphicsApi::Vulkan ? 1 : 0, device.UsesZeroToOneClipDepth() ? 1 : 0};
        ++m_frame;
        if (m_frame == 0) ++m_frame;
        if (lighting.virtualShadowAdaptiveBudget)
        {
            const auto timing = device.GetTimingStats("Scene");
            if (!timing.hasGpuResult || !timing.gpuObservationId) m_budgetPolicy = {};
            else
                for (const auto &scope : timing.gpuScopes)
                    if (scope.name == "RHI Virtual Shadow Pages")
                        m_budgetPolicy.Observe(timing.gpuObservationId, scope.milliseconds, timing.frameGpuMs);
        }
        else m_budgetPolicy = {};
        m_stats->rootLevel = parameters.levels.x;
        m_stats->effectiveTriangleBudget = m_budgetPolicy.Budget(std::clamp(lighting.virtualShadowTriangleBudget, 1u, 16000000u));
        parameters.limits = {m_casterCount, m_frame, PageUpdateBudget(lighting, poolCapacity), m_stats->effectiveTriangleBudget};
        parameters.levels.y = m_staticDepth ? m_staticGeneration : 0u;
        parameters.levels.z = static_cast<std::uint32_t>(m_capacity);
        parameters.settings = {lighting.shadowSoftness, 1, m_frame == 1 ? 1 : 0, m_resolutionPolicy.Scale()};
        auto inputKey = parameters;
        inputKey.limits.y = 0; inputKey.settings.z = 0;
        changed |= std::memcmp(&inputKey, &m_previousInputs, sizeof(inputKey)) != 0;
        m_previousInputs = inputKey;
        if (changed) m_inputChangeFrame = m_frame;
        const bool refinementPending = m_resolutionPolicy.WantsRecovery(m_stats->directionalFineRequested, m_stats->directionalFineCapacity);
        // Delayed counters may skip work only when they describe these exact
        // inputs. A changed camera/caster immediately resumes GPU planning.
        m_reuseFrame = !changed && !refinementPending && m_stats->gpuCountersAvailable &&
            m_stats->gpuFrame >= m_inputChangeFrame && m_stats->dirty == 0 && m_stats->updated == 0 &&
            m_stats->deferred == 0 && m_stats->overflow == 0;
        if (!m_reuseFrame) device.UpdateBuffer(m_parameters.Get(), 0, Bytes(parameters));
        return true;
    }
    void VirtualShadowMaps::ReleaseCasterModels(CasterPacket &packet)
    {
        m_batchedModels->Release(packet.models, packet.modelCount, m_packetFrame);
        packet.models = GpuRecordHeap::Invalid;
        packet.modelCount = 0;
    }
    void VirtualShadowMaps::SelectCasterVariants(CasterPacket &packet, const BasicDraw &draw,
                                                 std::span<const float> levelTexels) const
    {
        packet.variants.clear();
        const auto &chain = draw.shadowCasterLods;
        if (!chain || draw.shadowCasterLod >= chain->lods.size() || !draw.instanceModels || draw.instanceModels->empty()) return;
        // Variants replace the draw's range with its chain's ranges, so the draw must draw its chain's own LOD.
        const auto &own = chain->lods[draw.shadowCasterLod];
        if (own.firstIndex != draw.firstIndex || own.indexCount != packet.indexCount) return;
        if (packet.modelScale < 0.0f)
        {
            packet.modelScale = 0.0f;
            for (const auto &model : *draw.instanceModels)
                packet.modelScale = std::max({packet.modelScale, glm::length(glm::vec3(model[0])),
                                              glm::length(glm::vec3(model[1])), glm::length(glm::vec3(model[2]))});
        }
        auto variants = SelectShadowCasterLodVariants(*chain, draw.shadowCasterLod, chain->boundsRadius * packet.modelScale,
                                                      levelTexels);
        if (variants.size() > 1) packet.variants = std::move(variants);
    }
    void VirtualShadowMaps::BuildCasterChunks(CasterPacket &packet, const BasicDraw &draw, VirtualShadowChunking chunking,
                                              bool coalesced, const FoliageWindFrame &wind) const
    {
        packet.chunks.clear();
        packet.chunksValid = true;
        packet.chunking = chunking;
        packet.coalesced = coalesced;
        packet.chunkVariants.clear();
        if (packet.indexCount == 0) return;
        const bool clustered = chunking == VirtualShadowChunking::ClusteredLodVariants || chunking == VirtualShadowChunking::Clustered;
        const std::size_t modelCount = draw.instanceModels && !draw.instanceModels->empty() ? draw.instanceModels->size() : 1;
        const float radius = std::isfinite(draw.shadowBoundsRadius) ? draw.shadowBoundsRadius : -1.0f;
        // Appends the chunks of the draw's index range [rangeFirst, rangeFirst + rangeCount), drawn into `levels`: split
        // into mesh clusters of up to 16 models when `split` allows, otherwise whole, of up to 16 models planned
        // separately with `maskModels` or 64 planned together without.
        const auto appendRange = [&](std::uint32_t rangeFirst, std::uint32_t rangeCount, std::uint32_t levels, bool split,
                                     bool merge, bool maskModels)
        {
            auto clusters = SelectShadowGeometryClusters(draw.mesh->GetShadowClusters(), rangeFirst, rangeCount);
            const bool subdivide = split && rangeFirst % 3 == 0 && !clusters.empty();
            if (subdivide && merge)
                clusters = std::span<const ShadowGeometryCluster>(&packet.merged, 1);
            const auto parts = subdivide ? clusters.size() : 1;
            // Without subdivision a chunk covers the whole range; its bound is that range's under the chunk's models.
            const auto whole = subdivide ? ShadowGeometryCluster{} : MergeShadowGeometryClusters(clusters);
            // Whole draws plan up to 64 models together; every finer chunking plans each of up to 16 separately.
            const std::size_t instanceLimit = subdivide || maskModels ? PLUTO_VSM_MASKED_INSTANCES : 64;
            for (std::size_t part = 0; part < parts; ++part)
            {
                const auto firstIndex = subdivide ? std::max(rangeFirst, clusters[part].firstIndex) : rangeFirst;
                const auto endIndex = subdivide ? std::min(rangeFirst + rangeCount, clusters[part].firstIndex + clusters[part].indexCount)
                                                : rangeFirst + rangeCount;
                if (endIndex <= firstIndex) continue;
                for (std::size_t first = 0; first < modelCount; first += instanceLimit)
                {
                    const auto models = static_cast<std::uint32_t>(std::min(instanceLimit, modelCount - first));
                    CasterChunk chunk{endIndex - firstIndex, firstIndex, models, static_cast<std::uint32_t>(first), levels};
                    if (subdivide)
                    {
                        const auto transforms = draw.instanceModels && !draw.instanceModels->empty()
                            ? std::span<const glm::mat4>(*draw.instanceModels).subspan(first, models) : std::span<const glm::mat4>(&draw.model, 1);
                        auto bounds = ShadowClusterWorldBounds(clusters[part], transforms);
                        PadForWind(bounds, wind, draw, clusters[part], transforms);
                        chunk.bounds = bounds.sphere;
                        chunk.extents = bounds.extents;
                    }
                    else if (const auto bounds = InstanceChunkBounds(wind, draw, whole, first, models); bounds.sphere.w >= 0.0f)
                    {
                        chunk.bounds = bounds.sphere;
                        chunk.extents = bounds.extents;
                    }
                    else
                        chunk.bounds = glm::vec4(draw.shadowBoundsCenter, radius);
                    if (MaskedModels(models))
                        chunk.instanceBounds = ModelBounds(wind, draw, subdivide ? clusters[part] : whole,
                                                           std::span<const glm::mat4>(*draw.instanceModels).subspan(first, models));
                    packet.chunks.push_back(chunk);
                }
            }
        };
        if (chunking == VirtualShadowChunking::ClusteredLodVariants && !packet.variants.empty())
        {
            packet.chunkVariants = packet.variants;
            // Only the draw's own LOD splits into clusters. Coarser variants serve coarser levels, whose pages are wide
            // beside a model, so a cluster would seldom miss a page its model reaches; planning each model separately
            // is enough, and costs a chunk per 16 models rather than one per cluster as well.
            for (const auto &variant : packet.chunkVariants)
            {
                const auto &lod = draw.shadowCasterLods->lods[variant.lod];
                appendRange(lod.firstIndex, lod.indexCount, variant.levels, variant.lod == draw.shadowCasterLod, false, true);
            }
        }
        else appendRange(draw.firstIndex, packet.indexCount, PLUTO_VSM_ALL_LEVELS, clustered, coalesced,
                         chunking == VirtualShadowChunking::MaskedModels);
    }
    void VirtualShadowMaps::BindCompute(rhi::ICommandContext &commands, std::size_t pipeline)
    {
        commands.BindPipeline(m_compute[pipeline].Get());
        commands.BindUniformBuffer(0, m_parameters.Get());
        commands.BindTexture(1, m_planningDepth, m_sampler.Get());
        const std::array buffers{m_pages.Get(), m_casters.Get(), m_lists.Get(), m_indirect.Get(), m_requestList.Get(), m_counters.Get()};
        for (std::uint32_t index = 0; index < buffers.size(); ++index) commands.BindStorageBuffer(index + 2, buffers[index]);
        commands.BindStorageBuffer(0, m_membership.Get());
        commands.BindStorageBuffer(1, m_instanceBounds.Get());
        commands.BindStorageImage(0, m_requests.Get());
        commands.BindStorageImage(1, m_table.Get());
    }
    std::uint32_t VirtualShadowMaps::RecordChunks(rhi::ICommandContext &commands, const SubmitMesh &submit,
                                                  const std::vector<Chunk> &chunks, std::size_t first, std::size_t end,
                                                  ChunkPipelines pipelines)
    {
        // Submission callbacks only bind mesh buffers and issue the draw.
        // Retain pass-local state rather than repeating backend handle lookups
        // for every submesh of an imported architectural model.
        std::uint32_t submissions = 0;
        std::size_t boundPipeline = m_raster.size();
        rhi::TextureHandle boundTexture;
        for (std::size_t index = first; index < end; ++index)
        {
            const auto &chunk = chunks[index];
            // Batching covers every caster page chunk; receivers draw one chunk at a time.
            const bool batch = m_batchPages && pipelines.instanced == 1;
            const bool opaque = chunk.submission.draw->alphaMode != 1;
            const auto pipeline = batch ? 5
                : chunk.submission.instances == 1 ? (opaque ? pipelines.opaqueRigid : pipelines.rigid)
                : (opaque ? pipelines.opaqueInstanced : pipelines.instanced);
            if (pipeline != boundPipeline)
            {
                commands.BindPipeline(m_raster[pipeline].Get());
                boundPipeline = pipeline;
            }
            if (batch)
            {
                commands.BindStorageBuffer(5, m_batchedDraws->Get());
                commands.BindStorageBuffer(6, m_batchedModels->Get());
            }
            else commands.BindUniformBuffer(1, chunk.uniform.Get());
            if (!boundTexture || (chunk.submission.draw->alphaMode == 1 && chunk.texture != boundTexture))
            {
                commands.BindTexture(9, chunk.texture, m_materialSampler.Get());
                boundTexture = chunk.texture;
            }
            auto submission = chunk.submission;
            if (batch)
            {
                const auto limit = std::min(end - index, std::size_t(commands.MaxIndexedIndirectBatchSize()));
                while (submission.commandCount < limit)
                {
                    const auto &next = chunks[index + submission.commandCount];
                    if (next.geometry != chunk.geometry ||
                        (next.submission.draw->alphaMode == 1 && next.texture != boundTexture)) break;
                    ++submission.commandCount;
                }
            }
            submit(submission);
            ++submissions;
            index += submission.commandCount - 1;
        }
        return submissions;
    }
    void VirtualShadowMaps::RecordPlanning(rhi::ICommandContext &commands, const SubmitMesh &submit)
    {
        if (m_reuseFrame) return;
        if (!m_sceneDepth)
        {
            commands.BeginGpuScope("RHI VSM Receiver Depth");
            rhi::RenderingInfo depth;
            depth.depthAttachment = m_receiverDepth.Get();
            depth.clearColor = false;
            depth.width = m_width; depth.height = m_height; depth.clearDepthValue = 0;
            commands.BeginRendering(depth);
            commands.BindPipeline(m_raster[0].Get());
            commands.BindUniformBuffer(0, m_parameters.Get());
            RecordChunks(commands, submit, m_receiverChunks, 0, m_receiverCount, {0, 3, 6, 7});
            commands.EndRendering(); commands.EndGpuScope();
        }
        commands.BeginGpuScope("RHI VSM GPU Planning");
        using rhi::BarrierAccess;
        // Planning rewrites page state that earlier page rasterization and planning read.
        commands.PipelineBarrier(BarrierAccess::Graphics | BarrierAccess::Compute, BarrierAccess::Compute);
        static constexpr std::array planningScopes{
            "RHI VSM Planning / Reset", "RHI VSM Planning / Receiver requests",
            "RHI VSM Planning / Page allocation", "RHI VSM Planning / Caster signatures",
            "RHI VSM Planning / Update budget", "RHI VSM Planning / Caster binning"};
        for (std::size_t pass = 0; pass < 6; ++pass)
        {
            commands.BeginGpuScope(planningScopes[pass]);
            BindCompute(commands, pass);
            if (pass == 0) commands.Dispatch(PLUTO_VSM_LEVELS * PLUTO_VSM_LEVEL_PAGES / 64, 1, 1);
            else if (pass == 1) commands.Dispatch((m_width + 7) / 8, (m_height + 7) / 8, 1);
            else if (pass == 3) commands.Dispatch(m_poolTiles * m_poolTiles, 1, 1);
            else if (pass == 5) commands.Dispatch(static_cast<std::uint32_t>((m_casterCount + 63) / 64 + (m_casterCount == 0)), 1, 1);
            else commands.Dispatch(1, 1, 1);
            // The last pass feeds page rasterization (indirect commands, page lists) and receiver shading.
            commands.PipelineBarrier(BarrierAccess::Compute, pass + 1 < planningScopes.size()
                ? BarrierAccess::Compute : BarrierAccess::Compute | BarrierAccess::Graphics);
            commands.EndGpuScope();
        }
        commands.EndGpuScope();
    }
    void VirtualShadowMaps::RecordPages(rhi::ICommandContext &commands, const SubmitMesh &submit)
    {
        m_stats->pageDrawBatches = 0;
        if (m_reuseFrame) return;
        commands.BeginGpuScope("RHI Virtual Shadow Pages");
        // One pass per depth target: the cleared tiles' casters, then (with a static layer) the restored tiles'
        // dynamic casters over the static casters' depth. Binning lists each caster only for the pages it draws.
        const auto tileDraws = m_poolTiles * m_poolTiles * 6;
        const auto beginAtlas = [&](rhi::TextureHandle depth, std::size_t tilePipeline)
        {
            rhi::RenderingInfo atlas;
            atlas.colorAttachments = {m_color.Get()}; atlas.depthAttachment = depth;
            atlas.width = atlas.height = m_poolTiles * PLUTO_VSM_PAGE_SIZE;
            atlas.clearDepth = atlas.clearColor = false;
            commands.BeginRendering(atlas);
            commands.BindPipeline(m_raster[tilePipeline].Get());
            commands.BindUniformBuffer(0, m_parameters.Get()); commands.BindStorageBuffer(2, m_pages.Get());
            if (tilePipeline == RestorePipeline) commands.BindTexture(3, m_staticDepth.Get(), m_sampler.Get());
            commands.Draw(tileDraws);
            commands.BindPipeline(m_raster[1].Get());
            commands.BindUniformBuffer(0, m_parameters.Get()); commands.BindStorageBuffer(2, m_pages.Get());
            commands.BindStorageBuffer(4, m_lists.Get());
        };
        constexpr ChunkPipelines pagePipelines{1, 4, 1, 4};
        if (m_staticDepth)
        {
            beginAtlas(m_staticDepth.Get(), ClearPipeline);
            m_stats->pageDrawBatches = RecordChunks(commands, submit, m_casterChunks, 0, m_staticCasterCount, pagePipelines);
            commands.EndRendering();
            beginAtlas(m_depth.Get(), RestorePipeline);
            m_stats->pageDrawBatches += RecordChunks(commands, submit, m_casterChunks, m_staticCasterCount, m_casterCount, pagePipelines);
        }
        else
        {
            beginAtlas(m_depth.Get(), ClearPipeline);
            m_stats->pageDrawBatches = RecordChunks(commands, submit, m_casterChunks, 0, m_casterCount, pagePipelines);
        }
        commands.EndRendering(); commands.EndGpuScope();
        using rhi::BarrierAccess;
        commands.PipelineBarrier(BarrierAccess::Graphics, BarrierAccess::Compute);
        BindCompute(commands, 6);
        commands.Dispatch((m_poolTiles * m_poolTiles + 63) / 64, 1, 1);
        // Published pages feed receiver shading, later planning and the counter readback.
        commands.PipelineBarrier(BarrierAccess::Compute,
                                 BarrierAccess::Graphics | BarrierAccess::Compute | BarrierAccess::TransferRead);
        const std::weak_ptr<VirtualShadowStats> weakStats = m_stats;
        const auto frame = m_frame;
        commands.QueueBufferReadback(m_counters.Get(), PLUTO_VSM_COUNTER_COUNT * 4, [weakStats, frame](std::span<const std::byte> bytes)
        {
            if (const auto stats = weakStats.lock(); stats && bytes.size() == PLUTO_VSM_COUNTER_COUNT * 4 && frame > stats->gpuFrame)
            {
                std::array<std::uint32_t, PLUTO_VSM_COUNTER_COUNT> values{};
                std::memcpy(values.data(), bytes.data(), bytes.size());
                stats->requested = values[0]; stats->resident = values[1]; stats->dirty = values[2]; stats->cacheHits = values[3];
                stats->evicted = values[4]; stats->overflow = values[5]; stats->casterPagePairs = values[6]; stats->submittedTriangles = values[7];
                stats->deferred = values[8]; stats->updated = values[9]; stats->indirectDraws = values[10];
                stats->directionalFineRequested = values[11]; stats->directionalFineResident = values[12]; stats->directionalFineCapacity = values[13];
                stats->localFineRequested = values[14]; stats->coarseRequested = values[15];
                stats->oldestDirtyAge = values[16]; stats->oversizedUpdates = values[17];
                stats->historicalDirtyAge = values[PLUTO_VSM_STATS_HISTORICAL_AGE];
                for (std::size_t level = 0; level < PLUTO_VSM_LEVELS; ++level)
                {
                    stats->updatedTrianglesByLevel[level] = values[PLUTO_VSM_STATS_UPDATED_TRIANGLES_BASE + level];
                    stats->dirtyTrianglesByLevel[level] = values[PLUTO_VSM_STATS_DIRTY_TRIANGLES_BASE + level];
                }
                stats->maxDirtyPageTriangles = values[PLUTO_VSM_STATS_MAX_DIRTY_TRIANGLES];
                stats->triangleBudgetDeferred = values[PLUTO_VSM_STATS_TRIANGLE_DEFERRED]; stats->pageBudgetDeferred = values[PLUTO_VSM_STATS_PAGE_DEFERRED];
                stats->newDirty = values[PLUTO_VSM_STATS_NEW_DIRTY]; stats->changedDirty = values[PLUTO_VSM_STATS_CHANGED_DIRTY];
                stats->staticDirty = values[PLUTO_VSM_STATS_STATIC_DIRTY];
                stats->gpuFrame = frame; stats->gpuCountersAvailable = true;
            }
        });
    }
    VirtualShadowStats VirtualShadowMaps::GetStats() const
    {
        auto stats = *m_stats;
        const auto poolCapacity = m_poolTiles * m_poolTiles;
        stats.physicalCapacity = poolCapacity;
        stats.resolutionScale = m_resolutionPolicy.Scale();
        stats.reusedFrame = m_reuseFrame;
        stats.submittedIndirectCommands = m_reuseFrame ? 0 : static_cast<std::uint32_t>(m_casterCount);
        stats.dynamicCasterChunks = static_cast<std::uint32_t>(m_casterCount - m_staticCasterCount);
        stats.staticLayer = static_cast<bool>(m_staticDepth);
        stats.receiverDraws = m_reuseFrame || m_sceneDepth ? 0 : static_cast<std::uint32_t>(m_receiverCount);
        stats.memoryBytes = std::uint64_t(poolCapacity) * PLUTO_VSM_PAGE_SIZE * PLUTO_VSM_PAGE_SIZE * 8 +
            PLUTO_VSM_LEVELS * PLUTO_VSM_LEVEL_PAGES * 12 + poolCapacity * PLUTO_VSM_PAGE_METADATA_BYTES + sizeof(VirtualShadowParameters) + PLUTO_VSM_COUNTER_COUNT * 4 + 16 +
            m_capacity * (sizeof(Caster) + sizeof(InstanceBoundsRecord) + m_listEntries * 4 + poolCapacity * sizeof(glm::uvec2) + 20) +
            PLUTO_VSM_CAPACITY * 4 +
            std::uint64_t(m_width) * m_height * 8 + m_batchedDraws->MemoryBytes() + m_batchedModels->MemoryBytes() +
            (m_staticDepth ? std::uint64_t(poolCapacity) * PLUTO_VSM_PAGE_SIZE * PLUTO_VSM_PAGE_SIZE * 4 : 0);
        for (const auto &chunk : m_receiverChunks) stats.memoryBytes += chunk.uploaded.size();
        for (const auto &chunk : m_casterChunks) stats.memoryBytes += chunk.uploaded.size();
        return stats;
    }
}
