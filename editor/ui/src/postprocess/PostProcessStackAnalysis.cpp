#include "PlutoGE/ui/postprocess/PostProcessViews.h"

#include "PlutoGE/ui/postprocess/PostProcessPresetSession.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>

namespace PlutoGE::ui::postprocess
{
    std::optional<render::PostProcessStage> StageOf(const std::string &typeName)
    {
        if (const auto *descriptor = render::FindPostProcessEffectDescriptor(typeName))
            return descriptor->stage;
        return std::nullopt;
    }

    std::string EffectDisplayName(const std::string &typeName)
    {
        const auto *defaults = render::GetPostProcessEffectDefaults(typeName);
        return defaults ? defaults->displayName : typeName;
    }

    std::vector<int> PipelineOrder(const assets::PostProcessPresetAsset &preset)
    {
        std::vector<int> order(preset.effects.size());
        for (int index = 0; index < static_cast<int>(order.size()); ++index)
            order[index] = index;
        const auto rank = [&](int index)
        {
            const auto stage = StageOf(preset.effects[index].typeName);
            return stage ? static_cast<int>(*stage) : render::PostProcessStageCount;
        };
        std::ranges::stable_sort(order, {}, rank);
        return order;
    }

    int InsertionIndex(const assets::PostProcessPresetAsset &preset, const std::string &typeName)
    {
        const auto stage = StageOf(typeName);
        if (!stage)
            return static_cast<int>(preset.effects.size());
        int insertAt = 0;
        for (int index = 0; index < static_cast<int>(preset.effects.size()); ++index)
        {
            const auto existing = StageOf(preset.effects[index].typeName);
            if (existing && *existing <= *stage)
                insertAt = index + 1;
        }
        return insertAt;
    }

    namespace
    {
        float FloatParameter(const assets::PostProcessEffectAsset &effect, const char *name, float fallback)
        {
            const auto found = std::ranges::find(effect.parameters, name, &render::PostProcessParameter::name);
            if (found == effect.parameters.end())
                return fallback;
            char *end = nullptr;
            const float value = std::strtof(found->value.c_str(), &end);
            return end != found->value.c_str() && std::isfinite(value) ? value : fallback;
        }

        std::string Metres(float value)
        {
            char text[32];
            std::snprintf(text, sizeof(text), "%.0f", value);
            return std::string(text) == "-0" ? "0" : text;
        }

