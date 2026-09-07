#pragma once

#include "PlutoGE/scene/Entity.h"
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    std::vector<scene::Entity *> SelectionRoots(const std::vector<scene::Entity *> &selection);
    // Validate every local TRS before changing any entity. Shear cannot be stored by Entity.
    bool TransformSelection(const std::vector<scene::Entity *> &selection,
                            const glm::mat4 &worldDelta, std::string &error);
    struct CommonComponent
    {
        std::string name;
        std::vector<scene::Component *> instances;
        std::vector<scene::Property> properties;
    };
    std::vector<CommonComponent> FindCommonComponents(const std::vector<scene::Entity *> &selection);
    void SetCommonProperty(const CommonComponent &group, const scene::Property &property, int vectorAxis = -1);
}
