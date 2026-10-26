#include "PlutoGE/ui/ComponentInspectors.h"

#include "PlutoGE/assets/AssetType.h"
#include "PlutoGE/scene/ComponentRegistry.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/UIStyleComponents.h"
#include "PlutoGE/scene/ui/UIStyleCatalog.h"
#include "PlutoGE/scene/ui/UIThemeAssets.h"
#include "PlutoGE/ui/AssetReferencePicker.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/EditorWidgets.h"
#include "PlutoGE/ui/theme/UIThemeDocument.h"
#include "PlutoGE/ui/theme/UIThemeWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>

// Inspectors for UI theme and style components, and the annotation that marks
// properties a style is driving in every other component's inspector.
namespace PlutoGE::ui
{
    namespace
    {
        constexpr const char *kThemeType = "UIStyleSheetComponent";
        constexpr const char *kStyleType = "UIStyleComponent";
        const ImVec4 kErrorColor(1.0f, 0.55f, 0.45f, 1.0f);

        void Remove(std::vector<scene::Property> &properties, std::initializer_list<std::string_view> names)
        {
            std::erase_if(properties, [&](const scene::Property &property)
                          { return std::find(names.begin(), names.end(), property.name) != names.end(); });
        }

        void Changed(scene::Entity &entity, const char *type, std::initializer_list<const char *> properties)
        {
            for (const char *property : properties)
                entity.AddPrefabOverride(std::string("Component:") + type + ":" + property);
            EditorShell::GetInstance().MarkSceneDirty();
        }

        scene::Entity *FindEntity(std::uint32_t id)
        {
            auto *scene = EditorShell::GetInstance().GetScene();
            return scene ? scene->FindEntityByID(id) : nullptr;
        }

        // The theme to edit for a style used at `entity`: the nearest theme
        // component's local styles or asset that defines it, else the nearest
        // component's asset (or its local styles when it has none).
        UIThemeTarget ThemeTargetFor(const scene::Entity &entity, std::string_view style)
        {
            std::optional<UIThemeTarget> fallback;
            for (const auto *current = &entity; current; current = current->GetParent())
            {
                const auto *theme = current->GetComponent<scene::UIThemeComponent>();
                if (!theme || !theme->IsEnabled())
                    continue;
                const auto local = UIThemeTarget::ForLocalStyles(current->GetID());
                const auto asset = theme->GetThemeAsset().empty() ? local : UIThemeTarget::ForAsset(theme->GetThemeAsset());
                if (!fallback)
                    fallback = asset;
                if (style.empty())
                    break;
                if (theme->GetLocalTheme().FindStyle(style))
                    return local;
                if (!theme->GetThemeAsset().empty())
                {
                    std::vector<std::shared_ptr<const scene::UITheme>> layers;
                    std::vector<std::string> ignored;
                    scene::UIThemeAssets::Get().AppendLayers(theme->GetThemeAsset(), layers, ignored);
                    if (std::any_of(layers.begin(), layers.end(), [&](const auto &layer) { return layer->FindStyle(style); }))
                        return asset;
                }
            }
            return fallback.value_or(UIThemeTarget::ForLocalStyles(entity.GetID()));
        }

        void OpenStyle(const scene::Entity &entity, const std::string &style)
        {
            EditorShell::GetInstance().OpenUIThemeEditor(ThemeTargetFor(entity, style), style);
        }

        // --- Theme component ------------------------------------------------

