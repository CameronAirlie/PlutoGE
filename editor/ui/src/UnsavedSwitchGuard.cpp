#include "PlutoGE/ui/UnsavedSwitchGuard.h"

#include "PlutoGE/ui/EditorDocument.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/EditorWidgets.h"

#include <imgui.h>

namespace PlutoGE::ui
{
    std::optional<std::string> UnsavedSwitchGuard::DrawPrompt(IEditorDocument &document, const char *popupId)
    {
        if (std::exchange(m_openPrompt, false))
            ImGui::OpenPopup(popupId);
        if (!ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            return std::nullopt;

        std::optional<std::string> next;
        ImGui::Text("Save changes to %s before opening another asset?", document.GetDocumentName().c_str());
        if (!m_error.empty())
            ImGui::TextColored(GetEditorColor(EditorColor::Error), "%s", m_error.c_str());
        const auto proceed = [&]
        {
            next = std::exchange(m_pending, std::nullopt);
            m_error.clear();
            ImGui::CloseCurrentPopup();
        };
        if (widgets::PrimaryButton("Save"))
        {
            m_error.clear();
            if (document.SaveDocument(m_error))
                proceed();
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save"))
        {
            document.DiscardChanges();
            proceed();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            // Stay on the edited asset. The selection is still remembered as followed, so the prompt returns only
            // when the user selects something else.
            m_pending.reset();
            m_error.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return next;
    }
}
