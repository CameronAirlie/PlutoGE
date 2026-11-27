// Shadowmask (docs/BAKED_LIGHTING.md, "Mixed shadows"): a Mixed sun whose static shadows are baked keeps realtime
// shadows for moving objects only. The renderer's CPU contracts: the screen tiles the realtime shadow is filtered
// over, when static casters leave the realtime maps, and the sun's contact slot. The bake and the scene setting are
// covered by BakedLightingTests.
#include "Shadowmask.h"
#include "lighting/LightVisibilitySlots.h"


#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace PlutoGE::render;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    constexpr std::uint32_t kWidth = 640, kHeight = 360;

    // A camera at (0, 4, 12) looking at the origin.
    glm::mat4 ViewProjection()
    {
        return glm::perspective(glm::radians(60.0f), float(kWidth) / float(kHeight), 0.1f, 200.0f) *
               glm::lookAt(glm::vec3(0.0f, 4.0f, 12.0f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    }

    bool Disjoint(const ScreenRegion &region)
    {
        for (std::size_t a = 0; a < region.rectangles.size(); ++a)
            for (std::size_t b = a + 1; b < region.rectangles.size(); ++b)
            {
                const auto &p = region.rectangles[a], &q = region.rectangles[b];
                const bool apart = p.x + std::int32_t(p.width) <= q.x || q.x + std::int32_t(q.width) <= p.x ||
                                   p.y + std::int32_t(p.height) <= q.y || q.y + std::int32_t(q.height) <= p.y;
                if (!apart) return false;
            }
        return true;
    }

    bool Covers(const ScreenRegion &region, glm::vec3 world)
    {
        const glm::vec4 clip = ViewProjection() * glm::vec4(world, 1.0f);
        const glm::vec2 pixel = (glm::vec2(clip) / clip.w * 0.5f + 0.5f) * glm::vec2(kWidth, kHeight);
        return std::any_of(region.rectangles.begin(), region.rectangles.end(), [&](const rhi::Scissor &r)
        {
            return pixel.x >= float(r.x) && pixel.x < float(r.x + std::int32_t(r.width)) &&
                   pixel.y >= float(r.y) && pixel.y < float(r.y + std::int32_t(r.height));
        });
    }

    void CheckTileGrid()
    {
        {
            ScreenTileGrid tiles(ViewProjection(), kWidth, kHeight, false);
            tiles.MarkSphere({0.0f, 0.0f, 0.0f}, 0.5f);
            tiles.MarkSphere({3.0f, 1.0f, -2.0f}, 0.5f);
            const auto region = tiles.Region();
            Require(!region.Empty() && region.PixelCount() < std::uint64_t(kWidth) * kHeight / 4,
                    "Two small spheres should cover a small part of the view");
            Require(Disjoint(region), "Region rectangles must not overlap (passes that blend would add twice)");
            Require(Covers(region, {0.0f, 0.0f, 0.0f}) && Covers(region, {3.0f, 1.0f, -2.0f}), "Marked spheres are not covered");
        }
        {
            ScreenTileGrid tiles(ViewProjection(), kWidth, kHeight, false);
            tiles.MarkSphere({0.0f, 4.0f, 30.0f}, 1.0f); // behind the camera
            tiles.MarkSphere({200.0f, 0.0f, 0.0f}, 1.0f); // off to the side
            Require(tiles.Region().Empty(), "Spheres out of view must mark nothing");
        }
        {
            ScreenTileGrid tiles(ViewProjection(), kWidth, kHeight, false);
            tiles.MarkSphere({0.0f, 4.0f, 12.0f}, 1.0f); // around the eye
            Require(tiles.Full() && tiles.Region().PixelCount() == std::uint64_t(kWidth) * kHeight,
                    "A sphere across the eye plane must cover the whole view");
        }
        std::cout << "PASS screen tile grid\n";
    }

    // A rigid draw with world bounds, as RhiSceneRenderer builds them.
    BasicDraw Draw(const BasicMesh &mesh, glm::vec3 center, glm::vec3 extents, bool staticGeometry)
    {
        BasicDraw draw;
        draw.mesh = &mesh;
        draw.staticGeometry = staticGeometry;
        draw.shadowBoundsCenter = center;
        draw.shadowBoundsRadius = glm::length(extents);
        draw.localBoundingSphere = glm::vec4(0.0f, 0.0f, 0.0f, glm::length(extents));
        draw.model = glm::translate(glm::mat4(1.0f), center);
        draw.occlusionBoundsCenter = center;
        draw.occlusionBoundsExtents = extents;
        return draw;
    }

    void CheckRealtimeShadowRegion()
    {
        BasicMesh mesh;
        BasicLighting lighting;
        lighting.directionalDirection = glm::normalize(glm::vec3(1.0f, -1.0f, 0.0f));
        lighting.bakedLighting.shadowmask = true;
        const auto floor = Draw(mesh, {0.0f, -0.1f, 0.0f}, {20.0f, 0.1f, 20.0f}, true);
        const auto statue = Draw(mesh, {-4.0f, 1.0f, 0.0f}, {0.5f, 1.0f, 0.5f}, true);
        const auto ball = Draw(mesh, {0.0f, 2.0f, 0.0f}, {0.5f, 0.5f, 0.5f}, false);

        const std::vector<BasicDraw> staticOnly{floor, statue};
        Require(RealtimeShadowRegion(staticOnly, staticOnly, lighting, ViewProjection(), kWidth, kHeight, false).Empty(),
                "Static casters' shadows are baked: no realtime shadow is needed");

        const std::vector<BasicDraw> scene{floor, statue, ball};
        const auto region = RealtimeShadowRegion(scene, scene, lighting, ViewProjection(), kWidth, kHeight, false);
        Require(!region.Empty() && region.PixelCount() < std::uint64_t(kWidth) * kHeight / 2,
                "A moving ball should need a small realtime region, not " + std::to_string(region.PixelCount()));
        Require(Disjoint(region), "Realtime region rectangles must not overlap");
        // The ball, and where its shadow lands: 2 m along the light from its centre reaches the floor.
        Require(Covers(region, {0.0f, 2.0f, 0.0f}), "The moving caster itself is not covered");
        Require(Covers(region, {2.0f, 0.0f, 0.0f}), "Where the moving caster's shadow lands is not covered");
        Require(!Covers(region, {-8.0f, 0.0f, 0.0f}), "Floor far from the moving caster's shadow should not be covered");

        BasicDraw unknown = ball;
        unknown.shadowBoundsRadius = -1.0f;
        unknown.localBoundingSphere.w = -1.0f;
        const std::vector<BasicDraw> withUnknown{floor, unknown};
        Require(RealtimeShadowRegion(withUnknown, withUnknown, lighting, ViewProjection(), kWidth, kHeight, false).PixelCount() ==
                    std::uint64_t(kWidth) * kHeight,
                "A moving caster of unknown extent must cover the whole view");
        std::cout << "PASS realtime shadow region\n";
    }

    void CheckStaticCasterPolicy()
    {
        BasicMesh mesh;
        BasicLighting lighting;
        lighting.bakedLighting.shadowmask = true;
        PlanShadowmask(lighting, {});
        Require(lighting.bakedLighting.excludeStaticCasters, "Static casters should leave the realtime maps");
        const auto statue = Draw(mesh, {}, glm::vec3(1.0f), true);
        const auto ball = Draw(mesh, {}, glm::vec3(1.0f), false);
        Require(!CastsRealtimeDirectionalShadow(statue, lighting) && CastsRealtimeDirectionalShadow(ball, lighting),
                "Under shadowmask only moving objects cast into the sun's realtime maps");

        BasicPostProcessEffect fog;
        fog.type = BasicPostProcessEffectType::VolumetricFog;
        PlanShadowmask(lighting, std::span(&fog, 1));
        Require(!lighting.bakedLighting.excludeStaticCasters && StaticShadowCastersNeeded(lighting, std::span(&fog, 1)),
                "Volumetric fog reads static casters from the realtime maps");
        Require(CastsRealtimeDirectionalShadow(statue, lighting), "Kept static casters must still cast");

        BasicLighting spot = lighting;
        spot.shadowMethod = ShadowMethod::Virtual;
        BasicLighting::SpotLight light;
        light.light.castsShadows = true;
        light.light.range = 10.0f;
        spot.spotLights.push_back(light);
        PlanShadowmask(spot, {});
        Require(!spot.bakedLighting.excludeStaticCasters, "Spot shadows share the virtual shadow maps' casters");

        BasicLighting off;
        PlanShadowmask(off, {});
        Require(!off.bakedLighting.excludeStaticCasters && CastsRealtimeDirectionalShadow(statue, off),
                "Without shadowmask every caster stays realtime");
        std::cout << "PASS static caster policy\n";
    }

    void CheckContactSlot()
    {
        BasicLighting lighting;
        lighting.shadowsEnabled = true;
        lighting.directionalIntensity = 3.0f;
        lighting.directionalDirection = {0.0f, -1.0f, 0.0f};
        lighting.contactShadowLength = 0.25f;
        LightVisibilityBudget budget;
        budget.contactLights = 4;
        const auto realtime = SelectLightVisibilitySlots(lighting, budget, {});
        Require(std::any_of(realtime.begin(), realtime.end(), [](const auto &slot) { return slot.kind == LightVisibilitySlot::Kind::Directional; }),
                "A realtime sun keeps its contact shadow");
        lighting.bakedLighting.shadowmask = true;
        const auto shadowmask = SelectLightVisibilitySlots(lighting, budget, {});
        Require(std::none_of(shadowmask.begin(), shadowmask.end(), [](const auto &slot) { return slot.kind == LightVisibilitySlot::Kind::Directional; }),
                "Under shadowmask the sun's full-screen contact march is skipped");
        std::cout << "PASS sun contact slot\n";
    }

}

int main()
{
    try
    {
        CheckTileGrid();
        CheckRealtimeShadowRegion();
        CheckStaticCasterPolicy();
        CheckContactSlot();
        std::cout << "PASS shadowmask\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
