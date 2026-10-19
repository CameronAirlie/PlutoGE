#pragma once

#include "PlutoGE/render/Mesh.h"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <optional>
#include <span>
#include <vector>

namespace PlutoGE::assetimport
{
    // Skin weights enter simplification as one attribute per joint, so an edge
    // collapse that moves geometry between differently weighted regions pays
    // an attribute error. meshoptimizer limits the attribute count, which
    // previously left every dense humanoid rig at LOD0.
    //
    // A collapse only compares attributes within one vertex neighbourhood. Joints
    // that never influence the same one-ring therefore never compete and can
    // share a slot. Greedy colouring of that conflict graph assigns slots; rigs
    // within the budget keep the exact one-slot-per-joint encoding.
    struct SkinWeightAttributeSlots
    {
        std::vector<std::uint32_t> slotByJoint; // Indexed like the sorted joint list.
        std::uint32_t slotCount = 0;
    };

    // joints: sorted unique joint IDs with positive weight on these vertices.
    // localIndices index into globalByLocal, which indexes into vertices.
    inline std::optional<SkinWeightAttributeSlots> AssignSkinWeightAttributeSlots(
        std::span<const render::MeshVertexData> vertices, std::span<const std::uint32_t> globalByLocal,
        std::span<const unsigned int> localIndices, std::span<const int> joints, std::uint32_t maxSlots)
    {
        const auto jointCount = static_cast<std::uint32_t>(joints.size());
        SkinWeightAttributeSlots result;
        if (jointCount <= maxSlots)
        {
            result.slotByJoint.resize(jointCount);
            std::iota(result.slotByJoint.begin(), result.slotByJoint.end(), 0u);
            result.slotCount = jointCount;
            return result;
        }

        const auto jointSlotOf = [&](int joint)
        { return static_cast<std::uint32_t>(std::lower_bound(joints.begin(), joints.end(), joint) - joints.begin()); };
        const auto appendJoints = [&](std::uint32_t local, std::vector<std::uint32_t> &out)
        {
            const auto &vertex = vertices[globalByLocal[local]];
            for (std::size_t influence = 0; influence < 4; ++influence)
                if (vertex.weights[influence] > 0)
                    out.push_back(jointSlotOf(vertex.joints[influence]));
        };

        // Collect every joint touching each vertex's one-ring.
        std::vector<std::vector<std::uint32_t>> ringJoints(globalByLocal.size());
        std::vector<std::uint32_t> triangleJoints;
        for (std::size_t corner = 0; corner + 2 < localIndices.size(); corner += 3)
        {
            triangleJoints.clear();
            for (std::size_t k = 0; k < 3; ++k)
                appendJoints(localIndices[corner + k], triangleJoints);
            for (std::size_t k = 0; k < 3; ++k)
            {
                auto &ring = ringJoints[localIndices[corner + k]];
                ring.insert(ring.end(), triangleJoints.begin(), triangleJoints.end());
            }
        }

        std::vector<std::uint8_t> conflicts(std::size_t(jointCount) * jointCount, 0);
        for (auto &ring : ringJoints)
        {
            std::sort(ring.begin(), ring.end());
            ring.erase(std::unique(ring.begin(), ring.end()), ring.end());
            for (std::size_t a = 0; a < ring.size(); ++a)
                for (std::size_t b = a + 1; b < ring.size(); ++b)
                    conflicts[std::size_t(ring[a]) * jointCount + ring[b]] =
                        conflicts[std::size_t(ring[b]) * jointCount + ring[a]] = 1;
            ring = {};
        }

        // Welsh-Powell: colour the most constrained joints first.
        std::vector<std::uint32_t> degree(jointCount, 0), order(jointCount);
        for (std::uint32_t a = 0; a < jointCount; ++a)
            for (std::uint32_t b = 0; b < jointCount; ++b)
                degree[a] += conflicts[std::size_t(a) * jointCount + b];
        std::iota(order.begin(), order.end(), 0u);
        std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return degree[a] > degree[b]; });

        constexpr std::uint32_t unassigned = ~0u;
        result.slotByJoint.assign(jointCount, unassigned);
        std::vector<std::uint8_t> taken;
        for (const auto joint : order)
        {
            taken.assign(result.slotCount + 1, 0);
            for (std::uint32_t other = 0; other < jointCount; ++other)
                if (conflicts[std::size_t(joint) * jointCount + other] && result.slotByJoint[other] != unassigned)
                    taken[result.slotByJoint[other]] = 1;
            const auto slot = static_cast<std::uint32_t>(std::find(taken.begin(), taken.end(), 0) - taken.begin());
            if (slot >= maxSlots)
                return std::nullopt;
            result.slotByJoint[joint] = slot;
            result.slotCount = std::max(result.slotCount, slot + 1);
        }
        return result;
    }
}