        void InspectTheme(scene::Component &component, ComponentInspectorContext &context, std::vector<scene::Property> &properties)
        {
            auto &theme = static_cast<scene::UIThemeComponent &>(component);
            Remove(properties, {"Theme", "Rules"});
            auto *project = context.shell.GetProject();
            const float buttonWidth = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x;

            ImGui::TextDisabled("Theme");
            std::string reference = theme.GetThemeAsset();
            ImGui::SetNextItemWidth(-buttonWidth);
            if (RenderProjectAssetPicker("##ThemeAsset", project, assets::ProjectAssetType::UITheme, reference, {.noneLabel = "(none)"}))
            {
                theme.SetThemeAsset(reference);
                Changed(context.entity, kThemeType, {"Theme"});
            }
            ImGui::SameLine();
            if (!reference.empty())
            {
                if (widgets::IconButton("##EditTheme", icons::Palette, "Edit this theme"))
                    context.shell.OpenUIThemeEditor(UIThemeTarget::ForAsset(reference));
            }
            else if (widgets::IconButton("##NewTheme", icons::Plus, "Create a theme with ready-made styles"))
            {
                std::string error;
                const auto created = CreateUIThemeAsset(GetDefaultUIThemeDirectory(), scene::MakeUIThemeName(context.entity.GetName() + "-Theme"),
                                                        scene::MakeStarterUITheme(), error);
                if (!created.empty())
                {
                    theme.SetThemeAsset(created);
                    Changed(context.entity, kThemeType, {"Theme"});
                    context.shell.OpenUIThemeEditor(UIThemeTarget::ForAsset(created));
                }
            }

            ImGui::Spacing();
            const auto &local = theme.GetLocalTheme();
            ImGui::TextDisabled("Local styles");
            ImGui::SetItemTooltip("Styles and variables that extend or override the theme for this part of the UI only");
            ImGui::SameLine();
            if (local.styles.empty() && local.variables.empty())
                ImGui::TextDisabled("none");
            else
                ImGui::Text("%zu style%s, %zu variable%s", local.styles.size(), local.styles.size() == 1 ? "" : "s",
                            local.variables.size(), local.variables.size() == 1 ? "" : "s");
            if (ImGui::Button("Edit Local Styles"))
                context.shell.OpenUIThemeEditor(UIThemeTarget::ForLocalStyles(context.entity.GetID()));

            if (const auto resolved = scene::FindEffectiveUITheme(context.entity); resolved && !resolved->GetErrors().empty())
            {
                ImGui::Spacing();
                const auto &errors = resolved->GetErrors();
                for (std::size_t index = 0; index < std::min<std::size_t>(errors.size(), 4); ++index)
                    ImGui::TextColored(kErrorColor, "%s %s", icons::Warning, errors[index].c_str());
                if (errors.size() > 4)
                    ImGui::TextDisabled("%zu more in the theme editor", errors.size() - 4);
            }
        }

        // --- Style component: "Create Style from Element" -------------------

        struct Candidate
        {
            std::string component;
            std::string property;
            std::string value;
            std::string where; // empty on the element itself, else the child's name
            bool include = true;
        };

        struct CreateStyleState
        {
            std::uint32_t entity = 0;
            std::string name;
            std::string base;
            std::vector<Candidate> candidates;
            std::string error;
        };

        CreateStyleState &GetCreateStyleState()
        {
            static CreateStyleState state;
            return state;
        }

        void PrepareCreateStyle(scene::Entity &entity, const scene::UIStyleComponent &style)
        {
            auto &state = GetCreateStyleState();
            state = {};
            state.entity = entity.GetID();
            state.name = scene::MakeUIThemeName(entity.GetName());
            state.base = style.GetStyles().empty() ? std::string{} : style.GetStyles().front();
            // The element's changed look, and its children's for components it lacks (a button's label).
            const auto &catalog = scene::UIStyleCatalog::Get();
            const auto &registry = scene::ComponentRegistry::Get();
            for (const auto &info : catalog.GetComponents())
            {
                const auto *type = registry.Find(std::string_view(info.typeName));
                auto *target = type ? scene::FindUIStyleTarget(entity, type->typeID) : nullptr;
                if (!target)
                    continue;
                const auto &buckets = target->GetComponentBuckets();
                const auto *component = buckets[type->typeID].front();
                for (auto &property : catalog.GetChangedProperties(*component))
                    state.candidates.push_back({info.typeName, property.name, property.value,
                                                target == &entity ? std::string{} : target->GetName()});
            }
        }

