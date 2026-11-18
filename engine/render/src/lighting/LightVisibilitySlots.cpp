#include "LightVisibilitySlots.h"
#include "LocalLightPacking.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    static_assert(PLUTO_LIGHT_VISIBILITY_IDS <= PLUTO_LIGHT_VISIBILITY_ID_ROWS * 4, "The id table holds every light id");
    static_assert(PLUTO_LIGHT_VISIBILITY_DIRECTIONAL == PLUTO_MAX_LOCAL_LIGHTS, "Local ids precede the directional one");
    static_assert(PLUTO_LIGHT_VISIBILITY_SLOTS <= 3 * PLUTO_LIGHT_VISIBILITY_SLOTS_PER_CHANNEL,
                  "Slots fit the mask's three packed channels");
    static_assert(PLUTO_LIGHT_VISIBILITY_SLOTS_PER_CHANNEL * PLUTO_LIGHT_VISIBILITY_BITS <= 24,
                  "A channel's packed levels stay exact in a 32-bit float");

    namespace
    {
        struct Candidate
        {
            float score = 0.0f;
            std::uint32_t id = 0;
            const BasicPointLight *light = nullptr;
            float sourceRadius = 0.0f;
        };

        // Brightness at the camera, as candela over squared distance: what decides which lights' shadows show most.
        float CameraScore(const BasicPointLight &light, const glm::vec3 &camera)
        {
            const glm::vec3 offset = light.position - camera;
            return light.intensity / (1.0f + glm::dot(offset, offset));
        }

        bool Finite(const glm::vec3 &value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        void SortByScore(std::vector<Candidate> &candidates)
        {
            std::stable_sort(candidates.begin(), candidates.end(),
                             [](const Candidate &a, const Candidate &b) { return a.score > b.score; });
        }
    }

    std::vector<LightVisibilitySlot> SelectLightVisibilitySlots(const BasicLighting &lighting,
                                                                const LightVisibilityBudget &budget,
                                                                const LocalShadowMapFlags &shadowMaps)
    {
        std::vector<LightVisibilitySlot> slots;
        if (!lighting.shadowsEnabled)
            return slots;
        const auto full = [&] { return slots.size() == PLUTO_LIGHT_VISIBILITY_SLOTS; };
        const std::size_t contactBudget = std::min<std::size_t>(budget.contactLights, PLUTO_LIGHT_VISIBILITY_SLOTS);

        // Contact: the directional light first, then the local lights around the camera.
        const float directionLength = glm::length(lighting.directionalDirection);
        if (contactBudget > 0 && lighting.directionalIntensity > 0.0f && lighting.contactShadowLength > 0.0f &&
            std::isfinite(directionLength) && directionLength > 1.e-6f)
            slots.push_back({.kind = LightVisibilitySlot::Kind::Directional, .id = PLUTO_LIGHT_VISIBILITY_DIRECTIONAL,
                             .vector = -lighting.directionalDirection / directionLength,
                             .contactLength = std::min(lighting.contactShadowLength, 2.0f)});

        std::vector<Candidate> contact, traced;
        ForEachPackedLocalLight(lighting, [&](const BasicPointLight &light, std::uint32_t id, PackedLocalLightType type,
                                              std::size_t index)
        {
            if (!(light.intensity > 0.0f) || !(light.range > 0.02f) || !Finite(light.position))
                return;
            const float score = CameraScore(light, lighting.cameraPosition);
            // Rectangles have no emitter radius of their own: a world test jitters over a disc of their half extent.
            float sourceRadius = std::isfinite(light.sourceRadius) ? std::max(light.sourceRadius, 0.0f) : 0.0f;
            if (type == PackedLocalLightType::Rect)
            {
                const auto &rect = lighting.rectLights[index];
                sourceRadius = 0.5f * std::min(std::max(rect.width, 0.0f), std::max(rect.height, 0.0f));
            }
            const Candidate candidate{score, id, &light, sourceRadius};
            if (light.contactShadowLength > 0.0f && glm::length(light.position - lighting.cameraPosition) < light.range)
                contact.push_back(candidate);
            if (budget.tracedLights > 0 && light.tracedShadows && !shadowMaps[id])
                traced.push_back(candidate);
        });
        SortByScore(contact);
        SortByScore(traced);

        const auto localSlot = [](const Candidate &candidate)
        {
            return LightVisibilitySlot{.kind = LightVisibilitySlot::Kind::Local, .id = candidate.id,
                                       .vector = candidate.light->position, .range = candidate.light->range,
                                       .sourceRadius = candidate.sourceRadius};
        };
        for (const auto &candidate : contact)
        {
            if (slots.size() >= contactBudget || full())
                break;
            auto slot = localSlot(candidate);
            slot.contactLength = std::min(candidate.light->contactShadowLength, 2.0f);
            slots.push_back(slot);
        }
        std::size_t tracedCount = 0;
        for (const auto &candidate : traced)
        {
            if (tracedCount == budget.tracedLights)
                break;
            const auto existing = std::find_if(slots.begin(), slots.end(), [&](const LightVisibilitySlot &slot)
            { return slot.kind == LightVisibilitySlot::Kind::Local && slot.id == candidate.id; });
            if (existing != slots.end())
                existing->traced = true;
            else if (full())
                break;
            else
            {
                auto slot = localSlot(candidate);
                slot.traced = true;
                slots.push_back(slot);
            }
            ++tracedCount;
        }
        return slots;
    }

    LightVisibilitySlotTable MakeLightVisibilitySlotTable(const std::vector<LightVisibilitySlot> &slots)
    {
        LightVisibilitySlotTable table{};
        for (std::size_t slot = 0; slot < slots.size() && slot < PLUTO_LIGHT_VISIBILITY_SLOTS; ++slot)
        {
            const auto id = slots[slot].id;
            if (id < PLUTO_LIGHT_VISIBILITY_IDS)
                table[id / 4][static_cast<glm::length_t>(id % 4)] = static_cast<std::uint32_t>(slot + 1);
        }
        return table;
    }
}
