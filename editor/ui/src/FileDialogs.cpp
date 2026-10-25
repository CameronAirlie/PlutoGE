#include "PlutoGE/ui/FileDialogs.h"

#include "PlutoGE/ui/SystemShell.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>
#endif

namespace PlutoGE::ui
{
    namespace
    {
        std::filesystem::path PathFromUtf8(std::string_view text)
        {
            return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t *>(text.data()), text.size()));
        }

        std::string Utf8(const std::filesystem::path &path)
        {
            const auto text = path.u8string();
            return {reinterpret_cast<const char *>(text.data()), text.size()};
        }

        // "*.a;*.b" -> "*.a *.b", the form both Linux dialog programs expect.
        std::string SpaceSeparated(std::string_view patterns)
        {
            std::string result(patterns);
            for (char &character : result)
                if (character == ';') character = ' ';
            return result;
        }

#ifdef _WIN32
        std::wstring Wide(std::string_view text)
        {
            return PathFromUtf8(text).wstring();
        }

        // Double-null-terminated "Name\0patterns\0..." list.
        std::wstring BuildWindowsFilter(std::span<const FileDialogFilter> filters)
        {
            std::wstring filter;
            for (const auto &entry : filters)
            {
                filter += Wide(entry.name);
                filter.push_back(L'\0');
                filter += Wide(entry.patterns);
                filter.push_back(L'\0');
            }
            filter.push_back(L'\0');
            return filter;
        }

        std::vector<std::filesystem::path> RunWindowsDialog(FileDialogMode mode, std::span<const FileDialogFilter> filters,
                                                            const std::filesystem::path &initialPath, std::string_view defaultExtension)
        {
            // Large enough for multi-selection: a directory plus many names.
            std::wstring buffer(32768, L'\0');
            const auto initial = initialPath.wstring();
            initial.copy(buffer.data(), std::min(initial.size(), buffer.size() - 1));
            const auto filter = BuildWindowsFilter(filters);
            const auto extension = Wide(defaultExtension);
            OPENFILENAMEW dialog{};
            dialog.lStructSize = sizeof(dialog);
            dialog.lpstrFilter = filter.c_str();
            dialog.lpstrFile = buffer.data();
            dialog.nMaxFile = static_cast<DWORD>(buffer.size());
            dialog.lpstrDefExt = extension.empty() ? nullptr : extension.c_str();
            dialog.Flags = OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
            if (mode == FileDialogMode::Save)
                dialog.Flags |= OFN_OVERWRITEPROMPT;
            else
                dialog.Flags |= OFN_FILEMUSTEXIST | (mode == FileDialogMode::OpenMultiple ? OFN_ALLOWMULTISELECT : 0);
            const bool accepted = mode == FileDialogMode::Save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog);
            if (!accepted)
            {
                return {};
            }
            // Multi-selection returns "directory\0name\0name\0\0"; one item is just the path.
            std::vector<std::wstring> parts;
            for (const wchar_t *part = buffer.c_str(); *part; part += std::wcslen(part) + 1) parts.emplace_back(part);
            std::vector<std::filesystem::path> paths;
            if (parts.size() == 1)
            {
                paths.emplace_back(std::filesystem::path(parts.front()).lexically_normal());
            }
            else
            {
                for (std::size_t index = 1; index < parts.size(); ++index)
                    paths.emplace_back((std::filesystem::path(parts.front()) / parts[index]).lexically_normal());
            }
            return paths;
        }
