#include "PlutoGE/ui/contentbrowser/ContentPaths.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <system_error>

namespace PlutoGE::ui
{
    namespace
    {
        std::string Trimmed(std::string text)
        {
            std::replace(text.begin(), text.end(), '\\', '/');
            const auto first = text.find_first_not_of('/');
            if (first == std::string::npos)
            {
                return {};
            }
            const auto last = text.find_last_not_of('/');
            return text.substr(first, last - first + 1);
        }

        bool Exists(const std::filesystem::path &path)
        {
            std::error_code error;
            return std::filesystem::exists(std::filesystem::symlink_status(path, error));
        }
    }

    std::filesystem::path PathFromUtf8(std::string_view text)
    {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(text.data()), text.size()));
    }

    std::string PathToUtf8(const std::filesystem::path &path)
    {
        const auto text = path.u8string();
        return {reinterpret_cast<const char *>(text.data()), text.size()};
    }

    std::string NormalizeContentFolder(const std::filesystem::path &path)
    {
        const auto normalized = Trimmed(reinterpret_cast<const char *>(path.lexically_normal().generic_u8string().c_str()));
        return normalized == "." ? std::string{} : normalized;
    }

    std::string GetContentFolderParent(std::string_view folder)
    {
        const auto normalized = Trimmed(std::string(folder));
        const auto separator = normalized.find_last_of('/');
        return separator == std::string::npos ? std::string{} : normalized.substr(0, separator);
    }

    std::string GetContentFolderName(std::string_view folder)
    {
        const auto normalized = Trimmed(std::string(folder));
        const auto separator = normalized.find_last_of('/');
        return separator == std::string::npos ? normalized : normalized.substr(separator + 1);
    }

    std::string JoinContentFolder(std::string_view folder, std::string_view child)
    {
        if (folder.empty())
        {
            return std::string(child);
        }
        return std::string(folder) + "/" + std::string(child);
    }

    bool IsContentFolderAncestorOrSelf(std::string_view ancestor, std::string_view folder)
    {
        if (ancestor.empty() || folder == ancestor)
        {
            return true;
        }
        return folder.size() > ancestor.size() && folder.compare(0, ancestor.size(), ancestor) == 0 &&
               folder[ancestor.size()] == '/';
    }

    std::string RemapContentFolder(std::string_view folder, std::string_view from, std::string_view to)
    {
        if (from.empty() || !IsContentFolderAncestorOrSelf(from, folder))
        {
            return std::string(folder);
        }
        return std::string(to) + std::string(folder.substr(from.size()));
    }

    std::string ValidateContentItemName(std::string_view name)
    {
        if (name.empty() || std::all_of(name.begin(), name.end(), [](unsigned char c) { return std::isspace(c) != 0; }))
        {
            return "Enter a name.";
        }
        if (name == "." || name == "..")
        {
            return "That name is reserved.";
        }
        if (name.find_first_of("/\\<>:\"|?*") != std::string_view::npos ||
            std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 0x20; }))
        {
            return "Names cannot contain / \\ < > : \" | ? * or control characters.";
        }
        if (name.back() == '.' || name.back() == ' ' || name.front() == ' ')
        {
            return "Names cannot start with a space or end with a space or dot.";
        }
        // Windows device names are reserved regardless of extension.
        constexpr std::array kReserved{"CON", "PRN", "AUX", "NUL", "COM1", "COM2", "COM3", "COM4", "COM5", "COM6",
                                       "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6",
                                       "LPT7", "LPT8", "LPT9"};
        std::string stem(name.substr(0, name.find('.')));
        std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        if (std::find(kReserved.begin(), kReserved.end(), stem) != kReserved.end())
        {
            return "That name is reserved by Windows.";
        }
        return {};
    }

    std::filesystem::path MakeUniqueContentPath(const std::filesystem::path &directory,
                                                std::string_view stem, std::string_view extension)
    {
        const std::string base(stem);
        const std::string suffix(extension);
        auto candidate = directory / PathFromUtf8(base + suffix);
        for (unsigned int index = 2; Exists(candidate); ++index)
        {
            candidate = directory / PathFromUtf8(base + " " + std::to_string(index) + suffix);
        }
        return candidate;
    }

    std::filesystem::path MakeUniqueContentCopyPath(const std::filesystem::path &directory,
                                                    const std::filesystem::path &source)
    {
        std::error_code error;
        const bool isDirectory = std::filesystem::is_directory(source, error);
        const auto stem = (isDirectory ? source.filename() : source.stem()).u8string();
        const auto extension = isDirectory ? std::u8string{} : source.extension().u8string();
        return MakeUniqueContentPath(directory, reinterpret_cast<const char *>(stem.c_str()) + std::string(" Copy"),
                                     reinterpret_cast<const char *>(extension.c_str()));
    }
}
