#include "PlutoGE/import/FoliageCards.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>

namespace PlutoGE::assetimport
{
    namespace
    {
        glm::vec3 Position(const render::MeshVertexData &vertex) { return {vertex.position[0], vertex.position[1], vertex.position[2]}; }

        // The connected parts of an index list: each part's first indices (of its triangles).
        std::vector<std::vector<std::uint32_t>> Parts(std::span<const std::uint32_t> indices)
        {
            std::unordered_map<std::uint32_t, std::uint32_t> parent;
            const auto find = [&](std::uint32_t vertex)
            {
                auto root = vertex;
                while (parent.at(root) != root) root = parent.at(root);
                while (parent.at(vertex) != root) vertex = std::exchange(parent.at(vertex), root);
                return root;
            };
            for (const auto vertex : indices) parent.try_emplace(vertex, vertex);
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
                for (std::size_t corner = 1; corner < 3; ++corner)
                    if (const auto a = find(indices[i]), b = find(indices[i + corner]); a != b) parent.at(b) = a;
            std::unordered_map<std::uint32_t, std::size_t> partByRoot;
            std::vector<std::vector<std::uint32_t>> parts;
            for (std::uint32_t i = 0; i + 2 < indices.size(); i += 3)
            {
                const auto [found, inserted] = partByRoot.try_emplace(find(indices[i]), parts.size());
                if (inserted) parts.emplace_back();
                parts[found->second].push_back(i);
            }
            return parts;
        }

        // The part's distinct vertices.
        std::vector<std::uint32_t> PartVertices(std::span<const std::uint32_t> indices, std::span<const std::uint32_t> part)
        {
            std::vector<std::uint32_t> vertices;
            vertices.reserve(part.size() * 3);
            for (const auto first : part) vertices.insert(vertices.end(), indices.begin() + first, indices.begin() + first + 3);
            std::ranges::sort(vertices);
            vertices.erase(std::unique(vertices.begin(), vertices.end()), vertices.end());
            return vertices;
        }

        // The quad over a leaf's UV rectangle, from the least-squares affine map position = A (u, v, 1); none when its
        // UVs are degenerate or the leaf strays too far from that map (it is not one flat-mapped leaf).
        bool LeafQuad(std::span<const render::MeshVertexData> vertices, std::span<const std::uint32_t> indices,
                      std::span<const std::uint32_t> part, std::span<const std::uint32_t> leaf, CardGeometry &out)
        {
            if (part.size() > MaxCardLeafTriangles) return false;
            glm::dmat3 normal(0.0);
            glm::dmat3 rhs(0.0); // columns: x, y, z right-hand sides
            glm::vec2 low(std::numeric_limits<float>::max()), high(std::numeric_limits<float>::lowest());
            glm::vec3 lowPosition(std::numeric_limits<float>::max()), highPosition(std::numeric_limits<float>::lowest());
            glm::vec3 averageNormal(0.0f);
            for (const auto index : leaf)
            {
                const auto &vertex = vertices[index];
                const glm::dvec3 row(vertex.uv[0], vertex.uv[1], 1.0);
                normal += glm::outerProduct(row, row);
                rhs += glm::outerProduct(row, glm::dvec3(Position(vertex)));
                low = glm::min(low, glm::vec2(vertex.uv[0], vertex.uv[1]));
                high = glm::max(high, glm::vec2(vertex.uv[0], vertex.uv[1]));
                lowPosition = glm::min(lowPosition, Position(vertex));
                highPosition = glm::max(highPosition, Position(vertex));
                averageNormal += glm::vec3(vertex.normal[0], vertex.normal[1], vertex.normal[2]);
            }
            if (std::abs(glm::determinant(normal)) < 1e-18 || glm::any(glm::lessThanEqual(high - low, glm::vec2(1e-6f))))
                return false;
            // Columns of `map` give d(position)/du, d(position)/dv and the offset.
            const glm::dmat3 map = glm::transpose(glm::inverse(normal) * rhs);
            const auto at = [&](glm::vec2 uv) { return glm::vec3(map * glm::dvec3(uv, 1.0)); };
            const float size = glm::length(highPosition - lowPosition);
            for (const auto index : leaf)
                if (glm::length(at(glm::vec2(vertices[index].uv[0], vertices[index].uv[1])) - Position(vertices[index])) >
                    MaxCardFitError * size)
                    return false;
            // The leaf must fill enough of its UV rectangle for the card to show it rather than its neighbours.
            float uvArea = 0.0f;
            for (const auto first : part)
            {
                const auto uv = [&](std::size_t corner)
                { return glm::vec2(vertices[indices[first + corner]].uv[0], vertices[indices[first + corner]].uv[1]); };
                const glm::vec2 a = uv(1) - uv(0), b = uv(2) - uv(0);
                uvArea += 0.5f * std::abs(a.x * b.y - a.y * b.x);
            }
            if (uvArea < MinCardUvFill * (high.x - low.x) * (high.y - low.y)) return false;

            const glm::vec3 uAxis(map[0]), vAxis(map[1]);
            glm::vec3 cardNormal = glm::cross(uAxis, vAxis);
            if (glm::dot(cardNormal, cardNormal) < 1e-20f) return false;
            cardNormal = glm::normalize(cardNormal);
            if (glm::dot(cardNormal, averageNormal) < 0.0f) cardNormal = -cardNormal;
            const glm::vec3 tangent = glm::normalize(uAxis - cardNormal * glm::dot(cardNormal, uAxis));
            const float handedness = glm::dot(glm::cross(cardNormal, tangent), vAxis) < 0.0f ? -1.0f : 1.0f;

            const auto base = static_cast<std::uint32_t>(out.vertices.size());
            const auto &template_ = vertices[leaf.front()];
            for (const glm::vec2 uv : {low, glm::vec2(high.x, low.y), high, glm::vec2(low.x, high.y)})
            {
                auto vertex = template_;
                const glm::vec3 position = at(uv);
                vertex.position = {position.x, position.y, position.z};
                vertex.normal = {cardNormal.x, cardNormal.y, cardNormal.z};
                vertex.uv = {uv.x, uv.y};
                vertex.tangent = {tangent.x, tangent.y, tangent.z, handedness};
                out.vertices.push_back(vertex);
            }
            out.indices.insert(out.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            return true;
        }

        float PartArea(const CardGeometry &geometry, std::span<const std::uint32_t> part)
        {
            float area = 0.0f;
            for (const auto first : part)
            {
                const glm::vec3 a = Position(geometry.vertices[geometry.indices[first]]);
                area += 0.5f * glm::length(glm::cross(Position(geometry.vertices[geometry.indices[first + 1]]) - a,
                                                      Position(geometry.vertices[geometry.indices[first + 2]]) - a));
            }
            return area;
        }
    }

