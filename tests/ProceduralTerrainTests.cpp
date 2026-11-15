// Procedural terrain without a GPU: deterministic noise, rules and stacks, masks and distance sources, the terrain
// surface rules read, the biome definition's text format, biome blending, layer weight baking, foliage scatter
// (determinism, limits, exclusion, tile seams) and the component that applies it all to a terrain and its foliage.
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/ProceduralTerrainComponent.h"
#include "PlutoGE/scene/components/TerrainComponent.h"
#include "PlutoGE/scene/procedural/ProceduralGeneration.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>

using namespace PlutoGE;
using namespace PlutoGE::scene;
using namespace PlutoGE::scene::procedural;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool Near(float a, float b, float tolerance = 1e-4f) { return std::abs(a - b) <= tolerance; }

    // A surface from a height function of local x, z (metres).
    template <typename Height>
    TerrainSurface Surface(int samples, float cellSize, Height &&height, glm::vec3 origin = glm::vec3(0.0f))
    {
        return TerrainSurface(samples, samples, cellSize, glm::translate(glm::mat4(1.0f), origin),
                              [&](int x, int z) { return height(x * cellSize, z * cellSize); });
    }

    void Noise()
    {
        NoiseSettings settings;
        settings.seed = 7;
        const float a = EvaluateNoise(settings, {12.5f, -40.25f}, 3);
        Require(a == EvaluateNoise(settings, {12.5f, -40.25f}, 3), "Noise must be deterministic");
        Require(a != EvaluateNoise(settings, {12.5f, -40.25f}, 4), "The global seed changes the noise");
        float sum = 0.0f, low = 1.0f, high = 0.0f;
        for (int i = 0; i < 2000; ++i)
        {
            const float value = EvaluateNoise(settings, {i * 13.7f, i * 7.3f});
            low = std::min(low, value), high = std::max(high, value), sum += value;
        }
        Require(low >= 0.0f && high <= 1.0f && Near(sum / 2000.0f, 0.5f, 0.08f) && high - low > 0.4f, "Noise covers 0..1 around 0.5");
        Require(std::abs(EvaluateNoise(settings, {100.0f, 100.0f}) - EvaluateNoise(settings, {100.05f, 100.0f})) < 0.01f,
                "Noise is continuous");
        settings.type = NoiseType::Ridged;
        Require(EvaluateNoise(settings, {5.0f, 5.0f}) >= 0.0f, "Ridged noise is in range");
        Require(Hash(1, 2, 3, 4) == Hash(1, 2, 3, 4) && Hash(1, 2, 3, 4) != Hash(1, 2, 3, 5) && HashName("Pine") != HashName("Fir"),
                "Hashes are stable and distinct");
    }

    struct FixedInputs final : IRuleInputs
    {
        float mask = 0.5f, distance = 3.0f;
        float Mask(std::string_view name, glm::vec2) const override { return name == "Forest" ? mask : 0.0f; }
        float Distance(std::string_view, glm::vec2, float limit) const override { return std::min(distance, limit); }
        std::uint32_t Seed() const override { return 1; }
    };

    void Rules()
    {
        FixedInputs inputs;
        TerrainPoint point;
        point.slope = 35.0f;
        point.height = 20.0f;

        auto slope = TerrainRule::Make(RuleInput::Slope); // 30..90, falloff 5
        Require(Near(slope.Evaluate(point, inputs), 1.0f), "Inside the band");
        point.slope = 27.5f;
        Require(Near(slope.Evaluate(point, inputs), 0.5f), "Half way down the falloff");
        point.slope = 20.0f;
        Require(Near(slope.Evaluate(point, inputs), 0.0f), "Outside the falloff");
        slope.invert = true;
        Require(Near(slope.Evaluate(point, inputs), 1.0f), "Inverted");

        auto mask = TerrainRule::Make(RuleInput::Mask);
        mask.source = "Forest";
        Require(Near(mask.Evaluate(point, inputs), 0.5f), "A mask passes through a linear remap");
        mask.min = 0.25f;
        mask.max = 0.75f;
        Require(Near(mask.Evaluate(point, inputs), 0.5f), "Linear remap of the middle");
        mask.source = "Missing";
        Require(mask.Evaluate(point, inputs) == 0.0f, "A missing mask reads 0");

        auto distance = TerrainRule::Make(RuleInput::Distance); // 0..5, falloff 5
        Require(Near(distance.DistanceLimit(), 10.0f) && Near(distance.Evaluate(point, inputs), 1.0f), "Distance band");

        TerrainRuleStack stack;
        Require(stack.Evaluate(point, inputs) == 1.0f, "An empty stack is 1");
        auto half = TerrainRule::Make(RuleInput::Constant);
        half.constant = 0.5f;
        stack.rules.push_back(half);
        Require(Near(stack.Evaluate(point, inputs), 0.5f), "Multiply");
        auto add = half;
        add.blend = RuleBlend::Add;
        add.constant = 0.25f;
        stack.rules.push_back(add);
        Require(Near(stack.Evaluate(point, inputs), 0.75f), "Add");
        auto replace = half;
        replace.blend = RuleBlend::Replace;
        replace.constant = 0.9f;
        replace.opacity = 0.5f;
        stack.rules.push_back(replace);
        Require(Near(stack.Evaluate(point, inputs), 0.825f), "Replace at half opacity blends");
        auto subtract = half;
        subtract.blend = RuleBlend::Subtract;
        subtract.constant = 1.0f;
        stack.rules.push_back(subtract);
        Require(stack.Evaluate(point, inputs) == 0.0f, "The running value is clamped");
        stack.rules.back().enabled = false;
        Require(Near(stack.Evaluate(point, inputs), 0.825f), "Disabled rules are skipped");
        Require(stack.Uses(RuleInput::Constant) && !stack.Uses(RuleInput::Slope), "Uses");
    }

    void Inputs()
    {
        PolylineDistanceSet distances(4.0f);
        const std::vector<glm::vec2> river{{0.0f, 0.0f}, {100.0f, 0.0f}, {100.0f, 100.0f}};
        distances.Add("River", river, 2.0f);
        Require(Near(distances.Distance("River", {50.0f, 10.0f}, 100.0f), 8.0f), "Distance from the edge of a wide polyline");
        Require(distances.Distance("River", {50.0f, 1.0f}, 100.0f) == 0.0f, "Inside the width is 0");
        Require(distances.Distance("River", {50.0f, 60.0f}, 20.0f) == 20.0f, "Beyond the limit reads the limit");
        Require(distances.Distance("Road", {50.0f, 1.0f}, 30.0f) == 30.0f && !distances.Has("Road"), "Unknown sources are far away");
        // The grid finds the same nearest segment as testing every one.
        for (int i = 0; i < 200; ++i)
        {
            const glm::vec2 p(std::fmod(i * 37.1f, 140.0f) - 20.0f, std::fmod(i * 53.3f, 140.0f) - 20.0f);
            float brute = 1e9f;
            for (std::size_t s = 0; s + 1 < river.size(); ++s)
            {
                const glm::vec2 e = river[s + 1] - river[s];
                const float t = std::clamp(glm::dot(p - river[s], e) / glm::dot(e, e), 0.0f, 1.0f);
                brute = std::min(brute, std::max(glm::length(p - (river[s] + e * t)) - 2.0f, 0.0f));
            }
            Require(Near(distances.Distance("River", p, 30.0f), std::min(brute, 30.0f), 1e-3f), "Grid search disagrees with brute force");
        }

        ProceduralMask mask;
        auto values = std::make_shared<TerrainHeightField>(TerrainHeightField{2, 2, {0.0f, 1.0f, 0.0f, 1.0f}});
        mask.values = values;
        mask.worldMin = {100.0f, 100.0f};
        mask.worldSize = {50.0f, 50.0f};
        Require(Near(mask.Sample({125.0f, 125.0f}), 0.5f) && Near(mask.Sample({150.0f, 100.0f}), 1.0f) && mask.Sample({0.0f, 0.0f}) == 0.0f,
                "Masks are placed over their world rectangle and clamp outside it");

        // A 30 degree plane: slope 30 everywhere, curvature 0; a bowl: positive curvature.
        const float rise = std::tan(glm::radians(30.0f));
        const auto plane = Surface(9, 2.0f, [&](float x, float) { return x * rise; }, {10.0f, 5.0f, 0.0f});
        const auto point = plane.PointAt({8.0f, 8.0f});
        Require(Near(point.slope, 30.0f, 0.01f) && Near(point.curvature, 0.0f) && Near(point.height, 5.0f + 8.0f * rise, 1e-3f) &&
                    Near(point.world.x, 18.0f) && Near(point.world.y, 8.0f),
                "Plane surface attributes");
        const auto bowl = Surface(9, 1.0f, [](float x, float z) { return 0.1f * ((x - 4) * (x - 4) + (z - 4) * (z - 4)); });
        Require(bowl.PointAt({4.0f, 4.0f}).curvature > 0.3f && bowl.PointAt({4.0f, 4.0f}).slope < 0.01f, "A bowl's bottom is a hollow");
        Require(Near(glm::length(plane.NormalAt({4.0f, 4.0f})), 1.0f) && plane.NormalAt({4.0f, 4.0f}).x < -0.4f, "Normals lean downhill");
    }

    ProceduralTerrainDefinition TwoBiomes()
    {
        ProceduralTerrainDefinition definition;
        definition.seed = 42;
        definition.biomeBlend = 0.0f;
        ProceduralBiome lowland;
        lowland.name = "Low land";
        auto low = TerrainRule::Make(RuleInput::Height);
        low.min = -100.0f, low.max = 10.0f, low.falloff = 0.0f;
        lowland.weight.rules.push_back(low);
        lowland.layers.push_back({"Grass", {}});
        auto rock = TerrainRule::Make(RuleInput::Slope);
        rock.min = 30.0f, rock.max = 90.0f, rock.falloff = 0.0f;
        lowland.layers.push_back({"Rock", {{rock}}});
        ProceduralScatterRule trees;
        trees.foliageType = "Pine";
        trees.spacing = 4.0f;
        trees.slopeLimits = {0.0f, 25.0f};
        lowland.scatter.push_back(trees);

        ProceduralBiome highland;
        highland.name = "High=land";
        auto high = low;
        high.min = 10.0f, high.max = 1000.0f;
        highland.weight.rules.push_back(high);
        highland.layers.push_back({"Snow", {}});
        definition.biomes = {lowland, highland};
        definition.masks.push_back({"Forest map", "project://Masks/forest.png", 2, {1.0f, 2.0f}, {300.0f, 400.0f}});
        return definition;
    }

    void DefinitionFormat()
    {
        auto definition = TwoBiomes();
        definition.biomes[0].scatter[0].density.rules.push_back(TerrainRule::Make(RuleInput::Noise));
        definition.biomes[0].scatter[0].density.rules.back().noise.seed = 99;
        definition.biomes[0].scatter[0].density.rules.back().source = "a b%c";
        const auto text = definition.Encode();
        const auto decoded = ProceduralTerrainDefinition::Decode(text);
        Require(decoded && *decoded == definition, "The definition must round-trip:\n" + text);
        Require(decoded->ReferencedLayers() == std::vector<std::string>{"Grass", "Rock", "Snow"} &&
                    decoded->ReferencedFoliageTypes() == std::vector<std::string>{"Pine"},
                "Referenced names");
        Require(ProceduralTerrainDefinition::Decode("")->biomes.empty(), "Empty text is an empty definition");
        std::string error;
        Require(!ProceduralTerrainDefinition::Decode("PROCEDURAL version=99\n", &error) && !error.empty(), "Future versions are refused");
        Require(!ProceduralTerrainDefinition::Decode("BIOME name=x\n", &error), "Text without a header is refused");
        const auto future = ProceduralTerrainDefinition::Decode(text + "FUTURE thing=1\nRULE input=Slope future=2\n");
        Require(future && future->biomes.size() == 2, "Unknown records and keys are skipped");
    }

    // A terrain 64 m wide: flat at 2 m for x < 24, then a 45 degree ramp up to 42 m, then flat (the high land).
    float Ramp(float x, float) { return x < 24.0f ? 2.0f : (x < 64.0f ? 2.0f + (x - 24.0f) : 42.0f); }

    void BiomesAndTexturing()
    {
        const auto definition = TwoBiomes();
        const auto surface = Surface(33, 2.0f, Ramp);
        ProceduralInputs inputs;
        const auto biomes = EvaluateBiomeWeights(definition, surface, inputs, 32);
        // Texel 2 (x = 5 m) is low land, texel 30 (x = 61 m) high land.
        Require(Near(biomes.At(0, 2, 10), 1.0f) && Near(biomes.At(1, 2, 10), 0.0f), "Low ground is low land");
        Require(Near(biomes.At(1, 30, 10), 1.0f) && Near(biomes.At(0, 30, 10), 0.0f), "High ground is high land");
        for (int x = 0; x < 32; ++x)
            Require(biomes.At(0, x, 5) + biomes.At(1, x, 5) <= 1.0001f, "Biome weights never sum past 1");

        auto blended = definition;
        blended.biomeBlend = 16.0f;
        const auto soft = EvaluateBiomeWeights(blended, surface, inputs, 32);
        bool sharedTexel = false;
        for (int x = 0; x < 32; ++x)
            sharedTexel |= soft.At(0, x, 5) > 0.1f && soft.At(1, x, 5) > 0.1f;
        Require(sharedTexel, "A blend distance shares texels between neighbouring biomes");

        std::vector<std::string> warnings;
        const std::vector<std::string> layers{"Grass", "Rock", "Snow", "Sand"};
        const auto weights = BakeLayerWeights(definition, biomes, surface, inputs, layers, warnings);
        Require(warnings.empty(), "No warnings expected");
        Require(weights.GetWeight(2, 10, 0) == 255, "Flat low land is grass");
        // Texel 13 is x = 27 m: 5 m up the 45 degree ramp, still low land.
        Require(weights.GetWeight(13, 10, 1) == 255, "The ramp below 10 m is rock: " + std::to_string(weights.GetWeight(13, 10, 1)));
        Require(weights.GetWeight(30, 10, 2) == 255, "High land is snow");
        for (int x = 0; x < 32; ++x)
        {
            int sum = 0;
            for (std::size_t layer = 0; layer < TerrainWeightMap::kLayers; ++layer)
                sum += weights.GetWeight(x, 7, layer);
            Require(sum == 255, "Weights sum to 255");
        }

        auto unknown = definition;
        unknown.biomes[1].layers[0].layer = "Lava";
        warnings.clear();
        const auto partial = BakeLayerWeights(unknown, biomes, surface, inputs, layers, warnings);
        Require(warnings.size() == 1 && warnings[0].find("Lava") != std::string::npos, "Unknown layers warn");
        Require(partial.GetWeight(30, 10, 0) == 255, "A biome that paints nothing known leaves the base layer");
    }

    void Scattering()
    {
        const auto definition = TwoBiomes();
        const auto surface = Surface(33, 2.0f, Ramp);
        ProceduralInputs inputs;
        const auto biomes = EvaluateBiomeWeights(definition, surface, inputs, 32);
        std::vector<std::string> warnings;
        const std::vector<std::string> types{"Rock", "Pine"};
        const auto first = Scatter(definition, biomes, surface, inputs, types, warnings);
        const auto second = Scatter(definition, biomes, surface, inputs, types, warnings);
        Require(first == second, "Scatter must be deterministic");
        Require(first[0].empty() && !first[1].empty(), "Only the pines grow");
        std::set<std::uint64_t> ids;
        for (const auto &instance : first[1])
        {
            Require(IsProceduralFoliageInstanceId(instance.id) && ids.insert(instance.id).second, "IDs are procedural and unique");
            Require(instance.position.x < 24.5f, "Pines stay off the ramp (slope limit) and out of the high land");
            Require(Near(instance.position.y, 2.0f, 1e-3f), "Pines stand on the ground");
            Require(instance.scale.x >= 0.8f && instance.scale.x <= 1.2f, "Scale range");
        }
        for (std::size_t a = 0; a < first[1].size(); ++a)
            for (std::size_t b = a + 1; b < first[1].size(); ++b)
                Require(glm::distance(glm::vec2(first[1][a].position.x, first[1][a].position.z),
                                      glm::vec2(first[1][b].position.x, first[1][b].position.z)) >= 4.0f - 1e-3f,
                        "Footprints keep instances apart");

        auto sparse = definition;
        sparse.biomes[0].scatter[0].densityScale = 0.0f;
        Require(Scatter(sparse, biomes, surface, inputs, types, warnings)[1].empty(), "Zero density grows nothing");
        warnings.clear();
        (void)Scatter(definition, biomes, surface, inputs, {"Rock"}, warnings);
        Require(warnings.size() == 1 && warnings[0].find("Pine") != std::string::npos, "Missing foliage types warn");

        // Two tiles side by side grow what one terrain over both does: candidates sit on the world grid.
        auto seamless = definition;
        seamless.biomes[0].scatter[0].footprint = 0.01f;
        seamless.biomes[0].scatter[0].slopeLimits = {0.0f, 90.0f};
        const auto flat = [](float, float) { return 1.0f; };
        const auto whole = Surface(65, 1.0f, flat);
        const auto left = Surface(33, 1.0f, flat);
        const auto right = Surface(33, 1.0f, flat, {32.0f, 0.0f, 0.0f});
        const auto grown = [&](const TerrainSurface &tile, glm::vec2 offset) {
            std::vector<std::string> ignored;
            const auto weights = EvaluateBiomeWeights(seamless, tile, inputs, 16);
            std::set<std::pair<int, int>> positions;
            for (const auto &instance : Scatter(seamless, weights, tile, inputs, types, ignored)[1])
                positions.insert({static_cast<int>(std::lround((instance.position.x + offset.x) * 100.0f)),
                                  static_cast<int>(std::lround((instance.position.z + offset.y) * 100.0f))});
            return positions;
        };
        auto tiles = grown(left, {0.0f, 0.0f});
        const auto rightTile = grown(right, {32.0f, 0.0f});
        for (const auto &position : rightTile)
            Require(tiles.insert(position).second, "An instance was grown by both tiles");
        auto all = grown(whole, {0.0f, 0.0f});
        // The whole terrain's far edges are inclusive of 64 m only through the right tile's half-open edge; compare
        // what both cover.
        std::erase_if(all, [](const auto &position) { return position.first >= 6400 || position.second >= 3200; });
        std::erase_if(tiles, [](const auto &position) { return position.second >= 3200; });
        Require(all == tiles, "Tiles must continue each other's scatter");
    }

    void ComponentGeneration()
    {
        Scene scene;
        auto *entity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Terrain"}));
        auto *terrain = entity->CreateComponent<TerrainComponent>(TerrainComponentConfig{.width = 33, .depth = 33, .cellSize = 2.0f, .heightScale = 100.0f});
        TerrainHeightData data;
        data.field = {33, 33, std::vector<float>(33 * 33)};
        for (int z = 0; z < 33; ++z)
            for (int x = 0; x < 33; ++x)
                data.field.At(x, z) = Ramp(x * 2.0f, z * 2.0f);
        terrain->SetHeightData(data);
        terrain->Deserialize({{"SurfaceSmoothing", PropertyType::Float, "0"}});
        for (const char *name : {"Grass", "Rock", "Snow"})
        {
            TerrainLayer layer;
            layer.name = name;
            terrain->GetLayers().AddLayer(layer);
        }
        auto *foliage = entity->CreateComponent<FoliageComponent>();
        foliage->AddType("Pine");
        auto manual = foliage->CaptureInstanceSnapshot();
        manual[0].push_back({5, {1.0f, 2.0f, 1.0f}, {}, glm::vec3(1.0f)});
        foliage->RestoreInstanceSnapshot(manual);

        auto *procedural = entity->CreateComponent<ProceduralTerrainComponent>();
        procedural->SetDefinition(TwoBiomes());
        procedural->SetWeightResolution(32);
        const auto result = procedural->Generate();
        Require(result.weights.has_value(), "Weights were generated");
        bool maskWarning = false;
        for (const auto &warning : result.warnings)
            maskWarning |= warning.find("Forest map") != std::string::npos;
        Require(maskWarning, "A mask that cannot load is reported");
        Require(terrain->GetLayers().GetWeights().GetResolution() == 32 && terrain->GetLayers().GetWeights().GetWeight(30, 10, 2) == 255,
                "The terrain took the generated weights");

        const auto *type = foliage->GetType(0);
        const auto count = type->instances.size();
        Require(count > 1 && result.InstanceCount() == count - 1, "Generated pines join the foliage");
        Require(std::any_of(type->instances.begin(), type->instances.end(), [](const FoliageInstance &instance) { return instance.id == 5; }),
                "Hand-placed foliage is kept");
        procedural->Generate();
        Require(foliage->GetType(0)->instances.size() == count, "Regenerating replaces, rather than adds to, procedural foliage");

        // A manual instance placed after generation still counts up from the hand-placed IDs.
        foliage->SetSelectedTypeIndex(0);
        auto next = foliage->CaptureInstanceSnapshot();
        next[0].push_back({0, {3.0f, 2.0f, 3.0f}, {}, glm::vec3(1.0f)});
        foliage->RestoreInstanceSnapshot(next);
        bool counted = false;
        for (const auto &instance : foliage->GetType(0)->instances)
            counted |= !IsProceduralFoliageInstanceId(instance.id) && instance.id != 5 && instance.id < 100;
        Require(counted, "Procedural IDs must not move the hand-placed ID count");

        Scene other;
        auto *copyEntity = other.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Copy"}));
        auto *copy = copyEntity->CreateComponent<ProceduralTerrainComponent>();
        procedural->SetGenerateOnLoad(true);
        copy->Deserialize(procedural->Serialize());
        Require(copy->GetDefinition() == procedural->GetDefinition() && copy->GetGenerateOnLoad() && copy->GetWeightResolution() == 32,
                "The component round-trips");
        Require(!copy->Prepare(), "Without a terrain there is nothing to prepare");
    }
}

int main()
{
    try
    {
        Noise();
        Rules();
        Inputs();
        DefinitionFormat();
        BiomesAndTexturing();
        Scattering();
        ComponentGeneration();
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAILED: " << error.what() << "\n";
        return 1;
    }
    std::cout << "Procedural terrain tests passed.\n";
    return 0;
}
