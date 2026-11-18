#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace PlutoGE::render
{
    // A scene's local light as LocalLightSelector ranks it.
    struct LocalLightCandidate
    {
        enum class Type : std::uint8_t { Point, Spot, Rect };
        // Identifies the light from frame to frame (the scene light, for instance). Keys are compared, never followed.
        std::uintptr_t key = 0;
        Type type = Type::Point;
        glm::vec3 position{0.0f};
        float range = 0.0f;
        float intensity = 0.0f;
        bool castsShadows = false;
    };

    // How many lights of each type the renderer shades and gives shadow maps (BasicRenderer packs points and spots up
    // to 16 each, rectangles in the slots left of 32; points have 4 shadow cubes, spots PLUTO_VSM_SPOT_COUNT pages).
    struct LocalLightSelectionBudget
    {
        std::size_t points = 16, spots = 16, total = 32;
        std::size_t pointShadows = 4, spotShadows = 4;
    };

    // Chooses which local lights a view shades and which get its few shadow maps, by importance rather than scene
    // order: a light's brightness at the camera (intensity over squared distance), cut down when its range does not
    // reach the view. The renderer packs lights and assigns shadow maps in list order, so the selector orders each type
    // as: lights given shadow maps, the rest of the shaded lights, then the lights left out.
    //
    // Selections hold: a light chosen last frame keeps its place until another is clearly more important
    // (RetainedBonus, ShadowRetainedBonus), and chosen lights keep last frame's relative order, so shadow maps are not
    // reassigned (and their cached faces redrawn) as scores shift with the camera. Lights left out stay in key order,
    // so the full list (which GI injection hashes) changes only when the selection does.
    class LocalLightSelector
    {
    public:
        // Scores of lights already chosen are multiplied by these when competing to stay.
        static constexpr float RetainedBonus = 1.25f, ShadowRetainedBonus = 1.5f;
        // Importance kept by a light whose range lies wholly outside the view: it can still light what the camera sees
        // through GI and reflections, so it stays eligible, behind every light in view.
        static constexpr float OffscreenWeight = 0.05f;

        // Returns the candidates' indices in the order the renderer should list them (each type's in sequence: points,
        // spots, rectangles). `viewProjection` maps world to clip space; only its side planes are used.
        [[nodiscard]] std::vector<std::size_t> Order(std::span<const LocalLightCandidate> candidates,
                                                     const glm::vec3 &camera, const glm::mat4 &viewProjection,
                                                     const LocalLightSelectionBudget &budget = {});
        // Forgets earlier selections (another scene, a camera cut).
        void Reset() noexcept;

    private:
        struct Memory
        {
            // Last frame's chosen lights in their list order: those with shadow maps, and every shaded one.
            std::vector<std::uintptr_t> shadowed, shaded;
        };
        std::unordered_map<std::uint8_t, Memory> m_memory;
    };
}
