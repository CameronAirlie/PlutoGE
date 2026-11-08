#pragma once

#include <functional>
#include <span>
#include <string>
#include <vector>

namespace PlutoGE::ui
{
    // Something the user edits and saves explicitly: the scene, a material, an animation graph... Editor panels
    // that hold unsaved edits implement this, and the shell finds them through the panel list, so a new editor
    // gets the unsaved-changes prompt and Ctrl+S without changes to EditorShell.
    class IEditorDocument
    {
    public:
        virtual ~IEditorDocument() = default;

        // Shown in prompts, e.g. "Material Rock.plutomaterial".
        [[nodiscard]] virtual std::string GetDocumentName() const = 0;
        [[nodiscard]] virtual bool HasUnsavedChanges() const = 0;
        // Writes the document. On failure returns false with a reason and keeps the edits.
        virtual bool SaveDocument(std::string &error) = 0;
        // Throws away unsaved edits, returning the editor to the saved state.
        virtual void DiscardChanges() = 0;
        // Brings the document's editor to the front.
        virtual void FocusDocument() {}
    };

    enum class UnsavedChangesChoice
    {
        Save,
        Discard,
        Cancel,
    };

    // "Save changes to the following before continuing?" plus one bullet per document.
    [[nodiscard]] std::string FormatUnsavedChangesMessage(std::span<const std::string> documentNames);

    // Blocking Save / Don't Save / Cancel question (Windows message box; zenity or kdialog on Linux). When no
    // dialog is available it answers Cancel and explains why in `error`, so nothing is lost silently.
    [[nodiscard]] UnsavedChangesChoice AskToSaveChanges(std::span<const std::string> documentNames, std::string *error = nullptr);

    using UnsavedChangesAsker = std::function<UnsavedChangesChoice(std::span<const std::string>)>;

    // Asks once about every document with unsaved changes and carries out the answer. Returns true when the
    // caller may continue: nothing was dirty, the user discarded, or every save succeeded. A failed save stops,
    // focuses that document, and reports the reason in `error`.
    bool ResolveUnsavedDocuments(std::span<IEditorDocument *const> documents, const UnsavedChangesAsker &ask,
                                 std::string &error);
}
