#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/Mesh.h"
#include <memory>
namespace PlutoGE::render
{
    struct RhiSkinningBounds;
    struct RhiSkinningWorkStats
    {
        unsigned participants = 1;
        float dispatchMs = 0, callerMs = 0, waitMs = 0, mergeMs = 0;
    };
    // Synchronous, single-caller executor. Workers only access disjoint vertex
    // ranges; all work completes before returning or releasing palette/history.
    class RhiSkinningExecutor
    {
    public:
        explicit RhiSkinningExecutor(unsigned participants = 4);
        ~RhiSkinningExecutor();
        RhiSkinningExecutor(const RhiSkinningExecutor &) = delete;
        RhiSkinningExecutor &operator=(const RhiSkinningExecutor &) = delete;
        RhiSkinningWorkStats stats;
        RhiSkinningBounds Deform(std::span<const MeshVertexData>, std::span<const glm::mat4>,
                                std::span<const BasicVertex>, std::vector<BasicVertex> &);
    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
    struct RhiSkinningBounds
    {
        glm::vec3 center{0.0f};
        float radius = 0.0f;
    };

    // Previous may alias result when both already have source.size() entries.
    // Bounds are accumulated during deformation; empty input returns zero bounds.
    RhiSkinningBounds SkinRhiVerticesInto(std::span<const MeshVertexData> source,
                            std::span<const glm::mat4> joints,
                            std::span<const BasicVertex> previous,
                            std::vector<BasicVertex> &result);
    inline std::vector<BasicVertex> SkinRhiVertices(std::span<const MeshVertexData> source,
                                                  std::span<const glm::mat4> joints,
                                                  std::span<const BasicVertex> previous = {})
    {
        std::vector<BasicVertex> result;
        SkinRhiVerticesInto(source, joints, previous, result);
        return result;
    }
}
