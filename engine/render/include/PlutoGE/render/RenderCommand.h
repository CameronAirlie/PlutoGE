#pragma once
#include "PlutoGE/render/Mesh.h"
#include <glm/glm.hpp>
#include <limits>
#include <memory>
#include <vector>

namespace PlutoGE::render
{
    class Material;
    class Shader;
    struct RenderCommand
    {
        Material *material = nullptr; // Material to use for rendering
        Mesh *mesh = nullptr;         // Mesh to render
        Shader *shader = nullptr;
        glm::mat4 model = glm::mat4(1.0f); // Model matrix for the object (position, rotation, scale)
        glm::mat4 previousModel = glm::mat4(1.0f);
        MeshBounds worldBounds{};
        MeshBounds previousWorldBounds{};
        const std::vector<glm::mat4> *jointMatrices = nullptr;
        bool skinningPoseChanged = false;
        std::shared_ptr<const std::vector<glm::mat4>> instanceModels;
        std::shared_ptr<const std::vector<glm::mat4>> previousInstanceModels;
        uint32_t submeshIndex = 0;
        uint32_t lodIndex = 0;
        uint32_t minLodIndex = 0;
        uint32_t minShadowLodIndex = 0;
        float maxDrawDistance = std::numeric_limits<float>::max();
        float maxShadowDistance = std::numeric_limits<float>::max();
        bool isStatic = false;
        bool castsShadow = true;
        bool usePrimaryUvForLightmap = false;
        bool terrainGeomorph = false;
        // Optional producer-owned identity/revision for immutable rigid object
        // state (including motion history and bounds). Zero retains value-based
        // validation for procedural callers and mutable pose/instance arrays.
        std::uint64_t sourceObject = 0, sourceRevision = 0;

        // LOD transition state is transient and packed into the otherwise
        // unused high bits of minLodIndex.
        uint32_t GetMinLodIndex() const { return minLodIndex & 0xffu; }
        uint32_t GetLodTransitionIndex() const { return (minLodIndex >> 8u) & 0xffu; }
        float GetLodTransitionFade() const
        {
            return static_cast<float>((minLodIndex >> 16u) & 0xffffu) / 65535.0f;
        }
        void SetLodTransition(uint32_t transitionIndex, float fade)
        {
            const uint32_t encodedFade = static_cast<uint32_t>(glm::clamp(fade, 0.0f, 1.0f) * 65535.0f + 0.5f);
            minLodIndex = GetMinLodIndex() |
                          ((transitionIndex & 0xffu) << 8u) |
                          ((encodedFade & 0xffffu) << 16u);
        }
    };

}
