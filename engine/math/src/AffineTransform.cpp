#include "PlutoGE/math/AffineTransform.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace PlutoGE::math
{
    namespace
    {
        bool Finite(const glm::mat4 &matrix)
        {
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    if (!std::isfinite(matrix[column][row])) return false;
            return true;
        }
    }

    glm::mat4 ComposeLocalTransform(const Transform &transform, const glm::mat4 &correction)
    {
        auto matrix = glm::translate(glm::mat4(1), transform.position);
        matrix = glm::rotate(matrix, glm::radians(transform.rotation.x), glm::vec3(1, 0, 0));
        matrix = glm::rotate(matrix, glm::radians(transform.rotation.y), glm::vec3(0, 1, 0));
        matrix = glm::rotate(matrix, glm::radians(transform.rotation.z), glm::vec3(0, 0, 1));
        return glm::scale(matrix, transform.scale) * correction;
    }

    bool IsLinearTransformCorrection(const glm::mat4 &matrix)
    {
        return Finite(matrix) && matrix[0][3] == 0 && matrix[1][3] == 0 && matrix[2][3] == 0 &&
            matrix[3] == glm::vec4(0, 0, 0, 1);
    }

    bool FactorLocalTransform(const glm::mat4 &matrix, Transform &transform, glm::mat4 &correction)
    {
        if (!Finite(matrix) || matrix[0][3] != 0 || matrix[1][3] != 0 || matrix[2][3] != 0 || matrix[3][3] != 1)
            return false;
        double magnitude = 0;
        for (int column = 0; column < 3; ++column)
            for (int row = 0; row < 3; ++row)
                magnitude = std::max(magnitude, std::abs(static_cast<double>(matrix[column][row])));
        if (magnitude == 0) return false;
        glm::dmat4 normalized(matrix);
        for (int column = 0; column < 3; ++column) normalized[column] /= magnitude;
        Transform candidate;
        glm::dquat orientation;
        glm::dvec3 scale, position, skew;
        glm::dvec4 perspective;
        if (!glm::decompose(normalized, scale, orientation, position, skew, perspective)) return false;
        candidate.position = glm::vec3(matrix[3]);
        candidate.scale = glm::vec3(scale * magnitude);
        glm::dvec3 angles;
        glm::extractEulerAngleXYZ(glm::mat4_cast(orientation), angles.x, angles.y, angles.z);
        candidate.rotation = glm::vec3(glm::degrees(angles));
        const auto controls = ComposeLocalTransform(candidate);
        const glm::dmat4 precise(controls);
        if (!Finite(controls) || glm::determinant(precise) == 0) return false;
        auto residual = glm::mat4(glm::inverse(precise) * glm::dmat4(matrix));
        // Translation belongs exclusively to the authored position control.
        residual[3] = glm::vec4(0, 0, 0, 1);
        residual[0][3] = residual[1][3] = residual[2][3] = 0;
        if (!IsLinearTransformCorrection(residual)) return false;
        const auto rebuilt = ComposeLocalTransform(candidate, residual);
        for (int column = 0; column < 4; ++column)
            for (int row = 0; row < 4; ++row)
                if (!std::isfinite(rebuilt[column][row]) || std::abs(rebuilt[column][row] - matrix[column][row]) >
                    0.00001f * std::max(1.0f, std::abs(matrix[column][row]))) return false;
        transform = candidate;
        correction = residual;
        return true;
    }


    bool ResolveLocalTransform(const glm::mat4 &base,
        const std::optional<glm::vec3> &position, const std::optional<glm::vec3> &rotation,
        const std::optional<glm::vec3> &scale, ResolvedTransform &output)
    {
        const auto finite = [](const auto &value)
        {
            return !value || (std::isfinite(value->x) && std::isfinite(value->y) && std::isfinite(value->z));
        };
        if (!finite(position) || !finite(rotation) || !finite(scale)) return false;
        ResolvedTransform candidate;
        if (!FactorLocalTransform(base, candidate.controls, candidate.correction)) return false;
        if (position) candidate.controls.position = *position;
        if (rotation) candidate.controls.rotation = *rotation;
        if (scale) candidate.controls.scale = *scale;
        candidate.matrix = rotation || scale ? ComposeLocalTransform(candidate.controls, candidate.correction) : base;
        if (position) candidate.matrix[3] = glm::vec4(*position, 1);
        if (!Finite(candidate.matrix)) return false;
        output = candidate;
        return true;
    }
}
