#pragma once

#include <filesystem>
#include <string>
#include <string_view>

namespace PlutoGE::ui
{
    // Folders in the content browser are generic ('/'-separated) paths relative
    // to the project's asset directory; the empty string is the asset root.

    // UTF-8 text to a path, and back (std::filesystem::u8path is deprecated in C++20).
    [[nodiscard]] std::filesystem::path PathFromUtf8(std::string_view text);
    [[nodiscard]] std::string PathToUtf8(const std::filesystem::path &path);

    [[nodiscard]] std::string NormalizeContentFolder(const std::filesystem::path &path);
    [[nodiscard]] std::string GetContentFolderParent(std::string_view folder);
    [[nodiscard]] std::string GetContentFolderName(std::string_view folder);
    [[nodiscard]] std::string JoinContentFolder(std::string_view folder, std::string_view child);
    [[nodiscard]] bool IsContentFolderAncestorOrSelf(std::string_view ancestor, std::string_view folder);
    // Re-roots `folder` when `from` (or an ancestor) moved to `to`; otherwise unchanged.
    [[nodiscard]] std::string RemapContentFolder(std::string_view folder, std::string_view from, std::string_view to);

    // Empty when `name` is usable as a single file or folder name on every
    // supported platform; otherwise a short reason suitable for the UI.
    [[nodiscard]] std::string ValidateContentItemName(std::string_view name);

    // First of "<stem><ext>", "<stem> 2<ext>", ... that does not exist in `directory`.
    [[nodiscard]] std::filesystem::path MakeUniqueContentPath(const std::filesystem::path &directory,
                                                              std::string_view stem, std::string_view extension);
    // "<name> Copy", then "<name> Copy 2", ... keeping a file's extension.
    [[nodiscard]] std::filesystem::path MakeUniqueContentCopyPath(const std::filesystem::path &directory,
                                                                  const std::filesystem::path &source);
}
