#include "PlutoGE/render/surfacecache/SurfaceCache.h"

#include "PlutoGE/render/Mesh.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <glm/gtc/matrix_transform.hpp>

namespace PlutoGE::render
{
    namespace
    {
        int QuantizeResolution(float extent, int texelsPerUnit, int minimumResolution, int maximumResolution)
        {
            const int requested = std::max(1, static_cast<int>(std::ceil(extent * static_cast<float>(texelsPerUnit))));
            int result = 1;
            while (result < requested && result < maximumResolution)
                result *= 2;
            return std::clamp(result, minimumResolution, maximumResolution);
        }
    }

    std::vector<SurfaceCard> SurfaceCardGenerator::GenerateAxisCards(const Mesh &mesh, std::uint32_t submeshIndex,
                                                                     int texelsPerUnit, int minimumResolution,
                                                                     int maximumResolution)
    {
        if (submeshIndex >= mesh.GetSubmeshCount())
            return {};

        const auto &data = mesh.GetMeshData();
        const auto &submesh = mesh.GetSubmesh(submeshIndex);
        glm::vec3 minimum(std::numeric_limits<float>::max());
        glm::vec3 maximum(std::numeric_limits<float>::lowest());
        const std::size_t indexEnd = std::min<std::size_t>(data.indices.size(), submesh.indexOffset + submesh.indexCount);
        for (std::size_t index = submesh.indexOffset; index < indexEnd; ++index)
        {
            const auto vertexIndex = data.indices[index];
            if (vertexIndex >= data.vertices.size())
                continue;
            const auto &p = data.vertices[vertexIndex].position;
            const glm::vec3 position(p[0], p[1], p[2]);
            minimum = glm::min(minimum, position);
            maximum = glm::max(maximum, position);
        }
        if (minimum.x > maximum.x)
            return {};

        const glm::vec3 center = (minimum + maximum) * 0.5f;
        const glm::vec3 halfSize = glm::max((maximum - minimum) * 0.5f, glm::vec3(0.001f));
        struct Orientation { glm::vec3 normal; glm::vec3 up; int axisU; int axisV; int axisDepth; };
        constexpr std::array<Orientation, 6> orientations{{
            {{ 1, 0, 0}, {0, 1, 0}, 2, 1, 0}, {{-1, 0, 0}, {0, 1, 0}, 2, 1, 0},
            {{ 0, 1, 0}, {0, 0,-1}, 0, 2, 1}, {{ 0,-1, 0}, {0, 0, 1}, 0, 2, 1},
            {{ 0, 0, 1}, {0, 1, 0}, 0, 1, 2}, {{ 0, 0,-1}, {0, 1, 0}, 0, 1, 2},
        }};

        std::vector<SurfaceCard> cards;
        cards.reserve(orientations.size());
        for (const auto &orientation : orientations)
        {
            const float extentU = halfSize[orientation.axisU] * 2.0f;
            const float extentV = halfSize[orientation.axisV] * 2.0f;
            const float depth = halfSize[orientation.axisDepth];
            const glm::vec3 eye = center + orientation.normal * (depth + 0.01f);
            const glm::mat4 view = glm::lookAt(eye, center, orientation.up);
            const glm::mat4 projection = glm::ortho(-extentU * 0.5f, extentU * 0.5f,
                                                    -extentV * 0.5f, extentV * 0.5f,
                                                    0.01f, depth * 2.0f + 0.02f);
            SurfaceCard card;
            card.submeshIndex = submeshIndex;
            card.localCenter = center + orientation.normal * depth;
            card.localNormal = orientation.normal;
            card.localUp = orientation.up;
            card.halfExtent = glm::vec2(extentU, extentV) * 0.5f;
            card.halfDepth = depth;
            card.localViewProjection = projection * view;
            card.allocation.width = QuantizeResolution(extentU, texelsPerUnit, minimumResolution, maximumResolution);
            card.allocation.height = QuantizeResolution(extentV, texelsPerUnit, minimumResolution, maximumResolution);
            cards.push_back(card);
        }
        return cards;
    }

    SurfaceCacheAtlasAllocator::SurfaceCacheAtlasAllocator(int width, int height, int padding)
        : m_width(std::max(1, width)), m_height(std::max(1, height)), m_padding(std::max(0, padding)) {}

    std::optional<SurfaceCacheRect> SurfaceCacheAtlasAllocator::Allocate(int contentWidth, int contentHeight)
    {
        if (contentWidth <= 0 || contentHeight <= 0)
            return std::nullopt;
        const int paddedWidth = contentWidth + m_padding * 2;
        const int paddedHeight = contentHeight + m_padding * 2;
        if (paddedWidth > m_width || paddedHeight > m_height)
            return std::nullopt;
        if (m_cursorX + paddedWidth > m_width)
        {
            m_cursorX = 0;
            m_cursorY += m_rowHeight;
            m_rowHeight = 0;
        }
        if (m_cursorY + paddedHeight > m_height)
            return std::nullopt;
        SurfaceCacheRect result{m_cursorX + m_padding, m_cursorY + m_padding, contentWidth, contentHeight};
        m_cursorX += paddedWidth;
        m_rowHeight = std::max(m_rowHeight, paddedHeight);
        m_usedPixels += paddedWidth * paddedHeight;
        return result;
    }

    void SurfaceCacheAtlasAllocator::Reset()
    {
        m_cursorX = m_cursorY = m_rowHeight = m_usedPixels = 0;
    }
}