#else
        std::vector<std::filesystem::path> SplitLines(const std::string &output)
        {
            std::vector<std::filesystem::path> paths;
            std::size_t start = 0;
            while (start < output.size())
            {
                auto end = output.find('\n', start);
                if (end == std::string::npos) end = output.size();
                auto line = std::string_view(output).substr(start, end - start);
                if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
                if (!line.empty()) paths.push_back(PathFromUtf8(line));
                start = end + 1;
            }
            return paths;
        }

        std::filesystem::path WithDefaultExtension(std::filesystem::path path, std::string_view extension)
        {
            if (!path.empty() && !extension.empty() && !path.has_extension())
            {
                path += "." + std::string(extension);
            }
            return path;
        }

        // Tries zenity, then kdialog. Exit status 0 is a choice, 1 a cancel.
        std::vector<std::filesystem::path> RunLinuxDialog(FileDialogMode mode, std::span<const FileDialogFilter> filters,
                                                          const std::filesystem::path &initialPath, std::string *errorMessage)
        {
            for (const auto &arguments : {BuildZenityArguments(mode, filters, initialPath), BuildKDialogArguments(mode, filters, initialPath)})
            {
                const auto result = RunProcess(arguments);
                if (!result.started)
                {
                    continue;
                }
                if (errorMessage) errorMessage->clear();
                return result.exitStatus == 0 ? SplitLines(result.output) : std::vector<std::filesystem::path>{};
            }
            if (errorMessage)
            {
                *errorMessage = "No file dialog is available. Install zenity (GNOME and most desktops) or kdialog (KDE), "
                                "or drop files onto the editor window instead.";
            }
            return {};
        }
#endif

        std::vector<std::filesystem::path> RunDialog(FileDialogMode mode, std::span<const FileDialogFilter> filters,
                                                     const std::filesystem::path &initialPath, std::string_view defaultExtension,
                                                     std::string *errorMessage)
        {
            if (errorMessage) errorMessage->clear();
#ifdef _WIN32
            return RunWindowsDialog(mode, filters, initialPath, defaultExtension);
#else
            auto paths = RunLinuxDialog(mode, filters, initialPath, errorMessage);
            if (mode == FileDialogMode::Save && !paths.empty())
            {
                paths.front() = WithDefaultExtension(paths.front(), defaultExtension);
            }
            return paths;
#endif
        }
    }

    std::vector<std::string> BuildZenityArguments(FileDialogMode mode, std::span<const FileDialogFilter> filters,
                                                  const std::filesystem::path &initialPath)
    {
        std::vector<std::string> arguments{"zenity", "--file-selection"};
        if (mode == FileDialogMode::Save) arguments.emplace_back("--save");
        if (mode == FileDialogMode::OpenMultiple)
        {
            arguments.emplace_back("--multiple");
            arguments.emplace_back("--separator=\n");
        }
        if (!initialPath.empty()) arguments.push_back("--filename=" + Utf8(initialPath));
        for (const auto &filter : filters)
        {
            arguments.push_back("--file-filter=" + std::string(filter.name) + " | " + SpaceSeparated(filter.patterns));
        }
        return arguments;
    }

    std::vector<std::string> BuildKDialogArguments(FileDialogMode mode, std::span<const FileDialogFilter> filters,
                                                   const std::filesystem::path &initialPath)
    {
        std::vector<std::string> arguments{"kdialog"};
        arguments.emplace_back(mode == FileDialogMode::Save ? "--getsavefilename" : "--getopenfilename");
        if (mode == FileDialogMode::OpenMultiple)
        {
            arguments.emplace_back("--multiple");
            arguments.emplace_back("--separate-output");
        }
        arguments.push_back(initialPath.empty() ? std::string(".") : Utf8(initialPath));
        // kdialog filters are "patterns|Name" entries separated by newlines.
        std::string filter;
        for (const auto &entry : filters)
        {
            if (!filter.empty()) filter += '\n';
            filter += SpaceSeparated(entry.patterns) + "|" + std::string(entry.name);
        }
        if (!filter.empty()) arguments.push_back(filter);
        return arguments;
    }

    std::filesystem::path ShowOpenFileDialog(std::span<const FileDialogFilter> filters, std::string *errorMessage)
    {
        auto paths = RunDialog(FileDialogMode::Open, filters, {}, {}, errorMessage);
        return paths.empty() ? std::filesystem::path{} : paths.front();
    }

    std::vector<std::filesystem::path> ShowOpenFilesDialog(std::span<const FileDialogFilter> filters, std::string *errorMessage)
    {
        return RunDialog(FileDialogMode::OpenMultiple, filters, {}, {}, errorMessage);
    }

    std::filesystem::path ShowSaveFileDialog(std::span<const FileDialogFilter> filters, const std::filesystem::path &initialPath,
                                             std::string_view defaultExtension, std::string *errorMessage)
    {
        auto paths = RunDialog(FileDialogMode::Save, filters, initialPath, defaultExtension, errorMessage);
        return paths.empty() ? std::filesystem::path{} : paths.front();
    }
}
