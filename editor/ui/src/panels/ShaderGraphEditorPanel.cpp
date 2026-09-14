#include "PlutoGE/ui/panels/ShaderGraphEditorPanel.h"

#include "PlutoGE/assets/Project.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/GraphEditorPanelUtils.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <imgui.h>

namespace PlutoGE::ui
{
    namespace
    {
        std::uint64_t EditorGraphHash(const render::ShaderGraph &graph)
        {
            auto hash=render::HashShaderGraph(graph);
            auto mix=[&](size_t v){hash^=v+0x9e3779b9+(hash<<6)+(hash>>2);};
            for(const auto &n:graph.nodes)
            {
                mix(std::hash<std::string>{}(n.name));
                mix(std::hash<float>{}(n.position.x));mix(std::hash<float>{}(n.position.y));
                mix(std::hash<float>{}(n.size.x));mix(std::hash<float>{}(n.size.y));mix(n.collapsed);
            }
            return hash;
        }

        constexpr float kDefaultNodeWidth = 260.0f;
        constexpr float kNodeMinWidth = 180.0f;
        constexpr float kNodeMinHeight = 128.0f;
        constexpr float kCollapsedNodeHeight = 52.0f;
        constexpr float kResizeGripSize = 16.0f;
        constexpr float kSlotVerticalSpacing = 26.0f;
        constexpr float kPreviewHeight = 48.0f;

        constexpr const char *kPinOut[] = {"Out"};
        constexpr const char *kPinsVec2Output[] = {"Vec2","X","Y"};
        constexpr const char *kPinsVec3Output[] = {"Vec3","X","Y","Z"};
        constexpr const char *kPinsColorOutput[] = {"Color","R","G","B","A"};
        constexpr const char *kPinsVec2Packed[] = {"Vec2"};
        constexpr const char *kPinsVec3Packed[] = {"Vec3"};
        constexpr const char *kPinsColorPacked[] = {"Color"};
        constexpr const char *kPinsVec2Components[] = {"X", "Y"};
        constexpr const char *kPinsVec3Components[] = {"X", "Y", "Z"};
        constexpr const char *kPinsColorComponents[] = {"R", "G", "B", "A"};
        constexpr const char *kPinsBinary[] = {"A", "B"};
        constexpr const char *kPinsLerp[] = {"A", "B", "T"};
        constexpr const char *kPinsClamp[] = {"Value", "Min", "Max"};
        constexpr const char *kPinsTexture[] = {"UV"};
        constexpr const char *kPinsTextureOutput[] = {"Color","R","G","B","A"};
        constexpr const char *kPinsValue[] = {"Value"};
        constexpr const char *kPinsNoise[] = {"UV", "Scale", "Strength"};
        constexpr const char *kPinsNoiseOutput[] = {"Value", "Color"};
        constexpr const char *kPinsOutput[] = {"Albedo", "Normal", "Metallic", "Roughness", "Opacity", "Emission"};

        constexpr ImU32 kPinAnyColor = IM_COL32(170, 176, 188, 255);
        constexpr ImU32 kPinFloatColor = IM_COL32(230, 190, 92, 255);
        constexpr ImU32 kPinVec2Color = IM_COL32(84, 190, 210, 255);
        constexpr ImU32 kPinVec3Color = IM_COL32(96, 145, 230, 255);
        constexpr ImU32 kPinVec4Color = IM_COL32(214, 112, 206, 255);
        constexpr ImU32 kPinColorColor = IM_COL32(236, 118, 104, 255);

        ImU32 kPinVec2OutputColors[] = {kPinVec2Color,kPinFloatColor,kPinFloatColor};
        ImU32 kPinVec3OutputColors[] = {kPinVec3Color,kPinFloatColor,kPinFloatColor,kPinFloatColor};
        ImU32 kPinTextureOutputColors[] = {kPinColorColor,kPinFloatColor,kPinFloatColor,kPinFloatColor,kPinFloatColor};
        ImU32 kPinOutAnyColors[] = {kPinAnyColor};
        ImU32 kPinOutFloatColors[] = {kPinFloatColor};
        ImU32 kPinOutVec2Colors[] = {kPinVec2Color};
        ImU32 kPinOutVec3Colors[] = {kPinVec3Color};
        ImU32 kPinOutVec4Colors[] = {kPinVec4Color};
        ImU32 kPinOutColorColors[] = {kPinColorColor};
        ImU32 kPinBinaryColors[] = {kPinAnyColor, kPinAnyColor};
        ImU32 kPinLerpColors[] = {kPinAnyColor, kPinAnyColor, kPinFloatColor};
        ImU32 kPinClampColors[] = {kPinAnyColor, kPinAnyColor, kPinAnyColor};
        ImU32 kPinNormalizeColors[] = {kPinVec3Color};
        ImU32 kPinVec2ComponentColors[] = {kPinFloatColor, kPinFloatColor};
        ImU32 kPinVec3ComponentColors[] = {kPinFloatColor, kPinFloatColor, kPinFloatColor};
        ImU32 kPinColorComponentColors[] = {kPinFloatColor, kPinFloatColor, kPinFloatColor, kPinFloatColor};
        ImU32 kPinNoiseInputColors[] = {kPinVec2Color, kPinFloatColor, kPinFloatColor};
        ImU32 kPinNoiseOutputColors[] = {kPinFloatColor, kPinColorColor};
        ImU32 kPinOutputColors[] = {kPinColorColor, kPinVec3Color, kPinFloatColor, kPinFloatColor, kPinFloatColor, kPinVec3Color};

        bool SupportsComponentPins(render::ShaderGraphNodeKind kind)
        {
            return kind == render::ShaderGraphNodeKind::Vec2 ||
                   kind == render::ShaderGraphNodeKind::Vec3 ||
                   kind == render::ShaderGraphNodeKind::Color;
        }

        const char **InputPins(render::ShaderGraphNodeKind kind, bool componentPins, ImU8 &count)
        {
            switch (kind)
            {
            case render::ShaderGraphNodeKind::Vec2:
                count = componentPins ? 2 : 1;
                return componentPins ? const_cast<const char **>(kPinsVec2Components) : const_cast<const char **>(kPinsVec2Packed);
            case render::ShaderGraphNodeKind::Vec3:
                count = componentPins ? 3 : 1;
                return componentPins ? const_cast<const char **>(kPinsVec3Components) : const_cast<const char **>(kPinsVec3Packed);
            case render::ShaderGraphNodeKind::Color:
                count = componentPins ? 4 : 1;
                return componentPins ? const_cast<const char **>(kPinsColorComponents) : const_cast<const char **>(kPinsColorPacked);
            case render::ShaderGraphNodeKind::Add:
            case render::ShaderGraphNodeKind::Subtract:
            case render::ShaderGraphNodeKind::Multiply:
            case render::ShaderGraphNodeKind::Power:
            case render::ShaderGraphNodeKind::Dot:
            case render::ShaderGraphNodeKind::Divide:
                count = 2;
                return const_cast<const char **>(kPinsBinary);
            case render::ShaderGraphNodeKind::Lerp:
                count = 3;
                return const_cast<const char **>(kPinsLerp);
            case render::ShaderGraphNodeKind::Clamp:
                count = 3;
                return const_cast<const char **>(kPinsClamp);
            case render::ShaderGraphNodeKind::Sine:
            case render::ShaderGraphNodeKind::OneMinus:
            case render::ShaderGraphNodeKind::Normalize:
                count = 1;
                return const_cast<const char **>(kPinsValue);
            case render::ShaderGraphNodeKind::TextureSample:
                count = 1;
                return const_cast<const char **>(kPinsTexture);
            case render::ShaderGraphNodeKind::NoiseTexture:
                count = 3;
                return const_cast<const char **>(kPinsNoise);
            case render::ShaderGraphNodeKind::Output:
                count = 6;
                return const_cast<const char **>(kPinsOutput);
            default:
                count = 0;
                return nullptr;
            }
        }

        const char **InputPins(const render::ShaderGraphNode &node, ImU8 &count)
        {
            return InputPins(node.kind, node.componentPins, count);
        }

        const char **OutputPins(render::ShaderGraphNodeKind kind, bool componentPins, ImU8 &count)
        {
            if (kind == render::ShaderGraphNodeKind::TextureSample)
            {
                count = 5;
                return const_cast<const char **>(kPinsTextureOutput);
            }
            if (kind == render::ShaderGraphNodeKind::Vec2)
            {
                count = 3;
                return const_cast<const char **>(kPinsVec2Output);
            }

            if (kind == render::ShaderGraphNodeKind::Vec3)
            {
                count = 4;
                return const_cast<const char **>(kPinsVec3Output);
            }

            if (kind == render::ShaderGraphNodeKind::Color)
            {
                count = 5;
                return const_cast<const char **>(kPinsColorOutput);
            }

            if (kind == render::ShaderGraphNodeKind::NoiseTexture)
            {
                count = 2;
                return const_cast<const char **>(kPinsNoiseOutput);
            }

            if (kind == render::ShaderGraphNodeKind::Output)
            {
                count = 0;
                return nullptr;
            }

            count = 1;
            return const_cast<const char **>(kPinOut);
        }

        const char **OutputPins(const render::ShaderGraphNode &node, ImU8 &count)
        {
            return OutputPins(node.kind, node.componentPins, count);
        }

        const char *InputPinName(const render::ShaderGraphNode &node, GraphEditor::SlotIndex index)
        {
            ImU8 count = 0;
            const char **pins = InputPins(node, count);
            return index < count ? pins[index] : "";
        }

        const char *OutputPinName(const render::ShaderGraphNode &node, GraphEditor::SlotIndex index)
        {
            ImU8 count = 0;
            const char **pins = OutputPins(node, count);
            return index < count ? pins[index] : "";
        }

