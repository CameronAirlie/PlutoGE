// Terrain material layers without a GPU: weight painting and its invariants, layer edits, persistence, the layer
// stack's render data, and the packing of layer maps into the renderer's texture arrays.
#include "PlutoGE/render/RhiTerrainLayerCache.h"
#include "PlutoGE/render/TerrainLayers.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/TerrainLayerStack.h"
#include "PlutoGE/scene/TerrainWeightMap.h"

#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

using namespace PlutoGE;

namespace
{
    void Require(bool condition, const char *message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool EveryTexelSumsTo255(const scene::TerrainWeightMap &weights)
    {
        for (int y = 0; y < weights.GetResolution(); ++y)
            for (int x = 0; x < weights.GetResolution(); ++x)
            {
                int sum = 0;
                for (std::size_t layer = 0; layer < scene::TerrainWeightMap::kLayers; ++layer)
                    sum += weights.GetWeight(x, y, layer);
                if (sum != 255)
                    return false;
            }
        return true;
    }

    std::unique_ptr<render::Texture> SolidTexture(int size, std::array<unsigned char, 4> colour)
    {
        std::vector<unsigned char> pixels(static_cast<std::size_t>(size) * size * 4);
        for (std::size_t i = 0; i < pixels.size(); i += 4)
            std::copy(colour.begin(), colour.end(), pixels.begin() + static_cast<std::ptrdiff_t>(i));
        return render::Texture::CreateRgba8(size, size, pixels);
    }

    void WeightMapPainting()
    {
        scene::TerrainWeightMap weights(64);
        Require(EveryTexelSumsTo255(weights) && weights.GetWeight(10, 10, 0) == 255, "A new map belongs to layer 0");

        const scene::TerrainLayerBrush hard{1.0f, 1.0f};
        const auto changed = weights.Paint({0.5f, 0.5f}, {0.1f, 0.1f}, 2, hard);
        Require(!changed.Empty(), "Painting reported no change");
        Require(weights.GetWeight(32, 32, 2) == 255, "A full-strength hard dab did not cover the centre");
        Require(weights.GetWeight(2, 2, 2) == 0 && weights.GetWeight(2, 2, 0) == 255, "Paint leaked outside the brush");
        Require(changed.x0 > 20 && changed.x1 < 44 && changed.y0 > 20 && changed.y1 < 44, "Changed rectangle exceeds the brush");
        Require(EveryTexelSumsTo255(weights), "Painting broke the weight sum");

        // A soft brush falls off: more paint at the centre than near the rim.
        scene::TerrainWeightMap soft(64);
        soft.Paint({0.5f, 0.5f}, {0.2f, 0.2f}, 1, {0.5f, 0.0f});
        Require(soft.GetWeight(32, 32, 1) > soft.GetWeight(32 + 10, 32, 1) && soft.GetWeight(32 + 10, 32, 1) > 0,
                "Soft brush has no falloff");
        // Low strength keeps making progress until the layer is full (no rounding stall).
        for (int dab = 0; dab < 400; ++dab)
            soft.Paint({0.5f, 0.5f}, {0.2f, 0.2f}, 1, {0.05f, 1.0f});
        Require(soft.GetWeight(32, 32, 1) == 255, "Repeated weak dabs stalled short of a full layer");
        Require(EveryTexelSumsTo255(soft), "Repeated dabs broke the weight sum");
    }

    void WeightMapLayerEdits()
    {
        scene::TerrainWeightMap weights(16);
        weights.Paint({0.25f, 0.5f}, {0.3f, 0.3f}, 1, {0.6f, 0.0f});
        weights.Paint({0.75f, 0.5f}, {0.3f, 0.3f}, 2, {0.6f, 0.0f});
        const auto layer2Before = weights.GetWeight(12, 8, 2);

        auto moved = weights;
        moved.MoveLayer(2, 0);
        Require(moved.GetWeight(12, 8, 0) == layer2Before, "Moving a layer did not move its weights");
        moved.MoveLayer(0, 2);
        Require(moved == weights, "Moving a layer back did not restore the map");

        auto removed = weights;
        removed.RemoveLayer(1);
        Require(removed.GetWeight(12, 8, 1) == layer2Before, "Layers above a removed one did not move down");
        Require(EveryTexelSumsTo255(removed), "Removing a layer broke the weight sum");

        auto resized = weights;
        resized.Resize(32);
        Require(resized.GetResolution() == 32 && EveryTexelSumsTo255(resized), "Resizing broke the map");
        Require(std::abs(resized.SampleWeight({0.75f, 0.5f}, 2) - weights.SampleWeight({0.75f, 0.5f}, 2)) < 0.1f,
                "Resizing lost the painting");
        Require(weights.DominantLayer({0.75f, 0.5f}) == 2 && weights.DominantLayer({0.0f, 0.0f}) == 0,
                "Dominant layer query is wrong");

        // Undo regions restore exactly the painted rectangle.
        const auto before = weights;
        const auto rect = weights.Paint({0.5f, 0.5f}, {0.2f, 0.2f}, 3, {1.0f, 1.0f});
        const auto region = before.Capture(rect);
        Require(!(weights == before), "Paint did not change the map");
        weights.Restore(region);
        Require(weights == before, "Restoring the captured region did not undo the dab");

        // Planes round trip (GPU and file layout), and bad sums are repaired.
        scene::TerrainWeightMap planes(1);
        Require(planes.SetPlanes(16, std::vector<std::vector<std::uint8_t>>{weights.GetPlane(0), weights.GetPlane(1)}) && planes == weights,
                "Planes do not round trip");
        std::vector<std::uint8_t> unnormalized(16 * 16 * 4, 0);
        Require(planes.SetPlanes(16, std::vector<std::vector<std::uint8_t>>{unnormalized}) && EveryTexelSumsTo255(planes) &&
                planes.GetWeight(3, 3, 0) == 255, "Empty texels were not given to layer 0");
    }

    void LayerStackRenderData()
    {
        auto red = SolidTexture(8, {255, 0, 0, 255});
        auto blue = SolidTexture(8, {0, 0, 255, 255});
        std::map<std::string, const render::Texture *> textures{{"red", red.get()}, {"blue", blue.get()}};
        std::map<std::string, std::string> files;
        scene::TerrainLayerStack::Services services;
        services.loadTexture = [&](const std::string &reference, render::TextureColorSpace) -> const render::Texture * {
            const auto found = textures.find(reference);
            return found == textures.end() ? nullptr : found->second;
        };
        services.resolvePath = [](const std::string &reference) { return reference; };
        services.readFile = [&](const std::string &path, std::string &bytes) {
            const auto found = files.find(path);
            if (found == files.end())
                return false;
            bytes = found->second;
            return true;
        };
        scene::TerrainLayerStack stack(services);
        Require(!stack.GetRenderSet() && !stack.GetMacroAlbedo(), "An empty stack produced render data");

        scene::TerrainLayer sand;
        sand.albedoMap = "red";
        sand.tileSize = 2.0f;
        scene::TerrainLayer rock;
        rock.albedoMap = "blue";
        Require(stack.AddLayer(sand) && stack.AddLayer(rock), "Adding layers failed");
        stack.SetWeightResolution(32);
        stack.Paint({0.75f, 0.5f}, {0.2f, 0.4f}, 1, {1.0f, 1.0f});

        const auto set = stack.GetRenderSet();
        Require(set && set->layers.size() == 2 && set->layers[0].albedoMap == red.get() && set->layers[0].tileSize == 2.0f,
                "Render set does not describe the layers");
        Require(set->weightMaps[0] && set->weightMaps[0]->GetWidth() == 32, "Weight texture missing");
        Require(stack.GetRenderSet() == set, "Render set was rebuilt without a layer change");

        // Painting updates the weight texture in place (new revision, same set).
        const auto revision = set->weightMaps[0]->GetContentRevision();
        stack.Paint({0.25f, 0.5f}, {0.1f, 0.1f}, 1, {1.0f, 1.0f});
        Require(stack.GetRenderSet() == set && set->weightMaps[0]->GetContentRevision() > revision,
                "Painting did not update the weight texture in place");

        // The macro albedo follows the weights: red where unpainted, blue where layer 1 was painted.
        const auto *macro = stack.GetMacroAlbedo();
        Require(macro && macro->GetWidth() == 32, "Macro albedo missing");
        const auto pixels = macro->GetRgba8Pixels();
        const auto pixel = [&](int x, int y) { return &pixels[(static_cast<std::size_t>(y) * 32 + x) * 4]; };
        Require(pixel(2, 16)[0] > 200 && pixel(2, 16)[2] < 50, "Unpainted macro albedo is not the first layer's colour");
        Require(pixel(24, 16)[2] > 200 && pixel(24, 16)[0] < 50, "Painted macro albedo is not the second layer's colour");

        // Editing a layer makes a new set; arrays are keyed by textures, so the signature is unchanged.
        rock.tint = {0.5f, 0.5f, 0.5f};
        stack.SetLayer(1, rock);
        const auto edited = stack.GetRenderSet();
        Require(edited != set && render::RhiTerrainLayerCache::Signature(*edited) == render::RhiTerrainLayerCache::Signature(*set),
                "A tint edit should reuse the layer arrays");
        stack.SetDetailDistance(-5.0f);
        Require(stack.GetDetailDistance() == 0.0f, "A negative detail distance was not clamped");
        stack.SetDetailDistance(150.0f);
        Require(stack.GetRenderSet() != edited && stack.GetRenderSet()->detailDistance == 150.0f,
                "The detail distance did not reach the render set");

        // Weight maps persist through PNG files, in height-map orientation, and reload exactly.
        stack.SetWeightMapPaths({"weights0.png", "weights1.png"});
        const auto png = scene::TerrainLayerStack::EncodeWeightPlane(stack.GetWeights(), 0);
        files["weights0.png"] = std::string(png.begin(), png.end());
        scene::TerrainLayerStack reloaded(services);
        reloaded.SetLayers(stack.GetLayers());
        reloaded.SetWeightMapPaths(stack.GetWeightMapPaths());
        Require(reloaded.LoadWeightMaps() && reloaded.GetWeights() == stack.GetWeights(), "Weight maps did not round trip");

        // Undo snapshots restore layers and weights together.
        const auto snapshot = stack.Capture();
        stack.RemoveLayer(0);
        Require(stack.GetLayers().size() == 1 && EveryTexelSumsTo255(stack.GetWeights()), "Removing a layer failed");
        stack.Restore(snapshot);
        Require(stack.GetLayers() == snapshot.layers && stack.GetWeights() == snapshot.weights, "Snapshot restore failed");
    }

    void TextureRegionUpdates()
    {
        auto texture = SolidTexture(8, {0, 0, 0, 255});
        const auto start = texture->GetContentRevision();
        const std::vector<unsigned char> white(2 * 3 * 4, 255);
        Require(texture->UpdateRgba8Region({1, 2, 2, 3}, white), "Region update rejected");
        Require(!texture->UpdateRgba8Region({7, 7, 2, 2}, white), "A region outside the texture was accepted");
        Require(texture->GetRgba8Pixels()[(2 * 8 + 1) * 4] == 255 && texture->GetRgba8Pixels()[0] == 0,
                "Region update wrote the wrong texels");
        Require(texture->UpdateRgba8Region({5, 0, 1, 1}, std::vector<unsigned char>(4, 255)), "Second region rejected");
        const auto changed = texture->GetChangedRectSince(start);
        Require(changed && changed->x == 1 && changed->y == 0 && changed->width == 5 && changed->height == 5,
                "Changed rectangle is not the union of the region updates");
        Require(texture->GetChangedRectSince(texture->GetContentRevision())->Empty(), "Nothing changed, but a rectangle was reported");
        // A full replacement in between means the whole texture must be read again.
        const auto beforeFull = texture->GetContentRevision();
        texture->SetRgba8Pixels(std::vector<unsigned char>(8 * 8 * 4, 9));
        Require(!texture->GetChangedRectSince(beforeFull), "A full replacement was reported as a region");
        Require(!texture->GetChangedRectSince(start), "History across a full replacement was reported as a region");

        // Painting a layer stack patches its textures in the painted rectangle only.
        scene::TerrainLayerStack stack;
        stack.AddLayer({});
        stack.AddLayer({});
        stack.SetWeightResolution(64);
        const auto set = stack.GetRenderSet();
        const auto *macro = stack.GetMacroAlbedo();
        const auto weightRevision = set->weightMaps[0]->GetContentRevision();
        const auto macroRevision = macro->GetContentRevision();
        const auto painted = stack.Paint({0.25f, 0.25f}, {0.05f, 0.05f}, 1, {1.0f, 1.0f});
        Require(stack.GetRenderSet() == set && stack.GetMacroAlbedo() == macro, "Painting replaced the render resources");
        const auto weightRect = set->weightMaps[0]->GetChangedRectSince(weightRevision);
        Require(weightRect && weightRect->x == painted.x0 && weightRect->width == painted.x1 - painted.x0,
                "Weight texture was not patched in the painted rectangle");
        const auto macroRect = macro->GetChangedRectSince(macroRevision);
        Require(macroRect && !macroRect->Empty() && macroRect->width < macro->GetWidth(), "Macro albedo was not patched in place");
        stack.Fill(0);
        (void)stack.GetRenderSet(); // refreshes the weight textures
        Require(!set->weightMaps[0]->GetChangedRectSince(weightRevision), "Fill should replace the whole weight texture");
    }

    void ArrayPacking()
    {
        auto albedo = SolidTexture(16, {10, 20, 30, 255});
        auto roughness = SolidTexture(32, {200, 0, 0, 255});
        auto height = SolidTexture(8, {60, 0, 0, 255});
        render::TerrainLayerSet set;
        set.layers.resize(2);
        set.layers[0].albedoMap = albedo.get();
        set.layers[1].roughnessMap = roughness.get();
        set.layers[1].heightMap = height.get();
        const auto pixels = [](const render::Texture &texture) {
            const auto source = texture.GetRgba8Pixels();
            return std::vector<std::byte>(reinterpret_cast<const std::byte *>(source.data()),
                                          reinterpret_cast<const std::byte *>(source.data() + source.size()));
        };
        const auto arrays = render::BuildTerrainLayerArrays(set, pixels);
        Require(arrays.layers == 2 && arrays.size == 32, "Arrays take the largest map's size");
        const std::size_t layerBytes = 32 * 32 * 4;
        Require(arrays.albedo.size() == layerBytes * 2 && arrays.normal.size() == layerBytes * 2, "Array sizes are wrong");
        const auto at = [](const std::vector<std::byte> &data, std::size_t offset) { return std::to_integer<int>(data[offset]); };
        Require(at(arrays.albedo, 0) == 10 && at(arrays.albedo, 2) == 30, "Albedo was not resampled into layer 0");
        Require(at(arrays.albedo, layerBytes) == 255, "A missing albedo is not white");
        Require(at(arrays.normal, 0) == 128 && at(arrays.normal, 2) == 255, "A missing normal map is not flat");
        Require(at(arrays.albedo, 3) == 128 && at(arrays.normal, 3) == 255, "Missing height/roughness defaults are wrong");
        Require(at(arrays.albedo, layerBytes + 3) == 60 && at(arrays.normal, layerBytes + 3) == 200,
                "Height and roughness were not packed into the albedo and normal alpha");
        Require(render::TerrainLayerArraySize(set, 16) == 16, "The array size limit was ignored");
    }
}

int main()
try
{
    WeightMapPainting();
    WeightMapLayerEdits();
    LayerStackRenderData();
    ArrayPacking();
    TextureRegionUpdates();
    std::cout << "Terrain layer tests passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << "Terrain layer tests failed: " << error.what() << '\n';
    return 1;
}
