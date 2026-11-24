#include "PlutoGE/render/GraphicsQuality.h"
#include <cmath>
#include <limits>
#include <stdexcept>

using namespace PlutoGE::render;

static void Check(bool condition)
{
    if (!condition) throw std::runtime_error("Graphics quality contract failed");
}

int main()
{
    const GraphicsQuality authored;
    Check(!authored.enabled && authored.Allows(BasicPostProcessEffectType::VCTGI));
    BasicLighting source;
    source.shadowResolution = 256;
    source.shadowCascadeCount = 1;
    source.shadowDistance = 20;
    const auto low = GraphicsQuality::FromPreset(GraphicsPreset::Low);
    auto lighting = source;
    low.Apply(lighting);
    Check(lighting.shadowResolution == 256 && lighting.shadowCascadeCount == 1 && lighting.shadowDistance == 20);
    Check(!lighting.shadowsEnabled && lighting.shadowMethod == ShadowMethod::Cascaded);
    Check(source.shadowMethod == ShadowMethod::Virtual);
    Check(!low.Allows(BasicPostProcessEffectType::VCTGI) && !low.Allows(BasicPostProcessEffectType::SSR));
    Check(!low.Allows(BasicPostProcessEffectType::VolumetricCloud));
    Check(low.Allows(BasicPostProcessEffectType::ToneMapping) && low.Allows(BasicPostProcessEffectType::TAA));
    for (auto preset : {GraphicsPreset::Low, GraphicsPreset::Medium, GraphicsPreset::High, GraphicsPreset::Ultra})
        Check(GraphicsQuality::FromPreset(preset).IsValid());
    // Traced shadows: off on Low, allowed above it and whenever no tier is active.
    Check(!low.AllowsTracedShadows() && GraphicsQuality::FromPreset(GraphicsPreset::Medium).AllowsTracedShadows());
    Check(authored.AllowsTracedShadows());
    // Foliage shadow sway: Low freezes it (no caster's sway reaches the minimum on any level); other tiers and disabled
    // tiers keep the authored minimum.
    {
        BasicLighting windy = source;
        windy.wind.shadowSwayMinTexels = 1.0f;
        auto swaying = windy;
        low.Apply(swaying);
        Check(!low.foliageShadowSway && std::isfinite(swaying.wind.shadowSwayMinTexels) && swaying.wind.shadowSwayMinTexels > 1.0e30f);
        swaying = windy;
        GraphicsQuality::FromPreset(GraphicsPreset::Medium).Apply(swaying);
        Check(swaying.wind.shadowSwayMinTexels == 1.0f);
        swaying = windy;
        authored.Apply(swaying);
        Check(swaying.wind.shadowSwayMinTexels == 1.0f);
    }
    // Terrain anti-tiling: off on Low, kept above it and by disabled tiers.
    {
        auto terrain = source;
        low.Apply(terrain);
        Check(!low.terrainAntiTiling && !terrain.terrainAntiTiling);
        terrain = source;
        GraphicsQuality::FromPreset(GraphicsPreset::Medium).Apply(terrain);
        Check(terrain.terrainAntiTiling);
        terrain = source;
        authored.Apply(terrain);
        Check(terrain.terrainAntiTiling);
    }
    // Water reflections: tiers without screen-space reflections trace fewer steps.
    {
        auto water = source;
        low.Apply(water);
        Check(water.waterReflectionSteps == GraphicsQuality::WaterReflectionStepsWithoutReflections);
        water = source;
        GraphicsQuality::FromPreset(GraphicsPreset::High).Apply(water);
        Check(water.waterReflectionSteps == source.waterReflectionSteps);
    }
    auto custom = low;
    custom.reflections = true;
    Check(custom.Allows(BasicPostProcessEffectType::SSR));
    custom.shadowDistance = std::numeric_limits<float>::quiet_NaN();
    Check(!custom.IsValid());
    custom = low; custom.shadowCascades = 0;
    Check(!custom.IsValid());
    custom = low; custom.shadowResolution = 8193;
    Check(!custom.IsValid());
    custom = low; custom.enabled = false;
    Check(custom.Allows(BasicPostProcessEffectType::SSR));
    lighting = source; custom.Apply(lighting);
    Check(lighting.shadowMethod == source.shadowMethod);

    // Contact shadows: Low skips them (sun via lighting, local lights via AllowsContactShadows); disabled tiers keep them.
    source.contactShadowLength = 0.3f;
    lighting = source; low.Apply(lighting);
    Check(lighting.contactShadowLength == 0.0f && !low.AllowsContactShadows());
    lighting = source; custom.Apply(lighting);
    Check(lighting.contactShadowLength == 0.3f && custom.AllowsContactShadows());
    const auto ultra = GraphicsQuality::FromPreset(GraphicsPreset::Ultra);
    lighting = source; ultra.Apply(lighting);
    Check(lighting.contactShadowLength == 0.3f && ultra.AllowsContactShadows());

    custom = low; custom.grassDensity = 0.05f;
    Check(!custom.IsValid());
    custom = low; custom.grassDistance = 1.01f;
    Check(!custom.IsValid());
}