        void DrawCreateStylePopup(ComponentInspectorContext &context, scene::UIStyleComponent &style)
        {
            auto &state = GetCreateStyleState();
            if (!ImGui::BeginPopupModal("Create Style from Element", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
                return;
            const auto theme = scene::FindEffectiveUITheme(context.entity);
            const auto target = ThemeTargetFor(context.entity, {});
            ImGui::TextWrapped("Saves this element's look as a style that other elements can use.");
            ImGui::SetNextItemWidth(260.0f);
            theme::InputString("Name", state.name);
            if (theme)
            {
                ImGui::SetNextItemWidth(260.0f);
                theme::StyleCombo("Based on", *theme, state.base, true);
            }
            ImGui::TextDisabled("Saved in %s", target.kind == UIThemeTarget::Kind::Asset ? target.asset.c_str() : "the canvas's local styles");

            ImGui::SeparatorText("Include");
            if (state.candidates.empty())
                ImGui::TextDisabled("Nothing differs from the defaults yet. The style will start empty.");
            ImGui::BeginChild("##Candidates", ImVec2(420.0f, std::min(240.0f, 24.0f * (state.candidates.size() + 1))), ImGuiChildFlags_None);
            const auto &catalog = scene::UIStyleCatalog::Get();
            for (std::size_t index = 0; index < state.candidates.size(); ++index)
            {
                auto &candidate = state.candidates[index];
                ImGui::PushID(static_cast<int>(index));
                auto label = catalog.GetComponentLabel(candidate.component) + " / " + candidate.property;
                if (!candidate.where.empty())
                    label += "  (on " + candidate.where + ")";
                ImGui::Checkbox(label.c_str(), &candidate.include);
                ImGui::SetItemTooltip("%s", candidate.value.c_str());
                ImGui::PopID();
            }
            ImGui::EndChild();

            if (!state.error.empty())
                ImGui::TextColored(kErrorColor, "%s", state.error.c_str());
            if (widgets::PrimaryButton("Create Style"))
            {
                std::string created;
                const bool ok = EditUITheme(target, [&](scene::UITheme &edited)
                                            {
                                                auto &added = edited.AddStyle(state.name, state.base);
                                                for (const auto &candidate : state.candidates)
                                                    if (candidate.include)
                                                        added.Set(scene::UIStyleState::Normal, candidate.component, candidate.property, candidate.value);
                                                created = added.name;
                                            },
                                            state.error);
                if (ok)
                {
                    // The new style carries the old one's values through its base, so it replaces it.
                    style.SetStyles({created});
                    for (const auto &candidate : state.candidates)
                        if (candidate.include)
                            style.SetOverridden(candidate.component, candidate.property, false);
                    Changed(context.entity, kStyleType, {"Styles", "Overrides"});
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
                ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // --- Style component ------------------------------------------------

        bool AddThemeToCanvas(scene::Entity &entity)
        {
            auto *host = &entity;
            for (auto *current = &entity; current; current = current->GetParent())
                if (current->GetComponent<scene::CanvasComponent>())
                {
                    host = current;
                    break;
                }
            std::string error;
            const auto reference = CreateUIThemeAsset(GetDefaultUIThemeDirectory(), "Theme", scene::MakeStarterUITheme(), error);
            if (reference.empty())
                return false;
            const auto *info = scene::ComponentRegistry::Get().Find<scene::UIThemeComponent>();
            auto *theme = info ? static_cast<scene::UIThemeComponent *>(scene::ComponentRegistry::Get().AddToEntity(*host, *info)) : nullptr;
            if (!theme)
                return false;
            theme->SetThemeAsset(reference);
            Changed(*host, kThemeType, {"Theme"});
            return true;
        }

        void InspectStyle(scene::Component &component, ComponentInspectorContext &context, std::vector<scene::Property> &properties)
        {
            auto &style = static_cast<scene::UIStyleComponent &>(component);
            Remove(properties, {"Styles", "Overrides"});
            const auto theme = scene::FindEffectiveUITheme(context.entity);
            if (!theme)
            {
                ImGui::TextWrapped("No theme above this element. Themes hold the styles elements can use.");
                if (ImGui::Button("Add a Theme to the Canvas"))
                    AddThemeToCanvas(context.entity);
                return;
            }

            auto styles = style.GetStyles();
            bool changed = false;
            const float buttonsWidth = 2.0f * (ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x);
            for (std::size_t index = 0; index < styles.size(); ++index)
            {
                ImGui::PushID(static_cast<int>(index));
                const bool known = theme->FindStyle(styles[index]) != nullptr;
                ImGui::SetNextItemWidth(-buttonsWidth);
                if (!known)
                    ImGui::PushStyleColor(ImGuiCol_Text, kErrorColor);
                changed |= theme::StyleCombo("##Style", *theme, styles[index], false);
                if (!known)
                {
                    ImGui::PopStyleColor();
                    ImGui::SetItemTooltip("'%s' is not in the theme", styles[index].c_str());
                }
                ImGui::SameLine();
                if (widgets::IconButton("##Edit", icons::Palette, "Edit this style"))
                    OpenStyle(context.entity, styles[index]);
                ImGui::SameLine();
                if (widgets::IconButton("##Remove", icons::Close, "Stop using this style"))
                {
                    styles.erase(styles.begin() + static_cast<std::ptrdiff_t>(index));
                    changed = true;
                    ImGui::PopID();
                    break;
                }
                if (const auto *resolved = theme->FindStyle(styles[index]); resolved && resolved->chain.size() > 1)
                {
                    std::string chain;
                    for (std::size_t link = 1; link < resolved->chain.size(); ++link)
                        chain += (link > 1 ? "  >  " : "") + resolved->chain[link];
                    ImGui::TextDisabled("  based on %s", chain.c_str());
                }
                ImGui::PopID();
            }
            std::string added;
            ImGui::SetNextItemWidth(-FLT_MIN);
            if (theme::StyleCombo("##AddStyle", *theme, added, false, {}, styles.empty() ? "Choose a style..." : "+ Add another style") &&
                std::find(styles.begin(), styles.end(), added) == styles.end())
            {
                styles.push_back(added);
                changed = true;
            }
            if (styles.size() > 1)
                ImGui::TextDisabled("Later styles win where they set the same property.");
            if (changed)
            {
                style.SetStyles(std::move(styles));
                Changed(context.entity, kStyleType, {"Styles"});
            }

            if (!style.GetOverrides().empty())
            {
                ImGui::Spacing();
                const auto count = style.GetOverrides().size();
                if (ImGui::TreeNode("##Overrides", "%zu propert%s set on this element", count, count == 1 ? "y" : "ies"))
                {
                    ImGui::TextDisabled("These keep the element's own value instead of the style's.");
                    std::string revert;
                    for (const auto &key : style.GetOverrides())
                    {
                        ImGui::PushID(key.c_str());
                        if (ImGui::SmallButton(icons::Revert))
                            revert = key;
                        ImGui::SetItemTooltip("Use the style's value again");
                        ImGui::SameLine();
                        ImGui::TextUnformatted(key.c_str());
                        ImGui::PopID();
                    }
                    if (ImGui::SmallButton("Revert All"))
                        revert = "*";
                    if (!revert.empty())
                    {
                        if (revert == "*")
                            style.ClearOverrides();
                        else if (const auto dot = revert.find('.'); dot != std::string::npos)
                            style.SetOverridden(std::string_view(revert).substr(0, dot), std::string_view(revert).substr(dot + 1), false);
                        Changed(context.entity, kStyleType, {"Overrides"});
                    }
                    ImGui::TreePop();
                }
            }

            ImGui::Spacing();
            if (ImGui::Button("Create Style from Element..."))
            {
                PrepareCreateStyle(context.entity, style);
                ImGui::OpenPopup("Create Style from Element");
            }
            ImGui::SetItemTooltip("Save this element's current look as a new style in the theme");
            if (GetCreateStyleState().entity == context.entity.GetID())
                DrawCreateStylePopup(context, style);
        }

        // --- Annotation of styled properties --------------------------------

        std::optional<PropertyAnnotation> AnnotateStyledProperty(scene::Component &component, ComponentInspectorContext &context,
                                                                 const scene::Property &property)
        {
            const auto typeName = scene::ComponentRegistry::Get().GetTypeName(component);
            if (typeName.empty() || typeName == kThemeType || typeName == kStyleType || property.name == "Enabled")
                return std::nullopt;
            auto *owner = scene::FindUIStyleOwner(context.entity, component.GetTypeID());
            if (!owner || !owner->GetOwner())
                return std::nullopt;
            const std::uint32_t ownerId = owner->GetOwner()->GetID();
            const auto setOverridden = [ownerId, typeName, name = property.name](bool overridden)
            {
                auto *entity = FindEntity(ownerId);
                auto *style = entity ? entity->GetComponent<scene::UIStyleComponent>() : nullptr;
                if (!style)
                    return;
                style->SetOverridden(typeName, name, overridden);
                Changed(*entity, kStyleType, {"Overrides"});
            };

            PropertyAnnotation annotation;
            if (const auto *applied = owner->FindApplied(context.entity.GetID(), typeName, property.name))
            {
                annotation.kind = PropertyAnnotation::Kind::Driven;
                annotation.locked = true;
                annotation.tooltip = "Set by style '" + applied->sourceStyle + "'";
                if (applied->sourceState != scene::UIStyleState::Normal)
                    annotation.tooltip += " while " + std::string(scene::GetUIStyleStateName(applied->sourceState));
                annotation.tooltip += ".";
                annotation.actions.push_back({"Override on This Element", [setOverridden] { setOverridden(true); }});
                annotation.actions.push_back({"Edit Style '" + applied->sourceStyle + "'",
                                              [ownerId, styleName = applied->sourceStyle]
                                              {
                                                  if (const auto *entity = FindEntity(ownerId))
                                                      OpenStyle(*entity, styleName);
                                              }});
                return annotation;
            }
            if (owner->IsOverridden(typeName, property.name))
            {
                annotation.kind = PropertyAnnotation::Kind::Overridden;
                annotation.tooltip = "Set on this element instead of by its style.";
                annotation.actions.push_back({"Revert to Style", [setOverridden] { setOverridden(false); }});
                return annotation;
            }
            return std::nullopt;
        }
    }

    void RegisterUIStyleInspectors()
    {
        RegisterComponentInspector(scene::GetComponentTypeID<scene::UIThemeComponent>(), &InspectTheme);
        RegisterComponentInspector(scene::GetComponentTypeID<scene::UIStyleComponent>(), &InspectStyle);
        RegisterPropertyAnnotator(&AnnotateStyledProperty);
    }
}
