#include "PlutoGE/ui/contentbrowser/ContentCommands.h"

#include <array>

namespace PlutoGE::ui
{
    namespace
    {
#ifdef __APPLE__
        constexpr const char *kRevealLabel = "Reveal in Finder";
#elif defined(_WIN32)
        constexpr const char *kRevealLabel = "Show in Explorer";
#else
        constexpr const char *kRevealLabel = "Show in File Manager";
#endif

        // ImGuiMod_Ctrl maps to Cmd on macOS when io.ConfigMacOSXBehaviors is set.
        constexpr std::array kCommands{
            ContentCommandInfo{ContentCommand::Open, "Open", ImGuiKey_Enter, "Enter"},
            ContentCommandInfo{ContentCommand::NewFolder, "New Folder", ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_N, "Ctrl+Shift+N"},
            ContentCommandInfo{ContentCommand::Rename, "Rename", ImGuiKey_F2, "F2"},
            ContentCommandInfo{ContentCommand::Cut, "Cut", ImGuiMod_Ctrl | ImGuiKey_X, "Ctrl+X"},
            ContentCommandInfo{ContentCommand::Copy, "Copy", ImGuiMod_Ctrl | ImGuiKey_C, "Ctrl+C"},
            ContentCommandInfo{ContentCommand::Paste, "Paste", ImGuiMod_Ctrl | ImGuiKey_V, "Ctrl+V"},
            ContentCommandInfo{ContentCommand::Duplicate, "Duplicate", ImGuiMod_Ctrl | ImGuiKey_D, "Ctrl+D"},
            ContentCommandInfo{ContentCommand::Delete, "Move to Trash", ImGuiKey_Delete, "Del"},
            ContentCommandInfo{ContentCommand::DeletePermanently, "Delete Permanently", ImGuiMod_Shift | ImGuiKey_Delete, "Shift+Del"},
            ContentCommandInfo{ContentCommand::SelectAll, "Select All", ImGuiMod_Ctrl | ImGuiKey_A, "Ctrl+A"},
            ContentCommandInfo{ContentCommand::RevealInFileManager, kRevealLabel, ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_R, "Ctrl+Shift+R"},
            ContentCommandInfo{ContentCommand::OpenWithSystem, "Open with Default App", ImGuiKey_None, nullptr},
            ContentCommandInfo{ContentCommand::CopyPath, "Copy Path", ImGuiMod_Ctrl | ImGuiMod_Alt | ImGuiKey_C, "Ctrl+Alt+C"},
            ContentCommandInfo{ContentCommand::CopyReference, "Copy Asset Reference", ImGuiKey_None, nullptr},
            ContentCommandInfo{ContentCommand::GoBack, "Back", ImGuiMod_Alt | ImGuiKey_LeftArrow, "Alt+Left"},
            ContentCommandInfo{ContentCommand::GoForward, "Forward", ImGuiMod_Alt | ImGuiKey_RightArrow, "Alt+Right"},
            ContentCommandInfo{ContentCommand::GoUp, "Up to Parent Folder", ImGuiMod_Alt | ImGuiKey_UpArrow, "Alt+Up"},
            ContentCommandInfo{ContentCommand::Refresh, "Refresh", ImGuiKey_None, nullptr},
        };
    }

    const ContentCommandInfo &GetContentCommandInfo(ContentCommand command)
    {
        for (const auto &info : kCommands)
        {
            if (info.command == command)
            {
                return info;
            }
        }
        return kCommands.front();
    }

    std::span<const ContentCommandInfo> GetContentCommands()
    {
        return kCommands;
    }

    const char *GetContentCommandShortcut(ContentCommand command)
    {
        return GetContentCommandInfo(command).shortcutText;
    }
}
