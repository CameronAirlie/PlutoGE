#pragma once

#include "PlutoGE/assets/AnimationGraph.h"
#include "PlutoGE/assets/AnimationGraphValidation.h"

#include <imgui.h>

#include <functional>
#include <string>

namespace PlutoGE::assets
{
    class Project;
}

namespace PlutoGE::ui::animgraph
{
    class AnimationGraphDocument;
    class AnimationGraphLiveSession;
    class StateMachineCanvas;

    // Everything the editor's views share for one frame.
    struct EditorContext
    {
        AnimationGraphDocument &document;
        StateMachineCanvas &canvas;
        AnimationGraphLiveSession &live;
        const assets::Project *project = nullptr;
    };

    // Compact two-column label/value layout for inspectors.
    bool BeginProperties(const char *id);
    void PropertyRow(const char *label, const char *tooltip = nullptr);
    void EndProperties();

    // Muted, wrapped explanatory text.
    void Hint(const char *text);
    // Clickable inline text, used to jump to nodes and transitions.
    bool LinkText(const char *id, const std::string &text);
    ImVec4 SeverityColor(assets::animgraph::DiagnosticSeverity severity);
    const char *SeverityLabel(assets::animgraph::DiagnosticSeverity severity);

    // "Grounded / Locomotion"; "Any State" for the root group.
    std::string NodePathLabel(const assets::AnimationGraphAsset &graph, assets::AnimationGraphNodeRef node);

    using ParameterFilter = std::function<bool(const assets::AnimationGraphParameter &)>;
    // Combo over the graph's parameters. A noneLabel adds an empty choice.
    bool ParameterCombo(const char *id, const assets::AnimationGraphAsset &graph, std::string &value,
                        const ParameterFilter &filter = {}, const char *noneLabel = nullptr);
    bool IsFloatParameter(const assets::AnimationGraphParameter &parameter);
    bool IsSwitchParameter(const assets::AnimationGraphParameter &parameter);

    // Searchable picker over every state and (optionally) group in the graph.
    bool NodePicker(const char *id, const assets::AnimationGraphAsset &graph, assets::AnimationGraphNodeRef &value,
                    bool allowGroups, const std::function<bool(assets::AnimationGraphNodeRef)> &filter = {},
                    const char *placeholder = "Select...");
}
