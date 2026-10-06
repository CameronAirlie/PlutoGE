#pragma once
#include "PlutoGE/render/Mesh.h"
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace PlutoGE::scene
{
    struct TwoBoneIKTarget
    {
        std::string root, middle, tip;
        glm::vec3 position{0}, pole{0}; // Mesh space, including mesh offsets only at the caller.
        glm::quat rotation{1, 0, 0, 0};
        float weight = 1, rotationWeight = 0;
    };

    inline bool SolveTwoBoneIK(const render::Skeleton &skeleton, std::vector<glm::mat4> &palette,
                               const TwoBoneIKTarget &target)
    {
        const auto finite = [](glm::vec3 v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
        if (palette.size() != skeleton.joints.size() || !finite(target.position) || !finite(target.pole) ||
            !std::isfinite(target.weight) || !std::isfinite(target.rotationWeight) ||
            !std::isfinite(target.rotation.x) || !std::isfinite(target.rotation.y) ||
            !std::isfinite(target.rotation.z) || !std::isfinite(target.rotation.w)) return false;
        int root = -1, middle = -1, tip = -1;
        for (int i = 0; i < static_cast<int>(skeleton.joints.size()); ++i)
        {
            if (skeleton.joints[i].name == target.root) root = i;
            if (skeleton.joints[i].name == target.middle) middle = i;
            if (skeleton.joints[i].name == target.tip) tip = i;
        }
        if (root < 0 || middle < 0 || tip < 0 || root == middle || middle == tip || root == tip ||
            skeleton.joints[middle].parentJointIndex != root || skeleton.joints[tip].parentJointIndex != middle) return false;
        std::vector<glm::mat4> pose(palette.size());
        for (size_t i = 0; i < pose.size(); ++i)
        {
            pose[i] = palette[i] * glm::inverse(skeleton.joints[i].inverseBindMatrix);
            for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
                if (!std::isfinite(pose[i][c][r])) return false;
        }
        const glm::vec3 a(pose[root][3]), b(pose[middle][3]), c(pose[tip][3]);
        const float upper = glm::length(b-a), lower = glm::length(c-b);
        if (!std::isfinite(upper) || !std::isfinite(lower) || upper < 1e-6f || lower < 1e-6f) return false;
        const float weight = std::clamp(target.weight, 0.0f, 1.0f);
        if (weight == 0) return true;
        glm::vec3 direction = target.position-a;
        if (glm::length(direction) < 1e-6f) direction = c-a;
        if (glm::length(direction) < 1e-6f) direction = b-a;
        direction = glm::normalize(direction);
        glm::vec3 bend = target.pole-a;
        bend -= direction * glm::dot(bend, direction);
        if (glm::length(bend) < 1e-6f)
        {
            bend = b-a-direction*glm::dot(b-a, direction);
            if (glm::length(bend) < 1e-6f)
                bend = glm::cross(direction, std::abs(direction.y) < .9f ? glm::vec3(0,1,0) : glm::vec3(1,0,0));
        }
        bend = glm::normalize(bend);
        const float distance = std::clamp(glm::length(target.position-a), std::max(std::abs(upper-lower), 1e-6f), upper+lower);
        const float along = (upper*upper-lower*lower+distance*distance)/(2*distance);
        const glm::vec3 elbow = a+direction*along+bend*std::sqrt(std::max(0.0f, upper*upper-along*along));
        const auto between = [](glm::vec3 from, glm::vec3 to) {
            from = glm::normalize(from); to = glm::normalize(to);
            const float dot = std::clamp(glm::dot(from,to), -1.0f, 1.0f);
            if (dot < -.99999f)
                return glm::angleAxis(3.14159265359f, glm::normalize(glm::cross(from, std::abs(from.y)<.9f ? glm::vec3(0,1,0) : glm::vec3(1,0,0))));
            return glm::normalize(glm::quat(1+dot, glm::cross(from,to)));
        };
        const auto rotateSubtree = [&](int joint, glm::quat rotation, float blend) {
            const glm::vec3 pivot(pose[joint][3]);
            const auto delta = glm::translate(glm::mat4(1), pivot) *
                glm::mat4_cast(glm::slerp(glm::quat(1,0,0,0), rotation, blend)) * glm::translate(glm::mat4(1), -pivot);
            for (int i = 0; i < static_cast<int>(pose.size()); ++i)
                for (int ancestor = i, count = 0; ancestor >= 0 && ancestor < static_cast<int>(pose.size()) && count < static_cast<int>(pose.size());
                     ancestor = skeleton.joints[ancestor].parentJointIndex, ++count)
                    if (ancestor == joint) { pose[i] = delta*pose[i]; break; }
        };
        rotateSubtree(root, between(b-a, elbow-a), weight);
        const glm::vec3 solvedMiddle(pose[middle][3]), solvedTip(pose[tip][3]);
        const glm::vec3 desiredTip = a+direction*distance;
        if (glm::length(desiredTip-solvedMiddle) > 1e-6f)
            rotateSubtree(middle, between(solvedTip-solvedMiddle, desiredTip-solvedMiddle), weight);
        if (target.rotationWeight > 0 && glm::length(target.rotation) > 1e-6f)
        {
            glm::mat3 basis(pose[tip]);
            for (int i = 0; i < 3; ++i) basis[i] = glm::normalize(basis[i]);
            rotateSubtree(tip, glm::normalize(target.rotation)*glm::inverse(glm::normalize(glm::quat_cast(basis))),
                          weight*std::clamp(target.rotationWeight, 0.0f, 1.0f));
        }
        for (size_t i = 0; i < pose.size(); ++i) palette[i] = pose[i]*skeleton.joints[i].inverseBindMatrix;
        return true;
    }
}
