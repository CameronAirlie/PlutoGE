#pragma once

#include <optional>
#include <string>
#include <utility>

namespace PlutoGE::ui
{
    class IEditorDocument;

    // For asset editors that follow the editor's selection (the active material, particle system, ...). When the
    // selection moves while the editor holds unsaved edits, the editor stays on its asset and asks
    // Save / Don't Save / Cancel before switching, instead of silently dropping the edits.
    //
    //   if (auto next = m_switchGuard.Follow(shell.GetActiveXReference(), m_loadedReference, HasUnsavedChanges()))
    //       Load(*next);
    //   if (auto next = m_switchGuard.DrawPrompt(*this, "Unsaved Material"))
    //       Load(*next);
    class UnsavedSwitchGuard
    {
    public:
        // Returns the asset to load now, or nothing to keep showing `shown`.
        [[nodiscard]] std::optional<std::string> Follow(const std::string &selected, const std::string &shown, bool hasUnsavedChanges)
        {
            if (selected == m_followed)
                return std::nullopt;
            m_followed = selected;
            if (selected.empty() || selected == shown)
                return std::nullopt;
            if (hasUnsavedChanges)
            {
                m_pending = selected;
                m_openPrompt = true;
                return std::nullopt;
            }
            return selected;
        }

        // Draws the modal while a switch is pending. Returns the asset to load once the user saved or discarded.
        [[nodiscard]] std::optional<std::string> DrawPrompt(IEditorDocument &document, const char *popupId);

        [[nodiscard]] bool IsPromptPending() const { return m_pending.has_value(); }
        // Forget the followed selection, e.g. after the project changed, so the next Follow reloads.
        void Reset()
        {
            m_followed.clear();
            m_pending.reset();
            m_openPrompt = false;
            m_error.clear();
        }

    private:
        std::string m_followed;
        std::optional<std::string> m_pending;
        bool m_openPrompt = false;
        std::string m_error;
    };
}
