#include "PlutoGE/ui/theme/UIThemeViews.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/UIStyleComponents.h"
#include "PlutoGE/scene/ui/UIStyleCatalog.h"
#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/ui/EditorWidgets.h"
#include "PlutoGE/ui/theme/UIThemeDocument.h"
#include "PlutoGE/ui/theme/UIThemeWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <functional>

namespace PlutoGE::ui::theme
{
    namespace
    {
        using scene::UIStyleState;

        constexpr const char *kStyleDragPayload = "PLUTOGE_UI_STYLE";
        constexpr std::array<const char *, scene::kUIStyleStateCount> kStateLabels{"Normal", "Hover", "Pressed", "Selected", "Disabled"};
        constexpr std::array<const char *, scene::kUIStyleStateCount> kStateHints{
            "What every element with this style starts from.",
            "Layered over Normal while the pointer is over the control.",
            "Layered over Normal while the control is held down.",
            "Layered over Normal while the control has keyboard or gamepad focus.",
            "Used instead of Hover and Pressed while the control cannot be used.",
        };

        // Theme edits gathered while drawing and applied afterwards as one
        // change: drawing reads the resolved theme, which a change rebuilds.
        using Edit = std::function<void(scene::UITheme &)>;

        scene::UIStyle &EnsureStyle(scene::UITheme &theme, const std::string &name)
        {
            if (auto *style = theme.FindStyle(name))
                return *style;
            auto &style = theme.styles.emplace_back();
            style.name = name;
            return style;
        }

        bool ContainsInsensitive(std::string_view text, std::string_view search)
        {
            return std::search(text.begin(), text.end(), search.begin(), search.end(), [](char a, char b)
                               { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }) != text.end();
        }

        void ForEachStyledElement(const std::function<void(scene::Entity &, scene::UIStyleComponent &)> &visit)
        {
            auto *scene = EditorShell::GetInstance().GetScene();
            if (!scene)
                return;
            const std::function<void(scene::Entity &)> walk = [&](scene::Entity &entity)
            {
                if (auto *style = entity.GetComponent<scene::UIStyleComponent>())
                    visit(entity, *style);
                for (auto *child : entity.GetChildren())
                    walk(*child);
            };
            for (auto *root : scene->GetRootEntities())
                walk(*root);
        }

