// The lightmap atlas' layers (render/BakedLighting.h): which atlases are valid, and how the renderer stacks their
// layers into one texture (StackLightmapLayers, read by LightmapSampling.slang lightmapLayerCoordinate). File round
// trips and the bake itself are covered by BakedLightingTests.
#include "PlutoGE/render/BakedLighting.h"

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

    LightmapLayer Layer(LightmapEncoding encoding, std::uint16_t value, std::size_t texels = 2 * 2 * 4)
    {
        return {encoding, std::vector<std::uint16_t>(texels, value)};
    }

    LightmapAtlas Atlas(std::vector<LightmapLayer> layers)
    {
        LightmapAtlas atlas;
        atlas.width = 2;
        atlas.height = 2;
        atlas.layers = std::move(layers);
        return atlas;
    }

    void CheckValidity()
    {
        Require(Atlas({Layer(LightmapEncoding::IrradianceShadowMask, 1)}).IsValid(), "One irradiance layer is a valid atlas");
        Require(!Atlas({}).IsValid(), "An atlas needs a layer");
        Require(!Atlas({Layer(LightmapEncoding::SkyVisibility, 1)}).IsValid(), "The first layer must be irradiance");
        Require(!Atlas({Layer(LightmapEncoding::IrradianceShadowMask, 1), Layer(LightmapEncoding::IrradianceShadowMask, 2)}).IsValid(),
                "An encoding may appear once");
        Require(!Atlas({Layer(LightmapEncoding::IrradianceShadowMask, 1), Layer(LightmapEncoding::SkyVisibility, 2, 3)}).IsValid(),
                "Every layer covers the whole page");
        std::cout << "PASS lightmap atlas validity\n";
    }

    void CheckStacking()
    {
        const auto single = StackLightmapLayers(Atlas({Layer(LightmapEncoding::IrradianceShadowMask, 7)}));
        Require(single.layerCount == 1 && single.skyVisibilityLayer == -1 && single.texels.size() == 16,
                "One layer stacks alone, without sky visibility");
        // Layers stack in LightmapLayerOrder, whatever order the atlas lists them in after the first.
        const auto both = StackLightmapLayers(Atlas({Layer(LightmapEncoding::IrradianceShadowMask, 7),
                                                     Layer(LightmapEncoding::SkyVisibility, 9)}));
        Require(both.layerCount == 2 && both.skyVisibilityLayer == 1 && both.texels.size() == 32 &&
                    both.texels.front() == 7 && both.texels.back() == 9,
                "Sky visibility stacks below irradiance");
        Require(StackLightmapLayers(Atlas({})).layerCount == 0, "An invalid atlas stacks nothing");
        std::cout << "PASS lightmap layer stacking\n";
    }
}

int main()
{
    try
    {
        CheckValidity();
        CheckStacking();
        std::cout << "PASS lightmap layers\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
