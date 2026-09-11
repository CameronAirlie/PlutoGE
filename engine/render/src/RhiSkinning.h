#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/Mesh.h"
#include <cmath>

namespace PlutoGE::render
{
    // The RHI's shared vertex stream is consumed by lit, transparent, CSM and
    // virtual-shadow passes. Deform once per mesh/pose, rather than separately
    // in each material/pass. The supplied matrices already include inverse bind.
    inline std::vector<BasicVertex> SkinRhiVertices(std::span<const MeshVertexData> source,
                                                   std::span<const glm::mat4> joints,
                                                   std::span<const BasicVertex> previous = {})
    {
        std::vector<BasicVertex> result;
        result.reserve(source.size());
        for (std::size_t index = 0; index < source.size(); ++index)
        {
            const auto &vertex = source[index];
            glm::mat4 skin(0);
            float totalWeight = 0;
            for (std::size_t influence = 0; influence < 4; ++influence)
            {
                const int joint = vertex.joints[influence];
                const float weight = vertex.weights[influence];
                if (joint < 0 || static_cast<std::size_t>(joint) >= joints.size() ||
                    !std::isfinite(weight) || weight <= 0) continue;
                skin += joints[static_cast<std::size_t>(joint)] * weight;
                totalWeight += weight;
            }
            skin = totalWeight > .0001f ? skin / totalWeight : glm::mat4(1);
            const glm::vec3 position(skin * glm::vec4(vertex.position[0], vertex.position[1], vertex.position[2], 1));
            const glm::mat3 basis(skin);
            const float determinant = glm::determinant(basis);
            const glm::mat3 normalBasis = std::abs(determinant) > 1e-8f ? glm::transpose(glm::inverse(basis)) : glm::mat3(1);
            const auto normalized = [](glm::vec3 value, glm::vec3 fallback)
            {
                const float lengthSquared = glm::dot(value, value);
                return lengthSquared > 1e-12f && std::isfinite(lengthSquared) ? value / std::sqrt(lengthSquared) : fallback;
            };
            const auto normal = normalized(normalBasis * glm::vec3(vertex.normal[0], vertex.normal[1], vertex.normal[2]), {0,1,0});
            auto tangent = basis * glm::vec3(vertex.tangent[0], vertex.tangent[1], vertex.tangent[2]);
            tangent = normalized(tangent - normal * glm::dot(normal, tangent),
                                 normalized(glm::cross(std::abs(normal.y) < .99f ? glm::vec3(0,1,0) : glm::vec3(1,0,0), normal), {1,0,0}));
            BasicVertex output{{position.x, position.y, position.z}, {normal.x, normal.y, normal.z}, vertex.uv,
                               {tangent.x, tangent.y, tangent.z, (vertex.tangent[3] == 0 ? 1 : vertex.tangent[3]) * (determinant < 0 ? -1.0f : 1.0f)}};
            const auto &old = previous.size() == source.size() ? previous[index].position : output.position;
            output.previousPosition = {old[0], old[1], old[2], 1};
            result.push_back(output);
        }
        return result;
    }
}