        void DrawStyleList(UIThemeDocument &document, StylesViewState &state, std::vector<Edit> &edits)
        {
            const auto &merged = document.GetResolved().GetMerged();
            const auto &own = document.GetTheme();

            ImGui::SetNextItemWidth(-(ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.x));
            InputString("##Search", state.search, "Search styles");
            ImGui::SameLine();
            if (widgets::IconButton("##NewStyle", icons::Plus, "New style"))
            {
                state.nameBuffer = "New-Style";
                state.newStyleBase = state.selected;
                state.openNewStylePopup = true;
            }
            ImGui::Spacing();

            ImGui::BeginChild("##StyleTree");
            if (merged.styles.empty())
                ImGui::TextWrapped("No styles yet. Press + to add one.");

            const auto drawNode = [&](const scene::UIStyle &style, bool hasChildren) -> bool
            {
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
                if (!hasChildren)
                    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (style.name == state.selected)
                    flags |= ImGuiTreeNodeFlags_Selected;
                const bool local = own.FindStyle(style.name) != nullptr;
                if (!local)
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                const bool open = ImGui::TreeNodeEx(style.name.c_str(), flags);
                if (!local)
                    ImGui::PopStyleColor();
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                    state.selected = style.name;
                if (!local)
                    ImGui::SetItemTooltip("From a base theme. Changing it saves an override in this theme.");

                if (ImGui::BeginDragDropSource())
                {
                    ImGui::SetDragDropPayload(kStyleDragPayload, style.name.c_str(), style.name.size() + 1);
                    ImGui::Text("Base %s on...", style.name.c_str());
                    ImGui::EndDragDropSource();
                }
                if (ImGui::BeginDragDropTarget())
                {
                    if (const auto *payload = ImGui::AcceptDragDropPayload(kStyleDragPayload))
                    {
                        const std::string dragged(static_cast<const char *>(payload->Data));
                        if (!merged.InheritsFrom(style.name, dragged))
                            edits.push_back([dragged, base = style.name](scene::UITheme &theme) { EnsureStyle(theme, dragged).basedOn = base; });
                    }
                    ImGui::EndDragDropTarget();
                }

                if (ImGui::BeginPopupContextItem())
                {
                    state.selected = style.name;
                    if (ImGui::MenuItem("New Style Based on This"))
                    {
                        state.nameBuffer = style.name + "-Variant";
                        state.newStyleBase = style.name;
                        state.openNewStylePopup = true;
                    }
                    if (ImGui::MenuItem("Duplicate", nullptr, false, local))
                        edits.push_back([name = style.name, &state](scene::UITheme &theme)
                                        {
                                            if (const auto *copy = theme.DuplicateStyle(name))
                                                state.selected = copy->name;
                                        });
                    if (ImGui::MenuItem("Rename...", nullptr, false, local))
                    {
                        state.renaming = style.name;
                        state.nameBuffer = style.name;
                        state.openRenamePopup = true;
                    }
                    if (ImGui::MenuItem(local && document.GetInherited().FindStyle(style.name) ? "Remove Overrides" : "Delete", nullptr, false, local))
                        edits.push_back([name = style.name](scene::UITheme &theme) { theme.RemoveStyle(name); });
                    ImGui::EndPopup();
                }
                return open && hasChildren;
            };

            if (!state.search.empty())
            {
                for (const auto &style : merged.styles)
                    if (ContainsInsensitive(style.name, state.search))
                        drawNode(style, false);
            }
            else
            {
                const std::function<void(std::string_view, int)> drawChildren = [&](std::string_view parent, int depth)
                {
                    for (const auto &style : merged.styles)
                    {
                        // Styles in an inheritance cycle show as roots so they can be fixed.
                        const bool root = style.basedOn.empty() || !merged.FindStyle(style.basedOn) || merged.InheritsFrom(style.basedOn, style.name);
                        if (parent.empty() ? !root : style.basedOn != parent)
                            continue;
                        const bool hasChildren = depth < 16 && std::any_of(merged.styles.begin(), merged.styles.end(), [&](const scene::UIStyle &other)
                                                                            { return other.basedOn == style.name && other.name != style.name; });
                        if (drawNode(style, hasChildren))
                        {
                            drawChildren(style.name, depth + 1);
                            ImGui::TreePop();
                        }
                    }
                };
                drawChildren({}, 0);
            }
            // Dropping on empty space makes a style a root again.
            ImGui::Dummy(ImVec2(ImGui::GetContentRegionAvail().x, std::max(ImGui::GetContentRegionAvail().y, 24.0f)));
            if (ImGui::BeginDragDropTarget())
            {
                if (const auto *payload = ImGui::AcceptDragDropPayload(kStyleDragPayload))
                {
                    const std::string dragged(static_cast<const char *>(payload->Data));
                    edits.push_back([dragged](scene::UITheme &theme) { EnsureStyle(theme, dragged).basedOn.clear(); });
                }
                ImGui::EndDragDropTarget();
            }
            ImGui::EndChild();
        }

        // What a style would show for a property without its own value.
        const scene::UIResolvedProperty *InheritedValue(UIThemeDocument &document, const scene::UIStyle &style, UIStyleState state,
                                                       const std::string &component, const std::string &property)
        {
            if (const auto *base = document.GetInherited().FindStyle(style.name))
                if (const auto *value = base->Find(state, component, property))
                    return value;
            const auto *merged = document.GetResolved().GetMerged().FindStyle(style.name);
            if (merged && !merged->basedOn.empty())
                if (const auto *parent = document.GetResolved().FindStyle(merged->basedOn))
                    return parent->Find(state, component, property);
            return nullptr;
        }

