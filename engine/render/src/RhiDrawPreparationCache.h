#pragma once

#include "BasicDrawBatching.h"
#include "PlutoGE/render/Renderer.h"

namespace PlutoGE::render
{
    // Lists belong to one RhiSceneRenderer/viewport. Slot validation deliberately
    // uses source values, not owner addresses or the Static flag: callers also
    // submit procedural commands, and material configs support direct editing.
    class RhiDrawPreparationCache
    {
      public:
        std::vector<RenderCommand> giSource;
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
                return valid && !command.jointMatrices && !command.instanceModels && !command.previousInstanceModels &&
                       materialRevision == revision && input.mesh == command.mesh &&
                       input.material == command.material && input.submeshIndex == command.submeshIndex &&
                       input.lodIndex == command.lodIndex && input.castsShadow == command.castsShadow &&
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

            template<class Revision> void Reconcile(std::span<const RenderCommand> commands, Revision revision)
            {
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
            visible = {};
            shadows = {};
            gi = {};
            batched.clear();
            // Never recycle tokens while the downstream renderer is alive.
        }

        std::unordered_map<const Material *, MaterialEntry> materials;
        List visible, shadows, gi;
        std::vector<BasicDraw> batched;

      private:
        std::uint64_t m_revision = 0;
    };
} // namespace PlutoGE::render
