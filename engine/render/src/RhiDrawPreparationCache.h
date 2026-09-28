#pragma once

#include "BasicDrawBatching.h"
#include "RetainedOpaqueBatches.h"
#include "PlutoGE/render/Renderer.h"

namespace PlutoGE::render
{
    // Lists belong to one RhiSceneRenderer/viewport. Authoritative producer
    // revisions share immutable rigid packets across passes; procedural callers
    // retain value validation. Neither path treats addresses or Static as proof
    // of immutability, and material configs still support direct editing.
    class RhiDrawPreparationCache
    {
      public:
        std::vector<const RenderCommand *> giSource;
        struct MaterialEntry
        {
            BasicDraw draw;
            std::uint64_t revision = 0;
            std::uint64_t frame = 0;
        };
        struct Entry
        {
            RenderCommand input;
            std::uint64_t materialRevision = 0;
            BasicDraw draw;
            bool valid = false;
            bool emitted = false;

            bool Matches(const RenderCommand &command, std::uint64_t revision) const
            {
                // Array contents can mutate behind const shared_ptrs. Do not
                // infer pose/instance revisions from pointer equality.
                const bool common = valid && !command.jointMatrices && !command.instanceModels && !command.previousInstanceModels &&
                       materialRevision == revision && input.mesh == command.mesh &&
                       input.material == command.material && input.submeshIndex == command.submeshIndex &&
                       input.lodIndex == command.lodIndex && input.castsShadow == command.castsShadow;
                if (!common) return false;
                if (command.sourceObject && command.sourceRevision)
                    return input.sourceObject == command.sourceObject && input.sourceRevision == command.sourceRevision;
                return input.sourceObject == command.sourceObject &&
                       input.worldBounds.center == command.worldBounds.center &&
                       input.worldBounds.radius == command.worldBounds.radius &&
                       std::memcmp(&input.model[0][0], &command.model[0][0], sizeof(float) * 16) == 0 &&
                       std::memcmp(&input.previousModel[0][0], &command.previousModel[0][0], sizeof(float) * 16) == 0;
            }
            void Store(const RenderCommand &command, std::uint64_t revision, const BasicDraw *packet)
            {
                input = command;
                materialRevision = revision;
                emitted = packet != nullptr;
                if (packet)
                    draw = *packet;
                valid = !command.jointMatrices && !command.instanceModels && !command.previousInstanceModels;
            }
        };
        struct RetainedKey
        {
            std::uint64_t object;
            const Material *material;
            std::uint32_t submesh, lod;
            bool emissiveGi;
            bool operator==(const RetainedKey &) const = default;
        };
        struct RetainedHash
        {
            std::size_t operator()(const RetainedKey &key) const
            {
                std::size_t hash = 0;
                HashBatchValue(hash, key.object); HashBatchValue(hash, key.material);
                HashBatchValue(hash, key.submesh); HashBatchValue(hash, key.lod); HashBatchValue(hash, key.emissiveGi);
                return hash;
            }
        };
        struct RetainedEntry { Entry packet; std::uint64_t lastUse = 0; };
        static bool CanRetain(const RenderCommand &command)
        {
            return command.sourceObject && command.sourceRevision && !command.jointMatrices &&
                !command.instanceModels && !command.previousInstanceModels;
        }
        static RetainedKey Key(const RenderCommand &command, bool emissiveGi)
        { return {command.sourceObject, command.material, command.submeshIndex, command.lodIndex, emissiveGi}; }
        const Entry *FindRetained(const RenderCommand &command, std::uint64_t revision, bool emissiveGi, std::uint64_t frame)
        {
            if (!CanRetain(command)) return nullptr;
            const auto found = retained.find(Key(command, emissiveGi));
            if (found == retained.end() || !found->second.packet.Matches(command, revision)) return nullptr;
            found->second.lastUse = frame;
            return &found->second.packet;
        }
        void Retain(const Entry &entry, bool emissiveGi, std::uint64_t frame)
        {
            if (entry.valid && entry.emitted && CanRetain(entry.input))
            {
                const auto key = Key(entry.input, emissiveGi);
                if (retained.size() < MaxRetainedPackets || retained.contains(key))
                    retained.insert_or_assign(key, RetainedEntry{entry, frame});
            }
        }
        void BeginFrame(std::uint64_t frame)
        {
            // Lists retain their own active packets. The shared store keeps
            // recently invisible variants without accumulating dead producers.
            if (frame % RetentionFrames == 0)
                std::erase_if(retained, [&](const auto &item) { return item.second.lastUse + RetentionFrames < frame; });
        }
        static constexpr std::size_t MaxRetainedPackets = 16384;
        static constexpr std::uint64_t RetentionFrames = 120;
        struct List
        {
            std::vector<Entry> entries;
            std::vector<BasicDraw> draws;

