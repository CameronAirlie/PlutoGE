#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace PlutoGE::ui
{
    // Hands files to the operating system. Arguments are passed directly to the
    // launched process (never through a shell), so paths need no escaping.

    struct ProcessResult
    {
        bool started = false; // False when the program could not be launched (for example not installed).
        int exitStatus = -1;  // Exit code once it ran to completion.
        std::string output;   // Everything the process wrote to stdout.
    };

    // Runs `arguments[0]` (searched on PATH) to completion, capturing stdout.
    // Blocks the calling thread; intended for short helpers such as dialogs.
    [[nodiscard]] ProcessResult RunProcess(const std::vector<std::string> &arguments);

    // Opens a file with its default application, or a folder in the file manager.
    bool OpenWithSystem(const std::filesystem::path &path, std::string *errorMessage = nullptr);

    // Shows the item selected in the platform file manager. On Linux this uses
    // the freedesktop FileManager1 D-Bus interface and falls back to opening
    // the containing folder when no file manager provides it.
    bool RevealInFileManager(const std::filesystem::path &path, std::string *errorMessage = nullptr);

    // Moves a file or folder to the platform trash (Recycle Bin, Finder Trash,
    // or the freedesktop.org trash) so it can be restored. Fails rather than
    // deleting when the platform cannot trash the item.
    bool MoveToTrash(const std::filesystem::path &path, std::string *errorMessage = nullptr);

    // The freedesktop.org Trash specification against an explicit trash
    // directory (holding files/ and info/). Used on Linux; exposed for tests.
    bool MoveToFreedesktopTrash(const std::filesystem::path &path, const std::filesystem::path &trashDirectory,
                                std::string *errorMessage = nullptr);

    // Percent-encodes every byte of a UTF-8 path outside the unreserved set,
    // keeping '/' separators.
    [[nodiscard]] std::string PercentEncodePath(std::string_view utf8Path);

    // file:// URI with every byte outside the unreserved set percent-encoded.
    [[nodiscard]] std::string MakeFileUri(const std::filesystem::path &path);
}
