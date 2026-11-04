#include "PlutoGE/ui/postprocess/PostProcessViews.h"

#include "PlutoGE/ui/postprocess/PostProcessPresetSession.h"

#include <algorithm>
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
