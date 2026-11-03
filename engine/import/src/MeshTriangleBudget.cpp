#include "PlutoGE/import/MeshTriangleBudget.h"

#include "PlutoGE/import/FoliageCards.h"
#include "SimplificationSeamLocks.h"

#include <meshoptimizer.h>

#include <algorithm>
#include <array>
#include <execution>
#include <limits>
#include <map>
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
            // The range's geometry before sharing the budget (leaf cards for Cards) and after reduction: Solid ranges
            // keep global vertex indices in `simplified`; Cards ranges carry their own vertices in `cards`.
            CardGeometry cards;
            std::vector<std::uint32_t> simplified;
            [[nodiscard]] std::uint32_t Triangles() const
            {
                return static_cast<std::uint32_t>(surface == TriangleBudgetSurface::Cards ? cards.Triangles() : indexCount / 3);
            }
        };

        constexpr std::uint32_t AlignToTriangles(std::uint64_t indexCount)
        {
            return static_cast<std::uint32_t>(indexCount - indexCount % 3);
        }

        // Simplifies a Solid range toward its target without clustering. Positions and the attributes that shape
        // shading (normal, UV) guide the simplifier; vertices are local to the range so its working arrays stay
        // range-sized.
        void SimplifySolid(const render::MeshData &meshData, BudgetRange &range, TriangleBudgetSimplification simplification)
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
        }

        // Shares the budget among ranges by weighted triangles, never more than a range has; what a capped range
        // cannot use goes to the others.
        void ShareBudget(std::vector<BudgetRange> &ranges, std::uint32_t budget)
        {
            const auto weight = [](const BudgetRange &range)
            { return static_cast<double>(range.Triangles()) * (range.surface == TriangleBudgetSurface::Cards ? CardBudgetWeight : 1.0f); };
            std::vector<bool> capped(ranges.size(), false);
            double remaining = budget;
            for (int pass = 0; pass < 4; ++pass)
            {
                double weights = 0.0;
                for (std::size_t i = 0; i < ranges.size(); ++i)
                    if (!capped[i]) weights += weight(ranges[i]);
                if (weights <= 0.0) break;
                const double perWeight = remaining / weights;
                bool changed = false;
                for (std::size_t i = 0; i < ranges.size(); ++i)
                {
                    if (capped[i]) continue;
                    auto &range = ranges[i];
                    const double share = perWeight * weight(range);
                    range.targetTriangles = std::max<std::uint32_t>(1u, static_cast<std::uint32_t>(share));
                    if (share >= range.Triangles())
                    {
                        range.targetTriangles = range.Triangles();
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

        // Leaves become cards first, so the budget is shared on what they will cost.
        std::for_each(std::execution::par, ranges.begin(), ranges.end(), [&](BudgetRange &range)
        {
            if (range.surface == TriangleBudgetSurface::Cards)
                range.cards = MakeLeafCards(meshData, std::span(meshData.indices).subspan(range.indexOffset, range.indexCount));
        });
        ShareBudget(ranges, budget);
        std::for_each(std::execution::par, ranges.begin(), ranges.end(), [&](BudgetRange &range)
        {
            if (range.surface == TriangleBudgetSurface::Solid) SimplifySolid(meshData, range, simplification);
            else if (range.cards.Triangles() > range.targetTriangles) range.cards = ThinCards(range.cards, range.targetTriangles);
        });

        // Cards bring their own vertices: append them, then lay the ranges out in order.
        std::vector<unsigned int> indices;
        std::vector<std::uint32_t> offsets(ranges.size()), counts(ranges.size());
        for (std::size_t index = 0; index < ranges.size(); ++index)
        {
            auto &range = ranges[index];
            offsets[index] = static_cast<std::uint32_t>(indices.size());
            if (range.surface == TriangleBudgetSurface::Cards)
            {
                const auto base = static_cast<std::uint32_t>(meshData.vertices.size());
                meshData.vertices.insert(meshData.vertices.end(), range.cards.vertices.begin(), range.cards.vertices.end());
                for (const auto vertex : range.cards.indices) indices.push_back(base + vertex);
            }
            else
                indices.insert(indices.end(), range.simplified.begin(), range.simplified.end());
            counts[index] = static_cast<std::uint32_t>(indices.size()) - offsets[index];
            result.keptTriangles += counts[index] / 3;
        }
        for (auto &submesh : submeshes)
        {
            const auto rangeIndex = rangeByKey.at({submesh.indexOffset, submesh.indexCount});
            submesh.indexOffset = offsets[rangeIndex];
            submesh.indexCount = counts[rangeIndex];
            submesh.lods.clear();
        }
        meshData.indices = std::move(indices);
        CompactVertices(meshData);
        result.applied = true;
        return result;
    }
}
