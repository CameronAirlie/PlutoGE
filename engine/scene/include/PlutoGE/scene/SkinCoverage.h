#pragma once
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/SkeletonJointLookup.h"
#include <cstddef>
#include <map>
#include <memory>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace PlutoGE::scene
{
    // Skin hidden under clothing. A garment lists the body joints it covers (a coat: the spine and upper arms); body
    // triangles whose three corners are each mostly weighted to covered joints are left out of every draw, so the skin
    // cannot show through the garment when the two bend differently. Unlike hiding joints (JointHiding.h), a covered
    // joint hides only the geometry it carries: the joints below it stay (a covered forearm keeps its hand), and nothing
    // is moved, so the edge of the hidden area stays where the body has it.
    //
    // MeshComponent draws a copy of its mesh with those triangles removed (SetCoveredJoints); OutfitComponent sets the
    // joints from what is worn.

    // How much of a corner's weight must be on covered joints for it to count as covered.
    inline constexpr float CoveredSkinThreshold = 0.5f;

    // Per joint of `skeleton`: 1 when it is named (exact names, then humanoid aliases, as bone queries resolve them).
    [[nodiscard]] inline std::vector<char> ResolveCoveredJoints(const render::Skeleton &skeleton,
                                                                std::span<const std::string> names)
    {
        std::vector<char> covered(skeleton.joints.size(), 0);
        for (const auto &name : names)
            if (const int joint = render::FindSkeletonJoint(skeleton, name);
                joint >= 0 && static_cast<std::size_t>(joint) < covered.size())
                covered[static_cast<std::size_t>(joint)] = 1;
        return covered;
    }

    [[nodiscard]] inline float CoveredWeight(const render::MeshVertexData &vertex, std::span<const char> covered)
    {
        float weight = 0.0f;
        for (std::size_t influence = 0; influence < 4; ++influence)
        {
            const int joint = vertex.joints[influence];
            if (joint >= 0 && static_cast<std::size_t>(joint) < covered.size() && covered[static_cast<std::size_t>(joint)])
                weight += vertex.weights[influence];
        }
        return weight;
    }

    // `mesh` without its covered triangles, with every submesh and LOD range kept (shorter); null when no triangle is
    // covered. Vertices, skeleton and animation nodes are copied as they are, so the copy skins and draws like the
    // original with the same palette and materials. `hiddenTriangles` receives the number of triangles left out of the
    // full-detail ranges.
    [[nodiscard]] inline std::unique_ptr<render::Mesh> BuildCoveredMesh(const render::Mesh &mesh, std::span<const char> covered,
                                                                       float threshold = CoveredSkinThreshold,
                                                                       std::size_t *hiddenTriangles = nullptr)
    {
        if (hiddenTriangles)
            *hiddenTriangles = 0;
        const auto &source = mesh.GetMeshData();
        std::vector<char> coveredVertex(source.vertices.size(), 0);
        bool anyCovered = false;
        for (std::size_t vertex = 0; vertex < source.vertices.size(); ++vertex)
            anyCovered |= (coveredVertex[vertex] = CoveredWeight(source.vertices[vertex], covered) >= threshold) != 0;
        if (!anyCovered)
            return nullptr;

        render::MeshConfig config;
        config.data.vertices = source.vertices;
        config.data.indices.reserve(source.indices.size());
        config.skeleton = mesh.GetSkeleton();
        config.animationNodes = mesh.GetAnimationNodes();
        config.hasLightmapUvs = mesh.HasLightmapUvs();
        // Ranges are filtered once each: LODs may share index ranges with their submesh.
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<std::uint32_t, std::uint32_t>> filtered;
        std::size_t hidden = 0;
        const auto filter = [&](std::uint32_t offset, std::uint32_t count, bool fullDetail)
        {
            if (const auto found = filtered.find({offset, count}); found != filtered.end())
                return found->second;
            const auto start = static_cast<std::uint32_t>(config.data.indices.size());
            const auto end = std::min<std::size_t>(source.indices.size(), static_cast<std::size_t>(offset) + count);
            for (std::size_t index = offset; index + 2 < end; index += 3)
            {
                const unsigned a = source.indices[index], b = source.indices[index + 1], c = source.indices[index + 2];
                const bool hide = a < coveredVertex.size() && b < coveredVertex.size() && c < coveredVertex.size() &&
                                  coveredVertex[a] && coveredVertex[b] && coveredVertex[c];
                if (hide)
                {
                    hidden += fullDetail ? 1 : 0;
                    continue;
                }
                config.data.indices.insert(config.data.indices.end(), {a, b, c});
            }
            const std::pair range{start, static_cast<std::uint32_t>(config.data.indices.size()) - start};
            filtered.emplace(std::pair{offset, count}, range);
            return range;
        };
        for (std::size_t index = 0; index < mesh.GetSubmeshCount(); ++index)
        {
            auto submesh = mesh.GetSubmesh(index);
            std::tie(submesh.indexOffset, submesh.indexCount) = filter(submesh.indexOffset, submesh.indexCount, true);
            for (auto &lod : submesh.lods)
                std::tie(lod.indexOffset, lod.indexCount) = filter(lod.indexOffset, lod.indexCount, false);
            config.submeshes.push_back(std::move(submesh));
        }
        if (hiddenTriangles)
            *hiddenTriangles = hidden;
        if (hidden == 0)
            return nullptr;
        return std::unique_ptr<render::Mesh>(render::Mesh::FromConfig(config));
    }
}
