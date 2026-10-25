#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::ui
{
    // One entry in a dialog's file type list. Patterns are ';'-separated
    // globs, for example "*.gltf;*.glb" or "*" for every file.
    struct FileDialogFilter
    {
        std::string_view name;
        std::string_view patterns;
    };

    // Native file dialogs. They block until the user answers. An empty result
    // with an empty error means the user cancelled; when no dialog is
    // available (on Linux, neither zenity nor kdialog is installed) the error
    // says how to fix it.
    [[nodiscard]] std::filesystem::path ShowOpenFileDialog(std::span<const FileDialogFilter> filters,
                                                           std::string *errorMessage = nullptr);
    [[nodiscard]] std::vector<std::filesystem::path> ShowOpenFilesDialog(std::span<const FileDialogFilter> filters,
                                                                         std::string *errorMessage = nullptr);
    // `defaultExtension` (without the dot) is appended when the user omits one.
    [[nodiscard]] std::filesystem::path ShowSaveFileDialog(std::span<const FileDialogFilter> filters,
                                                           const std::filesystem::path &initialPath,
                                                           std::string_view defaultExtension,
                                                           std::string *errorMessage = nullptr);

    // Argument builders for the Linux dialog programs, exposed for tests.
    enum class FileDialogMode
    {
        Open,
        OpenMultiple,
        Save,
    };
    [[nodiscard]] std::vector<std::string> BuildZenityArguments(FileDialogMode mode, std::span<const FileDialogFilter> filters,
                                                                const std::filesystem::path &initialPath);
    [[nodiscard]] std::vector<std::string> BuildKDialogArguments(FileDialogMode mode, std::span<const FileDialogFilter> filters,
                                                                 const std::filesystem::path &initialPath);
}
