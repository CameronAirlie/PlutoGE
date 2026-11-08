#include "PlutoGE/ui/EditorDocument.h"

#include "PlutoGE/ui/SystemShell.h"

#include <filesystem>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace PlutoGE::ui
{
    std::string FormatUnsavedChangesMessage(std::span<const std::string> documentNames)
    {
        std::string message = documentNames.size() == 1 ? "Save changes to this before continuing?\n"
                                                        : "Save changes to these before continuing?\n";
        for (const auto &name : documentNames)
            message += "\n  \xE2\x80\xA2 " + name;
        message += "\n\nChanges you don't save are lost.";
        return message;
    }

    UnsavedChangesChoice AskToSaveChanges(std::span<const std::string> documentNames, std::string *error)
    {
        const auto message = FormatUnsavedChangesMessage(documentNames);
#ifdef _WIN32
        (void)error;
        const auto wideMessage = std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(message.data()), message.size())).wstring();
        // Yes / No / Cancel read as Save / Don't Save / Cancel with this wording.
        const int result = MessageBoxW(GetActiveWindow(), wideMessage.c_str(), L"Unsaved Changes",
                                       MB_ICONWARNING | MB_YESNOCANCEL | MB_DEFBUTTON1);
        if (result == IDYES)
            return UnsavedChangesChoice::Save;
        if (result == IDNO)
            return UnsavedChangesChoice::Discard;
        return UnsavedChangesChoice::Cancel;
#else
        const auto zenity = RunProcess({"zenity", "--question", "--title=Unsaved Changes", "--text=" + message, "--no-wrap",
                                        "--ok-label=Save", "--cancel-label=Cancel", "--extra-button=Don't Save"});
        if (zenity.started && zenity.exitStatus != 127)
        {
            if (zenity.exitStatus == 0)
                return UnsavedChangesChoice::Save;
            // The extra button prints its label and exits 1, like Cancel.
            return zenity.output.find("Don't Save") != std::string::npos ? UnsavedChangesChoice::Discard
                                                                           : UnsavedChangesChoice::Cancel;
        }
        const auto kdialog = RunProcess({"kdialog", "--title", "Unsaved Changes", "--warningyesnocancel", message,
                                         "--yes-label", "Save", "--no-label", "Don't Save"});
        if (kdialog.started && kdialog.exitStatus != 127)
        {
            if (kdialog.exitStatus == 0)
                return UnsavedChangesChoice::Save;
            return kdialog.exitStatus == 1 ? UnsavedChangesChoice::Discard : UnsavedChangesChoice::Cancel;
        }
        if (error)
            *error = "Save or discard your changes first: no dialog program is available to ask (install zenity or kdialog).";
        return UnsavedChangesChoice::Cancel;
#endif
    }

    bool ResolveUnsavedDocuments(std::span<IEditorDocument *const> documents, const UnsavedChangesAsker &ask, std::string &error)
    {
        std::vector<IEditorDocument *> dirty;
        std::vector<std::string> names;
        for (auto *document : documents)
        {
            if (document && document->HasUnsavedChanges())
            {
                dirty.push_back(document);
                names.push_back(document->GetDocumentName());
            }
        }
        if (dirty.empty())
            return true;

        switch (ask(names))
        {
        case UnsavedChangesChoice::Cancel:
            return false;
        case UnsavedChangesChoice::Discard:
            for (auto *document : dirty)
                document->DiscardChanges();
            return true;
        case UnsavedChangesChoice::Save:
            break;
        }

        for (auto *document : dirty)
        {
            std::string saveError;
            if (!document->SaveDocument(saveError))
            {
                error = "Could not save " + document->GetDocumentName() + (saveError.empty() ? "." : ": " + saveError);
                document->FocusDocument();
                return false;
            }
        }
        return true;
    }
}
