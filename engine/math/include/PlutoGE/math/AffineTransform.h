#pragma once
#include <glm/glm.hpp>
#include <optional>

namespace PlutoGE::math
{
    struct Transform
    {
        glm::vec3 position{0.0f};
        glm::vec3 rotation{0.0f}; // XYZ Euler angles in degrees.
        glm::vec3 scale{1.0f};
    };

    struct ResolvedTransform
    {
        glm::mat4 matrix{1.0f};
        Transform controls;
        glm::mat4 correction{1.0f};
    };

    // Override individual controls while inheriting the base linear correction.
    // Position-only changes preserve the base linear matrix exactly. Authored
    // scale may be zero; the source base must remain nonsingular.
    bool ResolveLocalTransform(const glm::mat4 &base,
        const std::optional<glm::vec3> &position, const std::optional<glm::vec3> &rotation,
        const std::optional<glm::vec3> &scale, ResolvedTransform &output);

    // Local = T * Rx * Ry * Rz * S * correction. Correction contains only
    // a linear basis, retaining shear without changing position edit semantics.
    glm::mat4 ComposeLocalTransform(const Transform &transform, const glm::mat4 &correction = glm::mat4(1));
    bool IsLinearTransformCorrection(const glm::mat4 &matrix);
    // Nonsingular affine input only. Failure preserves both outputs.
    bool FactorLocalTransform(const glm::mat4 &matrix, Transform &transform, glm::mat4 &correction);
}
