#include "PlutoGE/assets/AssetStorageMap.h"
#include "PlutoGE/assets/Project.h"
#include <algorithm>

namespace PlutoGE::assets
{
    bool AssetStorageMap::Replace(std::vector<ImportedAssetStorage> entries, std::string *errorMessage)
    {
        std::sort(entries.begin(), entries.end(), [](const auto &a, const auto &b) { return a.reference < b.reference; });
        std::unordered_map<std::string, std::size_t> byReference;
        for (std::size_t index = 0; index < entries.size(); ++index)
        {
            const auto &entry = entries[index];
            const auto relative = Project::IsProjectAssetReference(entry.reference)
                ? std::filesystem::path(entry.reference.substr(Project::kProjectAssetScheme.size())) : std::filesystem::path{};
            const bool valid = !relative.empty() && !relative.has_root_path() && entry.path.is_absolute() &&
                entry.reference.find_first_of("\r\n") == std::string::npos && entry.reference.find('\0') == std::string::npos &&
                std::none_of(relative.begin(), relative.end(), [](const auto &part) { return part == ".." || part == "." || part.empty(); });
            if (!valid || !byReference.emplace(entry.reference, index).second)
            {
                if (errorMessage) *errorMessage = "Invalid or duplicate imported storage location: " + entry.reference;
                return false;
            }
        }
        m_entries = std::move(entries);
        m_byReference = std::move(byReference);
        if (errorMessage) errorMessage->clear();
        return true;
    }

    const ImportedAssetStorage *AssetStorageMap::Find(std::string_view reference) const
    {
        const auto found = m_byReference.find(std::string(reference));
        return found == m_byReference.end() ? nullptr : &m_entries[found->second];
    }
    std::optional<std::string> AssetStorageMap::FindReferenceByPath(const std::filesystem::path &path) const
    {
        auto key = [](const auto &value)
        {
            auto text = value.lexically_normal().generic_string();
#ifdef _WIN32
            for (auto &character : text) if (character >= 'A' && character <= 'Z') character += 'a' - 'A';
#endif
            return text;
        };
        const auto target = key(path);
        std::optional<std::string> reference;
        for (const auto &entry : m_entries)
            if (key(entry.path) == target)
            {
                if (reference) return std::nullopt;
                reference = entry.reference;
            }
        return reference;
    }

}