        void DrawAddPropertyMenu(const scene::UIResolvedStyle &style, UIStyleState state, std::vector<Edit> &edits)
        {
            const auto &catalog = scene::UIStyleCatalog::Get();
            const auto add = [&](const std::string &component, const std::string &property, const std::string &value)
            {
                edits.push_back([name = style.name, state, component, property, value](scene::UITheme &theme)
                                { EnsureStyle(theme, name).Set(state, component, property, value); });
            };
            const auto isSet = [&](std::string_view component, std::string_view property)
            { return style.Find(state, component, property) != nullptr; };

            if (state != UIStyleState::Normal)
            {
                bool any = false;
                for (const auto &value : style.states[0])
                {
                    if (isSet(value.component, value.property))
                        continue;
                    if (!any)
                        ImGui::SeparatorText(("Change on " + std::string(kStateLabels[static_cast<std::size_t>(state)])).c_str());
                    any = true;
                    const auto label = catalog.GetComponentLabel(value.component) + " / " + value.property;
                    if (ImGui::MenuItem(label.c_str()))
                        add(value.component, value.property, value.authored);
                }
                if (any)
                    ImGui::SeparatorText("Any property");
            }
            for (const auto &component : catalog.GetComponents())
            {
                if (!ImGui::BeginMenu(component.displayName.c_str()))
                    continue;
                for (const auto &property : component.properties)
                {
                    if (isSet(component.typeName, property.name))
                        continue;
                    if (ImGui::MenuItem(property.name.c_str()))
                    {
                        // Start from what the element shows in Normal, else the default.
                        const auto *normal = style.Find(UIStyleState::Normal, component.typeName, property.name);
                        add(component.typeName, property.name, normal ? normal->authored : property.value);
                    }
                }
                ImGui::EndMenu();
            }
        }

        void DrawStyleDetails(UIThemeDocument &document, StylesViewState &state, const scene::UIResolvedStyle &style, std::vector<Edit> &edits)
        {
            const auto &resolved = document.GetResolved();
            const auto &merged = resolved.GetMerged();
            const auto *own = document.GetTheme().FindStyle(style.name);
            const auto &catalog = scene::UIStyleCatalog::Get();

            if (!own)
                ImGui::TextDisabled("Defined in a base theme. Changes are saved as overrides in %s.", document.GetLabel().c_str());

            if (ImGui::BeginTable("##StyleHeader", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
            {
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8.5f);
                ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch);
                const auto row = [](const char *label)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    ImGui::TextUnformatted(label);
                    ImGui::TableNextColumn();
                };
                row("Name");
                ImGui::SetNextItemWidth(-FLT_MIN);
                ImGui::BeginDisabled(!own);
                std::string name = state.renaming == style.name ? state.nameBuffer : style.name;
                if (InputString("##Name", name))
                {
                    state.renaming = style.name;
                    state.nameBuffer = name;
                }
                if (ImGui::IsItemDeactivatedAfterEdit() && state.renaming == style.name)
                {
                    const auto newName = scene::MakeUIThemeName(state.nameBuffer);
                    if (newName != style.name && !merged.FindStyle(newName))
                        edits.push_back([from = style.name, newName, &state](scene::UITheme &theme)
                                        {
                                            if (theme.RenameStyle(from, newName))
                                            {
                                                RenameStyleInScene(from, newName);
                                                state.selected = newName;
                                            }
                                        });
                    state.renaming.clear();
                }
                ImGui::EndDisabled();

                row("Based on");
                ImGui::SetNextItemWidth(-FLT_MIN);
                std::string base = merged.FindStyle(style.name)->basedOn;
                if (StyleCombo("##BasedOn", resolved, base, true, style.name))
                    edits.push_back([name = style.name, base](scene::UITheme &theme) { EnsureStyle(theme, name).basedOn = base; });
                if (style.chain.size() > 1)
                {
                    std::string chain;
                    for (std::size_t index = 1; index < style.chain.size(); ++index)
                        chain += (index > 1 ? "  >  " : "") + style.chain[index];
                    ImGui::TextDisabled("Inherits from %s", chain.c_str());
                }
                else
                    ImGui::TextDisabled("Pick a base to inherit its values; this style's own values win.");
                ImGui::EndTable();
            }

            // State selector; a dot marks states with values.
            std::array<std::string, scene::kUIStyleStateCount> labels;
            std::array<const char *, scene::kUIStyleStateCount> labelPointers{};
            for (std::size_t index = 0; index < labels.size(); ++index)
            {
                labels[index] = std::string(kStateLabels[index]) + (index > 0 && !style.states[index].empty() ? " \xe2\x80\xa2" : "");
                labelPointers[index] = labels[index].c_str();
            }
            int stateIndex = static_cast<int>(state.state);
            ImGui::Spacing();
            if (widgets::SegmentedControl("##State", &stateIndex, labelPointers))
                state.state = static_cast<UIStyleState>(stateIndex);
            ImGui::TextDisabled("%s", kStateHints[static_cast<std::size_t>(state.state)]);
            ImGui::Spacing();

