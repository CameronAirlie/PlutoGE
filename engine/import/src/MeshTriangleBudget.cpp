#include "PlutoGE/import/MeshTriangleBudget.h"

#include "SimplificationSeamLocks.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <execution>
#include <limits>
#include <map>
#include <numeric>
#include <unordered_map>
#include <utility>

namespace PlutoGE::assetimport
{
    namespace
    {
        // One distinct source index range of the mesh (submeshes may share a range).
        struct BudgetRange
        {
            std::uint32_t indexOffset = 0;
            std::uint32_t indexCount = 0;
            TriangleBudgetSurface surface = TriangleBudgetSurface::Solid;
            std::uint32_t targetTriangles = 0;
            std::vector<std::uint32_t> simplified; // global vertex indices
        };

        constexpr std::uint32_t AlignToTriangles(std::uint64_t indexCount)
        {
            return static_cast<std::uint32_t>(indexCount - indexCount % 3);
        }

        glm::vec3 Position(const render::MeshData &meshData, std::uint32_t vertex)
        {
            const auto &position = meshData.vertices[vertex].position;
            return {position[0], position[1], position[2]};
        }

        float Area(const render::MeshData &meshData, std::span<const std::uint32_t> indices)
        {
            float area = 0.0f;
            for (std::size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                const glm::vec3 a = Position(meshData, indices[i]);
                area += 0.5f * glm::length(glm::cross(Position(meshData, indices[i + 1]) - a, Position(meshData, indices[i + 2]) - a));
            }
            return area;
        }

        // The triangles of each connected part (sharing vertices) of an index list: one list of first indices each.
        std::vector<std::vector<std::uint32_t>> Islands(std::span<const std::uint32_t> indices)
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
            std::unordered_map<std::uint32_t, std::size_t> islandByRoot;
            std::vector<std::vector<std::uint32_t>> islands;
            for (std::uint32_t i = 0; i + 2 < indices.size(); i += 3)
            {
                const auto [found, inserted] = islandByRoot.try_emplace(find(indices[i]), islands.size());
                if (inserted) islands.emplace_back();
                islands[found->second].push_back(i);
            }
            return islands;
        }

        // Keeps whole cards, in an order that spreads them over the surface (a hash of each card's first vertex),
        // until the next would pass the target; at least one card stays.
        void DropCards(std::vector<std::uint32_t> &indices, std::uint32_t targetTriangles)
        {
            auto islands = Islands(indices);
            const auto order = [&](const std::vector<std::uint32_t> &island)
            {
                std::uint64_t hash = indices[island.front()] * 0x9E3779B97F4A7C15ull;
                return hash ^ (hash >> 29);
            };
            std::ranges::sort(islands, {}, order);
            std::vector<std::uint32_t> kept;
            for (const auto &island : islands)
            {
                if (!kept.empty() && kept.size() / 3 + island.size() > targetTriangles) continue;
                for (const auto first : island) kept.insert(kept.end(), indices.begin() + first, indices.begin() + first + 3);
            }
            indices = std::move(kept);
        }

        // Enlarges each kept card about its centre so that the cards cover as much as the source did.
        void EnlargeCards(render::MeshData &meshData, std::span<const std::uint32_t> source, std::span<const std::uint32_t> kept)
        {
            const float keptArea = Area(meshData, kept);
            if (keptArea <= 0.0f) return;
            const float scale = std::clamp(std::sqrt(Area(meshData, source) / keptArea), 1.0f, MaxCardEnlargement);
            if (scale <= 1.001f) return;
            for (const auto &island : Islands(kept))
            {
                std::vector<std::uint32_t> vertices;
                for (const auto first : island) vertices.insert(vertices.end(), kept.begin() + first, kept.begin() + first + 3);
                std::ranges::sort(vertices);
                vertices.erase(std::unique(vertices.begin(), vertices.end()), vertices.end());
                glm::vec3 centre{0.0f};
                for (const auto vertex : vertices) centre += Position(meshData, vertex);
                centre /= static_cast<float>(vertices.size());
                for (const auto vertex : vertices)
                {
                    const glm::vec3 position = centre + (Position(meshData, vertex) - centre) * scale;
                    meshData.vertices[vertex].position = {position.x, position.y, position.z};
                }
            }
        }

