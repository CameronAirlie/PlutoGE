#include "PlutoGE/scene/AffineTransform.h"
#include "PlutoGE/math/AffineTransform.h"
#include "PlutoGE/assets/SceneFormat.h"
#include <charconv>
#include <limits>
#include <stdexcept>

namespace PlutoGE::scene
{
    glm::mat4 ComposeLocalTransform(const Transform &transform, const glm::mat4 &correction)
    {
        return math::ComposeLocalTransform({transform.position, transform.rotation, transform.scale}, correction);
    }

    bool IsLinearTransformCorrection(const glm::mat4 &matrix)
    {
        return math::IsLinearTransformCorrection(matrix);
    }

    bool FactorLocalTransform(const glm::mat4 &matrix, Transform &transform, glm::mat4 &correction)
    {
        math::Transform controls;
        if (!math::FactorLocalTransform(matrix, controls, correction)) return false;
        transform = {controls.position, controls.rotation, controls.scale};
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
