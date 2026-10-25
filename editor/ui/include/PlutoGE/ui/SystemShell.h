#pragma once

#include <filesystem>
#include <string>

namespace PlutoGE::ui
{
    // Hands files to the operating system. Arguments are passed directly to the
    // launched process (never through a shell), so paths need no escaping.

    // Opens a file with its default application, or a folder in the file manager.
    bool OpenWithSystem(const std::filesystem::path &path, std::string *errorMessage = nullptr);

    // Shows the item selected in the platform file manager. On Linux this uses
    // the freedesktop FileManager1 D-Bus interface and falls back to opening
    // the containing folder when no file manager provides it.
    bool RevealInFileManager(const std::filesystem::path &path, std::string *errorMessage = nullptr);

    // file:// URI with every byte outside the unreserved set percent-encoded.
    [[nodiscard]] std::string MakeFileUri(const std::filesystem::path &path);
}
