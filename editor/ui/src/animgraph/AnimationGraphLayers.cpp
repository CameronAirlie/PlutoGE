#include "PlutoGE/ui/animgraph/AnimationGraphSidebar.h"

#include "PlutoGE/assets/AnimationGraphUtils.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/animgraph/AnimationGraphDocument.h"
#include "PlutoGE/ui/animgraph/StateMachineView.h"

#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <array>

namespace PlutoGE::ui::animgraph
{
    namespace ag = assets::animgraph;

    namespace
    {
        const ProjectAssetPickerOptions kClipPicker{.includeEngineAssets = true};

        void AddUpperBodyPreset(assets::AnimationGraphAsset &graph)
        {
            const int maskId = ag::NextBoneMaskId(graph);
            assets::AnimationGraphBoneMask mask{.id = maskId, .name = "Upper Body", .defaultWeight = 0.0f};
            mask.entries = {
                {.bone = render::HumanoidBone::Spine, .weight = 0.25f, .includeChildren = true},
                {.bone = render::HumanoidBone::Chest, .weight = 0.75f, .includeChildren = true},
                {.bone = render::HumanoidBone::UpperChest, .weight = 1.0f, .includeChildren = true},
                {.bone = render::HumanoidBone::LeftShoulder, .weight = 1.0f, .includeChildren = true},
                {.bone = render::HumanoidBone::RightShoulder, .weight = 1.0f, .includeChildren = true},
            };
            graph.boneMasks.push_back(std::move(mask));
            if (!ag::FindParameter(graph, "Shoot"))
                graph.parameters.push_back(assets::AnimationGraphParameter{
                    .id = ag::NextParameterId(graph), .name = "Shoot", .type = assets::AnimationGraphParameterType::Trigger});
            graph.layers.push_back(assets::AnimationGraphLayer{
                .id = ag::NextLayerId(graph), .name = "Upper Body Action", .maskId = maskId, .activationParameter = "Shoot"});
        }

        bool DrawLayer(EditorContext &context, assets::AnimationGraphAsset &graph, assets::AnimationGraphLayer &layer,
                       const ProjectAssetPickerOptions &graphPicker)
        {
            bool changed = false;
            if (!BeginProperties("Layer"))
                return false;

            PropertyRow("Name");
            changed |= ImGui::InputText("##Name", &layer.name);
            PropertyRow("Enabled");
            changed |= ImGui::Checkbox("##Enabled", &layer.enabled);
            PropertyRow("Source graph", "Plays another graph's state machine on this layer. None plays a single clip.");
            changed |= RenderProjectAssetPicker("##Graph", context.project, assets::ProjectAssetType::AnimationGraph, layer.graphReference,
                                                graphPicker);
            if (layer.graphReference.empty())
            {
                PropertyRow("Clip");
                if (RenderProjectAssetPicker("##Clip", context.project, assets::ProjectAssetType::AnimationClip, layer.clipReference, kClipPicker))
                {
                    layer.clipName = ClipDisplayName(layer.clipReference, "");
                    changed = true;
                }
                PropertyRow("Loop");
                changed |= ImGui::Checkbox("##Loop", &layer.loop);
            }

            PropertyRow("Bone mask");
            const auto *mask = ag::FindBoneMask(graph, layer.maskId);
            if (ImGui::BeginCombo("##Mask", mask ? mask->name.c_str() : "Full Body"))
            {
                if (ImGui::Selectable("Full Body", layer.maskId == 0))
                {
                    layer.maskId = 0;
                    changed = true;
                }
                for (const auto &candidate : graph.boneMasks)
                    if (ImGui::Selectable(candidate.name.c_str(), layer.maskId == candidate.id))
                    {
                        layer.maskId = candidate.id;
                        changed = true;
                    }
                ImGui::EndCombo();
            }

            PropertyRow("Blend");
            int blendMode = static_cast<int>(layer.blendMode);
            constexpr std::array<const char *, 2> blendModes{"Override", "Additive"};
            if (ImGui::Combo("##BlendMode", &blendMode, blendModes.data(), static_cast<int>(blendModes.size())))
            {
                layer.blendMode = static_cast<assets::AnimationGraphLayerBlendMode>(blendMode);
                changed = true;
            }
            PropertyRow("Weight");
            changed |= ImGui::SliderFloat("##Weight", &layer.weight, 0.0f, 1.0f);
            PropertyRow("Weight parameter", "Optional Float (0..1) multiplying the weight.");
            changed |= ParameterCombo("##WeightParameter", graph, layer.weightParameter, IsFloatParameter, "None");
            PropertyRow("Activation", "Always on, or driven by a Bool (sustained) or Trigger (one-shot).");
            changed |= ParameterCombo("##Activation", graph, layer.activationParameter, IsSwitchParameter, "Always");
            PropertyRow("Restart on activation");
            changed |= ImGui::Checkbox("##Restart", &layer.restartOnActivation);
            PropertyRow("Speed");
            changed |= ImGui::DragFloat("##Speed", &layer.speed, 0.01f, 0.0f, 10.0f, "%.2fx");
            PropertyRow("Fade in");
            changed |= ImGui::DragFloat("##FadeIn", &layer.fadeIn, 0.005f, 0.0f, 5.0f, "%.3f s");
            PropertyRow("Fade out");
            changed |= ImGui::DragFloat("##FadeOut", &layer.fadeOut, 0.005f, 0.0f, 5.0f, "%.3f s");
            EndProperties();
            return changed;
        }

