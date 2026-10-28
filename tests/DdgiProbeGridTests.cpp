#include "PlutoGE/render/DdgiConfig.h"
#include "PlutoGE/render/DdgiProbeGrid.h"
#include "PlutoGE/render/DdgiPacket.h"
#include "PlutoGE/render/GraphicsQuality.h"
#include "PlutoGE/render/postprocess/DDGIEffect.h"
#include "PlutoGE/render/RhiPostProcessAdapter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

using namespace PlutoGE::render;

namespace
{
    void Check(bool condition, const char *message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    // Storage slots are a bijection of the grid, and scrolling keeps a probe's slot while it stays in range.
    void CheckAddressing()
    {
        DdgiGridSettings settings;
        settings.counts = {5, 3, 4};
        settings.spacing = 2.0f;
        DdgiProbeGrid grid(settings);
        Check(grid.Recenter({0, 0, 0}).reset, "first placement must reset");
        std::set<std::uint32_t> slots;
        for (int z = 0; z < 4; ++z)
            for (int y = 0; y < 3; ++y)
                for (int x = 0; x < 5; ++x)
                    slots.insert(grid.StorageIndex(grid.StorageCoordinate({x, y, z})));
        Check(slots.size() == 60 && *slots.rbegin() == 59, "storage indices are not a bijection");
        const auto tiles = DdgiAtlasTiles(grid.ProbeCount());
        Check(tiles.x * tiles.y >= 60 && tiles.x == 8, "atlas tiles");

        const auto before = grid.OriginCell();
        // Within a cell of the centre: no scroll (hysteresis).
        Check(grid.Recenter({1.9f, 0, 0}).delta == glm::ivec3(0), "scrolled inside the hysteresis band");
        const auto kept = grid.StorageIndex(grid.StorageCoordinate({3, 1, 2}));
        const auto scroll = grid.Recenter({4.5f, 0, 0});
        Check(!scroll.reset && scroll.delta == glm::ivec3(2, 0, 0) && grid.OriginCell() == before + glm::ivec3(2, 0, 0),
              "scroll delta");
        // Probe (3, 1, 2) before the scroll is probe (1, 1, 2) after it: same world point, same slot.
        Check(grid.StorageIndex(grid.StorageCoordinate({1, 1, 2})) == kept, "scrolling moved a probe's data");
        std::vector<std::uint32_t> entered;
        grid.AppendEnteredProbes(scroll, entered);
        Check(entered.size() == 2 * 3 * 4, "entered slab size");
        for (int z = 0; z < 4; ++z)
            for (int y = 0; y < 3; ++y)
                for (int x = 3; x < 5; ++x)
                    Check(std::find(entered.begin(), entered.end(),
                                    grid.StorageIndex(grid.StorageCoordinate({x, y, z}))) != entered.end(),
                          "entered probes are the new slab");
        Check(grid.Recenter({100, 0, 0}).reset, "teleport must reset");
    }

    void CheckScheduler()
    {
        DdgiProbeScheduler scheduler;
        scheduler.Reset(100);
        std::vector<std::uint32_t> probes;
        std::set<std::uint32_t> seen;
        for (int frame = 0; frame < 4; ++frame)
        {
            scheduler.Next(25, probes);
            Check(probes.size() == 25, "budget not used");
            Check(std::set<std::uint32_t>(probes.begin(), probes.end()).size() == probes.size(), "duplicate in a frame");
            seen.insert(probes.begin(), probes.end());
        }
        Check(seen.size() == 100, "a sweep must visit every probe");
        // Queued probes go first, once, and are not repeated by the sweep in the same frame.
        const std::array queued{7u, 7u, 42u, 99u};
        scheduler.Enqueue(queued);
        Check(scheduler.Pending() == 3, "queue deduplication");
        scheduler.Next(10, probes);
        Check(probes[0] == 7 && probes[1] == 42 && probes[2] == 99 && scheduler.Pending() == 0, "queue order");
        Check(std::set<std::uint32_t>(probes.begin(), probes.end()).size() == probes.size(), "queue and sweep overlap");
        scheduler.Enqueue(std::array{1u, 2u, 3u});
        scheduler.Next(2, probes);
        Check(probes.size() == 2 && scheduler.Pending() == 1, "queue beyond the budget waits");
    }

    void CheckSettings()
    {
        DdgiGridSettings huge;
        huge.counts = {64, 32, 64};
        huge.spacing = -1.0f;
        huge.blendedRays = 4;
        huge.probesPerFrame = 0;
        const auto sane = huge.Sanitized();
        Check(sane.TotalProbes() <= DdgiGridSettings::MaxProbes && sane.spacing > 0.0f, "grid limits");
        // At least one probe per cascade per frame.
        Check(sane.blendedRays == 32 && sane.probesPerFrame == 3 && sane.FramesPerSweep() == sane.ProbeCount(),
              "ray and budget limits");
        const DdgiGridSettings defaults;
        Check(defaults.TracedRays() == PLUTO_DDGI_FIXED_RAYS + 96 && defaults.cascades == 3, "defaults");
        Check(defaults.MemoryBytes() > 40u << 20 && defaults.MemoryBytes() < 64u << 20, "memory estimate");
        // Cascades triple in spacing and split the budget 4:2:1, so the finest refreshes most often.
        Check(defaults.CascadeSpacing(2) == 18.0f && defaults.Extent(2).x == 23 * 18.0f, "cascade spacing");
        Check(defaults.CascadeBudget(0) == 987 && defaults.CascadeBudget(1) == 493 && defaults.CascadeBudget(2) == 246 &&
                  defaults.FrameBudget() == 987 + 493 + 246, "cascade budgets");
        Check(defaults.FramesPerSweep(0) == 8 && defaults.FramesPerSweep(2) == 29, "cascade refresh");
        DdgiGridSettings single = defaults;
        single.cascades = 1;
        Check(single.CascadeBudget(0) == 1728 && single.TotalProbes() == single.ProbeCount(), "single cascade");
        const DdgiProbeGrid outer(defaults, 2);
        Check(outer.Spacing() == 18.0f && outer.GlobalIndex(5) == 2 * defaults.ProbeCount() + 5, "cascade indices");

        for (const auto rotation : {DdgiRayRotation(0), DdgiRayRotation(1), DdgiRayRotation(12345)})
        {
            const auto identity = glm::transpose(rotation) * rotation;
            for (int row = 0; row < 3; ++row)
                for (int column = 0; column < 3; ++column)
                    Check(std::abs(identity[row][column] - (row == column ? 1.0f : 0.0f)) < 1e-4f, "ray rotation");
        }
        Check(DdgiRayRotation(3) != DdgiRayRotation(4), "rays must rotate between updates");

        DdgiSettings settings;
        settings.debugView = PLUTO_DDGI_VIEW_PROBES;
        settings.voxels.injectLocalLights = false;
        settings.relocation = false;
        settings.intensity = 1.5f;
        settings.reflectionOcclusion = 0.25f;
        const auto decoded = DecodeDdgiPacket(EncodeDdgiPacket(settings, &settings));
        Check(decoded == settings.Sanitized(), "packet round trip");
        for (std::size_t level = 0; level < DdgiQualityNames.size(); ++level)
        {
            const auto quality = static_cast<DdgiQuality>(level);
            Check(MatchesDdgiQuality(ApplyDdgiQuality(settings, quality), quality), "quality levels");
        }
        Check(MatchesDdgiQuality(DdgiSettings{}, DdgiQuality::Medium), "defaults are Medium quality");
    }

    void CheckEffect()
    {
        DDGIEffect effect;
        Check(effect.GetQuality() == DdgiQuality::Medium && !effect.GetSummary().empty(), "effect defaults");
        auto parameters = effect.GetParameters();
        // The inspector passes every parameter back; choosing a quality applies it over the unchanged others.
        for (auto &parameter : parameters)
            if (parameter.name == "Quality") parameter.value = "2";
        effect.ApplyParameters(parameters);
        Check(effect.GetQuality() == DdgiQuality::High && effect.GetSettings().grid.counts.x == 32, "quality choice");
        // Editing a controlled value makes it custom, and the same list re-applied keeps it so.
        parameters = effect.GetParameters();
        for (auto &parameter : parameters)
            if (parameter.name == "Rays Per Probe") parameter.value = "200";
        effect.ApplyParameters(parameters);
        Check(!effect.GetQuality() && effect.GetSettings().grid.blendedRays == 200, "custom settings");
        const auto saved = effect.GetParameters();
        DDGIEffect loaded;
        loaded.ApplyParameters(saved);
        Check(loaded.GetSettings() == effect.GetSettings(), "parameters must restore the same settings");
        Check(loaded.GetParameters().front().value == "4", "custom quality label");

        // Reflection occlusion is not quality-controlled: changing it keeps the quality level.
        DDGIEffect occluded;
        auto occlusionParameters = occluded.GetParameters();
        bool listed = false;
        for (auto &parameter : occlusionParameters)
            if (parameter.name == "Reflection Occlusion")
            {
                parameter.value = "0.5";
                listed = true;
            }
        occluded.ApplyParameters(occlusionParameters);
        Check(listed && occluded.GetSettings().reflectionOcclusion == 0.5f &&
                  occluded.GetQuality() == DdgiQuality::Medium, "reflection occlusion parameter");

        const auto packet = AdaptPostProcessEffect(effect);
        Check(packet && packet->type == BasicPostProcessEffectType::DDGI && packet->historyOwner == &effect &&
                  DecodeDdgiPacket(*packet) == effect.GetSettings(), "adapter");
        Check(IsRhiPostProcessEffectSupported("DDGI"), "adapter registration");
        Check(StageFor(BasicPostProcessEffectType::DDGI) < StageFor(BasicPostProcessEffectType::SSGI) &&
                  StageFor(BasicPostProcessEffectType::SSGI) < StageFor(BasicPostProcessEffectType::SSAO),
              "DDGI, then SSGI, then SSAO");
        Check(HasInput(InputsFor(BasicPostProcessEffectType::DDGI), BasicPostProcessInput::Indirect) &&
                  HasInput(InputsFor(BasicPostProcessEffectType::SSGI), BasicPostProcessInput::Indirect),
              "GI effects need the indirect target");
        // DDGI occludes the sky share of the geometry pass's environment reflection.
        Check(HasInput(InputsFor(BasicPostProcessEffectType::DDGI), BasicPostProcessInput::EnvironmentSpecular),
              "DDGI needs the environment reflection");
        Check(DdgiViewReplacesImage(PLUTO_DDGI_VIEW_SKY_VISIBILITY) && DdgiViewReplacesImage(PLUTO_DDGI_VIEW_REFLECTIONS) &&
                  DdgiViewReplacesImage(PLUTO_DDGI_VIEW_IRRADIANCE) && !DdgiViewReplacesImage(PLUTO_DDGI_VIEW_PROBES) &&
                  !DdgiViewReplacesImage(PLUTO_DDGI_VIEW_NONE),
              "debug views that replace the image");
        Check(!GraphicsQuality::FromPreset(GraphicsPreset::Low).Allows(BasicPostProcessEffectType::DDGI),
              "quality gating");
    }
}

int main()
{
    try
    {
        CheckAddressing();
        CheckScheduler();
        CheckSettings();
        CheckEffect();
    }
    catch (const std::exception &error)
    {
        std::cerr << "DDGI probe grid tests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "DDGI probe grid, scheduling, settings and effect tests passed\n";
    return 0;
}
