#pragma once

#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/LightClusterConfig.h"
#include "PlutoGE/render/LightVisibilityConfig.h"

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace PlutoGE::render
{
    // One light the light visibility mask (LightVisibilityMask) holds this frame, and the producers that run for it.
    struct LightVisibilitySlot
    {
        enum class Kind : std::uint8_t { Directional, Local };
        Kind kind = Kind::Local;
        // The id surfaces look the slot up by: a local light's packed index, or PLUTO_LIGHT_VISIBILITY_DIRECTIONAL.
        std::uint32_t id = 0;
        // Directional: world direction towards the light (unit). Local: the light's world position.
        glm::vec3 vector{0.0f, 1.0f, 0.0f};
        // World length of the screen-space contact march towards the light; 0 runs none.
        float contactLength = 0.0f;
        // Whether a world test runs along the whole segment to the light (local lights without a shadow map).
        bool traced = false;
        // Local lights: the range beyond which they add nothing, and the emitter's radius world tests jitter over.
        float range = 0.0f;
        float sourceRadius = 0.0f;

        bool operator==(const LightVisibilitySlot &) const = default;
    };

    // How many lights each producer may cover in a frame. Each costs a march (contact) or a world test (traced) per
    // pixel the light reaches.
    struct LightVisibilityBudget
    {
        std::size_t contactLights = 3;
        std::size_t tracedLights = 0;
    };

    // Per packed local light (index = shader light id): whether it has a shadow map this frame. Traced tests are for
    // the lights that do not.
    using LocalShadowMapFlags = std::array<bool, PLUTO_MAX_LOCAL_LIGHTS>;

    // Chooses the frame's slots, at most PLUTO_LIGHT_VISIBILITY_SLOTS:
    //  - contact: the directional light, then local lights with a contact length that reach the camera, brightest
    //    there first (intensity over squared distance), up to `budget.contactLights` together;
    //  - traced: local lights marked BasicPointLight::tracedShadows that have no shadow map, brightest at the camera
    //    first, up to `budget.tracedLights`. A light with both shares one slot.
    // Nothing when the lighting has shadows off. Slots appear in that order, contact first.
    [[nodiscard]] std::vector<LightVisibilitySlot> SelectLightVisibilitySlots(const BasicLighting &lighting,
                                                                             const LightVisibilityBudget &budget,
                                                                             const LocalShadowMapFlags &shadowMaps);

    // The surfaces' light id to slot table (SceneLightingParameters.slang lightVisibilitySlots): entry [id / 4][id % 4]
    // holds the slot index plus one, or 0 for lights without one.
    using LightVisibilitySlotTable = std::array<glm::uvec4, PLUTO_LIGHT_VISIBILITY_ID_ROWS>;
    [[nodiscard]] LightVisibilitySlotTable MakeLightVisibilitySlotTable(const std::vector<LightVisibilitySlot> &slots);
}
