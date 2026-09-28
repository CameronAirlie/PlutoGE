#include "PlutoGE/render/RetainedRenderScene.h"
#include "PlutoGE/render/Material.h"
#include <algorithm>
#include <stdexcept>
#include <unordered_map>

namespace PlutoGE::render
{
    struct RetainedRenderScene::Impl
    {
        struct MaterialTopology
        {
            Material *material = nullptr;
            Shader *shader = nullptr;
            const void *graph = nullptr;
            float tessellation = 0;
            std::vector<Material *> overlays;
            bool operator==(const MaterialTopology &) const = default;
        };
        struct Producer
        {
            std::uint64_t id = 0, revision = 0, seen = 0, generation = 0;
            Revisions revisions;
            bool queued = false, structuralDirty = true;
            std::vector<RenderCommand> source, expanded;
            std::vector<std::size_t> expandedSources;
            std::vector<MaterialTopology> dependencies;
        };
        struct MaterialState { MaterialTopology topology; std::uint64_t frame = 0; };
        std::uint64_t frame = 1, generation = 0, revision = 0;
        std::vector<std::unique_ptr<Producer>> slots;
        std::vector<std::uint32_t> freeSlots;
        std::unordered_map<std::uint64_t, Handle> producers;
        std::vector<Handle> dirty;
        std::vector<RenderCommand *> commands;
        std::unordered_map<Material *, MaterialState> materialSnapshots;
        bool synchronized = false, membershipChanged = false;
        Stats stats;

        const MaterialTopology &Snapshot(Material *material)
        {
            auto &entry = materialSnapshots[material];
            if (entry.frame != frame)
            {
                entry.frame = frame;
                auto &snapshot = entry.topology;
                snapshot.material = material;
                snapshot.shader = material->GetShader();
                const auto &config = material->GetConfig();
                snapshot.graph = config.shaderGraphProgram.get();
                snapshot.tessellation = config.shaderGraphProgram ? config.shaderGraphProgram->data.header.w : 0;
                snapshot.overlays.clear();
                for (const auto &pass : config.additionalPasses) snapshot.overlays.push_back(pass.get());
            }
            return entry.topology;
        }
        void Expand(Producer &producer, const RenderCommand &source, std::size_t sourceIndex, std::vector<Material *> &path)
        {
            if (source.material && std::find(path.begin(), path.end(), source.material) != path.end())
                throw std::logic_error("Cyclic material additional passes");
            auto command = source;
            command.sourceObject = producer.id;
            command.sourceRevision = producer.revision;
            if (source.material)
            {
                // Copy before recursion: inserting another material can rehash.
                const auto snapshot = Snapshot(source.material);
                if (std::ranges::none_of(producer.dependencies, [&](const auto &dependency) { return dependency.material == source.material; }))
                    producer.dependencies.push_back(snapshot);
                if (snapshot.graph && command.mesh)
                    command.mesh = command.mesh->GetTessellated(static_cast<unsigned>(snapshot.tessellation));
                producer.expanded.push_back(command);
                producer.expandedSources.push_back(sourceIndex);
                path.push_back(source.material);
                for (auto *material : snapshot.overlays)
                {
                    if (!material) continue;
                    auto overlay = source;
                    overlay.material = material;
                    overlay.castsShadow = false;
                    Expand(producer, overlay, sourceIndex, path);
                }
                path.pop_back();
            }
            else { producer.expanded.push_back(command); producer.expandedSources.push_back(sourceIndex); }
        }
        void Queue(Handle handle)
        {
            auto &producer = *slots[handle.index];
            if (!producer.queued) { producer.queued = true; dirty.push_back(handle); }
            synchronized = false;
        }
    };