        // Simplifies one range toward its target without clustering. Positions and the attributes that shape shading
        // (normal, UV) guide the simplifier; vertices are local to the range so its working arrays stay range-sized.
        void SimplifyRange(const render::MeshData &meshData, BudgetRange &range, TriangleBudgetSimplification simplification)
        {
            std::unordered_map<std::uint32_t, std::uint32_t> localByGlobal;
            std::vector<std::uint32_t> globalByLocal;
            std::vector<unsigned int> localIndices;
            localByGlobal.reserve(range.indexCount);
            localIndices.reserve(range.indexCount);
            for (std::uint32_t i = 0; i < range.indexCount; ++i)
            {
                const auto global = meshData.indices[range.indexOffset + i];
                const auto [it, inserted] = localByGlobal.emplace(global, static_cast<std::uint32_t>(globalByLocal.size()));
                if (inserted) globalByLocal.push_back(global);
                localIndices.push_back(it->second);
            }
            constexpr std::size_t attributeCount = 5;
            std::vector<float> positions(globalByLocal.size() * 3), attributes(globalByLocal.size() * attributeCount);
            for (std::size_t local = 0; local < globalByLocal.size(); ++local)
            {
                const auto &vertex = meshData.vertices[globalByLocal[local]];
                std::copy(vertex.position.begin(), vertex.position.end(), positions.begin() + local * 3);
                std::copy(vertex.normal.begin(), vertex.normal.end(), attributes.begin() + local * attributeCount);
                std::copy(vertex.uv.begin(), vertex.uv.end(), attributes.begin() + local * attributeCount + 3);
            }
            constexpr float attributeWeights[attributeCount]{0.5f, 0.5f, 0.5f, 1.0f, 1.0f};
            // The budget, not an error bound, decides how far to go.
            constexpr float targetError = 1.0f;
            std::vector<unsigned int> result(range.indexCount);
            const auto simplify = [&](unsigned int options, const unsigned char *locks = nullptr)
            {
                return AlignToTriangles(meshopt_simplifyWithAttributes(
                    result.data(), localIndices.data(), localIndices.size(), positions.data(), globalByLocal.size(),
                    sizeof(float) * 3, attributes.data(), sizeof(float) * attributeCount, attributeWeights, attributeCount,
                    locks, std::size_t(range.targetTriangles) * 3, targetError, options));
            };
            const unsigned int prune = simplification == TriangleBudgetSimplification::PruneIslands ? meshopt_SimplifyPrune : 0u;
            std::size_t count = simplify(prune);
            // Pruning can remove every island of a small range; a submesh must never vanish, so simplify it instead.
            if (count == 0) count = simplify(0u);
            // Split-normal and UV-seamed meshes lock almost every vertex and barely reduce: retry permissively, as the
            // standard LODs do, protecting only the seams that would visibly tear (SimplificationSeamLocks.h).
            if (count > std::size_t(range.targetTriangles) * 3 * 2)
            {
                std::vector<std::array<float, 3>> localPositions(globalByLocal.size());
                for (std::size_t local = 0; local < globalByLocal.size(); ++local)
                    localPositions[local] = meshData.vertices[globalByLocal[local]].position;
                const auto locks = BuildTearingSeamLocks(meshData.vertices, globalByLocal, localPositions);
                // Without pruning: a range that got here may be one pruning empties.
                if (const auto permissive = simplify(meshopt_SimplifyPermissive, locks.data()); permissive > 0 && permissive < count)
                    count = permissive;
            }
            range.simplified.resize(count);
            for (std::size_t i = 0; i < count; ++i) range.simplified[i] = globalByLocal[result[i]];
            if (range.surface == TriangleBudgetSurface::Cards && count / 3 > range.targetTriangles)
                DropCards(range.simplified, range.targetTriangles);
        }