        // Volumetric Fog settings that leave a layer invisible or unlit (VolumetricFog.slang): the height fog thins
        // exponentially above Height Offset, so a camera far above it sees only the horizon haze.
        template <class Add>
        void AnalyzeVolumetricFog(const assets::PostProcessEffectAsset &effect, int index,
                                  const std::vector<PostProcessViewer> &viewers, Add add)
        {
            using Severity = PostProcessIssue::Severity;
            const float density = FloatParameter(effect, "Density", 0.035f);
            const float falloff = FloatParameter(effect, "Height Falloff", 0.12f);
            const float offset = FloatParameter(effect, "Height Offset", 0.0f);
            const float opacity = FloatParameter(effect, "Max Opacity", 1.0f);
            const float haze = FloatParameter(effect, "Horizon Haze", 1.0f);
            if (density <= 0.0f || opacity <= 0.0f)
            {
                const std::string reason = density <= 0.0f ? "Density is 0" : "Max Opacity is 0";
                if (haze <= 0.0f)
                    add(Severity::Warning, index, "Volumetric Fog: " + reason + " and Horizon Haze is 0, so it draws nothing.");
                else
                    add(Severity::Note, index, "Volumetric Fog: " + reason + ", so only the horizon haze shows. Of the other "
                                               "settings only Color and the Horizon Haze group change anything.");
                return;
            }

            const float scattering = FloatParameter(effect, "Scattering", 0.65f);
            const float ambient = FloatParameter(effect, "Ambient Contribution", 1.0f);
            const float directional = FloatParameter(effect, "Directional Contribution", 6.0f);
            const bool black = FloatParameter(effect, "Color R", 1.0f) <= 0.0f && FloatParameter(effect, "Color G", 1.0f) <= 0.0f &&
                               FloatParameter(effect, "Color B", 1.0f) <= 0.0f;
            if (scattering <= 0.0f || (ambient <= 0.0f && directional <= 0.0f) || black)
                add(Severity::Warning, index, "Volumetric Fog: the height fog scatters no light (Scattering, both Contributions "
                                              "or Color is 0), so it only darkens the scene toward black.");

            if (falloff <= 0.0f || viewers.empty())
                return;
            // Above this height the fog holds under 1% of Density.
            const float faintHeight = offset + std::log(100.0f) / falloff;
            const auto lowest = std::ranges::min_element(viewers, {}, &PostProcessViewer::height);
            if (lowest->height <= faintHeight)
                return;
            std::string where = viewers.size() == 1 ? lowest->label + " is" : "Every camera using it is above that; the lowest, " + lowest->label + ", is";
            where[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(where[0])));
            add(Severity::Warning, index,
                "Volumetric Fog: the height fog is at full Density at Y = " + Metres(offset) + " and thins to 1% by Y = " +
                    Metres(faintHeight) + ". " + where + " at Y = " + Metres(lowest->height) +
                    ", so the fog barely shows there and most of its settings seem to do nothing. Raise Height Offset "
                    "toward the ground or lower Height Falloff.");
        }
    }

    void PostProcessStackAnalysis::SetViewers(std::vector<PostProcessViewer> cameras)
    {
        if (cameras == viewers)
            return;
        viewers = std::move(cameras);
        revision = 0;
    }

    void PostProcessStackAnalysis::Update(const PostProcessPresetSession &session)
    {
        if (revision == session.GetRevision())
            return;
        revision = session.GetRevision();
        Analyze(session.GetPreset());
    }

    void PostProcessStackAnalysis::Analyze(const assets::PostProcessPresetAsset &preset)
    {
        using Severity = PostProcessIssue::Severity;
        issues.clear();
        effectWarnings.assign(preset.effects.size(), 0);
        const auto add = [&](Severity severity, int effect, std::string message)
        {
            if (effect >= 0 && severity == Severity::Warning)
                ++effectWarnings[effect];
            issues.push_back({severity, effect, std::move(message)});
        };

        std::map<std::string, int> firstOfType;
        const auto enabled = [&](const char *type)
        {
            return std::ranges::any_of(preset.effects, [&](const auto &effect) { return effect.enabled && effect.typeName == type; });
        };
        for (int index = 0; index < static_cast<int>(preset.effects.size()); ++index)
        {
            const auto &effect = preset.effects[index];
            const std::string name = EffectDisplayName(effect.typeName);
            if (!render::FindPostProcessEffectDescriptor(effect.typeName))
            {
                add(Severity::Warning, index, "\"" + effect.typeName + "\" is not an effect this version of the engine knows. "
                                              "It is kept in the file but does nothing.");
                continue;
            }
            if (const auto [first, inserted] = firstOfType.emplace(effect.typeName, index); !inserted)
                add(Severity::Warning, index, name + " is in the stack more than once. Remove the extra copy.");
            if (effect.enabled && effect.typeName == "VolumetricFog")
                AnalyzeVolumetricFog(effect, index, viewers, add);
        }

        if (!enabled("ToneMapping"))
            add(Severity::Warning, -1, "No Tone Mapping: bright lighting will clip to flat white. Add Tone Mapping unless "
                                       "the output is meant to stay in high dynamic range.");
        if (enabled("TAA") && enabled("FXAA"))
            add(Severity::Note, -1, "TAA and FXAA both smooth edges. One is usually enough; FXAA after TAA mostly adds blur.");
    }

    int PostProcessStackAnalysis::WarningCount() const
    {
        return static_cast<int>(std::ranges::count(issues, PostProcessIssue::Severity::Warning, &PostProcessIssue::severity));
    }
}