        ImU32 *InputPinColors(render::ShaderGraphNodeKind kind, bool componentPins)
        {
            switch (kind)
            {
            case render::ShaderGraphNodeKind::Vec2:
                return componentPins ? kPinVec2ComponentColors : kPinOutVec2Colors;
            case render::ShaderGraphNodeKind::Vec3:
                return componentPins ? kPinVec3ComponentColors : kPinOutVec3Colors;
            case render::ShaderGraphNodeKind::Color:
                return componentPins ? kPinColorComponentColors : kPinOutColorColors;
            case render::ShaderGraphNodeKind::Add:
            case render::ShaderGraphNodeKind::Subtract:
            case render::ShaderGraphNodeKind::Multiply:
            case render::ShaderGraphNodeKind::Power:
            case render::ShaderGraphNodeKind::Dot:
            case render::ShaderGraphNodeKind::Divide:
                return kPinBinaryColors;
            case render::ShaderGraphNodeKind::Lerp:
                return kPinLerpColors;
            case render::ShaderGraphNodeKind::Clamp:
                return kPinClampColors;
            case render::ShaderGraphNodeKind::Sine:
            case render::ShaderGraphNodeKind::OneMinus:
            case render::ShaderGraphNodeKind::Normalize:
                return kPinNormalizeColors;
            case render::ShaderGraphNodeKind::TextureSample: return kPinOutVec2Colors;
            case render::ShaderGraphNodeKind::NoiseTexture:
                return kPinNoiseInputColors;
            case render::ShaderGraphNodeKind::Output:
                return kPinOutputColors;
            default:
                return nullptr;
            }
        }

        ImU32 *OutputPinColors(render::ShaderGraphNodeKind kind, bool componentPins)
        {
            switch (kind)
            {
            case render::ShaderGraphNodeKind::TextureSample: return kPinTextureOutputColors;
            case render::ShaderGraphNodeKind::Time:
            case render::ShaderGraphNodeKind::Dot:
            case render::ShaderGraphNodeKind::Float:
                return kPinOutFloatColors;
            case render::ShaderGraphNodeKind::Vec2:
                return kPinVec2OutputColors;
            case render::ShaderGraphNodeKind::MeshUV:
                return kPinOutVec2Colors;
            case render::ShaderGraphNodeKind::Vec3:
                return kPinVec3OutputColors;
            case render::ShaderGraphNodeKind::Color:
                return kPinTextureOutputColors;
            case render::ShaderGraphNodeKind::NoiseTexture:
                return kPinNoiseOutputColors;
            case render::ShaderGraphNodeKind::Parameter:
            case render::ShaderGraphNodeKind::WorldPosition:
            case render::ShaderGraphNodeKind::WorldNormal:
            case render::ShaderGraphNodeKind::ViewDirection:
            case render::ShaderGraphNodeKind::Power:
            case render::ShaderGraphNodeKind::Sine:
            case render::ShaderGraphNodeKind::OneMinus:
            case render::ShaderGraphNodeKind::MaterialInput:
            case render::ShaderGraphNodeKind::Add:
            case render::ShaderGraphNodeKind::Subtract:
            case render::ShaderGraphNodeKind::Multiply:
            case render::ShaderGraphNodeKind::Divide:
            case render::ShaderGraphNodeKind::Lerp:
            case render::ShaderGraphNodeKind::Clamp:
            case render::ShaderGraphNodeKind::Normalize:
                return kPinOutAnyColors;
            default:
                return nullptr;
            }
        }

        int PinCount(render::ShaderGraphNodeKind kind, bool componentPins = false)
        {
            ImU8 inputCount = 0;
            ImU8 outputCount = 0;
            InputPins(kind, componentPins, inputCount);
            OutputPins(kind, componentPins, outputCount);
            return std::max(static_cast<int>(inputCount), static_cast<int>(outputCount));
        }

        float NodeHeight(render::ShaderGraphNodeKind kind, bool componentPins = false)
        {
            return std::max(kNodeMinHeight, 42.0f + static_cast<float>(std::max(1, PinCount(kind, componentPins))) * kSlotVerticalSpacing);
        }

        float NodeWidth(const render::ShaderGraphNode &node)
        {
            return std::max(kNodeMinWidth, node.size.x > 0.0f ? node.size.x : kDefaultNodeWidth);
        }

        float NodeHeight(const render::ShaderGraphNode &node)
        {
            if (node.collapsed)
            {
                return kCollapsedNodeHeight;
            }

            const float defaultHeight = NodeHeight(node.kind, node.componentPins);
            return std::max(defaultHeight, node.size.y > 0.0f ? node.size.y : defaultHeight);
        }

        float MinimumNodeHeight(const render::ShaderGraphNode &node)
        {
            return node.collapsed ? kCollapsedNodeHeight : NodeHeight(node.kind, node.componentPins);
        }

        float MeasureTextWidth(std::string_view text, float fontSize)
        {
            return ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.data(), text.data() + text.size()).x;
        }

        std::string EllipsizeText(std::string text, float maxWidth, float fontSize)
        {
            if (maxWidth <= 0.0f)
            {
                return {};
            }

            if (MeasureTextWidth(text, fontSize) <= maxWidth)
            {
                return text;
            }

            constexpr const char *ellipsis = "...";
            while (!text.empty() && MeasureTextWidth(text + ellipsis, fontSize) > maxWidth)
            {
                text.pop_back();
            }
            return text.empty() ? std::string(ellipsis) : text + ellipsis;
        }

        void DrawFittedText(ImDrawList *drawList, const ImVec2 &position, ImU32 color, std::string text, float maxWidth, float fontSize)
        {
            text = EllipsizeText(std::move(text), maxWidth, fontSize);
            if (text.empty())
            {
                return;
            }

            drawList->AddText(ImGui::GetFont(), fontSize, position, color, text.c_str());
        }

        ImU32 ColorFromVec4(const glm::vec4 &value)
        {
            return ImGui::ColorConvertFloat4ToU32(ImVec4(std::clamp(value.x, 0.0f, 1.0f),
                                                        std::clamp(value.y, 0.0f, 1.0f),
                                                        std::clamp(value.z, 0.0f, 1.0f),
                                                        std::clamp(value.w, 0.0f, 1.0f)));
        }

        float PreviewNoise(int x, int y, float seed)
        {
            const int n = x * 15731 + y * 789221 + static_cast<int>(seed * 131.0f);
            return static_cast<float>((n * (n * n * 15731 + 789221) + 1376312589) & 0x7fffffff) / 2147483647.0f;
        }

        float PreviewHash(float x, float y)
        {
            const float value = sinf(x * 127.1f + y * 311.7f) * 43758.5453f;
            return value - floorf(value);
        }

        float PreviewShaderNoise(const glm::vec2 &value)
        {
            const glm::vec2 cell(floorf(value.x), floorf(value.y));
            const glm::vec2 local(value.x - cell.x, value.y - cell.y);
            const glm::vec2 curve(local.x * local.x * (3.0f - 2.0f * local.x),
                                  local.y * local.y * (3.0f - 2.0f * local.y));
            const float bottomLeft = PreviewHash(cell.x, cell.y);
            const float bottomRight = PreviewHash(cell.x + 1.0f, cell.y);
            const float topLeft = PreviewHash(cell.x, cell.y + 1.0f);
            const float topRight = PreviewHash(cell.x + 1.0f, cell.y + 1.0f);
            const float bottom = bottomLeft + (bottomRight - bottomLeft) * curve.x;
            const float top = topLeft + (topRight - topLeft) * curve.x;
            return bottom + (top - bottom) * curve.y;
        }

