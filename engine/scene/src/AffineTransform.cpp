#include "PlutoGE/scene/AffineTransform.h"
#include "PlutoGE/assets/SceneFormat.h"
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace PlutoGE::scene
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

    bool ParseLinearTransformCorrection(std::string_view text, glm::mat4 &correction)
    {
        std::array<float, 9> values;
        if (!assets::ParseSceneLinearCorrection(text, values)) return false;
        glm::mat4 parsed(1);
        for (int index = 0; index < 9; ++index) parsed[index / 3][index % 3] = values[index];
        correction = parsed;
        return true;
    }

    std::string SerializeLinearTransformCorrection(const glm::mat4 &correction)
    {
        if (!IsLinearTransformCorrection(correction)) throw std::invalid_argument("Invalid linear transform correction.");
        std::string text;
        for (int index = 0; index < 9; ++index)
        {
            char buffer[64];
            const auto result = std::to_chars(buffer, buffer + sizeof(buffer), correction[index / 3][index % 3],
                std::chars_format::general, std::numeric_limits<float>::max_digits10);
            if (result.ec != std::errc{}) throw std::runtime_error("Cannot encode transform correction.");
            if (index) text += ',';
            text.append(buffer, result.ptr);
        }
        return text;
    }
}
