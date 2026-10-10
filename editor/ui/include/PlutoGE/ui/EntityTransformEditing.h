#pragma once
#include <glm/glm.hpp>
#include <string>

namespace PlutoGE::scene { class Entity; }
namespace PlutoGE::ui
{
    struct PreparedEntityTransformEdit
    {
        enum class Mode { Translation, Controls, Affine };
        scene::Entity *entity = nullptr;
        glm::mat4 local{1.0f};
        glm::vec3 position{0}, rotation{0}, scale{1};
        Mode mode = Mode::Translation;
    };

    // Prepare every selected root before publishing any change. Translation
    // preserves existing control representations and exact linear correction.
    bool PrepareWorldTransformEdit(scene::Entity &entity, const glm::mat4 &world,
        bool translationOnly, PreparedEntityTransformEdit &output, std::string &error);
    bool ApplyPreparedTransformEdit(const PreparedEntityTransformEdit &edit);
}
