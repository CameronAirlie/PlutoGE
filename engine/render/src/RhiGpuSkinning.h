#pragma once
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/rhi/Resource.h"
#include <memory>

namespace PlutoGE::render
{
    // Immutable topology is shared by actors; palette/output history is per actor.
    struct RhiGpuSkinningSource
    {
        rhi::Buffer vertices;
        std::uint32_t vertexCount = 0;
    };
    struct RhiGpuSkinningState
    {
        std::shared_ptr<const RhiGpuSkinningSource> source;
        rhi::Buffer palette, parameters;
        std::vector<glm::mat4> matrices;
        std::uint32_t jointCount = 0;
        bool pending = false;
    };

    // Records deformation into an existing vertex/storage buffer. Does not own
    // frame submission, camera history or visibility decisions.
    class RhiGpuSkinning
    {
    public:
        bool Initialize(rhi::IRenderDevice &, const rhi::ComputePipelineDescriptor::ShaderCode &);
        std::shared_ptr<RhiGpuSkinningSource> CreateSource(std::span<const MeshVertexData>);
        std::shared_ptr<RhiGpuSkinningState> CreateState(std::shared_ptr<const RhiGpuSkinningSource>);
        void Queue(RhiGpuSkinningState &, std::span<const glm::mat4> current, std::span<const glm::mat4> previous);
        // Caller brackets all dispatches with ShaderMemoryBarrier, outside rendering.
        bool Record(RhiGpuSkinningState &, rhi::BufferHandle output);
    private:
        rhi::IRenderDevice *m_device = nullptr;
        rhi::GraphicsPipeline m_pipeline;
    };
}
