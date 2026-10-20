#include "PlutoGE/assets/AnimationGraphSerialization.h"

#include "PlutoGE/assets/AnimationGraphUtils.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace PlutoGE::assets
{
    namespace
    {
        std::vector<std::string> SplitFields(std::string_view text)
        {
            std::vector<std::string> fields;
            std::size_t start = 0;
            while (true)
            {
                const std::size_t end = text.find('|', start);
                if (end == std::string_view::npos)
                {
                    fields.emplace_back(text.substr(start));
                    return fields;
                }
                fields.emplace_back(text.substr(start, end - start));
                start = end + 1;
            }
        }

        int ParseIntOr(std::string_view text, int fallback)
        {
            try
            {
                return std::stoi(std::string(text));
            }
            catch (...)
            {
                return fallback;
            }
        }

        float ParseFloatOr(std::string_view text, float fallback)
        {
            try
            {
                return std::stof(std::string(text));
            }
            catch (...)
            {
                return fallback;
            }
        }

        bool ParseBoolOr(std::string_view text, bool fallback)
        {
            if (text == "1" || text == "true" || text == "True")
                return true;
            if (text == "0" || text == "false" || text == "False")
                return false;
            return fallback;
        }

        std::string_view FieldOr(const std::vector<std::string> &fields, std::size_t index, std::string_view fallback = {})
        {
            return index < fields.size() ? std::string_view(fields[index]) : fallback;
        }

        const char *ToString(AnimationGraphConditionMode mode)
        {
            switch (mode)
            {
            case AnimationGraphConditionMode::IfNot:
                return "IfNot";
            case AnimationGraphConditionMode::Greater:
                return "Greater";
            case AnimationGraphConditionMode::Less:
                return "Less";
            case AnimationGraphConditionMode::Equals:
                return "Equals";
            case AnimationGraphConditionMode::NotEqual:
                return "NotEqual";
            case AnimationGraphConditionMode::If:
            default:
                return "If";
            }
        }

        AnimationGraphParameterType ParseParameterType(std::string_view text)
        {
            if (text == "Int" || text == "1")
                return AnimationGraphParameterType::Int;
            if (text == "Bool" || text == "2")
                return AnimationGraphParameterType::Bool;
            if (text == "Trigger" || text == "3")
                return AnimationGraphParameterType::Trigger;
            return AnimationGraphParameterType::Float;
        }

        AnimationGraphConditionMode ParseConditionMode(std::string_view text)
        {
            if (text == "IfNot" || text == "1")
                return AnimationGraphConditionMode::IfNot;
            if (text == "Greater" || text == "2")
                return AnimationGraphConditionMode::Greater;
            if (text == "Less" || text == "3")
                return AnimationGraphConditionMode::Less;
            if (text == "Equals" || text == "4")
                return AnimationGraphConditionMode::Equals;
            if (text == "NotEqual" || text == "5")
                return AnimationGraphConditionMode::NotEqual;
            return AnimationGraphConditionMode::If;
        }

        AnimationGraphNodeKind ParseNodeKind(std::string_view text)
        {
            return text == "G" || text == "Group" ? AnimationGraphNodeKind::Group : AnimationGraphNodeKind::State;
        }

        const char *ToString(AnimationGraphNodeKind kind)
        {
            return kind == AnimationGraphNodeKind::Group ? "G" : "S";
        }

        // Free text is the final field of its line; only line breaks and the
        // escape character itself need escaping.
        std::string EscapeText(std::string_view text)
        {
            std::string escaped;
            escaped.reserve(text.size());
            for (const char character : text)
            {
                if (character == '\\')
                    escaped += "\\\\";
                else if (character == '\n')
                    escaped += "\\n";
                else if (character != '\r')
                    escaped += character;
            }
            return escaped;
        }

        std::string UnescapeText(std::string_view text)
        {
            std::string unescaped;
            unescaped.reserve(text.size());
            for (std::size_t index = 0; index < text.size(); ++index)
            {
                if (text[index] == '\\' && index + 1 < text.size())
                {
                    const char next = text[++index];
                    unescaped += next == 'n' ? '\n' : next;
                }
                else
                    unescaped += text[index];
            }
            return unescaped;
        }

        // Splits "id|rest of line" where the rest may contain '|'.
        std::pair<int, std::string> SplitIdAndText(std::string_view value)
        {
            const auto delimiter = value.find('|');
            if (delimiter == std::string_view::npos)
                return {ParseIntOr(value, 0), {}};
            return {ParseIntOr(value.substr(0, delimiter), 0), UnescapeText(value.substr(delimiter + 1))};
        }

        std::string_view Trim(std::string_view text)
        {
            while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
                text.remove_prefix(1);
            while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r'))
                text.remove_suffix(1);
            return text;
        }

        AnimationGraphGroupLayout ParseLayout(const std::vector<std::string> &fields)
        {
            return AnimationGraphGroupLayout{
                .placed = true,
                .anyStateX = ParseFloatOr(FieldOr(fields, 1), 0.0f),
                .anyStateY = ParseFloatOr(FieldOr(fields, 2), 0.0f),
                .parentX = ParseFloatOr(FieldOr(fields, 3), 0.0f),
                .parentY = ParseFloatOr(FieldOr(fields, 4), 0.0f),
            };
        }

        void WriteLayout(std::ostream &output, int groupId, const AnimationGraphGroupLayout &layout)
        {
            if (!layout.placed)
                return;
            output << "GroupLayout=" << groupId << '|' << layout.anyStateX << '|' << layout.anyStateY << '|'
                   << layout.parentX << '|' << layout.parentY << "\n";
        }
    }

    AnimationGraphAsset ParseAnimationGraph(std::istream &input)
    {
        AnimationGraphAsset graph;
        std::unordered_map<int, std::size_t> transitionIndexById;
        std::unordered_map<int, std::size_t> boneMaskIndexById;
        std::vector<std::string> pendingComments;
        std::string line;

        const auto findState = [&graph](int stateId) { return animgraph::FindState(graph, stateId); };

        while (std::getline(input, line))
        {
            const std::string_view trimmed = Trim(line);
            if (!trimmed.empty() && trimmed.front() == '#')
            {
                pendingComments.emplace_back(Trim(trimmed.substr(1)));
                continue;
            }

            const auto delimiter = trimmed.find('=');
            if (delimiter == std::string_view::npos)
            {
                pendingComments.clear();
                continue;
            }

            const std::string_view key = trimmed.substr(0, delimiter);
            const std::string_view value = trimmed.substr(delimiter + 1);
            std::vector<std::string> comments;
            comments.swap(pendingComments);

            if (key == "DefaultStateId")
            {
                graph.defaultStateId = ParseIntOr(value, 0);
            }
            else if (key == "Parameter")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 6)
                    continue;
                graph.parameters.push_back(AnimationGraphParameter{
                    .id = ParseIntOr(fields[0], 0),
                    .name = fields[1],
                    .type = ParseParameterType(fields[2]),
                    .floatValue = ParseFloatOr(fields[3], 0.0f),
                    .intValue = ParseIntOr(fields[4], 0),
                    .boolValue = ParseBoolOr(fields[5], false),
                });
            }
            else if (key == "Group")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 2)
                    continue;
                graph.groups.push_back(AnimationGraphGroup{
                    .id = ParseIntOr(fields[0], 0),
                    .name = fields[1],
                    .parentId = ParseIntOr(FieldOr(fields, 2), kAnimationGraphRootGroupId),
                    .entryStateId = ParseIntOr(FieldOr(fields, 3), 0),
                    .positionX = ParseFloatOr(FieldOr(fields, 4), 60.0f),
                    .positionY = ParseFloatOr(FieldOr(fields, 5), 60.0f),
                });
            }
            else if (key == "GroupNote")
            {
                auto [groupId, text] = SplitIdAndText(value);
                if (auto *group = animgraph::FindGroup(graph, groupId))
                    group->description = std::move(text);
            }
            else if (key == "GroupLayout")
            {
                const auto fields = SplitFields(value);
                const int groupId = ParseIntOr(FieldOr(fields, 0), -1);
                if (groupId == kAnimationGraphRootGroupId)
                    graph.rootLayout = ParseLayout(fields);
                else if (auto *group = animgraph::FindGroup(graph, groupId))
                    group->layout = ParseLayout(fields);
            }
            else if (key == "State")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 8)
                    continue;

                std::string description;
                for (const auto &comment : comments)
                    description += (description.empty() ? "" : "\n") + comment;

                graph.states.push_back(AnimationGraphState{
                    .id = ParseIntOr(fields[0], 0),
                    .name = fields[1],
                    .clipReference = std::string(FieldOr(fields, 8)),
                    .clipName = fields[2],
                    .clipIndex = ParseIntOr(fields[3], 0),
                    .positionX = ParseFloatOr(fields[4], 60.0f),
                    .positionY = ParseFloatOr(fields[5], 60.0f),
                    .speed = ParseFloatOr(fields[6], 1.0f),
                    .loop = ParseBoolOr(fields[7], true),
                    .groupId = ParseIntOr(FieldOr(fields, 9), kAnimationGraphRootGroupId),
                    .description = std::move(description),
                });
            }
            else if (key == "StateNote")
            {
                auto [stateId, text] = SplitIdAndText(value);
                if (auto *state = findState(stateId))
                    state->description = std::move(text);
            }
            else if (key == "BlendSpace")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 3)
                    continue;
                if (auto *state = findState(ParseIntOr(fields[0], 0)))
                {
                    state->blendSpaceParameterX = fields[1];
                    state->blendSpaceParameterY = fields[2];
                }
            }
            else if (key == "BlendSpacePoint")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 6)
                    continue;
                if (auto *state = findState(ParseIntOr(fields[0], 0)))
                {
                    state->blendSpacePoints.push_back(AnimationGraphBlendSpacePoint{
                        .clipReference = fields[5] == "0" ? std::string{} : fields[5],
                        .clipName = fields[3],
                        .clipIndex = ParseIntOr(fields[4], 0),
                        .positionX = ParseFloatOr(fields[1], 0.0f),
                        .positionY = ParseFloatOr(fields[2], 0.0f),
                    });
                }
            }
            else if (key == "Transition")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 6)
                    continue;
                graph.transitions.push_back(AnimationGraphTransition{
                    .id = ParseIntOr(fields[0], 0),
                    .from = {ParseNodeKind(FieldOr(fields, 7)), ParseIntOr(fields[1], 0)},
                    .to = {ParseNodeKind(FieldOr(fields, 8)), ParseIntOr(fields[2], 0)},
                    .duration = ParseFloatOr(fields[3], 0.15f),
                    .hasExitTime = ParseBoolOr(fields[4], false),
                    .exitTime = ParseFloatOr(fields[5], 0.9f),
                    .offset = std::clamp(ParseFloatOr(FieldOr(fields, 6, "0"), 0.0f), 0.0f, 1.0f),
                    .canTransitionToSelf = ParseBoolOr(FieldOr(fields, 9), false),
                });
                transitionIndexById[graph.transitions.back().id] = graph.transitions.size() - 1;
            }
            else if (key == "Condition")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 4)
                    continue;
                const auto transitionIt = transitionIndexById.find(ParseIntOr(fields[0], 0));
                if (transitionIt == transitionIndexById.end() || transitionIt->second >= graph.transitions.size())
                    continue;
                graph.transitions[transitionIt->second].conditions.push_back(AnimationGraphCondition{
                    .parameterName = fields[1],
                    .mode = ParseConditionMode(fields[2]),
                    .threshold = ParseFloatOr(fields[3], 0.0f),
                });
            }
            else if (key == "BoneMask")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 3)
                    continue;
                graph.boneMasks.push_back(AnimationGraphBoneMask{
                    .id = ParseIntOr(fields[0], 0),
                    .name = fields[1],
                    .defaultWeight = std::clamp(ParseFloatOr(fields[2], 0.0f), 0.0f, 1.0f),
                });
                boneMaskIndexById[graph.boneMasks.back().id] = graph.boneMasks.size() - 1;
            }
            else if (key == "BoneMaskEntry")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 4)
                    continue;
                const auto maskIt = boneMaskIndexById.find(ParseIntOr(fields[0], 0));
                if (maskIt == boneMaskIndexById.end())
                    continue;
                const int bone = std::clamp(ParseIntOr(fields[1], 0), 0, static_cast<int>(render::HumanoidBone::Count) - 1);
                graph.boneMasks[maskIt->second].entries.push_back(AnimationGraphBoneMaskEntry{
                    .bone = static_cast<render::HumanoidBone>(bone),
                    .weight = std::clamp(ParseFloatOr(fields[2], 1.0f), 0.0f, 1.0f),
                    .includeChildren = ParseBoolOr(fields[3], true),
                });
            }
            else if (key == "Layer")
            {
                const auto fields = SplitFields(value);
                if (fields.size() < 17)
                    continue;
                graph.layers.push_back(AnimationGraphLayer{
                    .id = ParseIntOr(fields[0], 0),
                    .name = fields[1],
                    .graphReference = fields[16] == "0" ? std::string{} : fields[16],
                    .clipReference = fields[2],
                    .clipName = fields[3],
                    .clipIndex = ParseIntOr(fields[4], 0),
                    .maskId = ParseIntOr(fields[5], 0),
                    .blendMode = ParseIntOr(fields[6], 0) == 1 ? AnimationGraphLayerBlendMode::Additive : AnimationGraphLayerBlendMode::Override,
                    .weight = std::clamp(ParseFloatOr(fields[7], 1.0f), 0.0f, 1.0f),
                    .weightParameter = fields[8],
                    .activationParameter = fields[9],
                    .speed = std::max(0.0f, ParseFloatOr(fields[10], 1.0f)),
                    .fadeIn = std::max(0.0f, ParseFloatOr(fields[11], 0.08f)),
                    .fadeOut = std::max(0.0f, ParseFloatOr(fields[12], 0.12f)),
                    .loop = ParseBoolOr(fields[13], false),
                    .restartOnActivation = ParseBoolOr(fields[14], true),
                    .enabled = ParseBoolOr(fields[15], true),
                });
            }
        }

        if (graph.defaultStateId == 0 && !graph.states.empty())
            graph.defaultStateId = graph.states.front().id;
        return graph;
    }

    void WriteAnimationGraph(std::ostream &output, const AnimationGraphAsset &graph)
    {
        output << "AnimationGraphVersion=" << kAnimationGraphFormatVersion << "\n";
        output << "DefaultStateId=" << graph.defaultStateId << "\n";
        WriteLayout(output, kAnimationGraphRootGroupId, graph.rootLayout);
        for (const auto &parameter : graph.parameters)
        {
            output << "Parameter=" << parameter.id << '|'
                   << parameter.name << '|'
                   << animgraph::ParameterTypeName(parameter.type) << '|'
                   << parameter.floatValue << '|'
                   << parameter.intValue << '|'
                   << (parameter.boolValue ? "1" : "0") << "\n";
        }
        // Groups precede states so a reader can resolve membership in one pass.
        for (const auto &group : graph.groups)
        {
            output << "Group=" << group.id << '|'
                   << group.name << '|'
                   << group.parentId << '|'
                   << group.entryStateId << '|'
                   << group.positionX << '|'
                   << group.positionY << "\n";
            if (!group.description.empty())
                output << "GroupNote=" << group.id << '|' << EscapeText(group.description) << "\n";
            WriteLayout(output, group.id, group.layout);
        }
        for (const auto &state : graph.states)
        {
            output << "State=" << state.id << '|'
                   << state.name << '|'
                   << state.clipName << '|'
                   << state.clipIndex << '|'
                   << state.positionX << '|'
                   << state.positionY << '|'
                   << state.speed << '|'
                   << (state.loop ? "1" : "0") << '|'
                   << state.clipReference << '|'
                   << state.groupId << "\n";
            if (!state.description.empty())
                output << "StateNote=" << state.id << '|' << EscapeText(state.description) << "\n";
            if (!state.blendSpacePoints.empty())
            {
                output << "BlendSpace=" << state.id << '|'
                       << state.blendSpaceParameterX << '|'
                       << state.blendSpaceParameterY << "\n";
                for (const auto &point : state.blendSpacePoints)
                {
                    output << "BlendSpacePoint=" << state.id << '|'
                           << point.positionX << '|'
                           << point.positionY << '|'
                           << point.clipName << '|'
                           << point.clipIndex << '|'
                           << (point.clipReference.empty() ? "0" : point.clipReference) << "\n";
                }
            }
        }
        for (const auto &transition : graph.transitions)
        {
            output << "Transition=" << transition.id << '|'
                   << transition.from.id << '|'
                   << transition.to.id << '|'
                   << transition.duration << '|'
                   << (transition.hasExitTime ? "1" : "0") << '|'
                   << transition.exitTime << '|'
                   << std::clamp(transition.offset, 0.0f, 1.0f) << '|'
                   << ToString(transition.from.kind) << '|'
                   << ToString(transition.to.kind) << '|'
                   << (transition.canTransitionToSelf ? "1" : "0") << "\n";
            for (const auto &condition : transition.conditions)
            {
                output << "Condition=" << transition.id << '|'
                       << condition.parameterName << '|'
                       << ToString(condition.mode) << '|'
                       << condition.threshold << "\n";
            }
        }
        for (const auto &mask : graph.boneMasks)
        {
            output << "BoneMask=" << mask.id << '|'
                   << mask.name << '|'
                   << std::clamp(mask.defaultWeight, 0.0f, 1.0f) << "\n";
            for (const auto &entry : mask.entries)
            {
                output << "BoneMaskEntry=" << mask.id << '|'
                       << static_cast<int>(entry.bone) << '|'
                       << std::clamp(entry.weight, 0.0f, 1.0f) << '|'
                       << (entry.includeChildren ? "1" : "0") << "\n";
            }
        }
        for (const auto &layer : graph.layers)
        {
            output << "Layer=" << layer.id << '|'
                   << layer.name << '|'
                   << layer.clipReference << '|'
                   << layer.clipName << '|'
                   << layer.clipIndex << '|'
                   << layer.maskId << '|'
                   << (layer.blendMode == AnimationGraphLayerBlendMode::Additive ? 1 : 0) << '|'
                   << std::clamp(layer.weight, 0.0f, 1.0f) << '|'
                   << layer.weightParameter << '|'
                   << layer.activationParameter << '|'
                   << std::max(0.0f, layer.speed) << '|'
                   << std::max(0.0f, layer.fadeIn) << '|'
                   << std::max(0.0f, layer.fadeOut) << '|'
                   << (layer.loop ? "1" : "0") << '|'
                   << (layer.restartOnActivation ? "1" : "0") << '|'
                   << (layer.enabled ? "1" : "0") << '|'
                   << layer.graphReference << "\n";
        }
    }
}
