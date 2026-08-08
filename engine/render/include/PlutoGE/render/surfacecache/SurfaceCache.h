#pragma once

#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

namespace PlutoGE::render
{
    class Mesh;

    using SurfaceCardId = std::uint32_t;

    struct SurfaceCacheRect
    {
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;

        bool IsValid() const { return width > 0 && height > 0; }
    };

    struct SurfaceCard
    {
        SurfaceCardId id = 0;
        std::uint32_t submeshIndex = 0;
        glm::vec3 localCenter{0.0f};
        glm::vec3 localNormal{0.0f, 0.0f, 1.0f};
        glm::vec3 localUp{0.0f, 1.0f, 0.0f};
        glm::vec2 halfExtent{0.5f};
        float halfDepth = 0.5f;
        glm::mat4 localViewProjection{1.0f};
        SurfaceCacheRect allocation;
    };

    struct SurfaceCacheStats
    {
        int cardCount = 0;
        int residentCardCount = 0;
        int capturedCardCount = 0;
        int atlasUsedPixels = 0;
        int atlasTotalPixels = 0;
    };

    class SurfaceCardGenerator
    {
    public:
        static std::vector<SurfaceCard> GenerateAxisCards(const Mesh &mesh, std::uint32_t submeshIndex,
                                                          int texelsPerUnit, int minimumResolution,
                                                          int maximumResolution);
    };

    // Deterministic padded shelf allocator. Allocations are rebuilt as a batch
    // in milestone 1; later residency/eviction work can replace the policy
    // without changing card or capture code.
    class SurfaceCacheAtlasAllocator
    {
    public:
        SurfaceCacheAtlasAllocator(int width, int height, int padding);

        std::optional<SurfaceCacheRect> Allocate(int contentWidth, int contentHeight);
        void Reset();
        int GetUsedPixels() const { return m_usedPixels; }
        int GetWidth() const { return m_width; }
        int GetHeight() const { return m_height; }
        int GetPadding() const { return m_padding; }

    private:
        int m_width = 0;
        int m_height = 0;
        int m_padding = 0;
        int m_cursorX = 0;
        int m_cursorY = 0;
        int m_rowHeight = 0;
        int m_usedPixels = 0;
    };
}
