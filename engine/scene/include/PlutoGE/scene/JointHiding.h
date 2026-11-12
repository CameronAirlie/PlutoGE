#pragma once
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/SkeletonJointLookup.h"
#include <glm/gtc/matrix_transform.hpp>
#include <span>
#include <string>
#include <vector>

namespace PlutoGE::scene
{
    // Hiding joints of a skinned mesh in its skinning palette: every listed joint, and every joint below it, is shrunk
    // onto the listed joint's posed position, so the geometry they carry vanishes while the rest of the mesh (and the
    // pose itself, which bone queries and attachments read) is untouched. Vertices only partly weighted to a hidden
    // joint (a neck blending into the head) shrink part of the way, closing the opening smoothly.
    //
    // MeshComponent uses it for joints hidden from cameras (a first-person body without its head).

    // How far hidden geometry is scaled: small enough to vanish, non-zero so skinned normals stay finite.
    inline constexpr float HiddenJointScale = 0.001f;

    // For each joint of `skeleton`, the index of the hidden joint it collapses onto, or -1 when it is shown. Names
    // resolve as FindSkeletonJoint does (exact names, then humanoid aliases such as "Head"); unknown names are ignored.
    [[nodiscard]] inline std::vector<int> ResolveHiddenJoints(const render::Skeleton &skeleton,
                                                              std::span<const std::string> names)
    {
        const auto count = skeleton.joints.size();
        std::vector<char> named(count, 0);
        bool any = false;
        for (const auto &name : names)
            if (const int joint = render::FindSkeletonJoint(skeleton, name); joint >= 0 && static_cast<size_t>(joint) < count)
                named[joint] = any = true;
        std::vector<int> collapseOnto(count, -1);
        if (!any)
            return collapseOnto;
        for (size_t joint = 0; joint < count; ++joint)
        {
            // The highest named joint on the way to the root: a hidden joint's own children shrink with it.
            int current = static_cast<int>(joint);
            for (size_t steps = 0; current >= 0 && static_cast<size_t>(current) < count && steps <= count; ++steps)
            {
                if (named[current])
                    collapseOnto[joint] = current;
                current = skeleton.joints[current].parentJointIndex;
            }
        }
        return collapseOnto;
    }

    // True when `collapseOnto` (from ResolveHiddenJoints) hides any joint.
    [[nodiscard]] inline bool HidesAnyJoint(std::span<const int> collapseOnto)
    {
        for (const int target : collapseOnto)
            if (target >= 0) return true;
        return false;
    }

    // Shrinks the hidden joints' palette matrices onto their targets' posed positions. `palette` holds skinning
    // matrices (posed joint transform times inverse bind), one per joint of `skeleton`; a size mismatch leaves it as is.
    inline void CollapseHiddenJoints(const render::Skeleton &skeleton, std::span<const int> collapseOnto,
                                     std::vector<glm::mat4> &palette)
    {
        if (palette.size() != skeleton.joints.size() || collapseOnto.size() != palette.size())
            return;
        // Targets are read before any matrix changes: a target is itself collapsed (onto itself).
        std::vector<glm::vec3> centres(palette.size());
        for (size_t joint = 0; joint < palette.size(); ++joint)
            if (const int target = collapseOnto[joint]; target >= 0 && static_cast<size_t>(target) == joint)
                centres[joint] = glm::vec3((palette[joint] * glm::inverse(skeleton.joints[joint].inverseBindMatrix))[3]);
        for (size_t joint = 0; joint < palette.size(); ++joint)
        {
            const int target = collapseOnto[joint];
            if (target < 0)
                continue;
            const glm::vec3 centre = centres[target];
            palette[joint] = glm::translate(glm::mat4(1.0f), centre) *
                             glm::scale(glm::mat4(1.0f), glm::vec3(HiddenJointScale)) *
                             glm::translate(glm::mat4(1.0f), -centre) * palette[joint];
        }
    }
}