        bool DrawBoneMask(assets::AnimationGraphBoneMask &mask)
        {
            bool changed = false;
            if (BeginProperties("Mask"))
            {
                PropertyRow("Name");
                changed |= ImGui::InputText("##Name", &mask.name);
                PropertyRow("Default weight", "Weight of bones no entry covers.");
                changed |= ImGui::SliderFloat("##DefaultWeight", &mask.defaultWeight, 0.0f, 1.0f);
                EndProperties();
            }
            Hint("Entries apply in order, so later entries refine earlier ones (Spine 0.25, Chest 0.75, UpperChest 1).");

            int removeEntry = -1;
            if (ImGui::BeginTable("Entries", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoPadOuterX))
            {
                ImGui::TableSetupColumn("Bone", ImGuiTableColumnFlags_WidthStretch, 0.45f);
                ImGui::TableSetupColumn("Weight", ImGuiTableColumnFlags_WidthStretch, 0.35f);
                ImGui::TableSetupColumn("Children", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 2.2f);
                ImGui::TableSetupColumn("##Remove", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
                ImGui::TableHeadersRow();
                for (std::size_t index = 0; index < mask.entries.size(); ++index)
                {
                    auto &entry = mask.entries[index];
                    ImGui::PushID(static_cast<int>(index));
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    int bone = static_cast<int>(entry.bone);
                    if (ImGui::Combo("##Bone", &bone, render::kHumanoidBoneNames.data(), static_cast<int>(render::kHumanoidBoneCount)))
                    {
                        entry.bone = static_cast<render::HumanoidBone>(bone);
                        changed = true;
                    }
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    changed |= ImGui::SliderFloat("##Weight", &entry.weight, 0.0f, 1.0f, "%.2f");
                    ImGui::TableNextColumn();
                    changed |= ImGui::Checkbox("##Children", &entry.includeChildren);
                    ImGui::TableNextColumn();
                    if (ImGui::Button("x", ImVec2(ImGui::GetFrameHeight(), 0.0f)))
                        removeEntry = static_cast<int>(index);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (removeEntry >= 0)
            {
                mask.entries.erase(mask.entries.begin() + removeEntry);
                changed = true;
            }
            if (ImGui::Button("+ Add Bone"))
            {
                mask.entries.push_back({});
                changed = true;
            }
            return changed;
        }
    }

    void DrawAnimationGraphLayers(EditorContext &context)
    {
        auto &document = context.document;
        auto &graph = document.MutableGraph();
        bool changed = false;

        Hint("Layers play on top of the state machine. Each can be limited to part of the body with a bone mask.");
        const auto catalog = core::Engine::GetInstance().GetAssetManager().GetAssetCatalog();
        const auto selfReference = ResolveAssetPickerReference(catalog.get(), document.Reference());
        const ProjectAssetPickerOptions graphPicker{
            .noneLabel = "None (single clip)",
            .includeEngineAssets = true,
            .filter = [&selfReference](const AssetReferenceOption &option) { return option.reference != selfReference; },
            .catalog = catalog.get()};

        if (ImGui::Button("+ Layer"))
        {
            graph.layers.push_back(assets::AnimationGraphLayer{
                .id = ag::NextLayerId(graph), .name = "Layer " + std::to_string(graph.layers.size() + 1)});
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Upper Body Action Preset"))
        {
            AddUpperBodyPreset(graph);
            changed = true;
        }

        int removeLayer = -1;
        for (std::size_t index = 0; index < graph.layers.size(); ++index)
        {
            auto &layer = graph.layers[index];
            ImGui::PushID(layer.id);
            const std::string title = (layer.name.empty() ? std::string("Unnamed Layer") : layer.name) + (layer.enabled ? "" : " (disabled)") + "###Layer";
            if (ImGui::CollapsingHeader(title.c_str()))
            {
                changed |= DrawLayer(context, graph, layer, graphPicker);
                if (ImGui::Button("Delete Layer"))
                    removeLayer = static_cast<int>(index);
            }
            ImGui::PopID();
        }
        if (removeLayer >= 0)
        {
            graph.layers.erase(graph.layers.begin() + removeLayer);
            changed = true;
        }

        ImGui::SeparatorText("Bone Masks");
        if (ImGui::Button("+ Bone Mask"))
        {
            graph.boneMasks.push_back(assets::AnimationGraphBoneMask{
                .id = ag::NextBoneMaskId(graph), .name = "Bone Mask " + std::to_string(graph.boneMasks.size() + 1)});
            changed = true;
        }
        int removeMask = 0;
        for (auto &mask : graph.boneMasks)
        {
            ImGui::PushID(10000 + mask.id);
            if (ImGui::CollapsingHeader((mask.name + "###Mask").c_str()))
            {
                changed |= DrawBoneMask(mask);
                if (ImGui::Button("Delete Mask"))
                    removeMask = mask.id;
            }
            ImGui::PopID();
        }
        if (removeMask != 0)
        {
            ag::RemoveBoneMask(graph, removeMask);
            changed = true;
        }
        if (changed)
            document.MarkModified();
    }
}
