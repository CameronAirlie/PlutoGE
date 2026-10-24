#pragma once

#include "PlutoGE/render/rhi/RenderDevice.h"
#include "PlutoGE/render/rhi/Resource.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace PlutoGE::render
{
    struct GeometryArenaState;

    // Shared vertex and index buffers that meshes suballocate, so draws of different meshes bind the same buffers and
    // a multi-draw indirect batch can span meshes. A mesh's indices are stored rebased by its first vertex, so every draw
    // uses a vertex offset of zero; its first index (BaseIndex) locates it in the page's index buffer. A mesh can then
    // be drawn either by binding the index buffer at BaseIndex (mesh-relative first indices, as with a buffer of its
    // own) or at zero with BaseIndex added to each first index (needed for batches spanning meshes).
    //
    // Pages are created on demand; a mesh larger than a page gets a page of its own. Releasing an allocation returns
    // its ranges once the GPU has finished every submission that may read them (IRenderDevice submission serials).
    // Thread-safe.
    class GeometryArena
    {
    public:
        static constexpr std::uint32_t PageVertices = 256 * 1024;
        static constexpr std::uint32_t PageIndices = 1024 * 1024;

        // A mesh's ranges in one page. Destroying it returns them to the arena.
        class Allocation
        {
        public:
            ~Allocation();
            Allocation(const Allocation &) = delete;
            Allocation &operator=(const Allocation &) = delete;
            [[nodiscard]] rhi::BufferHandle VertexBuffer() const noexcept { return m_vertexBuffer; }
            [[nodiscard]] rhi::BufferHandle IndexBuffer() const noexcept { return m_indexBuffer; }
            [[nodiscard]] std::uint32_t BaseVertex() const noexcept { return m_baseVertex; }
            [[nodiscard]] std::uint32_t BaseIndex() const noexcept { return m_baseIndex; }

        private:
            friend class GeometryArena;
            Allocation() = default;
            std::shared_ptr<GeometryArenaState> m_state;
            std::size_t m_page = 0;
            rhi::BufferHandle m_vertexBuffer, m_indexBuffer;
            std::uint32_t m_baseVertex = 0, m_vertexCount = 0, m_baseIndex = 0, m_indexCount = 0;
        };

        struct Stats
        {
            std::size_t pages = 0, allocations = 0, vertexBytes = 0, indexBytes = 0, pendingReleases = 0;
        };

        GeometryArena(rhi::IRenderDevice &device, std::uint32_t vertexStride);
        ~GeometryArena();
        GeometryArena(const GeometryArena &) = delete;
        GeometryArena &operator=(const GeometryArena &) = delete;

        // Copies the vertices (stride bytes each) and indices (rebased) into the arena.
        [[nodiscard]] std::shared_ptr<Allocation> Allocate(std::span<const std::byte> vertices,
                                                           std::span<const std::uint32_t> indices);
        [[nodiscard]] Stats GetStats() const;

    private:
        std::shared_ptr<GeometryArenaState> m_state;
    };
}
