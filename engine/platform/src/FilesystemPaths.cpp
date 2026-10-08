#include "PlutoGE/platform/FilesystemPaths.h"
#include <stdexcept>
#include <system_error>
#include <utility>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#endif

namespace PlutoGE::content
{
    bool IsPathWithinDirectory(const std::filesystem::path &path, const std::filesystem::path &directory, bool allowEqual)
    {
        if (!path.is_absolute() || !directory.is_absolute()) return false;
        auto normalize = [](const auto &value)
        {
            auto result = value.lexically_normal();
            while (result.has_relative_path() && result.filename().empty()) result = result.parent_path();
            return result;
        };
        const auto target = normalize(path);
        const auto root = normalize(directory);
        auto part = target.begin();
        for (auto parent = root.begin(); parent != root.end(); ++parent, ++part)
        {
            if (part == target.end()) return false;
#ifdef _WIN32
            if (CompareStringOrdinal(part->native().c_str(), -1, parent->native().c_str(), -1, TRUE) != CSTR_EQUAL) return false;
#else
            if (*part != *parent) return false;
#endif
        }
        return allowEqual || part != target.end();
    }

    bool ResolveDirectoryForCreation(const std::filesystem::path &requested,
                                     std::filesystem::path &resolved, std::string *errorMessage)
    {
        try
        {
            if (requested.empty()) throw std::runtime_error("Directory path cannot be empty.");
            const auto absolute = std::filesystem::absolute(requested);
            auto existing = absolute;
            for (;;)
            {
                std::error_code error;
                const auto status = std::filesystem::symlink_status(existing, error);
                if (error && error != std::errc::no_such_file_or_directory)
                    throw std::filesystem::filesystem_error("Cannot inspect directory prefix", existing, error);
                if (!error && status.type() != std::filesystem::file_type::not_found) break;
                const auto parent = existing.parent_path();
                if (parent.empty() || parent == existing) throw std::runtime_error("Cannot resolve directory prefix.");
                existing = parent;
            }
            if (!std::filesystem::is_directory(existing)) throw std::runtime_error("Directory prefix is not a directory.");
            auto candidate = (std::filesystem::canonical(existing) / absolute.lexically_relative(existing)).lexically_normal();
            resolved = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error)
        {
            if (errorMessage) *errorMessage = std::string("Cannot resolve directory: ") + error.what();
            return false;
        }
    }
}
