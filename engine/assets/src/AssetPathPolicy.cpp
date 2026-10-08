#include "PlutoGE/assets/AssetPathPolicy.h"
#include <algorithm>
#include <cctype>
#include <string>

namespace PlutoGE::assets
{
    bool ValidateNoPendingAssetTransactions(const std::filesystem::path &projectRoot, std::string *errorMessage)
    {
        const auto root = projectRoot / ".pluto-import-transactions";
        std::error_code error;
        const auto status = std::filesystem::symlink_status(root, error);
        if (error == std::errc::no_such_file_or_directory) error.clear();
        if (error || (std::filesystem::exists(status) && (!std::filesystem::is_directory(status) ||
            std::filesystem::directory_iterator(root, error) != std::filesystem::directory_iterator{})) || error)
        {
            if (errorMessage) *errorMessage = "Project has pending or unreadable import transactions. Reopen or refresh the project in the editor to recover first.";
            return false;
        }
        return true;
    }

    bool IsAssetInfrastructurePath(const std::filesystem::path &projectRoot, const std::filesystem::path &path)
    {
        const auto relative = path.lexically_normal().lexically_relative(projectRoot.lexically_normal());
        if (relative.empty() || relative.is_absolute() || *relative.begin() == "..") return false;
        auto text = [](const auto &component)
        {
            const auto encoded = component.generic_u8string();
            std::string value(reinterpret_cast<const char *>(encoded.data()), encoded.size());
#ifdef _WIN32
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
#endif
            return value;
        };
        const auto first = text(*relative.begin());
#ifdef _WIN32
        constexpr const char *library = "library";
        constexpr const char *build = "build";
#else
        constexpr const char *library = "Library";
        constexpr const char *build = "Build";
#endif
        if (first == library || first == build || first == ".git" || first == ".pluto-import-transactions" || first == ".pluto-import.lock" || first == ".pluto-migration-backups") return true;
        for (const auto &component : relative)
            if (text(component).starts_with(".pluto-metadata-")) return true;
        return false;
    }
}
