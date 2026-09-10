#include "PlutoGE/render/VirtualShadowMaps.h"
#include "PlutoGE/render/BasicRenderer.h"
#include <algorithm>
#include <cmath>
#include <cstring>
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
        for (int level = 0; level < PLUTO_VSM_LEVELS; ++level)
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
        result.camera = glm::vec4(lighting.cameraPosition, 1);
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
        m_depth = Texture(device, device.CreateTexture({PLUTO_VSM_ATLAS_SIZE, PLUTO_VSM_ATLAS_SIZE, Format::D32Float,
            TextureUsage::DepthStencilAttachment, "VSM physical depth", true}));
        m_color = Texture(device, device.CreateTexture({PLUTO_VSM_ATLAS_SIZE, PLUTO_VSM_ATLAS_SIZE, Format::R32Float,
            TextureUsage::ColorAttachment, "VSM physical color", false}));
        m_table = Texture(device, device.CreateTexture({PLUTO_VSM_GRID, PLUTO_VSM_GRID * PLUTO_VSM_LEVELS, Format::R32Float,
            TextureUsage::Sampled, "VSM GPU page table", true, 1, true, 1}));
        m_requests = Texture(device, device.CreateTexture({PLUTO_VSM_GRID, PLUTO_VSM_GRID * PLUTO_VSM_LEVELS, Format::R32Uint,
            TextureUsage::Sampled, "VSM GPU request mask", false, 1, true, 1}));
        m_parameters = Buffer(device, device.CreateBuffer({sizeof(VirtualShadowParameters), BufferUsage::Uniform, "VSM clipmaps"}));
        m_pages = Buffer(device, device.CreateBuffer({PLUTO_VSM_CAPACITY * 64, BufferUsage::Storage, "VSM persistent physical metadata"}));
        m_requestList = Buffer(device, device.CreateBuffer({(PLUTO_VSM_LEVELS + PLUTO_VSM_LEVELS * PLUTO_VSM_LEVEL_PAGES) * 4,
            BufferUsage::Storage, "VSM compact GPU requests"}));
        m_counters = Buffer(device, device.CreateBuffer({64, BufferUsage::Storage, "VSM asynchronous counters"}));
        m_sampler = Sampler(device, device.CreateSampler({false, false, "VSM point sampler"}));
        m_materialSampler = Sampler(device, device.CreateSampler({true, true, "VSM material sampler"}));
        const std::array<std::uint8_t, 4> white{255, 255, 255, 255};
        m_white = Texture(device, device.CreateTexture({1, 1, Format::R8G8B8A8Unorm, TextureUsage::Sampled, "VSM neutral alpha"}, Bytes(white)));
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
        bool changed = m_frame == 0 || m_width != width || m_height != height;
        if (m_width != width || m_height != height)
        {
            m_receiverDepth = rhi::Texture(device, device.CreateTexture({width, height, rhi::Format::D32Float,
                rhi::TextureUsage::DepthStencilAttachment, "VSM receiver depth", true}));
            m_receiverColor = rhi::Texture(device, device.CreateTexture({width, height, rhi::Format::R32Float,
                rhi::TextureUsage::ColorAttachment, "VSM receiver color", false}));
            m_width = width; m_height = height;
        }
        std::vector<Caster> inputs;
        const auto prepare = [&](std::span<const BasicDraw> draws, std::vector<Chunk> &chunks, bool shadow)
        {
            std::size_t cursor = 0;
            for (std::size_t index = 0; index < draws.size(); ++index)
            {
                const auto &draw = draws[index];
                if (!draw.mesh || !draw.mesh->IsValid() || draw.alphaMode == 2 || draw.surfaceType == 1 || (shadow && !draw.castsShadow)) continue;
                const auto available = draw.firstIndex < draw.mesh->GetIndexCount() ? draw.mesh->GetIndexCount() - draw.firstIndex : 0;
                const auto count = std::min(draw.indexCount ? draw.indexCount : available, available);
                if (count == 0) continue;
                const std::size_t modelCount = draw.instanceModels && !draw.instanceModels->empty() ? draw.instanceModels->size() : 1;
                for (std::size_t first = 0; first < modelCount; first += 64)
                {
                    if (cursor == chunks.size()) chunks.emplace_back();
                    auto &chunk = chunks[cursor];
                    const auto models = static_cast<std::uint32_t>(std::min(std::size_t{64}, modelCount - first));
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
                        chunk.texture != texture || chunk.submission.indexCount != count || chunk.submission.firstIndex != draw.firstIndex;
                    chunk.mesh = draw.mesh; chunk.meshRevision = draw.mesh->GetRevision();
                    chunk.submission = {&draw, count, draw.firstIndex, models, {}, cursor * 20};
                    chunk.texture = draw.baseColorTexture ? draw.baseColorTexture : m_white.Get();
                    if (shadow)
                    {
                        const auto signature = signatures[index];
                        const float radius = std::isfinite(draw.shadowBoundsRadius) ? draw.shadowBoundsRadius : -1.0f;
                        inputs.push_back({glm::vec4(draw.shadowBoundsCenter, radius), {count, draw.firstIndex, models, index},
                            {std::uint32_t(signature), std::uint32_t(signature >> 32) ^ std::uint32_t(first), 0, 0}});
                    }
                    ++cursor;
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
            m_lists = rhi::Buffer(device, device.CreateBuffer({m_capacity * PLUTO_VSM_CAPACITY * 4, rhi::BufferUsage::Storage, "VSM compact caster page lists"}));
            m_indirect = rhi::Buffer(device, device.CreateBuffer({m_capacity * 20, rhi::BufferUsage::Storage, "VSM indexed indirect commands"}));
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
        // Delayed feedback never stalls the GPU. Fit the working set by changing
        // resolution globally, rather than leaving arbitrary fine-page islands.
        if (m_stats->gpuCountersAvailable && m_stats->gpuFrame > m_feedbackFrame &&
            m_stats->gpuFrame >= m_feedbackAfter)
        {
            m_feedbackFrame = m_stats->gpuFrame;
            const auto fineRequests = m_stats->requested > PLUTO_VSM_ROOT_PAGES ?
                m_stats->requested - PLUTO_VSM_ROOT_PAGES : 0;
            constexpr auto fineCapacity = PLUTO_VSM_CAPACITY - PLUTO_VSM_ROOT_PAGES;
            float nextScale = m_resolutionScale;
            if (m_stats->overflow && fineRequests > fineCapacity)
            {
                nextScale = std::min(16.0f, m_resolutionScale * 2.0f);
                m_lowPressureFrames = 0;
            }
            else if (fineRequests < fineCapacity / 8)
            {
                if (++m_lowPressureFrames >= 240) nextScale = std::max(1.0f, m_resolutionScale * 0.5f);
            }
            else m_lowPressureFrames = 0;
            if (nextScale != m_resolutionScale)
            {
                m_resolutionScale = nextScale;
                m_feedbackAfter = m_frame + 32;
                m_lowPressureFrames = 0;
            }
        }
        auto parameters = BuildClipmaps(lighting, m_frame != 0 ? &m_previousClipmaps : nullptr, m_resolutionScale);
        m_previousClipmaps = parameters;
        parameters.viewProjection = viewProjection;
        parameters.inverseViewProjection = glm::inverse(viewProjection);
        parameters.viewport = {width, height, device.GetApi() == rhi::GraphicsApi::Vulkan ? 1 : 0, device.UsesZeroToOneClipDepth() ? 1 : 0};
        ++m_frame;
        if (m_frame == 0) ++m_frame;
        parameters.limits = {m_casterCount, m_frame, std::clamp(lighting.virtualShadowPageBudget, 1u, std::uint32_t(PLUTO_VSM_CAPACITY)),
                             std::clamp(lighting.virtualShadowTriangleBudget, 1u, 16000000u)};
        parameters.settings = {lighting.shadowSoftness, 1, m_frame == 1 ? 1 : 0, m_resolutionScale};
        auto inputKey = parameters;
        inputKey.limits.y = 0; inputKey.settings.z = 0;
        changed |= std::memcmp(&inputKey, &m_previousInputs, sizeof(inputKey)) != 0;
        m_previousInputs = inputKey;
        if (changed) m_inputChangeFrame = m_frame;
        const auto fineRequests = m_stats->requested > PLUTO_VSM_ROOT_PAGES ? m_stats->requested - PLUTO_VSM_ROOT_PAGES : 0;
        const bool refinementPending = m_resolutionScale > 1.0f && fineRequests < (PLUTO_VSM_CAPACITY - PLUTO_VSM_ROOT_PAGES) / 8;
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
        commands.BindStorageImage(0, m_requests.Get());
        commands.BindStorageImage(1, m_table.Get());
    }
    void VirtualShadowMaps::Record(rhi::ICommandContext &commands, const SubmitMesh &submit)
    {
        if (m_reuseFrame) return;
        commands.BeginGpuScope("RHI VSM Receiver Depth");
        rhi::RenderingInfo depth;
        depth.colorAttachments = {m_receiverColor.Get()}; depth.depthAttachment = m_receiverDepth.Get();
        depth.width = m_width; depth.height = m_height; depth.clearDepthValue = 0;
        commands.BeginRendering(depth);
        commands.BindPipeline(m_raster[0].Get());
        commands.BindUniformBuffer(0, m_parameters.Get());
        for (std::size_t index = 0; index < m_receiverCount; ++index)
        {
            const auto &chunk = m_receiverChunks[index];
            commands.BindPipeline(m_raster[chunk.submission.instances == 1 ? 3 : 0].Get());
            commands.BindUniformBuffer(1, chunk.uniform.Get());
            commands.BindTexture(9, chunk.texture, m_materialSampler.Get());
            submit(chunk.submission);
        }
        commands.EndRendering(); commands.EndGpuScope();
        commands.BeginGpuScope("RHI VSM GPU Planning");
        commands.ShaderMemoryBarrier();
        for (std::size_t pass = 0; pass < 6; ++pass)
        {
            BindCompute(commands, pass);
            if (pass == 0) commands.Dispatch(PLUTO_VSM_LEVELS * PLUTO_VSM_LEVEL_PAGES / 64, 1, 1);
            else if (pass == 1) commands.Dispatch((m_width + 7) / 8, (m_height + 7) / 8, 1);
            else if (pass == 3) commands.Dispatch(PLUTO_VSM_CAPACITY / 64, 1, 1);
            else if (pass == 5) commands.Dispatch(static_cast<std::uint32_t>((m_casterCount + 63) / 64 + (m_casterCount == 0)), 1, 1);
            else commands.Dispatch(1, 1, 1);
            commands.ShaderMemoryBarrier();
        }
        commands.EndGpuScope();
        commands.BeginGpuScope("RHI Virtual Shadow Pages");
        rhi::RenderingInfo atlas;
        atlas.colorAttachments = {m_color.Get()}; atlas.depthAttachment = m_depth.Get();
        atlas.width = atlas.height = PLUTO_VSM_ATLAS_SIZE;
        atlas.clearDepth = atlas.clearColor = false;
        commands.BeginRendering(atlas);
        commands.BindPipeline(m_raster[2].Get());
        commands.BindUniformBuffer(0, m_parameters.Get()); commands.BindStorageBuffer(2, m_pages.Get());
        commands.Draw(PLUTO_VSM_CAPACITY * 6);
        commands.BindPipeline(m_raster[1].Get());
        commands.BindUniformBuffer(0, m_parameters.Get()); commands.BindStorageBuffer(2, m_pages.Get());
        commands.BindStorageBuffer(4, m_lists.Get());
        for (std::size_t index = 0; index < m_casterCount; ++index)
        {
            const auto &chunk = m_casterChunks[index];
            commands.BindPipeline(m_raster[chunk.submission.instances == 1 ? 4 : 1].Get());
            commands.BindUniformBuffer(1, chunk.uniform.Get());
            commands.BindTexture(9, chunk.texture, m_materialSampler.Get());
            submit(chunk.submission);
        }
        commands.EndRendering(); commands.EndGpuScope();
        commands.ShaderMemoryBarrier();
        BindCompute(commands, 6);
        commands.Dispatch(PLUTO_VSM_CAPACITY / 64, 1, 1);
        commands.ShaderMemoryBarrier();
        const std::weak_ptr<VirtualShadowStats> weakStats = m_stats;
        const auto frame = m_frame;
        commands.QueueBufferReadback(m_counters.Get(), 64, [weakStats, frame](std::span<const std::byte> bytes)
        {
            if (const auto stats = weakStats.lock(); stats && bytes.size() == 64)
            {
                std::array<std::uint32_t, 16> values{};
                std::memcpy(values.data(), bytes.data(), bytes.size());
                stats->requested = values[0]; stats->resident = values[1]; stats->dirty = values[2]; stats->cacheHits = values[3];
                stats->evicted = values[4]; stats->overflow = values[5]; stats->casterPagePairs = values[6]; stats->submittedTriangles = values[7];
                stats->deferred = values[8]; stats->updated = values[9]; stats->indirectDraws = values[10];
                stats->gpuFrame = frame; stats->gpuCountersAvailable = true;
            }
        });
    }
    VirtualShadowStats VirtualShadowMaps::GetStats() const
    {
        auto stats = *m_stats;
        stats.resolutionScale = m_resolutionScale;
        stats.reusedFrame = m_reuseFrame;
        stats.submittedIndirectCommands = m_reuseFrame ? 0 : static_cast<std::uint32_t>(m_casterCount);
        stats.receiverDraws = m_reuseFrame ? 0 : static_cast<std::uint32_t>(m_receiverCount);
        stats.memoryBytes = std::uint64_t(PLUTO_VSM_ATLAS_SIZE) * PLUTO_VSM_ATLAS_SIZE * 8 +
            PLUTO_VSM_LEVELS * PLUTO_VSM_LEVEL_PAGES * 12 + PLUTO_VSM_CAPACITY * 64 + sizeof(VirtualShadowParameters) + 80 +
            m_capacity * (sizeof(Caster) + PLUTO_VSM_CAPACITY * 4 + 20) +
            std::uint64_t(m_width) * m_height * 8;
        for (const auto &chunk : m_receiverChunks) stats.memoryBytes += chunk.uploaded.size();
        for (const auto &chunk : m_casterChunks) stats.memoryBytes += chunk.uploaded.size();
        return stats;
    }
}
