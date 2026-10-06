#include "PlutoGE/render/GraphicsQuality.h"
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
}
