#include "PlutoGE/render/RhiOcean.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/OceanComponent.h"
#include <algorithm>
#include <cmath>
namespace PlutoGE::render
{
    std::vector<BasicPostProcessEffect> CollectRhiOceans(const scene::Scene &scene, const BasicLighting &lighting)
    {
        std::vector<BasicPostProcessEffect> result;
        const auto visit = [&](auto &&self, const scene::Entity *entity) -> void
        {
            if (!entity->IsActive()) return;
            const auto transform = entity->GetWorldTransform();
            const float determinant = glm::determinant(transform);
            if (std::isfinite(determinant) && std::abs(determinant) > 1e-8f)
                for (const auto *ocean : entity->GetComponents<scene::OceanComponent>())
                {
                    if (!ocean->IsEnabled()) continue;
                    auto packet = std::make_shared<OceanParameters>();
                    packet->localToWorld = transform;
                    packet->shallowOpacity = {ocean->GetShallowColor(), ocean->GetOpacity()};
                    packet->deepSmoothness = {ocean->GetDeepColor(), ocean->GetSmoothness()};
                    packet->foamVisibility = {ocean->GetFoamColor(), ocean->GetMaxVisibilityDepth()};
                    packet->waves = {ocean->GetWaveAmplitude(), ocean->GetWaveLength(), ocean->GetWaveSpeed(), ocean->GetWaveChoppiness()};
                    packet->underwater = {ocean->GetUnderwaterFadeStart(), ocean->GetUnderwaterFadeSoftness(), ocean->GetUnderwaterDepthFalloff(), ocean->GetUnderwaterLightFalloff()};
                    packet->surface = {ocean->GetRefractionStrength(), ocean->GetFoamDistance(), ocean->GetFoamIntensity(), ocean->GetUnderwaterTurbidity()};
                    packet->sunDirectionTime = {lighting.directionalDirection, ocean->GetSimulationTime()};
                    packet->crestTintStyle = {ocean->GetCrestColor(), ocean->GetStylization()};
                    const auto spectrum = ocean->GetWaveSpectrum();
                    static_assert(scene::OceanWaveCount == std::tuple_size_v<decltype(packet->waveShape)>);
                    packet->waveShape = spectrum.shape;
                    packet->waveMotion = spectrum.motion;
                    packet->crestFoam = {ocean->GetCrestFoamThreshold(), ocean->GetCrestFoamIntensity(), ocean->GetFoamScale(), spectrum.heightBound};
                    packet->detail = {ocean->GetRippleStrength(), ocean->GetCausticsIntensity(), ocean->GetCausticsScale(), 0};
                    const float windAngle = glm::radians(ocean->GetWindDirection());
                    packet->flow = {std::cos(windAngle), std::sin(windAngle), 0, 0};
                    packet->mask.x = ocean->GetInvertAreaMask() ? 1 : 0;
                    packet->mask.y = static_cast<int>(std::min<std::size_t>(ocean->GetAreas().size(), 8));
                    for (int area = 0; area < packet->mask.y; ++area)
                    {
                        const auto &points = ocean->GetAreas()[area].points;
                        const int count = static_cast<int>(std::min<std::size_t>(points.size(), 32));
                        packet->areaCounts[area].x = count;
                        for (int point = 0; point < count; ++point)
                            packet->points[area * 32 + point] = {points[point].x, points[point].y, 0, 0};
                    }
                    BasicPostProcessEffect effect{BasicPostProcessEffectType::Ocean};
                    effect.worldToLocal = glm::inverse(transform); effect.ocean = std::move(packet);
                    result.push_back(std::move(effect));
                }
            for (const auto *child : entity->GetChildren()) self(self, child);
        };
        for (const auto *root : scene.GetRootEntities()) visit(visit, root);
        // Composite distant water first, so nearer overlapping surfaces win.
        std::stable_sort(result.begin(), result.end(), [&](const auto &a, const auto &b)
        {
            return std::abs((a.worldToLocal * glm::vec4(lighting.cameraPosition, 1)).y) >
                   std::abs((b.worldToLocal * glm::vec4(lighting.cameraPosition, 1)).y);
        });
        return result;
    }
}
