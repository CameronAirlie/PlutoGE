#pragma once
#include "PlutoGE/render/SceneWind.h"
#include <glm/glm.hpp>

namespace PlutoGE::render
{
    // How one kind of plant answers the scene's wind (a foliage type's wind settings). Heights are in mesh units,
    // measured along the mesh's +Y from its origin, so the response scales with each instance.
    struct FoliageWindResponse
    {
        bool enabled = false;
        float stiffness = 0.5f;  // 0 bends fully (grass), 1 does not sway (trunks); stiffer plants also sway faster
        float flutter = 0.3f;    // 0..1 fast, small per-vertex motion (leaves, blade tips)
        float bendHeight = 1.0f; // height where the sway reaches its authored lean; roughly the plant's height
        float pivotHeight = 0.0f; // geometry at or below this height stays put (roots, the base of a trunk)

        // Clamped to the ranges above (bendHeight at least 0.01).
        FoliageWindResponse Sanitized() const;
        bool operator==(const FoliageWindResponse &) const = default;
    };

    // Starting points for common plants, for a plant `height` mesh units tall.
    enum class FoliageWindPreset
    {
        Grass,   // soft, fast flutter, bends from the ground
        Flowers, // slightly stiffer stems
        Shrub,   // woody: moderate sway, leafy flutter
        Tree,    // stiff trunk anchored below its lowest fifth, slow sway, leaf flutter
    };
    FoliageWindResponse FoliageWindPresetResponse(FoliageWindPreset preset, float height);

    // The 64-byte block every pass that draws wind-animated geometry receives (forward, depth, shadows, VSM, voxels),
    // and FoliageWind.slang's FoliageWindParameters. All zero when the draw does not move, so a calm scene binds
    // exactly the constants it bound before wind existed.
    struct alignas(16) FoliageWindParameters
    {
        glm::vec4 field{0.0f};    // wind direction XZ, normalized strength, gustiness
        glm::vec4 timing{0.0f};   // time, previous frame's time, 1 / gust size, gust travel speed (m/s)
        glm::vec4 response{0.0f}; // sway, sway frequency (Hz), flutter amplitude, flutter frequency (Hz)
        glm::vec4 shape{0.0f};    // pivot height, 1 / bend height, bend height, 1 when active

        bool Active() const { return shape.w > 0.0f; }
        bool operator==(const FoliageWindParameters &) const = default;
    };
    static_assert(sizeof(FoliageWindParameters) == 64, "FoliageWindParameters must match FoliageWind.slang");

    // Whether a response moves vertices in this wind (enabled, not fully stiff, and not calm).
    bool FoliageWindAnimates(const SceneWindSettings &wind, const FoliageWindResponse &response);
    // The constants for one draw; all zero unless FoliageWindAnimates.
    FoliageWindParameters PackFoliageWind(const SceneWindSettings &wind, const FoliageWindResponse &response,
                                          float time, float previousTime);
    // Upper bound of a vertex's displacement per world unit of the instance's vertical scale, for any wind the
    // model accepts (it clamps strength), so bounds padded with it stay valid while the wind changes.
    float FoliageWindMaxDisplacement(const FoliageWindResponse &response);
    // The same bound under one wind, for vertices at most `localTop` (mesh space) high: far tighter, but valid only
    // while the wind's settings stay the same. Zero when the response does not animate in that wind.
    float FoliageWindMaxDisplacement(const SceneWindSettings &wind, const FoliageWindResponse &response, float localTop);
    // One frame's wind as the renderer's passes see it: what each draw's FoliageWindParameters are packed from.
    struct FoliageWindFrame
    {
        SceneWindSettings wind{};
        float time = 0.0f;
        float previousTime = 0.0f;

        // Whether a response moves this frame. Moving draws take per-frame constants and invalidate shadow caches.
        bool Animates(const FoliageWindResponse &response) const { return FoliageWindAnimates(wind, response); }
        FoliageWindParameters Parameters(const FoliageWindResponse &response) const
        {
            return PackFoliageWind(wind, response, time, previousTime);
        }
        // FoliageWindMaxDisplacement under this frame's wind: how far geometry at most `localTop` high can sway.
        float MaxDisplacement(const FoliageWindResponse &response, float localTop) const
        {
            return FoliageWindMaxDisplacement(wind, response, localTop);
        }
        // Whether a swaying caster whose bounding sphere is `bounds` (centre, radius) animates its virtual shadow
        // seen from `camera`: within SceneWindSettings::shadowSwayDistance of it, or everywhere when that is 0. A
        // caster that does not casts its rest pose, the calm draw: zero parameters and a signature without time.
        bool ShadowSways(const FoliageWindResponse &response, const glm::vec4 &bounds, const glm::vec3 &camera) const
        {
            if (!Animates(response)) return false;
            if (!(wind.shadowSwayDistance > 0.0f) || bounds.w < 0.0f) return true;
            return glm::length(glm::vec3(bounds) - camera) - bounds.w < wind.shadowSwayDistance;
        }
    };

    // CPU reference of FoliageWind.slang's foliageWindOffset: the world-space displacement of a vertex at
    // localPosition (mesh space) of an instance drawn with model, at the given time.
    glm::vec3 FoliageWindOffset(const FoliageWindParameters &wind, const glm::mat4 &model,
                                const glm::vec3 &localPosition, float time);
}
