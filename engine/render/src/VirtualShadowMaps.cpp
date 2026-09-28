#include "PlutoGE/render/VirtualShadowMaps.h"
#include "PlutoGE/render/BasicRenderer.h"
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
        };
        struct alignas(16) RigidDrawParameters
        {
            glm::mat4 model{};
            glm::uvec4 draw{};
            glm::vec4 alpha{};
        };
        static_assert(sizeof(RigidDrawParameters) == 96);
        struct alignas(16) Caster { glm::vec4 bounds; glm::uvec4 draw, identity; };
        static_assert(sizeof(Caster) == 48 && sizeof(DrawParameters) == 4128);
        ShadowGeometryCluster MergeShadowClusters(std::span<const ShadowGeometryCluster> clusters)
        {
            ShadowGeometryCluster result;
            glm::vec3 lo(std::numeric_limits<float>::max()), hi(-std::numeric_limits<float>::max());
            for (const auto &cluster : clusters)
            {
                if (cluster.extents.x < 0) return result;
                lo = glm::min(lo, cluster.center - cluster.extents);
                hi = glm::max(hi, cluster.center + cluster.extents);
            }
            if (!clusters.empty())
            {
                result.firstIndex = clusters.front().firstIndex;
                result.indexCount = clusters.back().firstIndex + clusters.back().indexCount - result.firstIndex;
                result.center = (lo + hi) * 0.5f; result.extents = (hi - lo) * 0.5f;
            }
            return result;
        }
        // Subdivision buys little when the entire mesh fits within one page's
        // width. Coalesce those ranges to avoid multiplying CPU submissions.
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
        result.culling = {std::clamp(lighting.virtualShadowCoarseMinCasterTexels, 0.0f, 4.0f), 0, 0, 0};
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
        if (previous && previous->metrics[PLUTO_VSM_ROOT_LEVEL].y == depthRange)
        {
            // A round-to-nearest centre avoids a global invalidation at world
            // zero. Hysteresis keeps the old projection across quantisation
            // boundaries while receivers/casters still have ample depth room.
            const float oldCentre = (previous->matrices[PLUTO_VSM_ROOT_LEVEL][3][2] - 0.5f) * depthRange;
            if (std::abs(centre.z - oldCentre) < depthRange * 0.25f)
                depthCentre = oldCentre;
        }
        // Projection epochs hash float bits; -0 and +0 are the same centre.
        if (depthCentre == 0.0f) depthCentre = 0.0f;
        for (int level = 0; level < PLUTO_VSM_DIRECTIONAL_LEVELS; ++level)
        {
            // Keep the resident root projection unchanged while fine coverage
            // adapts, so refinement changes never discard the safety coverage.
            const float scale = level == PLUTO_VSM_ROOT_LEVEL ? 1.0f : resolutionScale;
            const float span = std::ldexp(2.0f * distance * scale, std::min(level, PLUTO_VSM_FINE_LEVELS - 1) - (PLUTO_VSM_FINE_LEVELS - 1));
            const int grid = PLUTO_VSM_LEVEL_GRID(level);
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
        constexpr std::array<const char *, 7> names{"VSM reset", "VSM depth requests", "VSM residency", "VSM signatures", "VSM update budget", "VSM caster binning", "VSM publish"};
        for (std::size_t index = 0; index < m_compute.size(); ++index)
            m_compute[index] = GraphicsPipeline(device, device.CreateComputePipeline({shaders.compute[index], bindings, names[index]}));
        GraphicsPipelineDescriptor descriptor;
        descriptor.colorFormat = Format::R32Float;
        descriptor.cullMode = CullMode::None;
        descriptor.vertexLayout = {sizeof(BasicVertex), {
            {0, Format::R32G32B32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, position))},
            {1, Format::R32G32Float, static_cast<std::uint32_t>(offsetof(BasicVertex, uv))}}};
        const auto meshLayout = descriptor.vertexLayout;
        for (std::size_t index = 0; index < m_raster.size(); ++index)
        {
            const auto role = index < 3 ? index : index - 3;
            descriptor.vertexLayout = role == 2 ? decltype(meshLayout){} : meshLayout;
            descriptor.vertexShader = shaders.raster[index * 2];
            descriptor.fragmentShader = shaders.raster[index * 2 + 1];
            descriptor.depthCompare = role == 0 ? CompareOperation::Greater : role == 1 ? CompareOperation::Less : CompareOperation::Always;
            descriptor.clipDistanceCount = role == 1 ? 4 : 0;
            descriptor.resourceBindings = {{0, 0, 0, ResourceBindingType::UniformBuffer, ShaderStageMask::AllGraphics}};
            if (role != 2)
            {
                descriptor.resourceBindings.push_back({1, 0, 1, ResourceBindingType::UniformBuffer, ShaderStageMask::AllGraphics});
                descriptor.resourceBindings.push_back({9, 1, 1, ResourceBindingType::SampledTexture, ShaderStageMask::Fragment});
            }
            if (role != 0) descriptor.resourceBindings.push_back({2, 0, 2, ResourceBindingType::StorageBuffer, ShaderStageMask::Vertex});
            if (role == 1) descriptor.resourceBindings.push_back({4, 0, 4, ResourceBindingType::StorageBuffer, ShaderStageMask::Vertex});
            if (role == 2) descriptor.vertexLayout = {};
            descriptor.debugName = role == 0 ? "VSM receiver depth" : role == 1 ? "VSM indirect pages" : "VSM tile clears";
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
        Buffer pages(device, device.CreateBuffer({tiles * tiles * 64, BufferUsage::Storage, "VSM persistent physical metadata"}));
        m_depth = std::move(depth); m_color = std::move(color); m_pages = std::move(pages);
        m_poolTiles = tiles;
        m_frame = 0; m_capacity = 0;
        m_uploadedCasters.clear();
        m_stats = std::make_shared<VirtualShadowStats>();
        m_resolutionPolicy = {};
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
    bool VirtualShadowMaps::Prepare(rhi::IRenderDevice &device, const BasicLighting &lighting, const glm::mat4 &viewProjection,
                                   std::span<const BasicDraw> receivers, std::span<const BasicDraw> casters,
                                   std::span<const std::uint64_t> signatures, std::uint32_t width, std::uint32_t height)
    {
        if (signatures.size() != casters.size()) throw std::invalid_argument("VSM signature count mismatch");
        if (!CanPrepare(receivers, casters)) return false;
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
        }
        bool changed = m_frame == 0 || m_width != width || m_height != height;
        if (m_width != width || m_height != height)
        {
            m_receiverDepth = rhi::Texture(device, device.CreateTexture({width, height, rhi::Format::D32Float,
                rhi::TextureUsage::DepthStencilAttachment, "VSM receiver depth", true}));
            m_receiverColor = rhi::Texture(device, device.CreateTexture({width, height, rhi::Format::R32Float,
                rhi::TextureUsage::ColorAttachment, "VSM receiver color", false}));
            m_width = width; m_height = height;
        }
        // Directional adaptation uses its own demand and the capacity remaining
        // after coarse/local residency. Local lights cannot block its recovery.
        if (m_stats->gpuCountersAvailable)
            m_resolutionPolicy.Observe(m_stats->gpuFrame, m_frame,
                m_stats->directionalFineRequested, m_stats->directionalFineResident,
                // Do not create another refinement backlog while existing updates are deferred.
                m_stats->deferred == 0 ? m_stats->directionalFineCapacity : 0u);
        auto parameters = BuildClipmaps(lighting, m_frame != 0 ? &m_previousClipmaps : nullptr, m_resolutionPolicy.Scale());
        std::vector<Caster> inputs;
        inputs.reserve(m_casterCount);
        // Preserve the draw-capacity contract: exceptionally fragmented scenes
        // retain whole-draw submission instead of disabling shadows.
        std::size_t clusteredCount = 0;
        m_clusterPlans.resize(casters.size());
        m_stats->clusterBoundsBuilds = m_stats->clusterBoundsCacheHits = 0;
        for (std::size_t index = 0; lighting.virtualShadowClusterCulling && index < casters.size(); ++index)
        {
            const auto &draw = casters[index];
            auto &plan = m_clusterPlans[index];
            plan.coalesce = false;
            if (!draw.mesh || !draw.castsShadow || draw.alphaMode == 2 || draw.surfaceType == 1) continue;
            const auto available = draw.firstIndex < draw.mesh->GetIndexCount() ? draw.mesh->GetIndexCount() - draw.firstIndex : 0;
            const auto count = std::min(draw.indexCount ? draw.indexCount : available, available);
            const auto ranges = SelectShadowGeometryClusters(draw.mesh->GetShadowClusters(), draw.firstIndex, count);
            const auto models = draw.instanceModels && !draw.instanceModels->empty() ? draw.instanceModels->size() : 1;
            auto rangeCount = std::max<std::size_t>(1, ranges.size());
            if (models == 1 && ranges.size() > 1)
            {
                if (!draw.preparationRevision || plan.packetRevision != draw.preparationRevision ||
                    plan.mesh != draw.mesh || plan.meshRevision != draw.mesh->GetRevision() ||
                    plan.firstIndex != draw.firstIndex || plan.indexCount != count)
                {
                    ++m_stats->clusterBoundsBuilds;
                    plan.mesh = draw.mesh;
                    plan.meshRevision = draw.mesh->GetRevision();
                    plan.packetRevision = draw.preparationRevision;
                    plan.firstIndex = draw.firstIndex;
                    plan.indexCount = count;
                    plan.merged = MergeShadowClusters(ranges);
                    const auto &model = draw.instanceModels && !draw.instanceModels->empty() ? draw.instanceModels->front() : draw.model;
                    plan.worldBounds = ShadowClusterWorldSphere(plan.merged, std::span<const glm::mat4>(&model, 1));
                }
                else ++m_stats->clusterBoundsCacheHits;
                // Reevaluate projection-dependent fit when lights or clipmaps move.
                plan.coalesce = FitsShadowPage(parameters, plan.worldBounds);
                if (plan.coalesce) rangeCount = 1;
            }
            clusteredCount += draw.firstIndex % 3 == 0 ? rangeCount * ((models + 15) / 16) : (models + 63) / 64;
        }
        const bool useClusters = lighting.virtualShadowClusterCulling && clusteredCount <= PLUTO_VSM_MAX_DRAW_CHUNKS;
        const auto prepare = [&](std::span<const BasicDraw> draws, std::vector<Chunk> &chunks, bool shadow)
        {
            std::size_t cursor = 0;
            for (std::size_t index = 0; index < draws.size(); ++index)
            {
                const auto &draw = draws[index];
                if (!draw.mesh || !draw.mesh->IsValid() || draw.alphaMode == 2 || draw.surfaceType == 1 || (shadow && !draw.castsShadow)) continue;
                const auto available = draw.firstIndex < draw.mesh->GetIndexCount() ? draw.mesh->GetIndexCount() - draw.firstIndex : 0;
                const auto drawCount = std::min(draw.indexCount ? draw.indexCount : available, available);
                if (drawCount == 0) continue;
                const std::size_t modelCount = draw.instanceModels && !draw.instanceModels->empty() ? draw.instanceModels->size() : 1;
                auto clusters = SelectShadowGeometryClusters(draw.mesh->GetShadowClusters(), draw.firstIndex, drawCount);
                const bool subdivide = shadow && useClusters && draw.firstIndex % 3 == 0 && !clusters.empty();
                if (subdivide && m_clusterPlans[index].coalesce)
                    clusters = std::span<const ShadowGeometryCluster>(&m_clusterPlans[index].merged, 1);
                const auto rangeCount = subdivide ? clusters.size() : 1;
                const std::size_t instanceLimit = subdivide ? 16 : 64;
                for (std::size_t range = 0; range < rangeCount; ++range)
                {
                    const auto firstIndex = subdivide ? std::max(draw.firstIndex, clusters[range].firstIndex) : draw.firstIndex;
                    const auto endIndex = subdivide ? std::min(draw.firstIndex + drawCount, clusters[range].firstIndex + clusters[range].indexCount) : draw.firstIndex + drawCount;
                    if (endIndex <= firstIndex) continue;
                    const auto count = endIndex - firstIndex;
                    for (std::size_t first = 0; first < modelCount; first += instanceLimit)
                    {
                        if (cursor == chunks.size()) chunks.emplace_back();
                        auto &chunk = chunks[cursor];
                        const auto models = static_cast<std::uint32_t>(std::min(instanceLimit, modelCount - first));
                        if (draw.preparationRevision && chunk.preparationRevision == draw.preparationRevision &&
                            chunk.mesh == draw.mesh && chunk.meshRevision == draw.mesh->GetRevision() &&
                            chunk.firstInstance == first && chunk.submission.instances == models &&
                            chunk.submission.firstIndex == firstIndex && chunk.submission.indexCount == count && chunk.clustered == subdivide)
                        {
                            // Refresh the borrowed packet address even on a hit.
                            // Clipmap/camera/lighting feedback is still processed
                            // below; only immutable caster/receiver preparation skips.
                            chunk.submission.draw = &draw;
                            if (shadow)
                            {
                                const auto signature = signatures[index] ^ (std::uint64_t(firstIndex) * 0x9e3779b97f4a7c15ull);
                                inputs.push_back({chunk.bounds,
                                                  {count, firstIndex, models, index},
                                                  {std::uint32_t(signature),
                                                   std::uint32_t(signature >> 32) ^ std::uint32_t(first), 0, 0}});
                            }
                            ++cursor;
                            continue;
                        }
                        chunk.preparationRevision = draw.preparationRevision;
                        chunk.clustered = subdivide;
                        chunk.firstInstance = first;
                        const glm::uvec4 drawParameters(cursor, models, draw.alphaMode, 0);
                        const glm::vec4 alpha(draw.uvScale, draw.alphaCutoff, draw.baseColor.a);
                        const auto upload = [&](const auto &parameters)
                        {
                            const auto bytes = Bytes(parameters);
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
                            upload(RigidDrawParameters{model, drawParameters, alpha});
                        }
                        else
                        {
                            DrawParameters parameters;
                            std::copy_n(draw.instanceModels->begin() + first, models, parameters.models.begin());
                            parameters.draw = drawParameters; parameters.alpha = alpha;
                            upload(parameters);
                        }
                        const auto texture = draw.baseColorTexture ? draw.baseColorTexture : m_white.Get();
                        changed |= chunk.mesh != draw.mesh || chunk.meshRevision != draw.mesh->GetRevision() ||
                            chunk.texture != texture || chunk.submission.indexCount != count || chunk.submission.firstIndex != firstIndex;
                        chunk.mesh = draw.mesh; chunk.meshRevision = draw.mesh->GetRevision();
                        chunk.submission = {&draw, count, firstIndex, models, {}, cursor * 20};
                        chunk.texture = draw.baseColorTexture ? draw.baseColorTexture : m_white.Get();
                        if (shadow)
                        {
                            const auto signature = signatures[index] ^ (std::uint64_t(firstIndex) * 0x9e3779b97f4a7c15ull);
                            const float radius = std::isfinite(draw.shadowBoundsRadius) ? draw.shadowBoundsRadius : -1.0f;
                            const auto transforms = draw.instanceModels && !draw.instanceModels->empty()
                                ? std::span<const glm::mat4>(*draw.instanceModels).subspan(first, models) : std::span<const glm::mat4>(&draw.model, 1);
                            chunk.bounds = subdivide ? ShadowClusterWorldSphere(clusters[range], transforms) : glm::vec4(draw.shadowBoundsCenter, radius);
                            inputs.push_back({chunk.bounds, {count, firstIndex, models, index},
                                {std::uint32_t(signature), std::uint32_t(signature >> 32) ^ std::uint32_t(first), 0, 0}});
                        }
                        ++cursor;
                    }
                }
            }
            return cursor;
        };
        const auto receiverCount = prepare(receivers, m_receiverChunks, false);
        const auto casterCount = prepare(casters, m_casterChunks, true);
        changed |= receiverCount != m_receiverCount || casterCount != m_casterCount;
        m_receiverCount = receiverCount; m_casterCount = casterCount;
        if (m_capacity < std::max(m_casterCount, std::size_t{1}))
        {
            m_capacity = 1;
            while (m_capacity < m_casterCount) m_capacity *= 2;
            m_casters = rhi::Buffer(device, device.CreateBuffer({m_capacity * sizeof(Caster), rhi::BufferUsage::Storage, "VSM caster bounds/signatures"}));
            m_lists = rhi::Buffer(device, device.CreateBuffer({m_capacity * poolCapacity * 4, rhi::BufferUsage::Storage, "VSM compact caster page lists"}));
            m_indirect = rhi::Buffer(device, device.CreateBuffer({m_capacity * 20, rhi::BufferUsage::Storage, "VSM indexed indirect commands"}));
            // Zero epochs make every pair invalid until evaluated. Each record
            // stores exact caster/page generations and conservative membership.
            const std::vector<glm::uvec4> empty(m_capacity * poolCapacity, glm::uvec4(0));
            m_membership = rhi::Buffer(device, device.CreateBuffer({empty.size() * sizeof(glm::uvec4),
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
                // Membership depends only on conservative bounds, not mesh
                // identity. Reordering equal bounds is therefore safe as well.
                if (m_cacheMembership && input.bounds == previous.bounds) input.identity.z = previous.identity.z;
            }
        }
        const auto inputBytes = std::as_bytes(std::span(inputs));
        if (m_uploadedCasters.size() != inputBytes.size() ||
            (!inputBytes.empty() && std::memcmp(m_uploadedCasters.data(), inputBytes.data(), inputBytes.size()) != 0))
        {
            if (!inputBytes.empty()) device.UpdateBuffer(m_casters.Get(), 0, inputBytes);
            m_uploadedCasters.assign(inputBytes.begin(), inputBytes.end());
            changed = true;
        }
        for (std::size_t index = 0; index < m_casterCount; ++index) m_casterChunks[index].submission.indirect = m_indirect.Get();
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
        parameters.limits = {m_casterCount, m_frame, std::clamp(lighting.virtualShadowPageBudget, 1u, poolCapacity),
                             std::clamp(lighting.virtualShadowTriangleBudget, 1u, 16000000u)};
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
    void VirtualShadowMaps::BindCompute(rhi::ICommandContext &commands, std::size_t pipeline)
    {
        commands.BindPipeline(m_compute[pipeline].Get());
        commands.BindUniformBuffer(0, m_parameters.Get());
        commands.BindTexture(1, m_receiverDepth.Get(), m_sampler.Get());
        const std::array buffers{m_pages.Get(), m_casters.Get(), m_lists.Get(), m_indirect.Get(), m_requestList.Get(), m_counters.Get()};
        for (std::uint32_t index = 0; index < buffers.size(); ++index) commands.BindStorageBuffer(index + 2, buffers[index]);
        commands.BindStorageBuffer(0, m_membership.Get());
        commands.BindStorageImage(0, m_requests.Get());
        commands.BindStorageImage(1, m_table.Get());
    }
    void VirtualShadowMaps::Record(rhi::ICommandContext &commands, const SubmitMesh &submit)
    {
        if (m_reuseFrame) return;
        // Submission callbacks only bind mesh buffers and issue the draw.
        // Retain pass-local state rather than repeating backend handle lookups
        // for every submesh of an imported architectural model.
        const auto recordChunks = [&](const std::vector<Chunk> &chunks, std::size_t count,
                                      std::size_t instancedPipeline, std::size_t rigidPipeline)
        {
            std::size_t boundPipeline = m_raster.size();
            rhi::TextureHandle boundTexture;
            for (std::size_t index = 0; index < count; ++index)
            {
                const auto &chunk = chunks[index];
                const auto pipeline = chunk.submission.instances == 1 ? rigidPipeline : instancedPipeline;
                if (pipeline != boundPipeline)
                {
                    commands.BindPipeline(m_raster[pipeline].Get());
                    boundPipeline = pipeline;
                }
                commands.BindUniformBuffer(1, chunk.uniform.Get());
                if (chunk.texture != boundTexture)
                {
                    commands.BindTexture(9, chunk.texture, m_materialSampler.Get());
                    boundTexture = chunk.texture;
                }
                submit(chunk.submission);
            }
        };
        commands.BeginGpuScope("RHI VSM Receiver Depth");
        rhi::RenderingInfo depth;
        depth.colorAttachments = {m_receiverColor.Get()}; depth.depthAttachment = m_receiverDepth.Get();
        depth.width = m_width; depth.height = m_height; depth.clearDepthValue = 0;
        commands.BeginRendering(depth);
        commands.BindPipeline(m_raster[0].Get());
        commands.BindUniformBuffer(0, m_parameters.Get());
        recordChunks(m_receiverChunks, m_receiverCount, 0, 3);
        commands.EndRendering(); commands.EndGpuScope();
        commands.BeginGpuScope("RHI VSM GPU Planning");
        commands.ShaderMemoryBarrier();
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
            commands.ShaderMemoryBarrier();
            commands.EndGpuScope();
        }
        commands.EndGpuScope();
        commands.BeginGpuScope("RHI Virtual Shadow Pages");
        rhi::RenderingInfo atlas;
        atlas.colorAttachments = {m_color.Get()}; atlas.depthAttachment = m_depth.Get();
        atlas.width = atlas.height = m_poolTiles * PLUTO_VSM_PAGE_SIZE;
        atlas.clearDepth = atlas.clearColor = false;
        commands.BeginRendering(atlas);
        commands.BindPipeline(m_raster[2].Get());
        commands.BindUniformBuffer(0, m_parameters.Get()); commands.BindStorageBuffer(2, m_pages.Get());
        commands.Draw(m_poolTiles * m_poolTiles * 6);
        commands.BindPipeline(m_raster[1].Get());
        commands.BindUniformBuffer(0, m_parameters.Get()); commands.BindStorageBuffer(2, m_pages.Get());
        commands.BindStorageBuffer(4, m_lists.Get());
        recordChunks(m_casterChunks, m_casterCount, 1, 4);
        commands.EndRendering(); commands.EndGpuScope();
        commands.ShaderMemoryBarrier();
        BindCompute(commands, 6);
        commands.Dispatch((m_poolTiles * m_poolTiles + 63) / 64, 1, 1);
        commands.ShaderMemoryBarrier();
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
        stats.receiverDraws = m_reuseFrame ? 0 : static_cast<std::uint32_t>(m_receiverCount);
        stats.memoryBytes = std::uint64_t(poolCapacity) * PLUTO_VSM_PAGE_SIZE * PLUTO_VSM_PAGE_SIZE * 8 +
            PLUTO_VSM_LEVELS * PLUTO_VSM_LEVEL_PAGES * 12 + poolCapacity * 64 + sizeof(VirtualShadowParameters) + PLUTO_VSM_COUNTER_COUNT * 4 + 16 +
            m_capacity * (sizeof(Caster) + poolCapacity * (4 + sizeof(glm::uvec4)) + 20) + PLUTO_VSM_CAPACITY * 4 +
            std::uint64_t(m_width) * m_height * 8;
        for (const auto &chunk : m_receiverChunks) stats.memoryBytes += chunk.uploaded.size();
        for (const auto &chunk : m_casterChunks) stats.memoryBytes += chunk.uploaded.size();
        return stats;
    }
}
