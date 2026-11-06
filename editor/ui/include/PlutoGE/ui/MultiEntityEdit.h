#pragma once

#include "PlutoGE/scene/Entity.h"
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_set>
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
        // Filled by GetMixedProperties the first time something shows it.
        std::optional<std::unordered_set<std::string>> mixedProperties;
    };
    std::vector<CommonComponent> FindCommonComponents(const std::vector<scene::Entity *> &selection);
    // Names of the group's properties whose value differs between instances.
    const std::unordered_set<std::string> &GetMixedProperties(CommonComponent &group);

    // Keeps FindCommonComponents results until the selection, the scene or anything in it changes, since finding them
    // serializes every selected component. `sceneRevision` and `editRevision` are the shell's revisions for those.
    class CommonComponentCache
    {
    public:
        std::vector<CommonComponent> &Get(const std::vector<scene::Entity *> &selection, std::uint64_t sceneRevision, std::uint64_t editRevision);
        void Invalidate() { m_valid = false; }

    private:
        std::vector<scene::Entity *> m_selection;
        std::uint64_t m_sceneRevision = 0;
        std::uint64_t m_editRevision = 0;
        bool m_valid = false;
        std::vector<CommonComponent> m_groups;
    };

    void SetCommonProperty(const CommonComponent &group, const scene::Property &property, int vectorAxis = -1);
}