    RetainedRenderScene::RetainedRenderScene() : m_impl(std::make_unique<Impl>()) {}
    RetainedRenderScene::~RetainedRenderScene() = default;
    void RetainedRenderScene::BeginFrame()
    {
        auto &state = *m_impl;
        ++state.frame;
        state.stats = {};
        state.synchronized = false;
    }
    RetainedRenderScene::Handle RetainedRenderScene::Publish(std::uint64_t id, std::uint64_t revision,
                                                            std::span<const RenderCommand> commands)
    {
        if (!id || !revision) throw std::invalid_argument("Retained producers require nonzero identity and revision");
        auto &state = *m_impl;
        auto found = state.producers.find(id);
        if (found == state.producers.end() || state.slots[found->second.index]->revision != revision)
            for (const auto &command : commands)
                if (command.jointMatrices || command.instanceModels || command.previousInstanceModels)
                    throw std::invalid_argument("Mutable render arrays require transient submission");
        if (found == state.producers.end())
        {
            const auto index = state.freeSlots.empty() ? static_cast<std::uint32_t>(state.slots.size()) : state.freeSlots.back();
            if (state.freeSlots.empty()) state.slots.emplace_back();
            else state.freeSlots.pop_back();
            auto producer = std::make_unique<Impl::Producer>();
            producer->id = id; producer->generation = ++state.generation;
            state.slots[index] = std::move(producer);
            found = state.producers.emplace(id, Handle{index, state.generation}).first;
            state.membershipChanged = true;
        }
        const auto handle = found->second;
        auto &producer = *state.slots[handle.index];
        if (producer.seen != state.frame) state.synchronized = false;
        producer.seen = state.frame;
        if (producer.revision == revision) { ++state.stats.reusedProducers; return handle; }
        bool geometryChanged = producer.source.size() != commands.size();
        bool materialChanged = geometryChanged, transformChanged = geometryChanged;
        for (std::size_t i = 0; i < std::min(producer.source.size(), commands.size()); ++i)
        {
            const auto &a = producer.source[i], &b = commands[i];
            geometryChanged |= a.mesh != b.mesh || a.submeshIndex != b.submeshIndex || a.lodIndex != b.lodIndex || a.isStatic != b.isStatic ||
                a.castsShadow != b.castsShadow || a.minLodIndex != b.minLodIndex || a.minShadowLodIndex != b.minShadowLodIndex ||
                a.maxDrawDistance != b.maxDrawDistance || a.maxShadowDistance != b.maxShadowDistance ||
                a.usePrimaryUvForLightmap != b.usePrimaryUvForLightmap || a.terrainGeomorph != b.terrainGeomorph;
            materialChanged |= a.material != b.material || a.shader != b.shader;
            transformChanged |= a.model != b.model || a.previousModel != b.previousModel ||
                a.worldBounds.center != b.worldBounds.center || a.worldBounds.radius != b.worldBounds.radius ||
                a.previousWorldBounds.center != b.previousWorldBounds.center || a.previousWorldBounds.radius != b.previousWorldBounds.radius;
        }
        if (geometryChanged) ++producer.revisions.geometry;
        if (materialChanged) ++producer.revisions.material;
        if (transformChanged) ++producer.revisions.transform;
        producer.structuralDirty |= geometryChanged || materialChanged;
        producer.source.assign(commands.begin(), commands.end());
        producer.revision = revision;
        ++state.stats.updates;
        state.Queue(handle);
        return handle;
    }
    void RetainedRenderScene::Synchronize()
    {
        auto &state = *m_impl;
        if (state.synchronized) return;
        // Retire missing leases before inspecting any raw resource dependency.
        for (auto it = state.producers.begin(); it != state.producers.end();)
        {
            auto &producer = state.slots[it->second.index];
            if (producer->seen != state.frame)
            {
                state.freeSlots.push_back(it->second.index);
                producer.reset();
                it = state.producers.erase(it);
                ++state.stats.removedProducers;
                state.membershipChanged = true;
            }
            else
            {
                if (!producer->structuralDirty)
                    for (const auto &dependency : producer->dependencies)
                        if (!(dependency == state.Snapshot(dependency.material)))
                        {
                            ++producer->revisions.material;
                            producer->structuralDirty = true;
                            state.Queue(it->second);
                            break;
                        }
                ++it;
            }
        }
        bool changed = state.membershipChanged;
        for (const auto handle : state.dirty)
        {
            if (!IsValid(handle)) continue;
            auto &producer = *state.slots[handle.index];
            if (producer.structuralDirty)
            {
                producer.expanded.clear();
                producer.expandedSources.clear();
                producer.dependencies.clear();
                std::vector<Material *> path;
                for (std::size_t i = 0; i < producer.source.size(); ++i) state.Expand(producer, producer.source[i], i, path);
                changed = true;
            }
            else
            {
                // Transform/history updates preserve addresses, batch membership
                // and the last view's LOD. Only view selection may change LOD.
                for (std::size_t i = 0; i < producer.expanded.size(); ++i)
                {
                    auto &command = producer.expanded[i];
                    const auto &source = producer.source[producer.expandedSources[i]];
                    command.model = source.model; command.previousModel = source.previousModel;
                    command.worldBounds = source.worldBounds; command.previousWorldBounds = source.previousWorldBounds;
                    command.sourceRevision = producer.revision;
                }
            }
            state.stats.rebuiltCommands += producer.expanded.size();
            producer.queued = false;
            producer.structuralDirty = false;
        }
        state.dirty.clear();
        if (changed)
        {
            state.commands.clear();
            for (const auto &producer : state.slots)
                if (producer) for (auto &command : producer->expanded) state.commands.push_back(&command);
            ++state.revision;
        }
        state.stats.activeProducers = state.producers.size();
        state.stats.activeCommands = state.commands.size();
        std::erase_if(state.materialSnapshots, [&](const auto &item) { return item.second.frame != state.frame; });
        state.membershipChanged = false;
        state.synchronized = true;
    }
    bool RetainedRenderScene::IsValid(Handle handle) const
    {
        return handle.index < m_impl->slots.size() && m_impl->slots[handle.index] &&
            m_impl->slots[handle.index]->generation == handle.generation;
    }
    RetainedRenderScene::Revisions RetainedRenderScene::GetRevisions(Handle handle) const
    { if (!IsValid(handle)) throw std::out_of_range("Expired render producer handle"); return m_impl->slots[handle.index]->revisions; }
    std::span<RenderCommand *const> RetainedRenderScene::Commands() const { return m_impl->commands; }
    std::uint64_t RetainedRenderScene::Revision() const { return m_impl->revision; }
    const RetainedRenderScene::Stats &RetainedRenderScene::GetStats() const { return m_impl->stats; }
}
