#include "DetailGrassRenderer.h"

#include "ClipFrustum.h"
#include "PlutoGE/render/FoliageWind.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace PlutoGE::render
{
    namespace
    {
        // DetailGrassGenerate.slang's DetailGrassGenerateParameters (std140).
        struct alignas(16) GenerateParameters
        {
            glm::mat4 terrainToWorld{1.0f};
            glm::mat4 terrainNormalToWorld{1.0f};
            std::array<glm::vec4, 4> planes{};
            glm::vec4 cameraWorld{0.0f};
            glm::vec4 cameraLocal{0.0f};
            glm::vec4 terrainGrid{0.0f};
            glm::vec4 distances{0.0f};
            glm::vec4 sizes{0.0f};
            glm::vec4 placement{0.0f};
            glm::vec4 densityChannel{0.0f};
            glm::vec4 color{0.0f};
            glm::vec4 dryColor{0.0f};
            glm::vec4 surface{0.0f};
            glm::vec4 lods{0.0f};
            glm::uvec4 lattice{0u};
            glm::uvec4 regions{0u};
            glm::uvec4 capacities{0u};
            std::array<glm::ivec4, DetailGrassMaxRings> rings{};
        };
        static_assert(sizeof(GenerateParameters) == 512, "DetailGrassGenerate.slang DetailGrassGenerateParameters");
        // DetailGrassCommon.slang's DetailGrassInstance.
        constexpr std::size_t InstanceBytes = 32;
        // Words per indirect draw (RHI DrawIndexedIndirect).
        constexpr std::uint32_t ArgumentWords = 5;
        // Unused meshes and terrain samples are released after this many frames.
        constexpr std::uint64_t RetainFrames = 120;

        template <class T> std::span<const std::byte> Bytes(const std::vector<T> &values) { return std::as_bytes(std::span(values)); }
        template <class T> std::span<const std::byte> Bytes(const T &value) { return std::as_bytes(std::span(&value, 1)); }

        void Grow(rhi::IRenderDevice &device, rhi::Buffer &buffer, std::size_t &capacity, std::size_t bytes, const char *name)
        {
            if (buffer && capacity >= bytes) return;
            capacity = std::max<std::size_t>(bytes + bytes / 2, 4096);
            buffer = rhi::Buffer(device, device.CreateBuffer({capacity, rhi::BufferUsage::Storage, name}));
        }

        // The terrain samples the generation pass reads: height, cut-away cell, and the surface normal's x and z from
        // central differences (as TerrainComponent's mesh normals are computed from the same heights).
        std::vector<glm::vec4> PackSamples(const DetailGrassHeightField &field)
        {
            const auto columns = static_cast<int>(field.columns), rows = static_cast<int>(field.rows);
            const auto height = [&](int x, int z)
            {
                x = std::clamp(x, 0, columns - 1);
                z = std::clamp(z, 0, rows - 1);
                return field.heights[std::size_t(z) * field.columns + std::size_t(x)];
            };
            std::vector<glm::vec4> samples(field.heights.size());
            for (int z = 0; z < rows; ++z)
                for (int x = 0; x < columns; ++x)
                {
                    const glm::vec3 normal = glm::normalize(glm::vec3(height(x - 1, z) - height(x + 1, z), 2.0f * field.cellSize,
                                                                      height(x, z - 1) - height(x, z + 1)));
                    const std::size_t index = std::size_t(z) * field.columns + std::size_t(x);
                    const float hole = !field.holes.empty() && field.holes[index] != 0 ? 1.0f : 0.0f;
                    samples[index] = {field.heights[index], hole, normal.x, normal.z};
                }
            return samples;
        }

        // Instances the regions of each LOD must hold: every candidate of every ring whose distances meet the LOD's
        // can grow at most once, so this bound never overflows.
        std::array<std::uint32_t, DetailGrassLodCount> Capacities(const DetailGrassLattice &lattice, const DetailGrassLayer &layer)
        {
            const std::array<float, DetailGrassLodCount + 1> lodBands{0.0f, layer.lod1Distance, layer.lod2Distance,
                                                                      std::numeric_limits<float>::max()};
            std::array<std::uint32_t, DetailGrassLodCount> capacities{};
            for (std::uint32_t ring = 0; ring < lattice.rings; ++ring)
            {
                const float inner = ring == 0 ? 0.0f : lattice.ringRadius[ring - 1], outer = lattice.ringRadius[ring];
                const auto candidates = lattice.ringSide[ring] * lattice.ringSide[ring];
                for (std::uint32_t lod = 0; lod < DetailGrassLodCount; ++lod)
                    if (inner < lodBands[lod + 1] && outer > lodBands[lod])
                        capacities[lod] += candidates;
            }
            return capacities;
        }
    }

    BasicDraw DetailGrassDefaultMaterial(const DetailGrassLayer &source)
    {
        const auto layer = source.Sanitized();
        BasicDraw material;
        material.baseColor = glm::vec4(1.0f);
        material.roughness = layer.roughness;
        material.metallic = 0.0f;
        material.twoSided = true;
        material.diffuseTransmission = layer.translucency;
        // Light through a blade picks up its green; the clump colour already tints the reflected part.
        material.diffuseTransmissionColor = glm::vec3(0.8f, 1.0f, 0.45f);
        material.diffuseTransmissionAlbedoTint = 1.0f;
        return material;
    }

    bool DetailGrassRenderer::Initialize(rhi::IRenderDevice &device, const Shaders &shaders)
    {
        m_device = &device;
        if (!shaders.Available()) return false;
        using rhi::ResourceBindingType;
        const std::vector<rhi::GraphicsPipelineDescriptor::ResourceBinding> bindings{
            {0, 0, 0, ResourceBindingType::UniformBuffer, rhi::ShaderStageMask::Compute},
            {1, 0, 1, ResourceBindingType::StorageBuffer, rhi::ShaderStageMask::Compute},
            {2, 0, 2, ResourceBindingType::StorageBuffer, rhi::ShaderStageMask::Compute},
            {3, 0, 3, ResourceBindingType::StorageBuffer, rhi::ShaderStageMask::Compute},
            {4, 0, 4, ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Compute},
            {5, 0, 5, ResourceBindingType::SampledTexture, rhi::ShaderStageMask::Compute}};
        m_generate = rhi::GraphicsPipeline(device, device.CreateComputePipeline({shaders.generate, bindings, "Detail grass generation"}));
        m_sampler = rhi::Sampler(device, device.CreateSampler({true, false, "Detail grass maps"}));
        constexpr std::array<std::uint8_t, 4> white{255, 255, 255, 255};
        m_fallback = rhi::Texture(device, device.CreateTexture(
            {1, 1, rhi::Format::R8G8B8A8Unorm, rhi::TextureUsage::Sampled, "Detail grass fallback map", true, 1, false, 1},
            std::as_bytes(std::span(white))));
        if (!m_generate || !m_sampler || !m_fallback)
        {
            m_generate = {};
            return false;
        }
        return true;
    }

    const DetailGrassRenderer::ClumpMesh &DetailGrassRenderer::Clump(BasicRenderer &renderer, const DetailGrassClumpShape &shape)
    {
        for (auto &clump : m_clumps)
            if (clump->shape == shape)
            {
                clump->lastFrame = m_frame;
                return *clump;
            }
        auto clump = std::make_unique<ClumpMesh>();
        clump->shape = shape;
        clump->source = BuildDetailGrassClump(shape);
        std::vector<BasicVertex> vertices;
        vertices.reserve(clump->source.vertices.size());
        for (const auto &source : clump->source.vertices)
        {
            BasicVertex vertex;
            vertex.position = {source.position.x, source.position.y, source.position.z};
            vertex.normal = {source.normal.x, source.normal.y, source.normal.z};
            vertex.uv = {source.uv.x, source.uv.y};
            vertex.tangent = {source.tangent.x, source.tangent.y, source.tangent.z, source.tangent.w};
            vertices.push_back(vertex);
        }
        clump->mesh = renderer.CreateMesh({vertices, clump->source.indices});
        clump->lastFrame = m_frame;
        m_clumps.push_back(std::move(clump));
        return *m_clumps.back();
    }

    rhi::BufferHandle DetailGrassRenderer::Samples(const std::shared_ptr<const DetailGrassHeightField> &heights)
    {
        auto found = std::find_if(m_samples.begin(), m_samples.end(), [&](const TerrainSamples &entry)
                                  { return entry.key == heights.get() && !entry.source.expired(); });
        if (found == m_samples.end())
        {
            m_samples.push_back({heights, heights.get(), heights->revision + 1, {}, 0});
            found = std::prev(m_samples.end());
        }
        if (found->revision != heights->revision || !found->buffer)
        {
            // A new revision is a new buffer: the previous one may still be read by frames in flight.
            const auto samples = PackSamples(*heights);
            found->buffer = rhi::Buffer(*m_device, m_device->CreateBuffer(
                {samples.size() * sizeof(glm::vec4), rhi::BufferUsage::Storage, "Detail grass terrain samples"}, Bytes(samples)));
            found->revision = heights->revision;
        }
        found->lastFrame = m_frame;
        return found->buffer.Get();
    }

    void DetailGrassRenderer::Collect()
    {
        std::erase_if(m_samples, [&](const TerrainSamples &entry)
                      { return entry.source.expired() || entry.lastFrame + RetainFrames < m_frame; });
        std::erase_if(m_clumps, [&](const std::unique_ptr<ClumpMesh> &clump) { return clump->lastFrame + RetainFrames < m_frame; });
    }

    void DetailGrassRenderer::Prepare(BasicRenderer &renderer, std::span<const BasicDetailGrassField> fields, const View &view)
    {
        ++m_frame;
        m_draws.clear();
        m_dispatches.clear();
        m_candidates = 0;
        m_argumentBytes = 0;
        if (!Available()) return;
        auto &device = *m_device;
        const ClipFrustum frustum(view.viewProjection);

        struct Pending
        {
            GenerateParameters parameters;
            std::array<std::uint32_t, DetailGrassLodCount> capacities{};
        };
        std::vector<Pending> pending;
        std::vector<std::shared_ptr<BasicDetailGrassDraw>> grassDraws;
        std::vector<std::uint32_t> arguments;
        std::uint32_t instances = 0;
        for (const auto &field : fields)
        {
            if (!field.layer.enabled || !field.heights || !field.heights->Valid()) continue;
            const auto layer = field.layer.Sanitized();
            const auto lattice = MakeDetailGrassLattice(layer);
            const auto &clump = Clump(renderer, layer.shape);
            if (!clump.mesh.IsValid()) continue;

            const glm::mat4 worldToTerrain = glm::inverse(field.terrainToWorld);
            const glm::vec3 cameraLocal = glm::vec3(worldToTerrain * glm::vec4(view.cameraPosition, 1.0f));
            const float windReach = view.wind ? view.wind->MaxDisplacement(layer.wind, 1.0f) : FoliageWindMaxDisplacement(layer.wind);
            const float maxSlope = glm::radians(layer.maxSlopeDegrees);

            Pending entry;
            auto &parameters = entry.parameters;
            parameters.terrainToWorld = field.terrainToWorld;
            parameters.terrainNormalToWorld = glm::transpose(worldToTerrain);
            for (std::size_t plane = 0; plane < parameters.planes.size(); ++plane)
                parameters.planes[plane] = glm::vec4(frustum.planes[plane].normal, frustum.planes[plane].distance);
            parameters.cameraWorld = glm::vec4(view.cameraPosition, windReach);
            parameters.cameraLocal = glm::vec4(cameraLocal, lattice.spacing);
            parameters.terrainGrid = glm::vec4(field.heights->cellSize, float(field.heights->columns), float(field.heights->rows), 0.0f);
            parameters.distances = {layer.fullDensityDistance, layer.densityFalloff, layer.drawDistance, layer.maxWidthCompensation};
            parameters.sizes = {layer.minHeight, layer.maxHeight, layer.minWidth, layer.maxWidth};
            parameters.placement = {layer.weightThreshold, std::cos(maxSlope),
                                    std::cos(std::max(0.0f, maxSlope - glm::radians(10.0f))), 0.0f};
            parameters.densityChannel = field.densityChannel;
            parameters.color = glm::vec4(layer.color, layer.dryAmount);
            parameters.dryColor = glm::vec4(layer.dryColor, layer.colorJitter);
            parameters.surface = {1.0f / layer.patchSize, field.groundAlbedo ? 1.0f : 0.0f, clump.source.radius,
                                  field.density ? 1.0f : 0.0f};
            parameters.lods = {layer.lod1Distance, layer.lod2Distance, 0.0f, 0.0f};
            // A field's seed mixes its layer's with its identity, so two terrains with one layer differ.
            const auto seed = layer.seed ^ static_cast<std::uint32_t>(field.id) ^ static_cast<std::uint32_t>(field.id >> 32u);
            parameters.lattice = {lattice.rings, lattice.TopLevel(), seed, static_cast<std::uint32_t>(arguments.size())};
            std::uint32_t largestSide = 0;
            for (std::uint32_t ring = 0; ring < lattice.rings; ++ring)
            {
                const auto origin = lattice.RingOrigin(ring, {cameraLocal.x, cameraLocal.z});
                parameters.rings[ring] = {origin.x, origin.y, static_cast<int>(lattice.ringSide[ring]), 1 << ring};
                largestSide = std::max(largestSide, lattice.ringSide[ring]);
                m_candidates += std::size_t(lattice.ringSide[ring]) * lattice.ringSide[ring];
            }
            entry.capacities = Capacities(lattice, layer);
            for (std::uint32_t lod = 0; lod < DetailGrassLodCount; ++lod)
            {
                parameters.regions[lod] = instances;
                parameters.capacities[lod] = entry.capacities[lod];
                instances += entry.capacities[lod];
                // Arguments carry absolute first indices, so draws bind the mesh's index buffer at zero.
                const auto &range = clump.source.lods[lod];
                arguments.insert(arguments.end(), {range.indexCount, 0u, clump.mesh.BaseIndex() + range.firstIndex, 0u, 0u});
            }

            auto grass = std::make_shared<BasicDetailGrassDraw>();
            grass->argumentOffset = std::size_t(parameters.lattice.w) * sizeof(std::uint32_t);
            for (std::uint32_t lod = 0; lod < DetailGrassLodCount; ++lod) grass->firstInstance[lod] = parameters.regions[lod];
            grass->shading = {layer.normalBend, layer.rootOcclusion, layer.groundTint, layer.alignToGround};

            BasicDraw draw = field.material ? *field.material : DetailGrassDefaultMaterial(layer);
            // Clumps are standard surfaces seen from both sides; only their own vertex stages place them.
            draw.mesh = &clump.mesh;
            draw.model = glm::mat4(1.0f);
            draw.firstIndex = 0;
            draw.indexCount = 0;
            draw.twoSided = true;
            draw.surfaceType = 0;
            draw.alphaMode = std::min<std::uint32_t>(draw.alphaMode, 1u);
            draw.shaderGraphProgram = nullptr;
            draw.terrainLayers = nullptr;
            draw.impostor = nullptr;
            draw.spawnsImpostor = nullptr;
            draw.instanceModels = nullptr;
            draw.previousInstanceModels = nullptr;
            draw.instanceLods = nullptr;
            draw.outlineWidth = 0.0f;
            draw.castsShadow = false;
            draw.contributesToGi = false;
            draw.preparationRevision = 0;
            draw.foliageWind = layer.wind;
            m_draws.push_back(std::move(draw));
            grassDraws.push_back(std::move(grass));

            Dispatch dispatch;
            dispatch.samples = Samples(field.heights);
            dispatch.density = field.density;
            dispatch.groundAlbedo = field.groundAlbedo;
            const auto groups = (largestSide + PLUTO_DETAIL_GRASS_GROUP_SIZE - 1u) / PLUTO_DETAIL_GRASS_GROUP_SIZE;
            dispatch.groups = {groups, groups, lattice.rings};
            m_dispatches.push_back(dispatch);
            pending.push_back(entry);
        }
        Collect();
        if (pending.empty()) return;

        Grow(device, m_instances, m_instanceCapacity, std::max<std::size_t>(instances, 1u) * InstanceBytes, "Detail grass clumps");
        m_argumentBytes = arguments.size() * sizeof(std::uint32_t);
        Grow(device, m_arguments, m_argumentCapacity, m_argumentBytes, "Detail grass indirect draws");
        device.UpdateBuffer(m_arguments.Get(), 0, Bytes(arguments));
        while (m_parameters.size() < pending.size())
            m_parameters.emplace_back(device, device.CreateBuffer({sizeof(GenerateParameters), rhi::BufferUsage::Uniform,
                                                                   "Detail grass generation parameters"}));
        for (std::size_t index = 0; index < pending.size(); ++index)
            device.UpdateBuffer(m_parameters[index].Get(), 0, Bytes(pending[index].parameters));
        // The draws learn the frame's buffers once they exist.
        for (std::size_t index = 0; index < m_draws.size(); ++index)
        {
            grassDraws[index]->instances = m_instances.Get();
            grassDraws[index]->arguments = m_arguments.Get();
            m_draws[index].detailGrass = std::move(grassDraws[index]);
        }
    }

    void DetailGrassRenderer::Record(rhi::ICommandContext &commands)
    {
        if (m_dispatches.empty()) return;
        using rhi::BarrierAccess;
        commands.BeginGpuScope("RHI Detail grass generation");
        // The previous frame's draws read these buffers; the parameters and empty draws were uploaded before this.
        commands.PipelineBarrier(BarrierAccess::IndirectCommandRead | BarrierAccess::VertexShaderRead | BarrierAccess::ComputeShaderRead,
                                 BarrierAccess::ComputeShaderWrite | BarrierAccess::ComputeShaderRead);
        commands.BindPipeline(m_generate.Get());
        commands.BindStorageBuffer(2, m_instances.Get());
        commands.BindStorageBuffer(3, m_arguments.Get());
        for (std::size_t index = 0; index < m_dispatches.size(); ++index)
        {
            const auto &dispatch = m_dispatches[index];
            commands.BindUniformBuffer(0, m_parameters[index].Get());
            commands.BindStorageBuffer(1, dispatch.samples);
            commands.BindTexture(4, dispatch.density ? dispatch.density : m_fallback.Get(), m_sampler.Get());
            commands.BindTexture(5, dispatch.groundAlbedo ? dispatch.groundAlbedo : m_fallback.Get(), m_sampler.Get());
            commands.Dispatch(dispatch.groups.x, dispatch.groups.y, dispatch.groups.z);
        }
        commands.PipelineBarrier(BarrierAccess::ComputeShaderWrite,
                                 BarrierAccess::IndirectCommandRead | BarrierAccess::VertexShaderRead | BarrierAccess::TransferRead);
        commands.EndGpuScope();

        // Diagnostics only: the counts arrive frames later and decide nothing.
        const auto frame = m_frame;
        const std::weak_ptr<DetailGrassStats> weakStats = m_stats;
        const auto words = m_argumentBytes / sizeof(std::uint32_t);
        commands.QueueBufferReadback(m_arguments.Get(), m_argumentBytes, [weakStats, frame, words](std::span<const std::byte> bytes)
        {
            const auto stats = weakStats.lock();
            if (!stats || bytes.size() < words * sizeof(std::uint32_t) || frame <= stats->frame) return;
            std::vector<std::uint32_t> values(words);
            std::memcpy(values.data(), bytes.data(), words * sizeof(std::uint32_t));
            DetailGrassStats result;
            result.frame = frame;
            result.available = true;
            for (std::size_t draw = 0; draw * ArgumentWords + 1 < words; ++draw)
                result.clumps[draw % DetailGrassLodCount] += values[draw * ArgumentWords + 1];
            *stats = result;
        });
    }
}
