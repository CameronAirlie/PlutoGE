// Water effects without a GPU (docs/WATER.md): the surface snapshot, ripple field, interaction tracker, effect profile,
// foliage water habitats (brush, asset files, procedural scatter and the Water Depth rule), the runtime WaterSystem
// with physics bodies and its extension point, and the ripple data the ocean renderer receives.
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/AssetType.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/RhiOcean.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/FoliageTypeAsset.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/WaterBodies.h"
#include "PlutoGE/scene/components/ColliderComponent.h"
#include "PlutoGE/scene/components/FoliageComponent.h"
#include "PlutoGE/scene/components/OceanComponent.h"
#include "PlutoGE/scene/components/ParticleSystemComponent.h"
#include "PlutoGE/scene/components/RigidbodyComponent.h"
#include "PlutoGE/scene/components/WaterEffectsComponent.h"
#include "PlutoGE/scene/components/WaterInteractorComponent.h"
#include "PlutoGE/scene/procedural/ProceduralGeneration.h"
#include "PlutoGE/scene/water/WaterHabitat.h"
#include "PlutoGE/scene/water/WaterInteraction.h"
#include "PlutoGE/scene/water/WaterResponders.h"
#include "PlutoGE/scene/water/WaterRipples.h"
#include "PlutoGE/scene/water/WaterSurfaceSnapshot.h"
#include "PlutoGE/scene/water/WaterSystem.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

