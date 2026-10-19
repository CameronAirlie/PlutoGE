#include "SkinWeightAttributeSlots.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace PlutoGE;
using namespace PlutoGE::assetimport;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    struct Fixture
    {
        std::vector<render::MeshVertexData> vertices;
        std::vector<std::uint32_t> globalByLocal;
        std::vector<unsigned int> localIndices;
        std::vector<int> joints;

        std::optional<SkinWeightAttributeSlots> Assign(std::uint32_t maxSlots) const
        { return AssignSkinWeightAttributeSlots(vertices, globalByLocal, localIndices, joints, maxSlots); }
        void Finish()
        {
            for (std::uint32_t i = 0; i < vertices.size(); ++i) globalByLocal.push_back(i);
            for (const auto &vertex : vertices)
                for (std::size_t k = 0; k < 4; ++k)
                    if (vertex.weights[k] > 0) joints.push_back(vertex.joints[k]);
            std::sort(joints.begin(), joints.end());
            joints.erase(std::unique(joints.begin(), joints.end()), joints.end());
        }
    };

    render::MeshVertexData Weighted(int a, int b)
    {
        render::MeshVertexData vertex;
        vertex.joints = {a, b, 0, 0};
        vertex.weights = {.75f, .25f, 0, 0};
        return vertex;
    }

    // A long limb: a triangle strip whose joints change along its length, like
    // the forty-plus bones of a humanoid spread over one body submesh.
    Fixture Limb(int jointCount)
    {
        Fixture limb;
        for (int ring = 0; ring < jointCount; ++ring)
        {
            const int next = std::min(ring + 1, jointCount - 1);
            limb.vertices.push_back(Weighted(ring, next));
            limb.vertices.push_back(Weighted(ring, next));
        }
        for (unsigned v = 0; v + 3 < limb.vertices.size(); v += 2)
            limb.localIndices.insert(limb.localIndices.end(), {v, v + 1, v + 2, v + 1, v + 3, v + 2});
        limb.Finish();
        return limb;
    }
}

int main()
{
    try
    {
        {
            const auto limb = Limb(12);
            const auto slots = limb.Assign(27);
            Require(slots && slots->slotCount == 12, "Rigs within the budget keep one slot per joint");
            for (std::uint32_t i = 0; i < slots->slotByJoint.size(); ++i)
                Require(slots->slotByJoint[i] == i, "Rigs within the budget keep the identity encoding");
        }
        {
            const auto limb = Limb(60);
            const auto slots = limb.Assign(27);
            Require(slots.has_value(), "A sparse 60-joint rig must fit the attribute budget");
            Require(slots->slotCount < 27, "Non-neighbouring joints should share slots");
            // Every vertex neighbourhood must still see distinct slots for distinct joints.
            const auto slotOf = [&](int joint)
            { return slots->slotByJoint[std::lower_bound(limb.joints.begin(), limb.joints.end(), joint) - limb.joints.begin()]; };
            std::vector<std::vector<int>> rings(limb.vertices.size());
            for (std::size_t corner = 0; corner < limb.localIndices.size(); corner += 3)
                for (std::size_t a = 0; a < 3; ++a)
                    for (std::size_t b = 0; b < 3; ++b)
                        for (std::size_t k = 0; k < 2; ++k)
                            rings[limb.localIndices[corner + a]].push_back(limb.vertices[limb.localIndices[corner + b]].joints[k]);
            for (auto &ring : rings)
            {
                std::sort(ring.begin(), ring.end());
                ring.erase(std::unique(ring.begin(), ring.end()), ring.end());
                for (std::size_t a = 0; a < ring.size(); ++a)
                    for (std::size_t b = a + 1; b < ring.size(); ++b)
                        Require(slotOf(ring[a]) != slotOf(ring[b]), "Joints sharing a neighbourhood must not share a slot");
            }
        }
        {
            // A fan whose hub touches thirty joints cannot be encoded in 27 slots.
            Fixture fan;
            fan.vertices.push_back(Weighted(0, 1));
            for (int joint = 0; joint < 30; ++joint) fan.vertices.push_back(Weighted(joint, joint));
            for (unsigned v = 1; v < 30; ++v) fan.localIndices.insert(fan.localIndices.end(), {0, v, v + 1});
            fan.Finish();
            Require(!fan.Assign(27).has_value(), "Over-budget neighbourhoods must fall back to LOD0");
        }
        std::cout << "PASS: skin weight attribute slots share only non-neighbouring joints\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