            // Keep only the preceding list, with a reusable second buffer. Full
            // value validation remains authoritative; hashes only find candidates.
            std::vector<Entry> previous;
            std::vector<std::size_t> candidateHeads, candidateNext;

            static std::size_t Identity(const RenderCommand &command)
            {
                std::size_t hash = 0;
                HashBatchValue(hash, command.mesh);
                HashBatchValue(hash, command.material);
                HashBatchValue(hash, command.submeshIndex);
                // Translation separates repeated mesh instances cheaply. Full
                // transform/history/LOD validation handles hash collisions.
                HashBatchValue(hash, command.model[3]);
                return hash;
            }

            template<class Revision> void Reconcile(RenderCommandView commands, Revision revision)
            {
                // Animation invalidates individual packets every frame, but
                // usually leaves list slots in the same order. Retain storage
                // and cached packets in that case instead of rehashing and
                // moving the entire scene because one actor changed pose.
                // This is only a placement hint: Matches still validates every
                // value before a packet is reused, including repeated meshes.
                bool sameSlots = entries.size() == commands.size();
                for (size_t i = 0; sameSlots && i < commands.size(); ++i)
                    sameSlots = entries[i].input.mesh == commands[i].mesh &&
                                entries[i].input.material == commands[i].material &&
                                entries[i].input.submeshIndex == commands[i].submeshIndex;
                if (sameSlots)
                    return;
                previous.swap(entries);
                entries.clear();
                entries.resize(commands.size());
                constexpr auto end = std::numeric_limits<std::size_t>::max();
                // Flat chained buckets avoid one allocation per packet on
                // every moving-camera frame. Storage is reused on the next miss.
                candidateHeads.assign(std::bit_ceil(std::max(std::size_t{1}, previous.size() * 2)), end);
                candidateNext.resize(previous.size());
                const auto mask = candidateHeads.size() - 1;
                for (std::size_t i = 0; i < previous.size(); ++i)
                    if (previous[i].valid)
                    {
                        auto &head = candidateHeads[Identity(previous[i].input) & mask];
                        candidateNext[i] = head;
                        head = i;
                    }
                for (std::size_t i = 0; i < commands.size(); ++i)
                {
                    const auto materialRevision = revision(commands[i]);
                    auto *link = &candidateHeads[Identity(commands[i]) & mask];
                    while (*link != end)
                    {
                        const auto candidate = *link;
                        if (previous[candidate].Matches(commands[i], materialRevision))
                        {
                            entries[i] = std::move(previous[candidate]);
                            *link = candidateNext[candidate];
                            break;
                        }
                        link = &candidateNext[candidate];
                    }
                }
                previous.clear();
            }
        };

        std::uint64_t NextRevision()
        {
            return ++m_revision;
        }
        void Reset()
        {
            materials.clear();
            retained.clear();
            visible = {};
            shadows = {};
            gi = {};
            batched.clear();
            opaqueBatches = {};
            // Never recycle tokens while the downstream renderer is alive.
        }

        std::unordered_map<const Material *, MaterialEntry> materials;
        std::unordered_map<RetainedKey, RetainedEntry, RetainedHash> retained;
        List visible, shadows, gi;
        std::vector<BasicDraw> batched;
        RetainedOpaqueBatches opaqueBatches;

      private:
        std::uint64_t m_revision = 0;
    };
} // namespace PlutoGE::render