        // Shares the budget among ranges by weighted triangles, never more than a range has; what a capped range
        // cannot use goes to the others.
        void ShareBudget(std::vector<BudgetRange> &ranges, std::uint32_t budget)
        {
            std::vector<bool> capped(ranges.size(), false);
            double remaining = budget;
            for (int pass = 0; pass < 4; ++pass)
            {
                double weights = 0.0;
                for (std::size_t i = 0; i < ranges.size(); ++i)
                    if (!capped[i])
                        weights += ranges[i].indexCount / 3 * (ranges[i].surface == TriangleBudgetSurface::Solid ? SolidBudgetWeight : 1.0f);
                if (weights <= 0.0) break;
                const double perWeight = remaining / weights;
                bool changed = false;
                for (std::size_t i = 0; i < ranges.size(); ++i)
                {
                    if (capped[i]) continue;
                    auto &range = ranges[i];
                    const double share = perWeight * range.indexCount / 3 *
                                         (range.surface == TriangleBudgetSurface::Solid ? SolidBudgetWeight : 1.0f);
                    range.targetTriangles = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(share));
                    if (share >= range.indexCount / 3)
                    {
                        range.targetTriangles = range.indexCount / 3;
                        remaining -= range.targetTriangles;
                        capped[i] = changed = true;
                    }
                }
                if (!changed) break;
            }
        }

        // Keeps only the vertices the indices reference, in first-use order, and rewrites the indices to match.
        void CompactVertices(render::MeshData &meshData)
        {
            constexpr auto unused = std::numeric_limits<std::uint32_t>::max();
            std::vector<std::uint32_t> remap(meshData.vertices.size(), unused);
            std::vector<render::MeshVertexData> vertices;
            vertices.reserve(meshData.vertices.size());
            for (auto &index : meshData.indices)
            {
                if (remap[index] == unused)
                {
                    remap[index] = static_cast<std::uint32_t>(vertices.size());
                    vertices.push_back(meshData.vertices[index]);
                }
                index = remap[index];
            }
            meshData.vertices = std::move(vertices);
        }
    }

    TriangleBudgetResult ApplyTriangleBudget(render::MeshData &meshData, std::vector<render::Submesh> &submeshes,
                                             std::uint32_t budget, TriangleBudgetSimplification simplification,
                                             std::span<const TriangleBudgetSurface> surfaces)
    {
        TriangleBudgetResult result;
        if (budget == 0 || meshData.vertices.empty() || meshData.indices.empty() || submeshes.empty())
            return result;
        // Skinned vertices would need their weights preserved; budgets are for static props and plants.
        if (std::ranges::any_of(meshData.vertices, [](const auto &vertex)
                                { return std::ranges::any_of(vertex.weights, [](float weight) { return weight > 0.0f; }); }))
            return result;

        std::map<std::pair<std::uint32_t, std::uint32_t>, std::size_t> rangeByKey;
        std::vector<BudgetRange> ranges;
        for (std::size_t index = 0; index < submeshes.size(); ++index)
        {
            const auto &submesh = submeshes[index];
            if (submesh.indexOffset + static_cast<std::uint64_t>(submesh.indexCount) > meshData.indices.size())
                return result;
            const auto surface = index < surfaces.size() ? surfaces[index] : TriangleBudgetSurface::Solid;
            const auto [it, inserted] = rangeByKey.emplace(std::pair(submesh.indexOffset, submesh.indexCount), ranges.size());
            if (inserted) ranges.push_back({submesh.indexOffset, submesh.indexCount, surface});
            // A range drawn as both keeps its silhouette.
            else if (surface == TriangleBudgetSurface::Solid) ranges[it->second].surface = surface;
        }
        for (const auto &range : ranges) result.sourceTriangles += range.indexCount / 3;
        if (result.sourceTriangles <= budget)
        {
            result.keptTriangles = result.sourceTriangles;
            return result;
        }

        ShareBudget(ranges, budget);
        std::for_each(std::execution::par, ranges.begin(), ranges.end(),
                      [&](BudgetRange &range) { SimplifyRange(meshData, range, simplification); });
        // After every range is simplified: cards move their own vertices, which the other ranges never reference.
        for (const auto &range : ranges)
            if (range.surface == TriangleBudgetSurface::Cards)
                EnlargeCards(meshData, std::span(meshData.indices).subspan(range.indexOffset, range.indexCount), range.simplified);

        std::vector<unsigned int> indices;
        std::vector<std::uint32_t> offsets(ranges.size());
        for (std::size_t index = 0; index < ranges.size(); ++index)
        {
            offsets[index] = static_cast<std::uint32_t>(indices.size());
            indices.insert(indices.end(), ranges[index].simplified.begin(), ranges[index].simplified.end());
            result.keptTriangles += ranges[index].simplified.size() / 3;
        }
        for (auto &submesh : submeshes)
        {
            const auto rangeIndex = rangeByKey.at({submesh.indexOffset, submesh.indexCount});
            submesh.indexOffset = offsets[rangeIndex];
            submesh.indexCount = static_cast<std::uint32_t>(ranges[rangeIndex].simplified.size());
            submesh.lods.clear();
        }
        meshData.indices = std::move(indices);
        CompactVertices(meshData);
        result.applied = true;
        return result;
    }
}
