#include "PlutoGE/ui/ListEditor.h"

#include "PlutoGE/ui/EditorIcons.h"
#include "PlutoGE/ui/EditorWidgets.h"

#include <imgui.h>

#include <optional>
#include <string>
#include <string_view>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr const char *kListElementPayload = "PLUTO_LIST_ELEMENT";

        // Identifies the dragged row, so a drop only reorders the list it came from.
        struct ListElementPayload
        {
            ImGuiID list = 0;
            std::size_t index = 0;
        };

        struct PendingEdit
        {
            enum class Kind { Remove, Move } kind;
            std::size_t index = 0;
            std::size_t target = 0;
        };

        std::string_view VisibleLabel(const char *label)
        {
            const std::string_view text(label);
            return text.substr(0, text.find("##"));
        }

        // The drag handle is both the drag source and the drop target of its row.
        std::optional<PendingEdit> RenderReorderHandle(ImGuiID list, std::size_t index)
        {
            widgets::IconButton("##reorder", icons::Grip, "Drag to reorder");
            if (ImGui::BeginDragDropSource())
            {
                const ListElementPayload payload{list, index};
                ImGui::SetDragDropPayload(kListElementPayload, &payload, sizeof(payload));
                ImGui::Text("Element %zu", index);
                ImGui::EndDragDropSource();
            }

            std::optional<PendingEdit> edit;
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload(kListElementPayload))
                {
                    const auto *dragged = static_cast<const ListElementPayload *>(payload->Data);
                    if (payload->DataSize == sizeof(ListElementPayload) && dragged->list == list && dragged->index != index)
                        edit = PendingEdit{PendingEdit::Kind::Move, dragged->index, index};
                }
                ImGui::EndDragDropTarget();
            }
            return edit;
        }
    }

    bool RenderListEditor(const char *label, const ListEditorModel &model, const ListEditorOptions &options)
    {
        ImGui::PushID(label);
        const ImGuiID listId = ImGui::GetID("##list");
        const auto visibleLabel = VisibleLabel(label);
        // A field list is not part of a hierarchy: opt out of the theme's tree lines, which would
        // otherwise connect this header to the enclosing component header.
        const ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DrawLinesNone |
                                         (options.defaultOpen ? ImGuiTreeNodeFlags_DefaultOpen : ImGuiTreeNodeFlags_None);
        const bool open = ImGui::TreeNodeEx("##header", flags, "%.*s  [%zu]",
                                            static_cast<int>(visibleLabel.size()), visibleLabel.data(), model.count);
        if (!open)
        {
            ImGui::PopID();
            return false;
        }

        bool changed = false;
        std::optional<PendingEdit> pending;
        if (model.count == 0)
            ImGui::TextDisabled("%s", options.emptyLabel);

        const float buttonWidth = ImGui::GetFrameHeight();
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        for (std::size_t index = 0; index < model.count; ++index)
        {
            ImGui::PushID(static_cast<int>(index));
            if (options.reorderable && model.move)
            {
                if (auto edit = RenderReorderHandle(listId, index))
                    pending = edit;
                ImGui::SameLine();
            }
            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%zu", index);
            ImGui::SameLine();

            const float removeWidth = model.remove ? buttonWidth + spacing : 0.0f;
            ImGui::PushItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - removeWidth));
            changed |= model.renderElement && model.renderElement(index);
            ImGui::PopItemWidth();

            if (model.remove)
            {
                ImGui::SameLine();
                if (widgets::IconButton("##remove", icons::Close, "Remove element"))
                    pending = PendingEdit{PendingEdit::Kind::Remove, index};
            }
            ImGui::PopID();
        }

        if (model.add && ImGui::Button((std::string(icons::Plus) + " " + options.addLabel).c_str()))
        {
            model.add();
            changed = true;
        }

        if (pending)
        {
            if (pending->kind == PendingEdit::Kind::Remove && model.remove)
                model.remove(pending->index);
            else if (pending->kind == PendingEdit::Kind::Move && model.move)
                model.move(pending->index, pending->target);
            changed = true;
        }

        ImGui::TreePop();
        ImGui::PopID();
        return changed;
    }
}
