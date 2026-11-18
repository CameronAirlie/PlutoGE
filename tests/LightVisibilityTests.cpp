// CPU contracts of light visibility (docs/LIGHT_OCCLUSION.md): which lights take the light visibility mask's slots
// (SelectLightVisibilitySlots), the surfaces' id-to-slot table, and which scene lights a view shades and gives shadow
// maps (LocalLightSelector).
#include "lighting/LightVisibilitySlots.h"
#include "PlutoGE/render/LocalLightSelection.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace PlutoGE::render;

namespace
{
    void Check(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    BasicPointLight Lamp(glm::vec3 position, float intensity = 4.0f)
    {
        BasicPointLight light;
        light.position = position;
        light.range = 20.0f;
        light.intensity = intensity;
        return light;
    }

    const LightVisibilitySlot *SlotFor(const std::vector<LightVisibilitySlot> &slots, std::uint32_t id)
    {
        const auto found = std::find_if(slots.begin(), slots.end(), [&](const auto &slot) { return slot.id == id; });
        return found == slots.end() ? nullptr : &*found;
    }

    void CheckSlots()
    {
        BasicLighting lighting;
        lighting.shadowsEnabled = true;
        lighting.cameraPosition = {0.0f, 1.0f, 0.0f};
        lighting.directionalIntensity = 3.0f;
        lighting.directionalDirection = {0.0f, -1.0f, 0.0f};
        lighting.contactShadowLength = 0.25f;
        const LocalShadowMapFlags noMaps{};

        // Contact: the directional light first, with its id and the direction towards it.
        auto slots = SelectLightVisibilitySlots(lighting, {}, noMaps);
        Check(slots.size() == 1 && slots[0].kind == LightVisibilitySlot::Kind::Directional &&
                  slots[0].id == PLUTO_LIGHT_VISIBILITY_DIRECTIONAL && slots[0].vector == glm::vec3(0.0f, 1.0f, 0.0f) &&
                  !slots[0].traced,
              "The directional light did not take the first contact slot");

        // Local contact lights: brightest at the camera first, within the budget, only around the camera.
        lighting.pointLights = {Lamp({8.0f, 1.0f, 0.0f}), Lamp({1.0f, 1.0f, 0.0f}), Lamp({3.0f, 1.0f, 0.0f}),
                                Lamp({2.0f, 1.0f, 0.0f})};
        for (auto &light : lighting.pointLights) light.contactShadowLength = 0.4f;
        lighting.pointLights[0].range = 5.0f; // the camera is outside its range
        slots = SelectLightVisibilitySlots(lighting, {.contactLights = 3, .tracedLights = 0}, noMaps);
        Check(slots.size() == 3 && slots[1].id == 1 && slots[2].id == 3,
              "Contact slots were not the brightest local lights around the camera");

        // Traced: lights without a shadow map that opted in, brightest first, within the budget; a light that also has
        // contact shares its slot.
        for (auto &light : lighting.pointLights) light.contactShadowLength = 0.0f;
        lighting.pointLights[1].contactShadowLength = 0.4f;
        lighting.pointLights[0].range = 20.0f;
        LocalShadowMapFlags maps{};
        maps[3] = true; // light 3 has a shadow map
        lighting.pointLights[2].tracedShadows = true;
        lighting.pointLights.push_back(Lamp({0.0f, 1.0f, 2.0f}));
        lighting.pointLights[4].tracedShadows = false;
        slots = SelectLightVisibilitySlots(lighting, {.contactLights = 3, .tracedLights = 2}, maps);
        Check(SlotFor(slots, 1) && SlotFor(slots, 1)->traced && SlotFor(slots, 1)->contactLength > 0.0f,
              "A light with contact and traced shadows did not share one slot");
        Check(SlotFor(slots, 2) && SlotFor(slots, 2)->traced, "The next brightest traced light got no slot");
        Check(!SlotFor(slots, 0), "The traced budget was exceeded");
        Check(!SlotFor(slots, 3), "A light with a shadow map was traced");
        Check(!SlotFor(slots, 4), "A light that opted out of traced shadows was traced");
        Check(slots.size() == 3, "Slots were duplicated");

        // Never more slots than the mask holds.
        lighting.pointLights.assign(16, Lamp({0.0f, 1.0f, 3.0f}));
        lighting.spotLights.assign(16, BasicLighting::SpotLight{Lamp({0.0f, 1.0f, -3.0f})});
        slots = SelectLightVisibilitySlots(lighting, {.contactLights = 3, .tracedLights = 32}, noMaps);
        Check(slots.size() == PLUTO_LIGHT_VISIBILITY_SLOTS, "The mask's slot count was exceeded");

        // Rectangles jitter over half their smaller side; spots and points over their emitter radius.
        lighting.pointLights.clear();
        lighting.spotLights.clear();
        BasicRectLight rect;
        rect.light = Lamp({0.0f, 2.0f, 0.0f});
        rect.width = 2.0f;
        rect.height = 0.5f;
        lighting.rectLights = {rect};
        slots = SelectLightVisibilitySlots(lighting, {.contactLights = 1, .tracedLights = 1}, noMaps);
        Check(SlotFor(slots, 0) && SlotFor(slots, 0)->sourceRadius == 0.25f, "A rectangle's traced extent was wrong");

        // The id table: slot + 1 at [id / 4][id % 4], zero for lights without one.
        const auto table = MakeLightVisibilitySlotTable(slots);
        Check(table[PLUTO_LIGHT_VISIBILITY_DIRECTIONAL / 4][PLUTO_LIGHT_VISIBILITY_DIRECTIONAL % 4] == 1 &&
                  table[0][0] == 2 && table[0][1] == 0,
              "The light id to slot table was wrong");

        // Shadows off: nothing.
        lighting.shadowsEnabled = false;
        Check(SelectLightVisibilitySlots(lighting, {.contactLights = 3, .tracedLights = 8}, noMaps).empty(),
              "Slots were chosen with shadows off");
    }

    std::vector<LocalLightCandidate> Row(std::size_t count, float spacing, bool castsShadows)
    {
        std::vector<LocalLightCandidate> lights;
        for (std::size_t i = 0; i < count; ++i)
            lights.push_back({.key = 100 + i, .type = LocalLightCandidate::Type::Point,
                              .position = {0.0f, 1.0f, -2.0f - spacing * float(i)}, .range = 10.0f, .intensity = 10.0f,
                              .castsShadows = castsShadows});
        return lights;
    }

    void CheckSelection()
    {
        const glm::vec3 camera{0.0f, 1.0f, 0.0f};
        const glm::mat4 viewProjection = glm::perspective(glm::radians(60.0f), 1.5f, 0.1f, 500.0f) *
                                         glm::lookAt(camera, glm::vec3(0.0f, 1.0f, -10.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        // More lights than the renderer shades, in scene order farthest first: the nearest 16 come first, the nearest
        // four casters leading.
        auto lights = Row(20, 3.0f, true);
        std::reverse(lights.begin(), lights.end());
        LocalLightSelector selector;
        auto order = selector.Order(lights, camera, viewProjection);
        Check(order.size() == lights.size(), "The selector dropped or duplicated lights");
        for (std::size_t i = 0; i < 16; ++i)
            Check(lights[order[i]].key < 116, "A far light was shaded before a near one");
        for (std::size_t i = 0; i < 4; ++i)
            Check(lights[order[i]].key < 104, "The shadow maps did not go to the nearest casters");

        // Hysteresis: a newcomer slightly brighter than a shaded light does not displace it; a much brighter one does.
        LocalLightSelector held;
        auto pair = Row(2, 0.0f, true);
        pair[0].position = {0.0f, 1.0f, -5.0f};
        pair[1].position = {0.0f, 1.0f, -5.0f};
        pair[1].intensity = 0.0f; // not yet lit
        const LocalLightSelectionBudget oneShadow{.points = 16, .spots = 16, .total = 32, .pointShadows = 1, .spotShadows = 1};
        order = held.Order(pair, camera, viewProjection, oneShadow);
        Check(pair[order[0]].key == 100, "The only lit caster did not get the shadow map");
        pair[1].intensity = 10.0f * 1.3f; // brighter, within ShadowRetainedBonus
        order = held.Order(pair, camera, viewProjection, oneShadow);
        Check(pair[order[0]].key == 100, "A shadow map moved to a light only slightly brighter");
        pair[1].intensity = 10.0f * 2.0f;
        order = held.Order(pair, camera, viewProjection, oneShadow);
        Check(pair[order[0]].key == 101, "A shadow map stayed with a light much dimmer than another");

        // Shaded lights keep their relative order as their scores change, so shadow maps keep their rows.
        LocalLightSelector stable;
        auto four = Row(4, 2.0f, true);
        order = stable.Order(four, camera, viewProjection);
        std::reverse(four.begin(), four.end());
        for (std::size_t i = 0; i < four.size(); ++i) four[i].position = {0.0f, 1.0f, -2.0f - 2.0f * float(i)};
        const auto reordered = stable.Order(four, camera, viewProjection);
        for (std::size_t i = 0; i < four.size(); ++i)
            Check(four[reordered[i]].key == 100 + i, "Shaded lights were reordered as their scores changed");

        // A light whose range lies outside the view ranks behind one in view, however near.
        LocalLightSelector framed;
        std::vector<LocalLightCandidate> sides{
            {.key = 1, .position = {0.0f, 1.0f, 10.0f}, .range = 3.0f, .intensity = 10.0f, .castsShadows = true},
            {.key = 2, .position = {0.0f, 1.0f, -20.0f}, .range = 30.0f, .intensity = 10.0f, .castsShadows = true}};
        const LocalLightSelectionBudget single{.points = 1, .spots = 0, .total = 1, .pointShadows = 1, .spotShadows = 0};
        order = framed.Order(sides, camera, viewProjection, single);
        Check(sides[order[0]].key == 2, "A light behind the camera outranked one lighting the view");

        // Types keep their sequence: points, spots, rectangles.
        LocalLightSelector types;
        std::vector<LocalLightCandidate> mixed{
            {.key = 1, .type = LocalLightCandidate::Type::Rect, .position = {0, 1, -3}, .range = 10, .intensity = 10},
            {.key = 2, .type = LocalLightCandidate::Type::Spot, .position = {0, 1, -3}, .range = 10, .intensity = 10},
            {.key = 3, .type = LocalLightCandidate::Type::Point, .position = {0, 1, -3}, .range = 10, .intensity = 10}};
        order = types.Order(mixed, camera, viewProjection);
        Check(mixed[order[0]].key == 3 && mixed[order[1]].key == 2 && mixed[order[2]].key == 1,
              "Light types were interleaved");
    }
}

int main()
{
    try
    {
        CheckSlots();
        CheckSelection();
        std::cout << "PASS: light visibility slots, id table and local light selection\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