            const auto &values = style.states[static_cast<std::size_t>(state.state)];
            if (values.empty())
                ImGui::TextWrapped(state.state == UIStyleState::Normal ? "This style sets nothing yet. Add the properties it should control."
                                                                       : "Nothing changes in this state yet.");

            // Rows grouped by component, in catalog order, then any unknown components.
            std::vector<std::string> components;
            for (const auto &info : catalog.GetComponents())
                if (std::any_of(values.begin(), values.end(), [&](const auto &value) { return value.component == info.typeName; }))
                    components.push_back(info.typeName);
            for (const auto &value : values)
                if (std::find(components.begin(), components.end(), value.component) == components.end())
                    components.push_back(value.component);

            for (const auto &component : components)
            {
                ImGui::SeparatorText(catalog.GetComponentLabel(component).c_str());
                if (!catalog.FindComponent(component))
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.45f, 1.0f), "Unknown component '%s'", component.c_str());
                if (!ImGui::BeginTable(component.c_str(), 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_PadOuterX))
                    continue;
                ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8.5f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("From", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.0f);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() * 2.2f);
                for (const auto &row : values)
                {
                    if (row.component != component)
                        continue;
                    const auto *ownValue = own ? own->Find(state.state, row.component, row.property) : nullptr;
                    const auto *info = catalog.FindProperty(row.component, row.property);
                    ImGui::PushID(row.property.c_str());
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    if (ownValue)
                        ImGui::TextUnformatted(row.property.c_str());
                    else
                        ImGui::TextDisabled("%s", row.property.c_str());
                    if (!info)
                        ImGui::SetItemTooltip("Not a property of %s", component.c_str());

                    ImGui::TableNextColumn();
                    std::string value = ownValue ? ownValue->value : row.authored;
                    const auto setValue = [&](std::string newValue)
                    {
                        edits.push_back([name = style.name, s = state.state, c = row.component, p = row.property, v = std::move(newValue)](scene::UITheme &theme)
                                        { EnsureStyle(theme, name).Set(s, c, p, v); });
                    };
                    if (StyleValueEditor("##Value", info, value, resolved))
                        setValue(value);

                    ImGui::TableNextColumn();
                    ImGui::AlignTextToFramePadding();
                    const auto *fallback = InheritedValue(document, *merged.FindStyle(style.name), state.state, row.component, row.property);
                    if (!ownValue)
                        ImGui::TextDisabled("%s", row.sourceStyle == style.name ? "base theme" : row.sourceStyle.c_str());
                    else if (fallback)
                        ImGui::TextDisabled("overrides %s", fallback->sourceStyle.c_str());

                    ImGui::TableNextColumn();
                    const auto variableResult = VariableMenuButton("##Link", info ? info->type : scene::PropertyType::String, value, resolved);
                    if (variableResult == VariableMenuResult::Changed)
                        setValue(value);
                    else if (variableResult == VariableMenuResult::MakeVariable)
                    {
                        const auto type = VariableTypeFor(info ? info->type : scene::PropertyType::String);
                        edits.push_back([name = style.name, s = state.state, c = row.component, p = row.property, v = value, type](scene::UITheme &theme)
                                        {
                                            const auto &variable = theme.AddVariable(scene::MakeUIThemeName(name + "-" + p), type, v);
                                            EnsureStyle(theme, name).Set(s, c, p, "@" + variable.name);
                                        });
                    }
                    if (ownValue)
                    {
                        ImGui::SameLine(0.0f, 2.0f);
                        if (ImGui::SmallButton(fallback ? icons::Revert : icons::Close))
                            edits.push_back([name = style.name, s = state.state, c = row.component, p = row.property](scene::UITheme &theme)
                                            { EnsureStyle(theme, name).Clear(s, c, p); });
                        ImGui::SetItemTooltip(fallback ? "Use the inherited value from %s" : "Remove from this style",
                                              fallback ? fallback->sourceStyle.c_str() : "");
                    }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            ImGui::Spacing();
            if (ImGui::Button((std::string(icons::Plus) + "  Add Property").c_str()))
                ImGui::OpenPopup("AddProperty");
            if (ImGui::BeginPopup("AddProperty"))
            {
                DrawAddPropertyMenu(style, state.state, edits);
                ImGui::EndPopup();
            }
            if (const auto users = CountStyleUsersInScene(style.name))
            {
                ImGui::SameLine();
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("Used by %zu element%s in this scene", users, users == 1 ? "" : "s");
            }
        }

        void DrawPopups(UIThemeDocument &document, StylesViewState &state, std::vector<Edit> &edits)
        {
            if (std::exchange(state.openNewStylePopup, false))
                ImGui::OpenPopup("New Style");
            if (std::exchange(state.openRenamePopup, false))
                ImGui::OpenPopup("Rename Style");
            const auto &resolved = document.GetResolved();

            if (ImGui::BeginPopupModal("New Style", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();
                ImGui::SetNextItemWidth(260.0f);
                InputString("Name", state.nameBuffer);
                ImGui::SetNextItemWidth(260.0f);
                StyleCombo("Based on", resolved, state.newStyleBase, true);
                const auto name = document.GetTheme().MakeUniqueStyleName(state.nameBuffer);
                if (name != state.nameBuffer)
                    ImGui::TextDisabled("Will be named %s", name.c_str());
                if (widgets::PrimaryButton("Create") || ImGui::IsKeyPressed(ImGuiKey_Enter))
                {
                    edits.push_back([name = state.nameBuffer, base = state.newStyleBase, &state](scene::UITheme &theme)
                                    { state.selected = theme.AddStyle(name, base).name; });
                    state.state = UIStyleState::Normal;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
                    ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }

            if (ImGui::BeginPopupModal("Rename Style", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();
                ImGui::SetNextItemWidth(260.0f);
                InputString("Name", state.nameBuffer);
                const auto name = scene::MakeUIThemeName(state.nameBuffer);
                const bool taken = name != state.renaming && resolved.GetMerged().FindStyle(name);
                if (taken)
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.45f, 1.0f), "A style named %s already exists", name.c_str());
                else if (name != state.nameBuffer)
                    ImGui::TextDisabled("Will be named %s", name.c_str());
                ImGui::TextDisabled("Elements in this scene are updated; other scenes keep the old name.");
                ImGui::BeginDisabled(taken);
                if (widgets::PrimaryButton("Rename") || (!taken && ImGui::IsKeyPressed(ImGuiKey_Enter)))
                {
                    edits.push_back([from = state.renaming, name, &state](scene::UITheme &theme)
                                    {
                                        if (theme.RenameStyle(from, name))
                                        {
                                            RenameStyleInScene(from, name);
                                            state.selected = name;
                                        }
                                    });
                    state.renaming.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
                {
                    state.renaming.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }
    }

    void DrawStylesView(UIThemeDocument &document, StylesViewState &state)
    {
        std::vector<Edit> edits;
        const auto &resolved = document.GetResolved();
        if (state.selected.empty() && !resolved.GetStyles().empty())
            state.selected = resolved.GetStyles().front().name;

        ImGui::BeginChild("##StyleList", ImVec2(ImGui::GetFontSize() * 14.0f, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
        DrawStyleList(document, state, edits);
        ImGui::EndChild();
        ImGui::SameLine();
        ImGui::BeginChild("##StyleDetails");
        if (const auto *style = resolved.FindStyle(state.selected))
            DrawStyleDetails(document, state, *style, edits);
        else
            ImGui::TextDisabled("Select a style, or press + to create one.");
        ImGui::EndChild();
        DrawPopups(document, state, edits);

        if (!edits.empty())
            document.Modify([&](scene::UITheme &theme)
                            {
                                for (const auto &edit : edits)
                                    edit(theme);
                            });
    }

    std::size_t CountStyleUsersInScene(const std::string &style)
    {
        std::size_t count = 0;
        ForEachStyledElement([&](scene::Entity &, scene::UIStyleComponent &component) { count += component.HasStyle(style) ? 1 : 0; });
        return count;
    }

    void RenameStyleInScene(const std::string &from, const std::string &to)
    {
        ForEachStyledElement([&](scene::Entity &entity, scene::UIStyleComponent &component)
                             {
                                 if (!component.HasStyle(from))
                                     return;
                                 auto styles = component.GetStyles();
                                 std::replace(styles.begin(), styles.end(), from, to);
                                 component.SetStyles(std::move(styles));
                                 entity.AddPrefabOverride("Component:UIStyleComponent:Styles");
                                 EditorShell::GetInstance().MarkSceneDirty();
                             });
    }
}