        const render::ShaderGraphNode *FindPreviewNode(const render::ShaderGraph &graph, int nodeId)
        {
            const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(),
                                            [nodeId](const render::ShaderGraphNode &node)
                                            {
                                                return node.id == nodeId;
                                            });
            return found == graph.nodes.end() ? nullptr : &*found;
        }

        const render::ShaderGraphLink *FindPreviewInputLink(const render::ShaderGraph &graph, int nodeId, std::string_view pin)
        {
            const auto found = std::find_if(graph.links.begin(), graph.links.end(),
                                            [nodeId, pin](const render::ShaderGraphLink &link)
                                            {
                                                return link.toNodeId == nodeId && link.toPin == pin;
                                            });
            return found == graph.links.end() ? nullptr : &*found;
        }

        glm::vec4 ToPreviewVec4(const glm::vec4 &value, int components)
        {
            if (components <= 1)
            {
                return glm::vec4(value.x, value.x, value.x, 1.0f);
            }
            if (components == 2)
            {
                return glm::vec4(value.x, value.y, 0.0f, 1.0f);
            }
            if (components == 3)
            {
                return glm::vec4(value.x, value.y, value.z, 1.0f);
            }
            return value;
        }

        float ToPreviewFloat(const glm::vec4 &value)
        {
            return value.x;
        }

        glm::vec2 ToPreviewVec2(const glm::vec4 &value, int components)
        {
            if (components <= 1)
            {
                return glm::vec2(value.x);
            }
            return glm::vec2(value.x, value.y);
        }

        struct PreviewSample
        {
            glm::vec4 value{0.0f, 0.0f, 0.0f, 1.0f};
            int components = 4;
        };

        PreviewSample EvaluatePreviewNode(const render::ShaderGraph &graph,
                                          int nodeId,
                                          std::string_view pin,
                                          const glm::vec2 &uv,
                                          std::unordered_set<int> &visiting);

        PreviewSample EvaluatePreviewInput(const render::ShaderGraph &graph,
                                           int nodeId,
                                           const char *pin,
                                           const glm::vec2 &uv,
                                           const PreviewSample &fallback,
                                           std::unordered_set<int> &visiting)
        {
            if (const auto *link = FindPreviewInputLink(graph, nodeId, pin))
            {
                return EvaluatePreviewNode(graph, link->fromNodeId, link->fromPin, uv, visiting);
            }
            return fallback;
        }

        PreviewSample EvaluatePreviewMaterialInput(render::ShaderGraphMaterialInput input, const glm::vec2 &uv)
        {
            switch (input)
            {
            case render::ShaderGraphMaterialInput::Normal:
                return PreviewSample{glm::vec4(0.5f + (uv.x - 0.5f) * 0.35f, 0.5f + (uv.y - 0.5f) * 0.35f, 1.0f, 1.0f), 3};
            case render::ShaderGraphMaterialInput::Metallic:
                return PreviewSample{glm::vec4(0.0f), 1};
            case render::ShaderGraphMaterialInput::Roughness:
                return PreviewSample{glm::vec4(1.0f), 1};
            case render::ShaderGraphMaterialInput::Opacity:
                return PreviewSample{glm::vec4(1.0f), 1};
            case render::ShaderGraphMaterialInput::UV:
                return PreviewSample{glm::vec4(uv.x, uv.y, 0.0f, 1.0f), 2};
            case render::ShaderGraphMaterialInput::Emission:
                return PreviewSample{glm::vec4(0.0f), 3};
            case render::ShaderGraphMaterialInput::Color:
            default:
                return PreviewSample{glm::vec4(uv.x, uv.y, 1.0f - uv.x * 0.5f, 1.0f), 4};
            }
        }

        PreviewSample EvaluatePreviewNode(const render::ShaderGraph &graph,
                                          int nodeId,
                                          std::string_view pin,
                                          const glm::vec2 &uv,
                                          std::unordered_set<int> &visiting)
        {
            const auto *node = FindPreviewNode(graph, nodeId);
            if (!node || visiting.find(nodeId) != visiting.end())
            {
                return PreviewSample{};
            }

            visiting.insert(nodeId);
            PreviewSample result;
            switch (node->kind)
            {
            case render::ShaderGraphNodeKind::MaterialInput:
                result = EvaluatePreviewMaterialInput(node->materialInput, uv);
                break;
            case render::ShaderGraphNodeKind::Parameter:
            {
                const auto variable=std::find_if(graph.variables.begin(),graph.variables.end(),[&](const auto &v){return v.name==node->parameter;});
                if(variable!=graph.variables.end()) result={variable->value,int(variable->type)+1};
                break;
            }
            case render::ShaderGraphNodeKind::Time: result={glm::vec4(render::ShaderGraphTimeSeconds()),1};break;
            case render::ShaderGraphNodeKind::WorldPosition: result={glm::vec4(uv.x-.5f,uv.y-.5f,0,1),3};break;
            case render::ShaderGraphNodeKind::WorldNormal: result=EvaluatePreviewMaterialInput(render::ShaderGraphMaterialInput::Normal,uv);break;
            case render::ShaderGraphNodeKind::ViewDirection: result={glm::vec4(0,0,1,1),3};break;
            case render::ShaderGraphNodeKind::TextureSample:
                result={node->materialInput==render::ShaderGraphMaterialInput::Normal?glm::vec4(.5f,.5f,1,1):glm::vec4(1),4};
                if(pin!="Color") result={glm::vec4(result.value[int(std::min(std::string_view("RGBA").find(pin),size_t(3)))]),1};
                break;
            case render::ShaderGraphNodeKind::Sine:
            case render::ShaderGraphNodeKind::OneMinus:
            {
                auto value=EvaluatePreviewInput(graph,nodeId,"Value",uv,{glm::vec4(0),1},visiting);
                result={node->kind==render::ShaderGraphNodeKind::Sine?glm::sin(value.value):glm::vec4(1)-value.value,value.components};break;
            }
            case render::ShaderGraphNodeKind::Power:
            case render::ShaderGraphNodeKind::Dot:
            {
                auto a=EvaluatePreviewInput(graph,nodeId,"A",uv,{glm::vec4(node->kind==render::ShaderGraphNodeKind::Power?1:0),1},visiting);
                auto b=EvaluatePreviewInput(graph,nodeId,"B",uv,{glm::vec4(node->kind==render::ShaderGraphNodeKind::Power?1:0),1},visiting);
                int dimensions=std::max(a.components,b.components);
                if(node->kind==render::ShaderGraphNodeKind::Power) result={glm::pow(glm::max(a.value,glm::vec4(.000001f)),b.value),dimensions};
                else { float dot=0;for(int i=0;i<dimensions;++i) dot+=a.value[i]*b.value[i];result={glm::vec4(dot),1}; }
                break;
            }
            case render::ShaderGraphNodeKind::Float:
                result = PreviewSample{glm::vec4(node->value.x), 1};
                break;
            case render::ShaderGraphNodeKind::Vec2:
            case render::ShaderGraphNodeKind::Vec3:
            case render::ShaderGraphNodeKind::Color:
            {
                int dimensions=node->kind==render::ShaderGraphNodeKind::Vec2?2:node->kind==render::ShaderGraphNodeKind::Vec3?3:4;
                std::string_view packed=dimensions==2?"Vec2":dimensions==3?"Vec3":"Color";
                std::string_view components=dimensions==4?"RGBA":"XYZ";
                if(node->componentPins)
                {
                    glm::vec4 value(0,0,0,1);
                    for(int i=0;i<dimensions;++i)
                        value[i]=EvaluatePreviewInput(graph,nodeId,std::string(1,components[i]).c_str(),uv,{glm::vec4(node->value[i]),1},visiting).value.x;
                    result=pin==packed?PreviewSample{value,dimensions}:PreviewSample{glm::vec4(value[int(std::min(components.find(pin),size_t(dimensions-1)))]),1};
                }
                else
                {
                    auto value=EvaluatePreviewInput(graph,nodeId,std::string(packed).c_str(),uv,{node->value,dimensions},visiting);
                    result=pin==packed?value:PreviewSample{glm::vec4(value.value[int(std::min(components.find(pin),size_t(dimensions-1)))]),1};
                }
                break;
            }
            case render::ShaderGraphNodeKind::Add:
            case render::ShaderGraphNodeKind::Subtract:
            case render::ShaderGraphNodeKind::Multiply:
            case render::ShaderGraphNodeKind::Divide:
            {
                const PreviewSample a = EvaluatePreviewInput(graph, nodeId, "A", uv, PreviewSample{glm::vec4(node->kind == render::ShaderGraphNodeKind::Multiply || node->kind == render::ShaderGraphNodeKind::Divide ? 1.0f : 0.0f), 1}, visiting);
                const PreviewSample b = EvaluatePreviewInput(graph, nodeId, "B", uv, PreviewSample{glm::vec4(node->kind == render::ShaderGraphNodeKind::Multiply || node->kind == render::ShaderGraphNodeKind::Divide ? 1.0f : 0.0f), 1}, visiting);
                if (node->kind == render::ShaderGraphNodeKind::Add)
                {
                    result = PreviewSample{a.value + b.value, std::max(a.components, b.components)};
                }
                else if (node->kind == render::ShaderGraphNodeKind::Subtract)
                {
                    result = PreviewSample{a.value - b.value, std::max(a.components, b.components)};
                }
                else if (node->kind == render::ShaderGraphNodeKind::Multiply)
                {
                    result = PreviewSample{a.value * b.value, std::max(a.components, b.components)};
                }
                else
                {
                    auto divisor=b.value;
                    for(int i=0;i<4;++i) divisor[i]=divisor[i]<0?-std::max(-divisor[i],.0001f):std::max(divisor[i],.0001f);
                    result = PreviewSample{a.value / divisor, std::max(a.components, b.components)};
                }
                break;
            }
            case render::ShaderGraphNodeKind::Lerp:
            {
                const PreviewSample a = EvaluatePreviewInput(graph, nodeId, "A", uv, PreviewSample{glm::vec4(0.0f), 1}, visiting);
                const PreviewSample b = EvaluatePreviewInput(graph, nodeId, "B", uv, PreviewSample{glm::vec4(1.0f), 1}, visiting);
                const PreviewSample t = EvaluatePreviewInput(graph, nodeId, "T", uv, PreviewSample{glm::vec4(0.5f), 1}, visiting);
                result = PreviewSample{a.value + (b.value - a.value) * t.value, std::max(a.components, b.components)};
                break;
            }
            case render::ShaderGraphNodeKind::Clamp:
            {
                const PreviewSample value = EvaluatePreviewInput(graph, nodeId, "Value", uv, PreviewSample{glm::vec4(0.0f), 1}, visiting);
                const PreviewSample minValue = EvaluatePreviewInput(graph, nodeId, "Min", uv, PreviewSample{glm::vec4(0.0f), 1}, visiting);
                const PreviewSample maxValue = EvaluatePreviewInput(graph, nodeId, "Max", uv, PreviewSample{glm::vec4(1.0f), 1}, visiting);
                result = PreviewSample{glm::clamp(value.value, glm::min(minValue.value,maxValue.value), glm::max(minValue.value,maxValue.value)), value.components};
                break;
            }
            case render::ShaderGraphNodeKind::Normalize:
            {
                const PreviewSample value = EvaluatePreviewInput(graph, nodeId, "Value", uv, PreviewSample{glm::vec4(0.0f, 0.0f, 1.0f, 1.0f), 3}, visiting);
                float lengthSquared=0;for(int i=0;i<value.components;++i) lengthSquared+=value.value[i]*value.value[i];
                result = PreviewSample{value.value/std::max(std::sqrt(lengthSquared),.000001f),value.components};
                break;
            }
            case render::ShaderGraphNodeKind::NoiseTexture:
            {
                const PreviewSample uvInput = EvaluatePreviewInput(graph, nodeId, "UV", uv, PreviewSample{glm::vec4(uv.x, uv.y, 0.0f, 1.0f), 2}, visiting);
                const PreviewSample scaleInput = EvaluatePreviewInput(graph, nodeId, "Scale", uv, PreviewSample{glm::vec4(std::max(node->value.x,0.0f)), 1}, visiting);
                const PreviewSample strengthInput = EvaluatePreviewInput(graph, nodeId, "Strength", uv, PreviewSample{glm::vec4(std::max(node->value.y,0.0f)), 1}, visiting);
                const glm::vec2 noiseUv = ToPreviewVec2(uvInput.value, uvInput.components) * ToPreviewFloat(scaleInput.value);
                const float value = PreviewShaderNoise(noiseUv) * ToPreviewFloat(strengthInput.value);
                result = pin == "Color" ? PreviewSample{glm::vec4(value, value, value, 1.0f), 4} : PreviewSample{glm::vec4(value), 1};
                break;
            }
            case render::ShaderGraphNodeKind::MeshUV:
                result = PreviewSample{glm::vec4(uv.x, uv.y, 0.0f, 1.0f), 2};
                break;
            case render::ShaderGraphNodeKind::Output:
            {
                const PreviewSample albedo = EvaluatePreviewInput(graph, nodeId, "Albedo", uv, EvaluatePreviewMaterialInput(render::ShaderGraphMaterialInput::Color, uv), visiting);
                const PreviewSample normal = EvaluatePreviewInput(graph, nodeId, "Normal", uv, EvaluatePreviewMaterialInput(render::ShaderGraphMaterialInput::Normal, uv), visiting);
                const PreviewSample metallic = EvaluatePreviewInput(graph, nodeId, "Metallic", uv, EvaluatePreviewMaterialInput(render::ShaderGraphMaterialInput::Metallic, uv), visiting);
                const PreviewSample roughness = EvaluatePreviewInput(graph, nodeId, "Roughness", uv, EvaluatePreviewMaterialInput(render::ShaderGraphMaterialInput::Roughness, uv), visiting);
                const PreviewSample opacity = EvaluatePreviewInput(graph, nodeId, "Opacity", uv, EvaluatePreviewMaterialInput(render::ShaderGraphMaterialInput::Opacity, uv), visiting);
                const PreviewSample emission = EvaluatePreviewInput(graph, nodeId, "Emission", uv, EvaluatePreviewMaterialInput(render::ShaderGraphMaterialInput::Emission, uv), visiting);
                const glm::vec3 lightDir = glm::normalize(glm::vec3(-0.35f, 0.55f, 1.0f));
                const glm::vec3 previewNormal = glm::normalize(glm::vec3(normal.value) * 2.0f - glm::vec3(1.0f));
                const float diffuse = std::clamp(glm::dot(previewNormal, lightDir) * 0.5f + 0.5f, 0.0f, 1.0f);
                const float metal = std::clamp(metallic.value.x, 0.0f, 1.0f);
                const float rough = std::clamp(roughness.value.x, 0.04f, 1.0f);
                const glm::vec3 baseColor = glm::vec3(albedo.value);
                const glm::vec3 shaded = graph.unlit ? baseColor + glm::vec3(emission.value) : baseColor * (0.22f + 0.78f * diffuse) + glm::vec3(1.0f - rough) * metal * 0.18f + glm::max(glm::vec3(emission.value), glm::vec3(0.0f));
                result = PreviewSample{glm::vec4(glm::clamp(shaded, glm::vec3(0.0f), glm::vec3(1.0f)), std::clamp(opacity.value.x, 0.0f, 1.0f)), 4};
                break;
            }
            }

            visiting.erase(nodeId);
            result.value = ToPreviewVec4(result.value, result.components);
            return result;
        }

        void DrawPreviewBars(ImDrawList *drawList, const ImRect &rect, const float *values, const ImU32 *colors, int count)
        {
            const float padding = std::clamp(rect.GetHeight() * 0.12f, 1.0f, 5.0f);
            const float gap = std::max(1.0f, rect.GetHeight() * 0.05f);
            const float barHeight = (rect.GetHeight() - padding * 2.0f - gap * static_cast<float>(std::max(0, count - 1))) / static_cast<float>(count);
            for (int index = 0; index < count; ++index)
            {
                const float y0 = rect.Min.y + padding + (barHeight + gap) * static_cast<float>(index);
                const ImRect barRect(ImVec2(rect.Min.x + padding, y0), ImVec2(rect.Max.x - padding, y0 + std::max(1.0f, barHeight)));
                drawList->AddRectFilled(barRect.Min, barRect.Max, IM_COL32(42, 47, 58, 255), 2.0f);
                const float fill = std::clamp(values[index], 0.0f, 1.0f);
                drawList->AddRectFilled(barRect.Min, ImVec2(barRect.Min.x + barRect.GetWidth() * fill, barRect.Max.y), colors[index], 2.0f);
            }
        }

        void DrawPreviewStripes(ImDrawList *drawList, const ImRect &rect, const ImU32 *colors, int count)
        {
            const float stripeWidth = rect.GetWidth() / static_cast<float>(count);
            for (int index = 0; index < count; ++index)
            {
                drawList->AddRectFilled(ImVec2(rect.Min.x + stripeWidth * static_cast<float>(index), rect.Min.y),
                                        ImVec2(rect.Min.x + stripeWidth * static_cast<float>(index + 1), rect.Max.y),
                                        colors[index]);
            }
        }

        const char *PreviewPrimaryPin(const render::ShaderGraphNode &node)
        {
            if (node.kind == render::ShaderGraphNodeKind::TextureSample) return "Color";
            if (node.kind == render::ShaderGraphNodeKind::NoiseTexture)
            {
                return "Color";
            }
            if (node.kind == render::ShaderGraphNodeKind::Color)
            {
                return node.componentPins ? "R" : "Color";
            }
            if (node.kind == render::ShaderGraphNodeKind::Vec2)
            {
                return node.componentPins ? "X" : "Vec2";
            }
            if (node.kind == render::ShaderGraphNodeKind::Vec3)
            {
                return node.componentPins ? "X" : "Vec3";
            }
            if (node.kind == render::ShaderGraphNodeKind::Output)
            {
                return "Albedo";
            }
            return "Out";
        }

        void DrawNodePreview(ImDrawList *drawList, const render::ShaderGraph &graph, const render::ShaderGraphNode &node, const ImRect &rect, float zoomFactor)
        {
            if (rect.GetWidth() < 18.0f || rect.GetHeight() < 10.0f)
            {
                return;
            }

            const float rounding = std::max(1.0f, 4.0f * zoomFactor);
            const ImRect inner(ImVec2(rect.Min.x + 2.0f, rect.Min.y + 2.0f),
                               ImVec2(rect.Max.x - 2.0f, rect.Max.y - 2.0f));

            drawList->AddRectFilled(rect.Min, rect.Max, IM_COL32(20, 23, 29, 210), rounding);
            drawList->AddRect(rect.Min, rect.Max, IM_COL32(90, 100, 118, 180), rounding);

            const int columns = std::clamp(static_cast<int>(inner.GetWidth() / 5.0f), 4, 24);
            const int rows = std::clamp(static_cast<int>(inner.GetHeight() / 5.0f), 2, 12);
            const float cellWidth = inner.GetWidth() / static_cast<float>(columns);
            const float cellHeight = inner.GetHeight() / static_cast<float>(rows);
            for (int y = 0; y < rows; ++y)
            {
                for (int x = 0; x < columns; ++x)
                {
                    const glm::vec2 uv((static_cast<float>(x) + 0.5f) / static_cast<float>(columns),
                                       (static_cast<float>(y) + 0.5f) / static_cast<float>(rows));
                    std::unordered_set<int> visiting;
                    const PreviewSample sample = EvaluatePreviewNode(graph, node.id, PreviewPrimaryPin(node), uv, visiting);
                    drawList->AddRectFilled(ImVec2(inner.Min.x + cellWidth * static_cast<float>(x), inner.Min.y + cellHeight * static_cast<float>(y)),
                                            ImVec2(inner.Min.x + cellWidth * static_cast<float>(x + 1), inner.Min.y + cellHeight * static_cast<float>(y + 1)),
                                            ColorFromVec4(sample.value));
                }
            }
        }

        GraphEditor::Template BuildTemplate(render::ShaderGraphNodeKind kind, bool componentPins = false)
        {
            ImU8 inputCount = 0;
            ImU8 outputCount = 0;
            const char **inputs = InputPins(kind, componentPins, inputCount);
            const char **outputs = OutputPins(kind, componentPins, outputCount);
            ImU32 headerColor = IM_COL32(68, 86, 117, 255);
            ImU32 backgroundColor = IM_COL32(38, 44, 55, 255);
            ImU32 backgroundColorOver = IM_COL32(48, 57, 72, 255);

            if (kind == render::ShaderGraphNodeKind::Output)
            {
                headerColor = IM_COL32(108, 73, 45, 255);
                backgroundColor = IM_COL32(55, 44, 34, 255);
                backgroundColorOver = IM_COL32(72, 55, 40, 255);
            }
            else if (kind == render::ShaderGraphNodeKind::MaterialInput ||
                     kind == render::ShaderGraphNodeKind::MeshUV)
            {
                headerColor = IM_COL32(58, 102, 75, 255);
                backgroundColor = IM_COL32(35, 55, 43, 255);
                backgroundColorOver = IM_COL32(43, 70, 52, 255);
            }
            else if (kind == render::ShaderGraphNodeKind::Float ||
                     kind == render::ShaderGraphNodeKind::Vec2 ||
                     kind == render::ShaderGraphNodeKind::Vec3 ||
                     kind == render::ShaderGraphNodeKind::Color ||
                     kind == render::ShaderGraphNodeKind::NoiseTexture)
            {
                headerColor = IM_COL32(96, 76, 124, 255);
                backgroundColor = IM_COL32(49, 42, 61, 255);
                backgroundColorOver = IM_COL32(62, 52, 78, 255);
            }

            return GraphEditor::Template{
                headerColor,
                backgroundColor,
                backgroundColorOver,
                inputCount,
                inputs,
                InputPinColors(kind, componentPins),
                outputCount,
                outputs,
                OutputPinColors(kind, componentPins),
            };
        }

        int NextNodeId(const render::ShaderGraph &graph)
        {
            int id = 1;
            for (const auto &node : graph.nodes)
            {
                id = std::max(id, node.id + 1);
            }
            return id;
        }

        int NextLinkId(const render::ShaderGraph &graph)
        {
            int id = 1;
            for (const auto &link : graph.links)
            {
                id = std::max(id, link.id + 1);
            }
            return id;
        }

        render::ShaderGraphNode *FindNode(render::ShaderGraph &graph, int id)
        {
            const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(),
                                            [id](const render::ShaderGraphNode &node)
                                            {
                                                return node.id == id;
                                            });
            return found == graph.nodes.end() ? nullptr : &*found;
        }

        const render::ShaderGraphNode *FindNode(const render::ShaderGraph &graph, int id)
        {
            const auto found = std::find_if(graph.nodes.begin(), graph.nodes.end(),
                                            [id](const render::ShaderGraphNode &node)
                                            {
                                                return node.id == id;
                                            });
            return found == graph.nodes.end() ? nullptr : &*found;
        }

        bool IsSelected(const std::vector<int> &selectedNodeIds, int nodeId)
        {
            return std::find(selectedNodeIds.begin(), selectedNodeIds.end(), nodeId) != selectedNodeIds.end();
        }

        ImRect ResizeGripRect(const ImRect &nodeRect, float zoomFactor)
        {
            const float gripSize = std::max(10.0f, kResizeGripSize * zoomFactor);
            return ImRect(ImVec2(nodeRect.Max.x - gripSize, nodeRect.Max.y - gripSize),
                          ImVec2(nodeRect.Max.x + 2.0f * zoomFactor, nodeRect.Max.y + 2.0f * zoomFactor));
        }

        ImRect CollapseToggleRect(const ImRect &nodeRect, float zoomFactor)
        {
            const float toggleSize = std::max(10.0f, 15.0f * zoomFactor);
            return ImRect(ImVec2(nodeRect.Min.x + 4.0f * zoomFactor, nodeRect.Min.y + 2.0f * zoomFactor),
                          ImVec2(nodeRect.Min.x + 4.0f * zoomFactor + toggleSize, nodeRect.Min.y + 2.0f * zoomFactor + toggleSize));
        }

        std::size_t NodeIndexForId(const render::ShaderGraph &graph, int nodeId)
        {
            for (std::size_t index = 0; index < graph.nodes.size(); ++index)
            {
                if (graph.nodes[index].id == nodeId)
                {
                    return index;
                }
            }
            return static_cast<std::size_t>(-1);
        }

        GraphEditor::SlotIndex InputSlotForPin(const render::ShaderGraphNode &node, const std::string &pinName)
        {
            ImU8 count = 0;
            const char **pins = InputPins(node, count);
            for (GraphEditor::SlotIndex index = 0; index < count; ++index)
            {
                if (pinName == pins[index])
                {
                    return index;
                }
            }
            return 0;
        }

        GraphEditor::SlotIndex OutputSlotForPin(const render::ShaderGraphNode &node, const std::string &pinName)
        {
            ImU8 count = 0;
            const char **pins = OutputPins(node, count);
            for (GraphEditor::SlotIndex index = 0; index < count; ++index)
            {
                if (pinName == pins[index])
                {
                    return index;
                }
            }
            return 0;
        }

        bool CanCreateLink(const render::ShaderGraph &graph, std::size_t outputNodeIndex, std::size_t inputNodeIndex)
        {
            if (outputNodeIndex == inputNodeIndex ||
                outputNodeIndex >= graph.nodes.size() ||
                inputNodeIndex >= graph.nodes.size())
            {
                return false;
            }

            return graph.nodes[outputNodeIndex].kind != render::ShaderGraphNodeKind::Output;
        }

        void RemoveLinksForNode(render::ShaderGraph &graph, int nodeId)
        {
            graph.links.erase(std::remove_if(graph.links.begin(), graph.links.end(),
                                             [nodeId](const render::ShaderGraphLink &link)
                                             {
                                                 return link.fromNodeId == nodeId || link.toNodeId == nodeId;
                                             }),
                              graph.links.end());
        }

        class ShaderGraphDelegate : public GraphEditor::Delegate
        {
        public:
            ShaderGraphDelegate(render::ShaderGraph &graph,
                                std::vector<int> &selectedNodeIds,
                                int &selectedNodeId,
                                std::unordered_map<int, ImRect> &nodeScreenRects,
                                const GraphEditor::ViewState &viewState,
                                const ImVec2 &canvasScreenPos,
                                ImVec2 &addNodePosition,
                                int &resizingNodeId,
                                bool &openAddNodePopup,
                                bool showNodePreviews,
                                bool &dirty)
                : m_graph(graph),
                  m_selectedNodeIds(selectedNodeIds),
                  m_selectedNodeId(selectedNodeId),
                  m_nodeScreenRects(nodeScreenRects),
                  m_viewState(viewState),
                  m_canvasScreenPos(canvasScreenPos),
                  m_addNodePosition(addNodePosition),
                  m_resizingNodeId(resizingNodeId),
                  m_openAddNodePopup(openAddNodePopup),
                  m_showNodePreviews(showNodePreviews),
                  m_dirty(dirty)
            {
            }

            bool AllowedLink(GraphEditor::NodeIndex from, GraphEditor::NodeIndex to) override
            {
                return CanCreateLink(m_graph, to, from);
            }

            void SelectNode(GraphEditor::NodeIndex nodeIndex, bool selected) override
            {
                if (nodeIndex >= m_graph.nodes.size())
                {
                    return;
                }

                const int nodeId = m_graph.nodes[nodeIndex].id;
                if (selected && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    if (const auto rect = m_nodeScreenRects.find(nodeId); rect != m_nodeScreenRects.end())
                    {
                        const float zoomFactor = std::clamp((rect->second.GetWidth() + 8.0f) / NodeWidth(m_graph.nodes[nodeIndex]), 0.35f, 1.5f);
                        if (CollapseToggleRect(rect->second, zoomFactor).Contains(ImGui::GetIO().MouseClickedPos[0]))
                        {
                            auto &node = m_graph.nodes[nodeIndex];
                            node.collapsed = !node.collapsed;
                            if (node.size.x <= 0.0f)
                            {
                                node.size.x = kDefaultNodeWidth;
                            }
                            if (node.size.y <= 0.0f)
                            {
                                node.size.y = NodeHeight(node.kind, node.componentPins);
                            }
                            m_dirty = true;
                        }
                    }
                }

                if (selected)
                {
                    if (!IsSelected(m_selectedNodeIds, nodeId))
                    {
                        m_selectedNodeIds.push_back(nodeId);
                    }
                    m_selectedNodeId = nodeId;
                }
                else
                {
                    m_selectedNodeIds.erase(std::remove(m_selectedNodeIds.begin(), m_selectedNodeIds.end(), nodeId), m_selectedNodeIds.end());
                    if (m_selectedNodeId == nodeId)
                    {
                        m_selectedNodeId = m_selectedNodeIds.empty() ? 0 : m_selectedNodeIds.back();
                    }
                }
            }

            void MoveSelectedNodes(const ImVec2 delta) override
            {
                const ImVec2 clickPosition = ImGui::GetIO().MouseClickedPos[0];
                if (m_resizingNodeId == 0)
                {
                    for (auto &node : m_graph.nodes)
                    {
                        if (!IsSelected(m_selectedNodeIds, node.id))
                        {
                            continue;
                        }

                        const auto rect = m_nodeScreenRects.find(node.id);
                        if (rect == m_nodeScreenRects.end())
                        {
                            continue;
                        }

                        const float zoomFactor = std::clamp((rect->second.GetWidth() + 8.0f) / NodeWidth(node), 0.35f, 1.5f);
                        if (ResizeGripRect(rect->second, zoomFactor).Contains(clickPosition))
                        {
                            m_resizingNodeId = node.id;
                            break;
                        }
                    }
                }

                if (m_resizingNodeId != 0)
                {
                    if (auto *node = FindNode(m_graph, m_resizingNodeId))
                    {
                        node->size.x = std::max(kNodeMinWidth, NodeWidth(*node) + delta.x);
                        if (!node->collapsed)
                        {
                            node->size.y = std::max(MinimumNodeHeight(*node), NodeHeight(*node) + delta.y);
                        }
                        else if (node->size.y <= 0.0f)
                        {
                            node->size.y = NodeHeight(node->kind, node->componentPins);
                        }
                        m_dirty = true;
                    }
                    return;
                }

                for (auto &node : m_graph.nodes)
                {
                    if (!IsSelected(m_selectedNodeIds, node.id))
                    {
                        continue;
                    }
                    node.position.x += delta.x;
                    node.position.y += delta.y;
                    m_dirty = true;
                }
            }

            void AddLink(GraphEditor::NodeIndex inputNodeIndex,
                         GraphEditor::SlotIndex inputSlotIndex,
                         GraphEditor::NodeIndex outputNodeIndex,
                         GraphEditor::SlotIndex outputSlotIndex) override
            {
                if (!CanCreateLink(m_graph, inputNodeIndex, outputNodeIndex))
                {
                    return;
                }

                const auto &outputNode = m_graph.nodes[inputNodeIndex];
                const auto &inputNode = m_graph.nodes[outputNodeIndex];
                const std::string inputPin = InputPinName(inputNode, outputSlotIndex);
                m_graph.links.erase(std::remove_if(m_graph.links.begin(), m_graph.links.end(),
                                                   [&inputNode, &inputPin](const render::ShaderGraphLink &link)
                                                   {
                                                       return link.toNodeId == inputNode.id && link.toPin == inputPin;
                                                   }),
                                    m_graph.links.end());
                m_graph.links.push_back(render::ShaderGraphLink{
                    .id = NextLinkId(m_graph),
                    .fromNodeId = outputNode.id,
                    .fromPin = OutputPinName(outputNode, inputSlotIndex),
                    .toNodeId = inputNode.id,
                    .toPin = inputPin,
                });
                m_dirty = true;
            }

            void DelLink(GraphEditor::LinkIndex linkIndex) override
            {
                if (linkIndex >= m_graph.links.size())
                {
                    return;
                }
                m_graph.links.erase(m_graph.links.begin() + static_cast<std::ptrdiff_t>(linkIndex));
                m_dirty = true;
            }

            void CustomDraw(ImDrawList *drawList, ImRect rectangle, GraphEditor::NodeIndex nodeIndex) override
            {
                if (nodeIndex >= m_graph.nodes.size())
                {
                    return;
                }

                auto &node = m_graph.nodes[nodeIndex];
                const float zoomFactor = std::clamp((rectangle.GetWidth() + 8.0f) / NodeWidth(node), 0.35f, 1.5f);
                const float titleFontSize = std::max(8.0f, ImGui::GetFontSize() * zoomFactor);
                const float pinFontSize = std::max(7.0f, 13.0f * zoomFactor);
                const ImRect nodeRect(ImVec2(rectangle.Min.x - 3.0f, rectangle.Min.y - 23.0f),
                                      ImVec2(rectangle.Max.x + 3.0f, rectangle.Max.y + 3.0f));
                const ImVec2 titleMin = nodeRect.Min;
                const ImVec2 titleMax(nodeRect.Max.x, nodeRect.Min.y + 20.0f);
                const ImRect toggleRect = CollapseToggleRect(nodeRect, zoomFactor);
                const float toggleSize = toggleRect.GetWidth();
                m_nodeScreenRects[node.id] = nodeRect;

                const ImVec2 toggleCenter((toggleRect.Min.x + toggleRect.Max.x) * 0.5f,
                                          (toggleRect.Min.y + toggleRect.Max.y) * 0.5f);
                const float toggleHalf = toggleSize * 0.32f;
                drawList->AddRect(ImVec2(toggleCenter.x - toggleHalf, toggleCenter.y - toggleHalf),
                                  ImVec2(toggleCenter.x + toggleHalf, toggleCenter.y + toggleHalf),
                                  IM_COL32(210, 216, 226, 230),
                                  2.0f * zoomFactor);
                drawList->AddLine(ImVec2(toggleCenter.x - toggleHalf * 0.55f, toggleCenter.y),
                                  ImVec2(toggleCenter.x + toggleHalf * 0.55f, toggleCenter.y),
                                  IM_COL32(210, 216, 226, 230),
                                  1.2f * zoomFactor);
                if (node.collapsed)
                {
                    drawList->AddLine(ImVec2(toggleCenter.x, toggleCenter.y - toggleHalf * 0.55f),
                                      ImVec2(toggleCenter.x, toggleCenter.y + toggleHalf * 0.55f),
                                      IM_COL32(210, 216, 226, 230),
                                      1.2f * zoomFactor);
                }

                drawList->PushClipRect(titleMin, titleMax, true);
                DrawFittedText(drawList,
                               ImVec2(titleMin.x + 4.0f * zoomFactor + toggleSize + 6.0f * zoomFactor, titleMin.y + 2.0f * zoomFactor),
                               IM_COL32(238, 240, 245, 255),
                               node.name.empty() ? render::ToString(node.kind) : node.name,
                               std::max(0.0f, titleMax.x - titleMin.x - toggleSize - 16.0f * zoomFactor),
                               titleFontSize);
                drawList->PopClipRect();

                if (!node.collapsed)
                {
                    drawList->PushClipRect(rectangle.Min, rectangle.Max, true);
                    ImU8 inputCount = 0;
                    ImU8 outputCount = 0;
                    const char **inputPins = InputPins(node, inputCount);
                    const char **outputPins = OutputPins(node, outputCount);
                    const float nodeTop = rectangle.Min.y - 23.0f;
                    const float nodeHeight = rectangle.GetHeight() + 26.0f;
                    const float labelPadding = 16.0f * zoomFactor;
                    const float labelYAdjust = pinFontSize * 0.5f;
                    const float leftLabelX = rectangle.Min.x + labelPadding;
                    const float rightLimit = rectangle.Max.x - labelPadding;
                    const float inputLabelWidth = std::max(0.0f, rectangle.GetWidth() * 0.45f);
                    const float outputLabelWidth = std::max(0.0f, rectangle.GetWidth() * 0.45f);

                    for (ImU8 index = 0; index < inputCount; ++index)
                    {
                        const float y = nodeTop + nodeHeight * (static_cast<float>(index) + 1.0f) / (static_cast<float>(inputCount) + 1.0f) + 8.0f - labelYAdjust;
                        DrawFittedText(drawList, ImVec2(leftLabelX, y), IM_COL32(190, 198, 210, 255), inputPins[index], inputLabelWidth, pinFontSize);
                    }

                    for (ImU8 index = 0; index < outputCount; ++index)
                    {
                        std::string label = outputPins[index] ? outputPins[index] : "";
                        label = EllipsizeText(std::move(label), outputLabelWidth, pinFontSize);
                        const float textWidth = MeasureTextWidth(label, pinFontSize);
                        const float y = nodeTop + nodeHeight * (static_cast<float>(index) + 1.0f) / (static_cast<float>(outputCount) + 1.0f) + 8.0f - labelYAdjust;
                        drawList->AddText(ImGui::GetFont(), pinFontSize, ImVec2(std::max(rectangle.Min.x + labelPadding, rightLimit - textWidth), y), IM_COL32(190, 198, 210, 255), label.c_str());
                    }
                    drawList->PopClipRect();

                    if (m_showNodePreviews)
                    {
                        const float sidePadding = std::max(8.0f, 52.0f * zoomFactor);
                        const float bottomPadding = std::max(3.0f, 8.0f * zoomFactor);
                        const float previewHeight = std::max(14.0f, kPreviewHeight * zoomFactor);
                        const ImRect previewRect(ImVec2(rectangle.Min.x + sidePadding, rectangle.Max.y - bottomPadding - previewHeight),
                                                 ImVec2(rectangle.Max.x - sidePadding, rectangle.Max.y - bottomPadding));
                        if (previewRect.Min.y > rectangle.Min.y + 4.0f && previewRect.GetWidth() >= 18.0f)
                        {
                            DrawNodePreview(drawList, m_graph, node, previewRect, zoomFactor);
                        }
                    }
                }

                const float gripSize = std::max(10.0f, kResizeGripSize * zoomFactor);
                const ImRect gripRect = ResizeGripRect(nodeRect, zoomFactor);
                const ImVec2 gripMin = gripRect.Min;
                const ImVec2 gripMax = gripRect.Max;
                drawList->AddTriangleFilled(ImVec2(gripMax.x, gripMax.y),
                                            ImVec2(gripMin.x, gripMax.y),
                                            ImVec2(gripMax.x, gripMin.y),
                                            IM_COL32(210, 216, 226, 120));
                drawList->AddLine(ImVec2(gripMax.x - gripSize * 0.35f, gripMax.y - 2.0f * zoomFactor),
                                  ImVec2(gripMax.x - 2.0f * zoomFactor, gripMax.y - gripSize * 0.35f),
                                  IM_COL32(210, 216, 226, 190),
                                  1.0f * zoomFactor);
            }

            void RightClick(GraphEditor::NodeIndex nodeIndex, GraphEditor::SlotIndex, GraphEditor::SlotIndex) override
            {
                if (nodeIndex < m_graph.nodes.size())
                {
                    m_selectedNodeId = m_graph.nodes[nodeIndex].id;
                    return;
                }

                const ImVec2 mouse = ImGui::GetIO().MousePos;
                const float factor = std::max(0.001f, m_viewState.mFactor);
                m_addNodePosition = ImVec2((mouse.x - m_canvasScreenPos.x) / factor - m_viewState.mPosition.x,
                                           (mouse.y - m_canvasScreenPos.y) / factor - m_viewState.mPosition.y);
                m_openAddNodePopup = true;
            }

            const size_t GetTemplateCount() override
            {
                return m_templates.size();
            }

            const GraphEditor::Template GetTemplate(GraphEditor::TemplateIndex index) override
            {
                return m_templates[index];
            }

            const size_t GetNodeCount() override
            {
                return m_graph.nodes.size();
            }

            const GraphEditor::Node GetNode(GraphEditor::NodeIndex index) override
            {
                const auto &node = m_graph.nodes[index];
                return GraphEditor::Node{
                    "",
                    TemplateIndexForNode(node),
                    ImRect(ImVec2(node.position.x, node.position.y),
                           ImVec2(node.position.x + NodeWidth(node), node.position.y + NodeHeight(node))),
                    IsSelected(m_selectedNodeIds, node.id),
                };
            }

            const size_t GetLinkCount() override
            {
                return m_graph.links.size();
            }

            const GraphEditor::Link GetLink(GraphEditor::LinkIndex index) override
            {
                const auto &link = m_graph.links[index];
                const auto inputIndex = NodeIndexForId(m_graph, link.toNodeId);
                const auto outputIndex = NodeIndexForId(m_graph, link.fromNodeId);
                const auto *inputNode = FindNode(m_graph, link.toNodeId);
                const auto *outputNode = FindNode(m_graph, link.fromNodeId);
                return GraphEditor::Link{
                    outputIndex,
                    outputNode ? OutputSlotForPin(*outputNode, link.fromPin) : 0,
                    inputIndex,
                    inputNode ? InputSlotForPin(*inputNode, link.toPin) : 0,
                };
            }

        private:
            GraphEditor::TemplateIndex TemplateIndexForNode(const render::ShaderGraphNode &node) const
            {
                if (node.componentPins)
                {
                    if (node.kind == render::ShaderGraphNodeKind::Vec2)
                    {
                        return 25;
                    }
                    if (node.kind == render::ShaderGraphNodeKind::Vec3)
                    {
                        return 26;
                    }
                    if (node.kind == render::ShaderGraphNodeKind::Color)
                    {
                        return 27;
                    }
                }
                return static_cast<GraphEditor::TemplateIndex>(std::clamp(static_cast<int>(node.kind), 0, static_cast<int>(m_templates.size() - 1)));
            }

            static const std::array<GraphEditor::Template, 28> m_templates;

            render::ShaderGraph &m_graph;
            std::vector<int> &m_selectedNodeIds;
            int &m_selectedNodeId;
            std::unordered_map<int, ImRect> &m_nodeScreenRects;
            const GraphEditor::ViewState &m_viewState;
            ImVec2 m_canvasScreenPos;
            ImVec2 &m_addNodePosition;
            int &m_resizingNodeId;
            bool &m_openAddNodePopup;
            bool m_showNodePreviews = true;
            bool &m_dirty;
        };

        const std::array<GraphEditor::Template, 28> ShaderGraphDelegate::m_templates = {
            BuildTemplate(render::ShaderGraphNodeKind::MaterialInput),
            BuildTemplate(render::ShaderGraphNodeKind::Float),
            BuildTemplate(render::ShaderGraphNodeKind::Vec2),
            BuildTemplate(render::ShaderGraphNodeKind::Vec3),
            BuildTemplate(render::ShaderGraphNodeKind::Color),
            BuildTemplate(render::ShaderGraphNodeKind::Add),
            BuildTemplate(render::ShaderGraphNodeKind::Subtract),
            BuildTemplate(render::ShaderGraphNodeKind::Multiply),
            BuildTemplate(render::ShaderGraphNodeKind::Divide),
            BuildTemplate(render::ShaderGraphNodeKind::Lerp),
            BuildTemplate(render::ShaderGraphNodeKind::Clamp),
            BuildTemplate(render::ShaderGraphNodeKind::Normalize),
            BuildTemplate(render::ShaderGraphNodeKind::NoiseTexture),
            BuildTemplate(render::ShaderGraphNodeKind::MeshUV),
            BuildTemplate(render::ShaderGraphNodeKind::Output),
            BuildTemplate(render::ShaderGraphNodeKind::Parameter),
            BuildTemplate(render::ShaderGraphNodeKind::Time),
            BuildTemplate(render::ShaderGraphNodeKind::WorldPosition),
            BuildTemplate(render::ShaderGraphNodeKind::WorldNormal),
            BuildTemplate(render::ShaderGraphNodeKind::ViewDirection),
            BuildTemplate(render::ShaderGraphNodeKind::Dot),
            BuildTemplate(render::ShaderGraphNodeKind::Sine),
            BuildTemplate(render::ShaderGraphNodeKind::Power),
            BuildTemplate(render::ShaderGraphNodeKind::OneMinus),
            BuildTemplate(render::ShaderGraphNodeKind::TextureSample),
            BuildTemplate(render::ShaderGraphNodeKind::Vec2, true),
            BuildTemplate(render::ShaderGraphNodeKind::Vec3, true),
            BuildTemplate(render::ShaderGraphNodeKind::Color, true),
        };

        bool IsOutputNode(const render::ShaderGraphNode &node)
        {
            return node.kind == render::ShaderGraphNodeKind::Output;
        }

        render::ShaderGraphNode MakeNode(render::ShaderGraph &graph, render::ShaderGraphNodeKind kind, const char *name)
        {
            render::ShaderGraphNode node{
                .id = NextNodeId(graph),
                .kind = kind,
                .name = name,
                .position = {60.0f + static_cast<float>(graph.nodes.size() % 5) * 34.0f,
                             60.0f + static_cast<float>(graph.nodes.size() % 7) * 28.0f},
                .size = {kDefaultNodeWidth, NodeHeight(kind)},
            };

            if (kind == render::ShaderGraphNodeKind::Parameter && !graph.variables.empty()) node.parameter = graph.variables.front().name;
            if (kind == render::ShaderGraphNodeKind::NoiseTexture)
            {
                node.value.x = 8.0f;
                node.value.y = 1.0f;
            }

            return node;
        }

        render::ShaderGraphNode MakeNodeAt(render::ShaderGraph &graph, render::ShaderGraphNodeKind kind, const char *name, const ImVec2 &position)
        {
            render::ShaderGraphNode node = MakeNode(graph, kind, name);
            node.position = {position.x, position.y};
            return node;
        }

        bool AddNodeMenuItem(const char *label, render::ShaderGraph &graph, render::ShaderGraphNodeKind kind, const char *name, const ImVec2 &position, bool &dirty)
        {
            if (!ImGui::MenuItem(label))
            {
                return false;
            }

            graph.nodes.push_back(MakeNodeAt(graph, kind, name, position));
            dirty = true;
            return true;
        }
    }

    void ShaderGraphEditorPanel::LoadActiveGraph()
    {
        auto &editorShell = EditorShell::GetInstance();
        m_loadedReference = editorShell.GetActiveShaderGraphAssetReference();
        bool loaded = false;
        m_graph = core::Engine::GetInstance().GetAssetManager().LoadShaderGraphAsset(m_loadedReference, &loaded);
        if (!loaded)
        {
            m_graph = render::CreateDefaultShaderGraph();
        }
        m_selectedNodeId = m_graph.nodes.empty() ? 0 : m_graph.nodes.front().id;
        m_selectedNodeIds.clear();
        if (m_selectedNodeId != 0)
        {
            m_selectedNodeIds.push_back(m_selectedNodeId);
        }
        m_graphFit = GraphEditor::Fit_AllNodes;
        m_nodeScreenRects.clear();
        m_openAddNodePopup = false;
        m_resizingNodeId = 0;
        m_dirty = false;
        m_undo.clear(); m_redo.clear(); m_editGesture=false;
    }

    void ShaderGraphEditorPanel::Render()
    {
        auto &editorShell = EditorShell::GetInstance();
        const auto &reference = editorShell.GetActiveShaderGraphAssetReference();
        if (reference.empty())
        {
            ImGui::TextDisabled("No shader graph selected.");
            return;
        }

        if (reference != m_loadedReference)
        {
            LoadActiveGraph();
        }
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
        {
            m_resizingNodeId = 0;
        }

        m_graphOptions.mMinimap = ImRect(ImVec2(0.0f, 0.0f), ImVec2(0.0f, 0.0f));
        m_graphOptions.mNodeSlotRadius = 5.5f;
        m_graphOptions.mLineThickness = 3.0f;
        m_graphOptions.mBorderThickness = 2.0f;
        m_graphOptions.mBorderSelectionThickness = 3.0f;
        m_graphOptions.mDrawIONameOnHover = false;
        m_graphOptions.mDisplayLinksAsCurves = false;

        const auto graphBefore = m_graph;
        const auto beforeHash = EditorGraphHash(m_graph);
        bool historyAction = false;
        const bool engineGraph = assets::Project::IsEngineAssetReference(reference);
        ImGui::TextWrapped("Shader Graph: %s", reference.c_str());
        if (engineGraph)
        {
            ImGui::TextDisabled("Engine shader graph assets are read-only.");
        }

        const bool shortcuts=ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput && ImGui::GetIO().KeyCtrl;
        ImGui::BeginDisabled(engineGraph || m_undo.empty());
        if (ImGui::Button("Undo") || (!engineGraph && !m_undo.empty() && shortcuts && ImGui::IsKeyPressed(ImGuiKey_Z)))
        {
            m_redo.push_back(m_graph);m_graph=std::move(m_undo.back());m_undo.pop_back();
            m_dirty=true;historyAction=true;m_selectedNodeId=0;m_selectedNodeIds.clear();m_editGesture=false;
        }
        ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::BeginDisabled(engineGraph || m_redo.empty());
        if (ImGui::Button("Redo") || (!engineGraph && !m_redo.empty() && shortcuts && ImGui::IsKeyPressed(ImGuiKey_Y)))
        {
            m_undo.push_back(m_graph);m_graph=std::move(m_redo.back());m_redo.pop_back();
            m_dirty=true;historyAction=true;m_selectedNodeId=0;m_selectedNodeIds.clear();m_editGesture=false;
        }
        ImGui::EndDisabled(); ImGui::SameLine();
        ImGui::BeginDisabled(engineGraph);
        if (ImGui::Button("Fit"))
        {
            m_graphFit = GraphEditor::Fit_AllNodes;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Previews", &m_showNodePreviews);
        if (ImGui::Checkbox("Unlit surface", &m_graph.unlit)) m_dirty = true;
        if (ImGui::Checkbox("Outline pass", &m_graph.outline.enabled)) m_dirty = true;
        if (m_graph.outline.enabled)
        {
            if (ImGui::DragFloat("Outline width (world units)", &m_graph.outline.width, 0.001f, 0.0f, 10.0f, "%.3f", ImGuiSliderFlags_AlwaysClamp)) m_dirty = true;
            if (ImGui::ColorEdit3("Outline colour", &m_graph.outline.color.x)) m_dirty = true;
            ImGui::TextDisabled("Opaque meshes; smooth closed surfaces give the best outline.");
        }
        ImGui::EndDisabled();

        ImGui::Separator();

        std::string validationError;
        const bool graphValid = render::ValidateShaderGraph(m_graph, &validationError);
        if (!graphValid) ImGui::TextWrapped("Graph error: %s", validationError.c_str());

        const float footerHeight = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;
        ImGui::BeginChild("ShaderGraphEditorBody", ImVec2(0.0f, -footerHeight), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        if (ImGui::BeginTable("ShaderGraphEditorLayout", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings))
        {
            ImGui::TableSetupColumn("Graph", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("Inspector", ImGuiTableColumnFlags_WidthFixed, 340.0f);
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::BeginChild("ShaderGraphCanvasHost", ImVec2(0.0f, 0.0f), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
            const ImVec2 canvasScreenPos = ImGui::GetCursorScreenPos();
            ShaderGraphDelegate delegate(m_graph,
                                         m_selectedNodeIds,
                                         m_selectedNodeId,
                                         m_nodeScreenRects,
                                         m_graphViewState,
                                         canvasScreenPos,
                                         m_addNodePosition,
                                         m_resizingNodeId,
                                         m_openAddNodePopup,
                                         m_showNodePreviews,
                                         m_dirty);
            graph_editor_panel::ShowWithCanvasWheelGuard(delegate, m_graphOptions, m_graphViewState, !engineGraph, &m_graphFit);

            if (m_openAddNodePopup)
            {
                ImGui::OpenPopup("ShaderGraphAddNodePopup");
                m_openAddNodePopup = false;
            }
            ImGui::BeginDisabled(engineGraph);
            if (ImGui::BeginPopup("ShaderGraphAddNodePopup"))
            {
                if (ImGui::BeginMenu("Inputs"))
                {
                    AddNodeMenuItem("Material Input", m_graph, render::ShaderGraphNodeKind::MaterialInput, "Material Input", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("Mesh UV", m_graph, render::ShaderGraphNodeKind::MeshUV, "Mesh UV", m_addNodePosition, m_dirty);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Values"))
                {
                    AddNodeMenuItem("Parameter", m_graph, render::ShaderGraphNodeKind::Parameter, "Parameter", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("Time (seconds)", m_graph, render::ShaderGraphNodeKind::Time, "Time", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("World Position", m_graph, render::ShaderGraphNodeKind::WorldPosition, "World Position", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("World Normal", m_graph, render::ShaderGraphNodeKind::WorldNormal, "World Normal", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("View Direction", m_graph, render::ShaderGraphNodeKind::ViewDirection, "View Direction", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("Float", m_graph, render::ShaderGraphNodeKind::Float, "Float", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("Vec2", m_graph, render::ShaderGraphNodeKind::Vec2, "Vec2", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("Vec3", m_graph, render::ShaderGraphNodeKind::Vec3, "Vec3", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("Color", m_graph, render::ShaderGraphNodeKind::Color, "Color", m_addNodePosition, m_dirty);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Textures"))
                {
                    AddNodeMenuItem("Material Texture Sample", m_graph, render::ShaderGraphNodeKind::TextureSample, "Texture Sample", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("Noise Texture", m_graph, render::ShaderGraphNodeKind::NoiseTexture, "Noise Texture", m_addNodePosition, m_dirty);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Math"))
                {
                    AddNodeMenuItem("Dot", m_graph, render::ShaderGraphNodeKind::Dot, "Dot", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("Sine", m_graph, render::ShaderGraphNodeKind::Sine, "Sine", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("Power", m_graph, render::ShaderGraphNodeKind::Power, "Power", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("One Minus", m_graph, render::ShaderGraphNodeKind::OneMinus, "One Minus", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("Add", m_graph, render::ShaderGraphNodeKind::Add, "Add", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("Subtract", m_graph, render::ShaderGraphNodeKind::Subtract, "Subtract", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("Multiply", m_graph, render::ShaderGraphNodeKind::Multiply, "Multiply", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("Divide", m_graph, render::ShaderGraphNodeKind::Divide, "Divide", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("Lerp", m_graph, render::ShaderGraphNodeKind::Lerp, "Lerp", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("Clamp", m_graph, render::ShaderGraphNodeKind::Clamp, "Clamp", m_addNodePosition, m_dirty);
                    AddNodeMenuItem("Normalize", m_graph, render::ShaderGraphNodeKind::Normalize, "Normalize", m_addNodePosition, m_dirty);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Output"))
                {
                    ImGui::BeginDisabled(std::any_of(m_graph.nodes.begin(), m_graph.nodes.end(), [](const auto &n) { return n.kind == render::ShaderGraphNodeKind::Output; }));
                    AddNodeMenuItem("Geometry Output", m_graph, render::ShaderGraphNodeKind::Output, "Geometry Output", m_addNodePosition, m_dirty);
                    ImGui::EndDisabled();
                    ImGui::EndMenu();
                }
                ImGui::EndPopup();
            }
            ImGui::EndDisabled();
            ImGui::EndChild();

            ImGui::TableSetColumnIndex(1);
            ImGui::BeginChild("ShaderGraphInspector", ImVec2(0.0f, 0.0f), true);
            ImGui::TextUnformatted("Inspector");
            ImGui::Separator();
            render::ShaderGraphNode *selectedNode = FindNode(m_graph, m_selectedNodeId);
            if (selectedNode)
            {
                ImGui::BeginDisabled(engineGraph);
                char nameBuffer[128]{};
                strncpy_s(nameBuffer, selectedNode->name.c_str(), _TRUNCATE);
                if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer)))
                {
                    selectedNode->name = nameBuffer;
                    m_dirty = true;
                }

                float position[2] = {selectedNode->position.x, selectedNode->position.y};
                if (ImGui::DragFloat2("Position", position, 1.0f))
                {
                    selectedNode->position = {position[0], position[1]};
                    m_dirty = true;
                }

                bool collapsed = selectedNode->collapsed;
                if (ImGui::Checkbox("Collapsed", &collapsed))
                {
                    selectedNode->collapsed = collapsed;
                    if (selectedNode->size.x <= 0.0f)
                    {
                        selectedNode->size.x = kDefaultNodeWidth;
                    }
                    if (selectedNode->size.y <= 0.0f)
                    {
                        selectedNode->size.y = NodeHeight(selectedNode->kind, selectedNode->componentPins);
                    }
                    m_dirty = true;
                }

                float size[2] = {NodeWidth(*selectedNode), selectedNode->collapsed ? std::max(NodeHeight(selectedNode->kind, selectedNode->componentPins), selectedNode->size.y) : NodeHeight(*selectedNode)};
                if (ImGui::DragFloat2("Size", size, 1.0f, 0.0f, 0.0f, "%.0f"))
                {
                    selectedNode->size.x = std::max(kNodeMinWidth, size[0]);
                    selectedNode->size.y = std::max(NodeHeight(selectedNode->kind, selectedNode->componentPins), size[1]);
                    m_dirty = true;
                }

                if (SupportsComponentPins(selectedNode->kind))
                {
                    bool componentPins = selectedNode->componentPins;
                    if (ImGui::Checkbox("Component Pins", &componentPins))
                    {
                        selectedNode->componentPins = componentPins;
                        selectedNode->size.y = std::max(selectedNode->size.y, NodeHeight(selectedNode->kind, selectedNode->componentPins));
                        const auto inputPins=render::ShaderGraphInputPins(*selectedNode);
                        std::erase_if(m_graph.links,[&](const auto &link) { return link.toNodeId==selectedNode->id && std::find(inputPins.begin(),inputPins.end(),link.toPin)==inputPins.end(); });
                        m_dirty = true;
                    }
                }

                if (selectedNode->kind == render::ShaderGraphNodeKind::Parameter)
                {
                    if (ImGui::BeginCombo("Parameter", selectedNode->parameter.c_str()))
                    {
                        for (const auto &v : m_graph.variables)
                            if (ImGui::Selectable(v.name.c_str(), selectedNode->parameter == v.name))
                            { selectedNode->parameter = v.name; m_dirty = true; }
                        ImGui::EndCombo();
                    }
                    ImGui::TextDisabled("Create defaults in Parameters below.");
                }
                else if (selectedNode->kind == render::ShaderGraphNodeKind::TextureSample)
                {
                    int slot = int(selectedNode->materialInput);
                    const char *slots[]{"Albedo","Normal","Metallic","Roughness"};
                    if (ImGui::Combo("Material texture", &slot, slots, 4))
                    { selectedNode->materialInput = static_cast<render::ShaderGraphMaterialInput>(slot); m_dirty = true; }
                }
                else if (selectedNode->kind == render::ShaderGraphNodeKind::MaterialInput)
                {
                    int materialInput = static_cast<int>(selectedNode->materialInput);
                    const char *items[] = {"Color", "Normal", "Metallic", "Roughness", "Opacity", "UV", "Emission"};
                    if (ImGui::Combo("Input", &materialInput, items, IM_ARRAYSIZE(items)))
                    {
                        selectedNode->materialInput = static_cast<render::ShaderGraphMaterialInput>(materialInput);
                        m_dirty = true;
                    }
                }
                else if (selectedNode->kind == render::ShaderGraphNodeKind::Float)
                {
                    if (ImGui::DragFloat("Value", &selectedNode->value.x, 0.01f))
                    {
                        m_dirty = true;
                    }
                }
                else if (selectedNode->kind == render::ShaderGraphNodeKind::Vec2)
                {
                    if (ImGui::DragFloat2("Value", &selectedNode->value.x, 0.01f))
                    {
                        m_dirty = true;
                    }
                }
                else if (selectedNode->kind == render::ShaderGraphNodeKind::Vec3)
                {
                    if (ImGui::DragFloat3("Value", &selectedNode->value.x, 0.01f))
                    {
                        m_dirty = true;
                    }
                }
                else if (selectedNode->kind == render::ShaderGraphNodeKind::Color)
                {
                    if (ImGui::ColorEdit4("Value", &selectedNode->value.x))
                    {
                        m_dirty = true;
                    }
                }
                else if (selectedNode->kind == render::ShaderGraphNodeKind::NoiseTexture)
                {
                    if (ImGui::DragFloat("Scale", &selectedNode->value.x, 0.1f, 0.01f, 512.0f))
                    {
                        m_dirty = true;
                    }
                    if (ImGui::DragFloat("Strength", &selectedNode->value.y, 0.01f, 0.0f, 8.0f))
                    {
                        m_dirty = true;
                    }
                }

                ImGui::BeginDisabled(IsOutputNode(*selectedNode));
                if (ImGui::Button("Delete Node"))
                {
                    const int removedId = selectedNode->id;
                    m_graph.nodes.erase(std::remove_if(m_graph.nodes.begin(), m_graph.nodes.end(),
                                                       [removedId](const render::ShaderGraphNode &node)
                                                       {
                                                           return node.id == removedId;
                                                       }),
                                        m_graph.nodes.end());
                    m_graph.links.erase(std::remove_if(m_graph.links.begin(), m_graph.links.end(),
                                                       [removedId](const render::ShaderGraphLink &link)
                                                       {
                                                           return link.fromNodeId == removedId || link.toNodeId == removedId;
                                                       }),
                                        m_graph.links.end());
                    m_selectedNodeIds.erase(std::remove(m_selectedNodeIds.begin(), m_selectedNodeIds.end(), removedId), m_selectedNodeIds.end());
                    m_selectedNodeId = m_graph.nodes.empty() ? 0 : m_graph.nodes.front().id;
                    m_dirty = true;
                }
                ImGui::EndDisabled();
                ImGui::EndDisabled();
            }
            else
            {
                ImGui::TextDisabled("Select a node in the graph.");
            }
            ImGui::SeparatorText("Parameters");
            ImGui::BeginDisabled(engineGraph);
            if (ImGui::Button("Add Parameter"))
            {
                int suffix = 1;
                std::string name;
                do { name = "Parameter" + std::to_string(suffix++); }
                while (std::any_of(m_graph.variables.begin(), m_graph.variables.end(), [&](const auto &v) { return v.name == name; }));
                m_graph.variables.push_back({name,render::ShaderGraphValueType::Float,glm::vec4(1)});
                m_dirty = true;
            }
            for (size_t i=0;i<m_graph.variables.size();++i)
            {
                ImGui::PushID(int(i));
                auto &v=m_graph.variables[i];
                char name[128]{}; strncpy_s(name,v.name.c_str(),_TRUNCATE);
                if(ImGui::InputText("Name",name,sizeof(name)))
                {
                    for(auto &n:m_graph.nodes) if(n.kind==render::ShaderGraphNodeKind::Parameter && n.parameter==v.name) n.parameter=name;
                    v.name=name; m_dirty=true;
                }
                int type=int(v.type); const char *types[]{"Float","Vec2","Vec3","Color"};
                if(ImGui::Combo("Type",&type,types,4)) { v.type=static_cast<render::ShaderGraphValueType>(type);m_dirty=true; }
                bool changed=type==0?ImGui::DragFloat("Default",&v.value.x,.01f):type==1?ImGui::DragFloat2("Default",&v.value.x,.01f):type==2?ImGui::DragFloat3("Default",&v.value.x,.01f):ImGui::ColorEdit4("Default",&v.value.x);
                m_dirty |= changed;
                bool used=std::any_of(m_graph.nodes.begin(),m_graph.nodes.end(),[&](const auto &n){return n.kind==render::ShaderGraphNodeKind::Parameter && n.parameter==v.name;});
                ImGui::BeginDisabled(used);
                bool remove=ImGui::Button("Remove parameter");
                ImGui::EndDisabled();
                ImGui::PopID();
                if(remove) { m_graph.variables.erase(m_graph.variables.begin()+i);m_dirty=true;break; }
                ImGui::Separator();
            }
            ImGui::EndDisabled();
            ImGui::EndChild();

            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::Separator();
        ImGui::BeginDisabled(engineGraph || !m_dirty || !graphValid);
        if (ImGui::Button("Save"))
        {
            std::string errorMessage;
            if (core::Engine::GetInstance().GetAssetManager().SaveShaderGraphAsset(reference, m_graph, &errorMessage))
            {
                m_dirty = false;
                editorShell.MarkProjectDirty();
                editorShell.MarkSceneDirty();
                editorShell.Log(EditorShell::ConsoleSeverity::Info, "Saved shader graph: " + reference);
            }
            else
            {
                editorShell.Log(EditorShell::ConsoleSeverity::Error, errorMessage.empty() ? "Failed to save shader graph." : errorMessage);
            }
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!m_dirty);
        if (ImGui::Button("Revert"))
        {
            historyAction=true;
            LoadActiveGraph();
        }
        ImGui::EndDisabled();
        if (!historyAction && EditorGraphHash(m_graph)!=beforeHash)
        {
            if (!m_editGesture)
            {
                m_undo.push_back(graphBefore);
                if(m_undo.size()>64) m_undo.erase(m_undo.begin());
            }
            m_redo.clear();
            m_editGesture=ImGui::IsAnyItemActive() || ImGui::IsMouseDown(ImGuiMouseButton_Left);
        }
        if(!ImGui::IsAnyItemActive() && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) m_editGesture=false;
    }
}
