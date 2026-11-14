#pragma once
// A tiny skinned body for clothing tests (OutfitTests, OutfitInspectorTests): four joints, two triangles round each,
// and a skeleton that can be scaled (a body of other proportions), renamed (another rig) or re-posed (another rest pose).
#include "PlutoGE/render/Mesh.h"

#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstddef>

namespace outfit_fixtures
{
    using namespace PlutoGE;

    inline glm::mat4 At(glm::vec3 position) { return glm::translate(glm::mat4(1.0f), position); }

    // hips -> spine -> head, spine -> arm. `scale` makes a body of other proportions; `names` renames the joints.
    inline render::Skeleton Body(float scale = 1.0f, std::array<const char *, 4> names = {"hips", "spine", "head", "arm"},
                          float armDrop = 0.0f)
    {
        render::Skeleton skeleton;
        const int parents[] = {-1, 0, 1, 1};
        // The arm hangs `armDrop` radians below horizontal, turned about its parent (the spine), as a rest pose turns it.
        const glm::vec3 spine(0, 1.2f, 0), reach(0.5f, 0.2f, 0);
        const glm::vec3 arm = spine + glm::vec3(glm::rotate(glm::mat4(1.0f), -armDrop, glm::vec3(0, 0, 1)) * glm::vec4(reach, 0));
        const glm::vec3 bind[] = {{0, 0.9f, 0}, {0, 1.2f, 0}, {0, 1.6f, 0}, arm};
        for (int i = 0; i < 4; ++i)
            skeleton.joints.push_back({.name = names[i], .parentJointIndex = parents[i], .inverseBindMatrix = At(-bind[i] * scale)});
        skeleton.humanoidBoneMappings = {{.bone = render::HumanoidBone::Head, .targetJointIndex = 2}};
        return skeleton;
    }

    // Two triangles round each joint, wholly weighted to it, and one across the spine and the head. One submesh whose
    // single LOD shares its range.
    inline render::MeshConfig SkinnedConfig(const render::Skeleton &skeleton)
    {
        render::MeshConfig config;
        config.skeleton = skeleton;
        for (std::size_t joint = 0; joint < skeleton.joints.size(); ++joint)
        {
            const glm::vec3 centre(glm::inverse(skeleton.joints[joint].inverseBindMatrix)[3]);
            const auto base = static_cast<unsigned>(config.data.vertices.size());
            for (const glm::vec2 corner : {glm::vec2(-0.1f, -0.1f), glm::vec2(0.1f, -0.1f), glm::vec2(0.1f, 0.1f), glm::vec2(-0.1f, 0.1f)})
            {
                render::MeshVertexData vertex{};
                vertex.position = {centre.x + corner.x, centre.y + corner.y, centre.z + 0.1f};
                vertex.normal = {0, 0, 1};
                vertex.tangent = {1, 0, 0, 1};
                vertex.joints = {static_cast<int>(joint), 0, 0, 0};
                vertex.weights = {1, 0, 0, 0};
                config.data.vertices.push_back(vertex);
            }
            config.data.indices.insert(config.data.indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        }
        config.data.indices.insert(config.data.indices.end(), {4u + 2u, 4u + 3u, 8u + 0u}); // spine's top to the head
        config.animationNodes.resize(skeleton.joints.size());
        for (std::size_t i = 0; i < skeleton.joints.size(); ++i)
        {
            auto &joint = config.skeleton.joints[i];
            joint.nodeIndex = static_cast<int>(i);
            const int parent = joint.parentJointIndex;
            const glm::mat4 global = glm::inverse(joint.inverseBindMatrix);
            const glm::mat4 parentGlobal = parent >= 0 ? glm::inverse(skeleton.joints[parent].inverseBindMatrix) : glm::mat4(1.0f);
            joint.localBindTransform = glm::inverse(parentGlobal) * global;
            config.animationNodes[i] = {.name = joint.name, .parentNodeIndex = parent, .localBindTransform = joint.localBindTransform};
        }
        render::Submesh submesh{.indexOffset = 0, .indexCount = static_cast<uint32_t>(config.data.indices.size())};
        submesh.lods.push_back({.indexOffset = 0, .indexCount = submesh.indexCount});
        config.submeshes.push_back(submesh);
        return config;
    }
}
