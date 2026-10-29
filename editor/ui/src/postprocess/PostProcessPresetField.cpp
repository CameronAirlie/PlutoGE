#include "PlutoGE/ui/postprocess/PostProcessPresetField.h"

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/PostProcessPresetAsset.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"
#include "PlutoGE/ui/postprocess/PostProcessPresetSession.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <optional>
#include <utility>

namespace PlutoGE::ui
{
    namespace
    {
        struct NewPresetPrompt
        {
            std::string name;
            bool copyCurrent = false;
            std::string error;
        };

        // Display names of the enabled effects as wrapped pills.
        void DrawEffectPills(const std::vector<std::unique_ptr<render::IPostProcessEffect>> &effects)
        {
            const ImGuiStyle &style = ImGui::GetStyle();
            const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
            bool first = true;
            int enabled = 0;
            for (const auto &effect : effects)
            {
                if (!effect || !effect->IsEnabled())
                    continue;
                ++enabled;
                const std::string name = effect->GetDisplayName();
                const float width = ImGui::CalcTextSize(name.c_str()).x + style.FramePadding.x * 2.0f;
                if (!first && ImGui::GetItemRectMax().x + style.ItemSpacing.x + width <= right)
                    ImGui::SameLine();
                first = false;
                widgets::Pill(name.c_str(), GetEditorColor(EditorColor::TextMuted));
            }
            if (enabled == 0)
                ImGui::TextDisabled("Every effect is off: the camera shows the raw scene.");
        }

        // Asks for a name and creates the preset. Returns its reference once created.
        std::optional<std::string> DrawNewPresetPopup(NewPresetPrompt &prompt,
                                                      const std::vector<std::unique_ptr<render::IPostProcessEffect>> &effects)
        {
            std::optional<std::string> created;
            if (!ImGui::BeginPopup("##NewPreset"))
                return created;
            auto &shell = EditorShell::GetInstance();
            ImGui::TextUnformatted("New post process preset");
            if (ImGui::IsWindowAppearing())
                ImGui::SetKeyboardFocusHere();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0f);
            const bool submitted = ImGui::InputTextWithHint("##Name", "Name", &prompt.name, ImGuiInputTextFlags_EnterReturnsTrue);
            if (!effects.empty())
                ImGui::Checkbox("Start from this camera's effects", &prompt.copyCurrent);
            if (!prompt.error.empty())
                ImGui::TextColored(GetEditorColor(EditorColor::Error), "%s", prompt.error.c_str());
            ImGui::BeginDisabled(prompt.name.empty() || !shell.GetProject());
            if (widgets::PrimaryButton("Create") || (submitted && !prompt.name.empty()))
            {
                auto &assets = core::Engine::GetInstance().GetAssetManager();
                const auto preset = prompt.copyCurrent ? assets::CapturePostProcessPreset(effects) : assets::CreateDefaultPostProcessPresetAsset();
                prompt.error.clear();
                if (auto reference = CreatePostProcessPresetAsset(*shell.GetProject(), assets, prompt.name, preset, prompt.error); !reference.empty())
                {
                    shell.RefreshProjectAssets();
                    shell.Log(EditorShell::ConsoleSeverity::Info, "Created post process preset: " + reference);
                    created = std::move(reference);
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return created;
        }
    }

    bool DrawPostProcessPresetField(const char *id, std::string &reference, const std::vector<std::unique_ptr<render::IPostProcessEffect>> &effects)
    {
        static NewPresetPrompt prompt;
        auto &shell = EditorShell::GetInstance();
        const ImGuiStyle &style = ImGui::GetStyle();
        const bool legacyStack = reference.empty() && !effects.empty();
        bool changed = false;

        ImGui::PushID(id);
        const auto openNewPrompt = [&](bool copyCurrent)
        {
            prompt = {.name = copyCurrent ? "Camera Post Process" : "New Post Process", .copyCurrent = copyCurrent};
            ImGui::OpenPopup("##NewPreset");
        };

        ImGui::SetNextItemWidth(-(ImGui::GetFrameHeight() + style.ItemSpacing.x) * 2.0f);
        changed |= RenderProjectAssetPicker("##Preset", shell.GetProject(), assets::ProjectAssetType::PostProcessPreset, reference,
                                            {.noneLabel = "None", .emptyPreview = legacyStack ? "Inline stack (older scene)" : "None",
                                             .includeEngineAssets = true});
        ImGui::SetItemTooltip("The post process preset this camera renders with. Saving the preset updates every camera using it.");
        ImGui::SameLine();
        ImGui::BeginDisabled(reference.empty());
        if (widgets::IconButton("##Edit", icons::Settings, "Edit this preset in the Post Process Editor"))
            shell.OpenPostProcessPresetAsset(reference);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (widgets::IconButton("##New", icons::Plus, "Create a new preset for this camera"))
            openNewPrompt(legacyStack);

        if (legacyStack)
        {
            ImGui::PushStyleColor(ImGuiCol_Text, GetEditorColor(EditorColor::Warning));
            ImGui::TextWrapped("%s  This camera keeps its own effects from an older scene. Save them as a preset to edit "
                               "them in the Post Process Editor and share them with other cameras.", icons::Warning);
            ImGui::PopStyleColor();
            if (ImGui::Button("Save as Preset..."))
                openNewPrompt(true);
        }
        else if (reference.empty())
            ImGui::TextDisabled("No post-processing: the camera shows the raw scene.");
        if (!effects.empty())
            DrawEffectPills(effects);
        if (auto created = DrawNewPresetPopup(prompt, effects))
        {
            reference = std::move(*created);
            changed = true;
            shell.OpenPostProcessPresetAsset(reference);
        }
        ImGui::PopID();
        return changed;
    }
}
