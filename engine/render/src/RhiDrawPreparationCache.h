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
