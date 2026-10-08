#pragma once
#include <glm/glm.hpp>
#include <string>
#include <string_view>

namespace PlutoGE::scene
{
    struct Transform
    {
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f}; // XYZ Euler angles in degrees.
        glm::vec3 scale{1.0f};
    };

    // Local = T * Rx * Ry * Rz * S * correction. Correction contains only
    // a linear basis, retaining shear without changing position edit semantics.
    glm::mat4 ComposeLocalTransform(const Transform &transform, const glm::mat4 &correction = glm::mat4(1));
    bool IsLinearTransformCorrection(const glm::mat4 &matrix);
    // Nonsingular affine input only. Failure preserves both outputs.
    bool FactorLocalTransform(const glm::mat4 &matrix, Transform &transform, glm::mat4 &correction);
    // Nine column-major floats, locale independent and round-trip precise.
    bool ParseLinearTransformCorrection(std::string_view text, glm::mat4 &correction);
    std::string SerializeLinearTransformCorrection(const glm::mat4 &correction);
}
