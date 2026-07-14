#pragma once

#include "PlutoGE/assets/Project.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace PlutoGE::assets
{
    struct AssetRecord
    {
        std::string id;
        std::string reference;
        ProjectAssetType type = ProjectAssetType::Unknown;
        std::uintmax_t size = 0;
        std::uint64_t contentHash = 0;
        std::uint32_t importerVersion = 1;
        std::vector<std::string> dependencies;
    };

    class AssetDatabase
    {
    public:
        bool Scan(Project &project, std::string *errorMessage = nullptr);
        const AssetRecord *FindById(std::string_view id) const;
        const AssetRecord *FindByReference(std::string_view reference) const;
        std::string ResolveId(std::string_view id) const;
        const std::vector<AssetRecord> &GetRecords() const { return m_records; }

        static std::filesystem::path GetMetadataPath(const std::filesystem::path &assetPath);
        static std::uint64_t HashFile(const std::filesystem::path &path);

    private:
        std::vector<AssetRecord> m_records;
        std::unordered_map<std::string, std::size_t> m_byId;
        std::unordered_map<std::string, std::size_t> m_byReference;
    };

    struct CookOptions
    {
        bool includeSourceAssets = false;
        bool includeUnreferencedAssets = true;
    };

    bool CookProjectContent(Project &project,
                            const std::filesystem::path &destinationAssetDirectory,
                            const CookOptions &options = {},
                            std::string *errorMessage = nullptr);
}
