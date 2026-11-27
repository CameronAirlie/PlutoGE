#pragma once

#include "ScreenTileRegion.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>

// Shadowmask (docs/BAKED_LIGHTING.md, "Mixed shadows"): a Mixed sun whose static shadows are baked keeps realtime
// shadows for moving objects only. Static geometry's shadow comes from the bake (the lightmap's shadow mask, or the
// ambient volumes' sun visibility for surfaces without lightmaps), so the realtime shadow is filtered only where a
// moving caster's shadow can fall (RealtimeShadowRegion), and static casters leave the realtime shadow maps when
// nothing else reads them there (PlanShadowmask). Surfaces take the darker of the two, so where both cover a pixel
// the result is the same as without shadowmask.
namespace PlutoGE::render
{
    // Why static casters must stay in the realtime shadow maps this frame, or null when they can leave. Spot shadows
    // share the virtual shadow maps' caster list, and volumetric fog and water sample the sun through those maps.
    inline const char *StaticShadowCastersNeeded(const BasicLighting &lighting,
                                                 std::span<const BasicPostProcessEffect> effects)
    {
        if (lighting.shadowMethod == ShadowMethod::Virtual)
            for (const auto &spot : lighting.spotLights)
                if (spot.light.castsShadows && spot.light.range > 0.02f)
                    return "a shadowed spot light shares the virtual shadow maps";
        for (const auto &effect : effects)
        {
            if (effect.type == BasicPostProcessEffectType::VolumetricFog)
                return "volumetric fog is lit through the shadow maps";
            if (effect.type == BasicPostProcessEffectType::Ocean)
                return "water is shaded through the shadow maps";
        }
        return nullptr;
    }

    // Completes a frame's shadowmask decision once its effects are known (BakedLightingFrame::excludeStaticCasters).
    inline void PlanShadowmask(BasicLighting &lighting, std::span<const BasicPostProcessEffect> effects)
    {
        auto &baked = lighting.bakedLighting;
        baked.excludeStaticCasters = baked.shadowmask && !StaticShadowCastersNeeded(lighting, effects);
    }

    // Whether a draw casts into the directional light's realtime shadow maps (cascades or virtual).
    inline bool CastsRealtimeDirectionalShadow(const BasicDraw &draw, const BasicLighting &lighting) noexcept
    {
        return draw.castsShadow && !(draw.staticGeometry && lighting.bakedLighting.excludeStaticCasters);
    }

    // How far a ray from `origin` along unit `direction` runs inside the box [low, high]; zero when it misses.
    inline float DistanceInsideBox(const glm::vec3 &origin, const glm::vec3 &direction, const glm::vec3 &low,
                                   const glm::vec3 &high) noexcept
    {
        float enter = 0.0f, exit = std::numeric_limits<float>::max();
        for (int axis = 0; axis < 3; ++axis)
        {
            if (std::abs(direction[axis]) < 1.0e-6f)
            {
                if (origin[axis] < low[axis] || origin[axis] > high[axis])
                    return 0.0f;
                continue;
            }
            float entering = (low[axis] - origin[axis]) / direction[axis];
            float leaving = (high[axis] - origin[axis]) / direction[axis];
            if (entering > leaving) std::swap(entering, leaving);
            enter = std::max(enter, entering);
            exit = std::min(exit, leaving);
        }
        return exit > enter ? exit : 0.0f;
    }

    // Under shadowmask, the pixels whose realtime sun shadow can darken them beyond the baked shadow: those a moving
    // caster's shadow volume covers, each caster's bounds swept along the light until they leave the receivers'
    // bounds. Elsewhere the realtime shadow is skipped (DirectionalShadowMask clears it to lit). Conservative: a draw
    // of unknown extent covers the whole target.
    inline ScreenRegion RealtimeShadowRegion(std::span<const BasicDraw> casters, std::span<const BasicDraw> receivers,
                                             const BasicLighting &lighting, const glm::mat4 &viewProjection,
                                             std::uint32_t width, std::uint32_t height, bool flipY)
    {
        ScreenTileGrid tiles(viewProjection, width, height, flipY);
        // Where shadows can land: every receiver's bounds. A rigid draw's box where it has one (a sphere around a floor
        // slab reaches far above and below it), per instance for instanced draws (a group's own sphere spans the
        // space between its instances), else the draw's whole sphere.
        glm::vec3 low(std::numeric_limits<float>::max()), high(-std::numeric_limits<float>::max());
        const auto addReceiver = [&](const glm::vec3 &center, float radius)
        {
            low = glm::min(low, center - glm::vec3(radius));
            high = glm::max(high, center + glm::vec3(radius));
        };
        for (const auto &draw : receivers)
        {
            if (!draw.mesh || draw.alphaMode == 2 || draw.surfaceType == 1 || draw.outlinePass)
                continue;
            if (glm::all(glm::greaterThanEqual(draw.occlusionBoundsExtents, glm::vec3(0.0f))) && !draw.instanceModels &&
                !draw.foliageWind.enabled)
            {
                low = glm::min(low, draw.occlusionBoundsCenter - draw.occlusionBoundsExtents);
                high = glm::max(high, draw.occlusionBoundsCenter + draw.occlusionBoundsExtents);
                continue;
            }
            if (ForEachDrawSphere(draw, addReceiver))
                continue;
            if (draw.shadowBoundsRadius < 0.0f || draw.detailGrass)
                return ScreenRegion::Whole(width, height);
            addReceiver(draw.shadowBoundsCenter, draw.shadowBoundsRadius);
        }
        if (!(low.x <= high.x))
            return {};
        const float directionLength = glm::length(lighting.directionalDirection);
        if (!(directionLength > 1.0e-6f))
            return ScreenRegion::Whole(width, height);
        const glm::vec3 direction = lighting.directionalDirection / directionLength;
        for (const auto &draw : casters)
        {
            if (!draw.mesh || draw.staticGeometry || !CastsRealtimeDirectionalShadow(draw, lighting) ||
                draw.alphaMode == 2 || draw.surfaceType == 1)
                continue;
            const bool known = ForEachDrawSphere(draw, [&](const glm::vec3 &center, float radius)
            {
                const float reach = DistanceInsideBox(center, direction, low - glm::vec3(radius), high + glm::vec3(radius));
                tiles.MarkSweptSphere(center, radius, direction * reach);
            });
            if (!known)
                tiles.MarkAll();
            if (tiles.Full())
                break;
        }
        return tiles.Region();
    }
}