using namespace PlutoGE;
using namespace PlutoGE::scene;
using namespace PlutoGE::scene::water;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    bool Near(float a, float b, float tolerance = 1e-3f) { return std::abs(a - b) <= tolerance; }

    // A lake: an ocean at height `level` whose only water is the strip x in [0, 32] (Invert Area Mask).
    Entity *AddLake(Scene &scene, float level)
    {
        auto *entity = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Lake"}));
        entity->SetPosition({0.0f, level, 0.0f});
        auto *ocean = entity->CreateComponent<OceanComponent>();
        ocean->ApplyLakePreset();
        ocean->SetAreas({{{{0.0f, -100.0f}, {32.0f, -100.0f}, {32.0f, 100.0f}, {0.0f, 100.0f}}}});
        ocean->SetInvertAreaMask(true);
        return entity;
    }

    void TestHabitat()
    {
        const WaterHabitat anywhere;
        Require(anywhere.Accepts(std::nullopt) && anywhere.Accepts(5.0f) && !anywhere.NeedsWater(), "Anywhere accepts everything");
        const WaterHabitat dry{.mode = WaterHabitatMode::DryLand};
        Require(dry.Accepts(std::nullopt) && dry.Accepts(-2.0f) && dry.Accepts(0.0f) && !dry.Accepts(0.1f), "Dry land rejects water");
        const WaterHabitat reeds{.mode = WaterHabitatMode::Underwater, .minDepth = 0.0f, .maxDepth = 0.6f};
        Require(reeds.Accepts(0.3f) && !reeds.Accepts(0.8f) && !reeds.Accepts(std::nullopt) && !reeds.Accepts(-0.1f) && !reeds.Accepts(0.0f),
                "Underwater needs water within the depths");
        WaterHabitat broken{.mode = WaterHabitatMode::Underwater, .minDepth = 5.0f, .maxDepth = -1.0f};
        SanitizeWaterHabitat(broken);
        Require(broken.minDepth == 5.0f && broken.maxDepth == 5.0f, "Sanitizing orders the depths");
        Require(ParseWaterHabitatMode(WaterHabitatModeName(WaterHabitatMode::Underwater)) == WaterHabitatMode::Underwater &&
                    ParseWaterHabitatMode("nonsense") == WaterHabitatMode::Anywhere,
                "Habitat names round trip; unknown names grow anywhere");
    }

    void TestSnapshot()
    {
        Scene scene;
        auto *lake = AddLake(scene, 2.0f);
        auto *ocean = lake->GetComponent<OceanComponent>();
        ocean->Update(3.0f); // waves move away from the still level

        const auto snapshot = WaterSurfaceSnapshot::Capture(scene);
        Require(snapshot.BodyCount() == 1 && snapshot.BodyEntities().front() == lake->GetID(), "The lake is captured");
        const auto still = snapshot.Sample({10.0f, 0.0f, 5.0f}, WaterSampleMode::Still);
        Require(still && Near(still->height, 2.0f) && Near(still->stillHeight, 2.0f) && still->entity == lake->GetID(),
                "The still surface is the entity's height");
        const auto animated = snapshot.Sample({10.0f, 0.0f, 5.0f});
        const auto expected = SampleWaterSurface(scene, {10.0f, 0.0f, 5.0f});
        Require(animated && expected && Near(animated->height, expected->height) && Near(animated->stillHeight, 2.0f),
                "Animated samples match the scene query");
        Require(!snapshot.Sample({40.0f, 0.0f, 5.0f}), "Outside the lake's area is dry");
        Require(Near(*snapshot.StillDepth({10.0f, 5.0f}, 0.5f), 1.5f) && !snapshot.StillDepth({40.0f, 5.0f}, 0.0f),
                "Still depth is the water above the ground, nothing on dry land");
        const auto shore = snapshot.NearestSurfacePoint(lake->GetID(), {40.0f, 7.0f, 5.0f});
        Require(shore && Near(shore->x, 32.0f) && Near(shore->y, 2.0f) && Near(shore->z, 5.0f), "The nearest water is on the shore");
        const auto over = snapshot.NearestSurfacePoint(lake->GetID(), {10.0f, 7.0f, 5.0f});
        Require(over && Near(over->x, 10.0f) && Near(over->y, 2.0f), "Over the water, the point straight below");
        Require(!snapshot.NearestSurfacePoint(9999, {}), "Unknown bodies have no point");

        ocean->SetEnabled(false);
        Require(WaterSurfaceSnapshot::Capture(scene).Empty(), "Disabled bodies are not captured");
    }

    void TestRipples()
    {
        WaterRippleField field;
        field.SetSettings({.strength = 0.1f, .speed = 2.0f, .wavelength = 0.5f, .lifetime = 2.0f});
        field.Emit({1.0f, 1.0f}, 1.0f);
        Require(field.GetRipples().size() == 1, "A ring was emitted");
        Require(Near(field.SampleHeight({1.0f, 1.0f}), 0.1f, 1e-4f), "A fresh ring peaks at its centre with the strength");

        field.Advance(0.5f); // radius 1 m
        // The analytic gradient matches finite differences.
        for (const glm::vec2 point : {glm::vec2(1.9f, 1.2f), glm::vec2(1.0f, 2.05f), glm::vec2(0.3f, 0.6f)})
        {
            const float h = 1e-3f;
            const glm::vec2 numeric((field.SampleHeight(point + glm::vec2(h, 0)) - field.SampleHeight(point - glm::vec2(h, 0))) / (2 * h),
                                    (field.SampleHeight(point + glm::vec2(0, h)) - field.SampleHeight(point - glm::vec2(0, h))) / (2 * h));
            const glm::vec2 analytic = field.SampleGradient(point);
            Require(glm::length(numeric - analytic) < 2e-3f, "Ripple gradient matches finite differences");
        }
        Require(std::abs(field.SampleHeight({2.0f, 1.0f})) > std::abs(field.SampleHeight({1.0f, 1.0f})) * 2.0f,
                "The crest travels outward with the ring");

        std::array<PackedWaterRipple, MaxRenderedRipples> packed{};
        Require(field.Pack(packed) == 1 && Near(packed[0].z, 1.0f) && packed[0].w > 0.0f && packed[0].w < 0.1f,
                "Packed rings carry their radius and faded amplitude");

        field.Advance(1.6f);
        Require(field.Empty(), "Rings disappear after their lifetime");
        field.Advance(-1.0f);
        field.Advance(std::nanf(""));

        for (int i = 0; i < 40; ++i)
            field.Emit({static_cast<float>(i), 0.0f}, 0.1f);
        Require(field.GetRipples().size() == MaxRenderedRipples, "The field holds at most the rendered ring count");
        field.Emit({100.0f, 0.0f}, 3.0f);
        Require(std::any_of(field.GetRipples().begin(), field.GetRipples().end(), [](const WaterRipple &r) { return r.center.x == 100.0f; }),
                "A strong ring replaces the faintest when full");
        field.Emit({200.0f, 0.0f}, 0.001f);
        Require(std::none_of(field.GetRipples().begin(), field.GetRipples().end(), [](const WaterRipple &r) { return r.center.x == 200.0f; }),
                "A fainter ring than all the others is dropped when full");

        WaterRippleField off;
        off.SetSettings({.enabled = false});
        off.Emit({0, 0}, 1.0f);
        off.Emit({std::nanf(""), 0}, 1.0f);
        Require(off.Empty(), "Disabled fields and invalid positions make no rings");
    }

    // Flat water at y = 0 everywhere x < 50, flowing +x at 1 m/s.
    std::optional<WaterSurfaceSample> FlatWater(glm::vec3 position)
    {
        if (position.x >= 50.0f)
            return std::nullopt;
        WaterSurfaceSample sample;
        sample.entity = 7;
        sample.flow = {1.0f, 0.0f, 0.0f};
        return sample;
    }

    void TestTracker()
    {
        WaterInteractionTracker tracker;
        std::vector<WaterInteractionEvent> events;
        WaterProbe probe{.entity = 1, .center = {0.0f, 2.0f, 0.0f}, .velocity = {1.0f, -6.0f, 0.0f}, .radius = 0.5f};
        tracker.Update(std::span(&probe, 1), FlatWater, 1.0f / 60.0f, events);
        Require(events.empty() && !tracker.IsTouchingWater(1), "Above the water: nothing");
        probe.center.y = 0.2f;
        tracker.Update(std::span(&probe, 1), FlatWater, 1.0f / 60.0f, events);
        Require(events.size() == 1 && events[0].type == WaterInteractionType::Enter && events[0].waterEntity == 7 &&
                    Near(events[0].position.y, 0.0f) && Near(events[0].intensity, 1.0f, 0.05f) && Near(events[0].velocity.x, 0.0f),
                "Breaking the surface at 6 m/s splashes with intensity ~1, velocity relative to the flow");
        Require(tracker.IsTouchingWater(1), "The body touches the water");

        // Bobbing straight back out within the cooldown makes no exit splash.
        events.clear();
        probe.center.y = 1.0f;
        probe.velocity = {1.0f, 3.0f, 0.0f};
        tracker.Update(std::span(&probe, 1), FlatWater, 0.05f, events);
        Require(events.empty(), "No second splash within the cooldown");
        probe.center.y = 0.2f;
        probe.velocity = {1.0f, -3.0f, 0.0f};
        tracker.Update(std::span(&probe, 1), FlatWater, 0.5f, events);
        events.clear();
        probe.center.y = 1.0f;
        probe.velocity = {1.0f, 3.0f, 0.0f};
        tracker.Update(std::span(&probe, 1), FlatWater, 0.5f, events);
        Require(events.size() == 1 && events[0].type == WaterInteractionType::Exit, "Leaving upward fast splashes after the cooldown");

        // Wading along the surface leaves a wake every half metre.
        events.clear();
        WaterProbe wader{.entity = 2, .center = {0.0f, 0.3f, 0.0f}, .velocity = {0.0f, 0.0f, 3.0f}, .radius = 0.9f};
        int wakes = 0;
        for (int frame = 0; frame < 60; ++frame)
        {
            tracker.Update(std::span(&wader, 1), FlatWater, 1.0f / 60.0f, events);
            wader.center.z += 3.0f / 60.0f;
        }
        for (const auto &event : events)
            wakes += event.type == WaterInteractionType::Wake ? 1 : 0;
        Require(wakes >= 3 && wakes <= 6 && events.size() == static_cast<std::size_t>(wakes),
                "3 m of wading (flow-relative speed ~3.2 m/s) leaves a wake about every 0.68 m: " + std::to_string(wakes));
        Require(tracker.TrackedCount() == 1, "Bodies missing from an update are forgotten");

        // Spawned underwater, slowly entering and walking ashore: no events.
        events.clear();
        WaterProbe diver{.entity = 3, .center = {0.0f, -5.0f, 0.0f}, .velocity = {0.0f, -9.0f, 0.0f}};
        tracker.Update(std::span(&diver, 1), FlatWater, 1.0f / 60.0f, events);
        WaterProbe stroller{.entity = 4, .center = {0.0f, 0.6f, 0.0f}, .velocity = {0.0f, -0.2f, 0.0f}};
        tracker.Update(std::span(&stroller, 1), FlatWater, 1.0f / 60.0f, events);
        stroller.center.y = 0.4f;
        tracker.Update(std::span(&stroller, 1), FlatWater, 1.0f / 60.0f, events);
        stroller.center = {60.0f, 0.4f, 0.0f};
        stroller.velocity = {0.0f, 2.0f, 0.0f};
        tracker.Update(std::span(&stroller, 1), FlatWater, 1.0f / 60.0f, events);
        Require(events.empty(), "First sight under water, slow entry and walking ashore make no splash");
    }

    void TestBuiltinSplash()
    {
        const auto reference = std::string(assets::kBuiltinWaterSplashParticlesReference);
        const auto builtins = assets::Project::GetBuiltinAssetReferences();
        Require(std::find(builtins.begin(), builtins.end(), reference) != builtins.end(), "The splash is listed with the engine's built-ins");
        Require(assets::Project::GetAssetTypeForReference(reference) == assets::ProjectAssetType::ParticleSystem,
                "The splash is classified as a particle system (pickers offer it)");
        bool loaded = false;
        const auto asset = core::Engine::GetInstance().GetAssetManager().LoadParticleSystemAsset(reference, &loaded);
        Require(loaded && asset.shape == assets::ParticleShape::Cone && asset.gravityModifier > 0.0f &&
                    asset.simulationSpace == assets::ParticleSimulationSpace::World && asset.emissionRateOverTime == 0.0f,
                "The built-in splash loads without a file: droplets thrown up in a cone that fall back");
        Require(!assets::FindBuiltinParticleSystemAsset("engine://builtin/particles/unknown"), "Unknown built-ins do not exist");

        const WaterEffectsProfile profile;
        Require(profile.enterSplash.particles == reference && profile.exitSplash.particles == reference &&
                    profile.wakeSplash.particles == reference,
                "New profiles splash with the built-in");
        WaterEffectsComponent none;
        none.Deserialize({{"EnterSplash.Particles", PropertyType::String, ""}});
        Require(none.GetProfile().enterSplash.particles.empty(), "An empty reference still means no particles");
    }

    void TestProfile()
    {
        WaterEffectsProfile profile;
        profile.enterSplash = {.particles = "project://Splash.plutoparticle", .heavyParticles = "project://BigSplash.plutoparticle",
                               .minCount = 4, .maxCount = 20};
        profile.enterSound = {.sound = "project://Plop.wav", .heavySound = "", .volume = 1.0f, .pitchVariation = 0.1f};
        auto splash = ChooseSplash(profile, WaterInteractionType::Enter, 0.5f);
        Require(splash.particles && *splash.particles == "project://Splash.plutoparticle" && splash.count == 12, "Light splash");
        splash = ChooseSplash(profile, WaterInteractionType::Enter, 2.0f);
        Require(splash.particles && *splash.particles == "project://BigSplash.plutoparticle" && splash.count == 36, "Heavy splash");
        Require(!ChooseSplash(profile, WaterInteractionType::Enter, 0.01f).particles, "Below the minimum: rings only");
        profile.exitSplash.particles.clear();
        Require(!ChooseSplash(profile, WaterInteractionType::Exit, 1.0f).particles, "Empty particles play nothing");
        const auto sound = ChooseSound(profile, WaterInteractionType::Enter, 2.0f, 1.0f);
        Require(sound.clip && *sound.clip == "project://Plop.wav" && Near(sound.volume, std::sqrt(2.0f)) && Near(sound.pitch, 1.05f),
                "Heavy events without a heavy sound use the sound, louder and lower");

        // The component's properties round trip, sanitize and leave unnamed fields alone.
        WaterEffectsComponent component;
        profile.ripples.strength = 0.2f;
        profile.ambientSound = "project://Surf.ogg";
        profile.wakeSound.maxDistance = -5.0f;
        component.SetProfile(profile);
        Require(component.GetProfile().wakeSound.maxDistance >= component.GetProfile().wakeSound.minDistance, "Profiles are sanitized");
        WaterEffectsComponent copy;
        copy.Deserialize(component.Serialize());
        Require(copy.GetProfile() == component.GetProfile(), "Water effects round trip");
        copy.Deserialize({{"Ripples.Strength", PropertyType::Float, "0.05"}});
        Require(Near(copy.GetProfile().ripples.strength, 0.05f) && copy.GetProfile().ambientSound == "project://Surf.ogg",
                "Partial property lists change only what they name");
        Require(Near(copy.GetRipples().GetSettings().strength, 0.05f), "Ripple settings reach the live field");
        Require(WaterEffectsComponent::IsAssetPathProperty("EnterSplash.Particles") && WaterEffectsComponent::IsAssetPathProperty("Ambient.Sound") &&
                    !WaterEffectsComponent::IsAssetPathProperty("Ambient.Volume"),
                "Asset properties are recognised");

        WaterInteractorComponent interactor;
        interactor.SetRadius(0.8f);
        interactor.SetCenterOffset({0.0f, 0.9f, 0.0f});
        interactor.SetChannels({.ripples = true, .splashes = false, .sounds = true});
        WaterInteractorComponent interactorCopy;
        interactorCopy.Deserialize(interactor.Serialize());
        Require(Near(interactorCopy.GetRadius(), 0.8f) && Near(interactorCopy.GetCenterOffset().y, 0.9f) && !interactorCopy.GetChannels().splashes,
                "Water interactors round trip");
    }

    void TestFoliageAssetHabitat()
    {
        FoliageTypeAsset asset;
        asset.waterHabitat = {.mode = WaterHabitatMode::Underwater, .minDepth = 0.2f, .maxDepth = 3.0f};
        std::stringstream file;
        Require(WriteFoliageTypeAsset(file, asset), "Write a foliage type");
        FoliageTypeAsset loaded;
        Require(ReadFoliageTypeAsset(file, loaded) && loaded.waterHabitat == asset.waterHabitat, "The habitat round trips through the file");

        std::stringstream old("PLUTOFOLIAGE\t2\nSCALE_RANGE\t1\t2\n");
        FoliageTypeAsset legacy;
        Require(ReadFoliageTypeAsset(old, legacy) && legacy.waterHabitat.mode == WaterHabitatMode::Anywhere, "Older files grow anywhere");

        const auto changed = ChangedFoliageTypeProperties(FoliageTypeAsset{}, asset);
        Require(std::find(changed.begin(), changed.end(), "WaterHabitat") != changed.end(), "Habitat edits are prefab overrides");

        FoliageComponent foliage;
        foliage.AddType("Reeds").asset.waterHabitat = asset.waterHabitat;
        FoliageComponent restored;
        restored.Deserialize(foliage.Serialize());
        Require(restored.GetTypeCount() == 1 && restored.GetType(0)->asset.waterHabitat == asset.waterHabitat,
                "The habitat round trips through the component");
    }

    void TestFoliageBrushHabitat()
    {
        Scene scene;
        AddLake(scene, 1.0f);
        auto *ground = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Ground"}));
        auto *foliage = ground->CreateComponent<FoliageComponent>();
        render::MeshConfig config{};
        render::Mesh mesh(config);
        foliage->SetMesh(&mesh);
        const auto flat = [](float, float) -> std::optional<FoliageSurfaceSample> { return FoliageSurfaceSample{0.0f, {0, 1, 0}}; };
        const FoliageBrush brush{.mode = FoliageBrushMode::Add, .radius = 20.0f, .density = 100}; // the brush caps density at 100

        foliage->GetSelectedType()->asset.waterHabitat = {.mode = WaterHabitatMode::Underwater, .minDepth = 0.5f, .maxDepth = 2.0f};
        Require(foliage->ApplyBrush(brush, {32.0f, 0.0f, 0.0f}, flat), "Underwater plants paint in the lake");
        for (const auto &instance : foliage->GetInstances())
            Require(instance.position.x >= 0.0f && instance.position.x <= 32.0f, "Underwater plants stay in the water");
        const auto wet = foliage->GetSelectedTypeInstanceCount();

        foliage->ApplyBrush({.mode = FoliageBrushMode::Remove, .radius = 100.0f}, {32.0f, 0.0f, 0.0f}, flat);
        foliage->GetSelectedType()->asset.waterHabitat = {.mode = WaterHabitatMode::DryLand};
        Require(foliage->ApplyBrush(brush, {32.0f, 0.0f, 0.0f}, flat), "Land plants paint ashore");
        for (const auto &instance : foliage->GetInstances())
            Require(instance.position.x > 32.0f || instance.position.x < 0.0f, "Land plants stay out of the water");
        Require(wet > 25 && foliage->GetSelectedTypeInstanceCount() > 25, "Both halves of the brush grew: " + std::to_string(wet) + ", " + std::to_string(foliage->GetSelectedTypeInstanceCount()));

        foliage->ApplyBrush({.mode = FoliageBrushMode::Remove, .radius = 100.0f}, {32.0f, 0.0f, 0.0f}, flat);
        foliage->GetSelectedType()->asset.waterHabitat = {.mode = WaterHabitatMode::Underwater, .minDepth = 5.0f, .maxDepth = 10.0f};
        Require(!foliage->ApplyBrush(brush, {32.0f, 0.0f, 0.0f}, flat), "Too shallow for a deep-water plant");
    }

    void TestProcedural()
    {
        using namespace procedural;
        Scene scene;
        AddLake(scene, 1.0f);
        auto inputs = std::make_shared<ProceduralInputs>();
        inputs->SetWater(std::make_shared<const WaterSurfaceSnapshot>(WaterSurfaceSnapshot::Capture(scene)));
        Require(inputs->HasWater(), "Inputs hold the lake");

        // The Water Depth input.
        const auto rule = TerrainRule::Make(RuleInput::WaterDepth);
        Require(ParseRuleInput(ToString(RuleInput::WaterDepth)) == RuleInput::WaterDepth, "Water Depth's name round trips");
        Require(Near(rule.Evaluate({.world = {10.0f, 0.0f}, .height = 0.0f}, *inputs), 1.0f) &&
                    Near(rule.Evaluate({.world = {40.0f, 0.0f}, .height = 0.0f}, *inputs), 0.0f) &&
                    Near(rule.Evaluate({.world = {10.0f, 0.0f}, .height = 1.5f}, *inputs), 0.0f),
                "Water Depth reads the still water above the ground (none above the water's level)");
        const float shallow = rule.Evaluate({.world = {10.0f, 0.0f}, .height = 0.9f}, *inputs);
        Require(shallow > 0.0f && shallow < 1.0f, "The first 25 cm of water ease in");
        ProceduralInputs dry;
        Require(Near(rule.Evaluate({.world = {10.0f, 0.0f}}, dry), 0.0f), "Without water everywhere reads as dry");

        // Scatter: a flat 64 m terrain at height 0, the lake over its first half.
        TerrainSurface surface(65, 65, 1.0f, glm::mat4(1.0f), [](int, int) { return 0.0f; });
        ProceduralTerrainDefinition definition;
        ProceduralBiome biome;
        biome.scatter.push_back({.foliageType = "Reeds", .spacing = 2.0f, .footprint = 0.01f});
        biome.scatter.push_back({.foliageType = "Grass", .spacing = 2.0f, .footprint = 0.01f});
        definition.biomes.push_back(biome);
        ProceduralGenerationInput input;
        input.definition = &definition;
        input.surface = &surface;
        input.inputs = inputs.get();
        input.foliageTypes = {"Reeds", "Grass"};
        input.foliageHabitats = {{.mode = WaterHabitatMode::Underwater, .maxDepth = 2.0f}, {.mode = WaterHabitatMode::DryLand}};
        input.texture = false;
        const auto result = Generate(input);
        Require(result.instances.size() == 2 && result.instances[0].size() > 50 && result.instances[1].size() > 50,
                "Both types grow: " + std::to_string(result.instances[0].size()) + ", " + std::to_string(result.instances[1].size()));
        for (const auto &reed : result.instances[0])
            Require(reed.position.x <= 32.0f, "Reeds grow under water only");
        for (const auto &grass : result.instances[1])
            Require(grass.position.x >= 32.0f, "Grass grows on dry land only");
        const auto again = Generate(input);
        Require(again.instances == result.instances, "Habitat scatter is deterministic");

        input.inputs = &dry;
        const auto withoutWater = Generate(input);
        Require(withoutWater.instances[0].empty() && !withoutWater.warnings.empty(), "Underwater types without water warn and grow nowhere");
    }

    // Counts what a gameplay listener hears: the extension point.
    struct CountingListener final : IWaterInteractionListener
    {
        std::map<WaterInteractionType, int> counts;
        int frames = 0;
        void OnWaterInteraction(Scene &, const WaterInteractionEvent &event) override { ++counts[event.type]; }
        void OnWaterFrame(Scene &, float) override { ++frames; }
    };

    void TestRuntime()
    {
        Scene scene;
        auto *sea = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Sea"}));
        auto *ocean = sea->CreateComponent<OceanComponent>();
        ocean->ApplyLakePreset();
        ocean->SetAreas({});
        ocean->SetInvertAreaMask(false); // the whole plane is water
        auto *effects = sea->CreateComponent<WaterEffectsComponent>();
        auto profile = effects->GetProfile();
        profile.enterSplash.particles = "project://Missing.plutoparticle";
        effects->SetProfile(profile);

        auto *ball = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Ball"}));
        ball->SetPosition({0.0f, 4.0f, 0.0f});
        ball->CreateComponent<ColliderComponent>(ColliderComponentConfig{.shape = ColliderShape::Sphere, .radius = 0.5f});
        ball->CreateComponent<RigidbodyComponent>();

        auto *ignored = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Ignored"}));
        ignored->SetPosition({10.0f, 4.0f, 0.0f});
        ignored->CreateComponent<ColliderComponent>(ColliderComponentConfig{.shape = ColliderShape::Sphere, .radius = 0.5f});
        ignored->CreateComponent<RigidbodyComponent>();
        ignored->CreateComponent<WaterInteractorComponent>()->SetEnabled(false);

        auto *wader = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Wader"}));
        wader->SetPosition({-10.0f, 0.0f, 0.0f});
        auto *interactor = wader->CreateComponent<WaterInteractorComponent>();
        interactor->SetRadius(0.9f);
        interactor->SetCenterOffset({0.0f, 0.1f, 0.0f});

        auto listener = std::make_shared<CountingListener>();
        auto &water = scene.GetWaterSystem();
        water.AddListener(listener);
        Require(water.GetListeners().size() == 4, "Three built-in responders and the gameplay listener");

        scene.StartRuntime();
        std::size_t mostRings = 0;
        for (int frame = 0; frame < 90; ++frame)
        {
            wader->SetPosition(wader->GetWorldPosition() + glm::vec3(0.0f, 0.0f, 2.0f / 60.0f));
            scene.Update(1.0f / 60.0f);
            mostRings = std::max(mostRings, effects->GetRipples().GetRipples().size());
        }
        Require(listener->counts[WaterInteractionType::Enter] == 1, "The falling ball splashed once: " +
                                                                       std::to_string(listener->counts[WaterInteractionType::Enter]));
        Require(listener->counts[WaterInteractionType::Wake] >= 3, "The wading interactor left a wake");
        Require(listener->frames == 90, "Listeners hear every runtime frame");
        Require(mostRings >= 2, "Splashes and wakes made rings");
        Require(water.GetProbes().size() == 2, "The disabled interactor opts its rigidbody out");
        Require(water.GetSplashResponder().GetFailedAssets().contains("project://Missing.plutoparticle"),
                "Missing splash particles are reported");
        // Wakes still use the default built-in splash: one pooled emitter, emitting at the surface.
        Require(water.GetSplashResponder().EmitterCount() == 1, "The built-in splash has one pooled emitter");
        const auto *emitter = scene.FindEntityByName("Water Splash Emitter");
        const auto *system = emitter ? emitter->GetComponent<ParticleSystemComponent>() : nullptr;
        Require(system && system->GetParticleSystemAssetReference() == assets::kBuiltinWaterSplashParticlesReference &&
                    system->GetSimulationSpace() == ParticleSimulationSpace::World && system->GetEmissionRateOverTime() == 0.0f,
                "The pooled emitter is the built-in splash, world space and silent between splashes");

        // The renderer receives the live rings.
        Require(water.Disturb(scene, {3.0f, 0.0f, 3.0f}, 1.0f), "Disturbing the sea works");
        Require(!water.Disturb(scene, {3.0f, 0.0f, 3.0f}, std::nanf("")), "Invalid intensities are rejected");
        render::BasicLighting lighting{};
        const auto oceans = render::CollectRhiOceans(scene, lighting, nullptr);
        Require(oceans.size() == 1 && oceans[0].ocean && oceans[0].ocean->rippleCount.x > 0 &&
                    oceans[0].ocean->rippleShape.x > 0.0f,
                "The ocean packet carries the rings");

        // Without movement the rings fade out.
        interactor->SetEnabled(false);
        for (int frame = 0; frame < 240; ++frame)
            scene.Update(1.0f / 60.0f);
        Require(effects->GetRipples().Empty(), "Rings fade after their lifetime");

        effects->GetRipples().Emit({0, 0}, 1.0f);
        scene.StopRuntime();
        Require(effects->GetRipples().Empty() && water.GetProbes().empty(), "Stopping play clears the water's runtime state");
        water.RemoveListener(listener.get());
        Require(water.GetListeners().size() == 3, "Listeners can be removed");
    }
}

int main()
{
    try
    {
        TestHabitat();
        TestSnapshot();
        TestRipples();
        TestTracker();
        TestBuiltinSplash();
        TestProfile();
        TestFoliageAssetHabitat();
        TestFoliageBrushHabitat();
        TestProcedural();
        TestRuntime();
        std::cout << "Water effects tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << "Water effects tests failed: " << error.what() << '\n';
        return 1;
    }
}