    CardGeometry MakeLeafCards(const render::MeshData &mesh, std::span<const std::uint32_t> indices)
    {
        CardGeometry cards;
        cards.sourceTriangles = indices.size() / 3;
        std::unordered_map<std::uint32_t, std::uint32_t> copied;
        for (const auto &part : Parts(indices))
        {
            const auto leaf = PartVertices(indices, part);
            if (part.size() >= 4 && LeafQuad(mesh.vertices, indices, part, leaf, cards))
            {
                cards.cardedSourceTriangles += part.size();
                continue;
            }
            // Parts of fewer than 4 triangles are already as cheap as cards: authored leaf cards.
            if (part.size() < 4) cards.cardedSourceTriangles += part.size();
            // Kept as it is.
            for (const auto first : part)
                for (std::size_t corner = 0; corner < 3; ++corner)
                {
                    const auto source = indices[first + corner];
                    const auto [found, inserted] = copied.try_emplace(source, static_cast<std::uint32_t>(cards.vertices.size()));
                    if (inserted) cards.vertices.push_back(mesh.vertices[source]);
                    cards.indices.push_back(found->second);
                }
        }
        return cards;
    }

    CardGeometry ThinCards(const CardGeometry &cards, std::uint32_t targetTriangles)
    {
        auto parts = Parts(cards.indices);
        // A hash of each part's first vertex orders them independently of their position in the source, so any prefix
        // is spread over the whole surface.
        const auto order = [&](const std::vector<std::uint32_t> &part)
        {
            std::uint64_t hash = cards.indices[part.front()] * 0x9E3779B97F4A7C15ull;
            return hash ^ (hash >> 29);
        };
        std::ranges::sort(parts, {}, order);
        std::vector<const std::vector<std::uint32_t> *> kept;
        std::size_t keptTriangles = 0;
        float totalArea = 0.0f, keptArea = 0.0f;
        for (const auto &part : parts)
        {
            const float area = PartArea(cards, part);
            totalArea += area;
            if (!kept.empty() && keptTriangles + part.size() > targetTriangles) continue;
            kept.push_back(&part);
            keptTriangles += part.size();
            keptArea += area;
        }
        const float scale = keptArea > 0.0f ? std::clamp(std::sqrt(totalArea / keptArea), 1.0f, MaxCardEnlargement) : 1.0f;

        CardGeometry thinned;
        for (const auto *part : kept)
        {
            const auto vertices = PartVertices(cards.indices, *part);
            glm::vec3 centre(0.0f);
            for (const auto vertex : vertices) centre += Position(cards.vertices[vertex]);
            centre /= static_cast<float>(vertices.size());
            std::unordered_map<std::uint32_t, std::uint32_t> remap;
            for (const auto vertex : vertices)
            {
                remap[vertex] = static_cast<std::uint32_t>(thinned.vertices.size());
                auto copy = cards.vertices[vertex];
                const glm::vec3 position = centre + (Position(copy) - centre) * scale;
                copy.position = {position.x, position.y, position.z};
                thinned.vertices.push_back(copy);
            }
            for (const auto first : *part)
                for (std::size_t corner = 0; corner < 3; ++corner)
                    thinned.indices.push_back(remap.at(cards.indices[first + corner]));
        }
        return thinned;
    }
}
