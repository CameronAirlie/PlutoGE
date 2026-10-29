#pragma once

#include "PlutoGE/assets/PostProcessPresetAsset.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace PlutoGE::render
{
    class IPostProcessEffect;
}

namespace PlutoGE::ui
{
    class PostProcessPresetSession;
}

// The post-process editor's views. Views keep their own UI state and edit through
// PostProcessPresetSession::Modify, so every change is undoable.
namespace PlutoGE::ui::postprocess
{
    struct PostProcessIssue
    {
        enum class Severity
        {
            Note,    // worth knowing, often intended
            Warning, // probably not what was wanted
        };
        Severity severity = Severity::Warning;
        int effect = -1; // index into the preset's effects, -1 for the whole stack
        std::string message;
    };

    // Findings about a preset, rebuilt only when it changes.
    struct PostProcessStackAnalysis
    {
        std::uint64_t revision = 0;
        std::vector<PostProcessIssue> issues;
        std::vector<int> effectWarnings; // per effect

        void Update(const PostProcessPresetSession &session);
        void Analyze(const assets::PostProcessPresetAsset &preset);
        [[nodiscard]] int WarningCount() const;
    };

    // Effect indices in the order the renderer runs them: by stage, then as listed. Unknown types go last.
    [[nodiscard]] std::vector<int> PipelineOrder(const assets::PostProcessPresetAsset &preset);
    // The stage an effect type runs in; nullopt for a type the engine does not know.
    [[nodiscard]] std::optional<render::PostProcessStage> StageOf(const std::string &typeName);
    // Where a new effect of `typeName` goes: after the last effect of the same or an earlier stage.
    [[nodiscard]] int InsertionIndex(const assets::PostProcessPresetAsset &preset, const std::string &typeName);
    // The effect's display name, or its type name when unknown.
    [[nodiscard]] std::string EffectDisplayName(const std::string &typeName);

    struct EffectsViewState
    {
        int selected = -1;
        std::string addSearch;
        // A live instance of the selected effect for its read-only summary (cost, memory), rebuilt on change.
        std::unique_ptr<render::IPostProcessEffect> summaryEffect;
        std::uint64_t summaryRevision = 0;
        int summaryIndex = -1;

        EffectsViewState();
        ~EffectsViewState();
        EffectsViewState(EffectsViewState &&) noexcept;
        EffectsViewState &operator=(EffectsViewState &&) noexcept;
    };
    // The stack, grouped by pipeline stage, beside the selected effect's settings.
    void DrawEffectsView(PostProcessPresetSession &session, EffectsViewState &state, const PostProcessStackAnalysis &analysis);

    // Something that renders with the preset.
    struct PostProcessPresetUser
    {
        std::string label;  // "Main Camera", "Editor viewport"
        std::string detail; // "Scene camera", "Overlay camera"
        std::uint32_t entity = 0; // scene entity id, 0 for the editor camera
    };
    // Who uses the preset. Returns the user clicked, to select it.
    [[nodiscard]] std::optional<int> DrawUsageView(const std::vector<PostProcessPresetUser> &users, bool editorCameraUsesPreset,
                                                   bool &assignToEditorCamera);
}
