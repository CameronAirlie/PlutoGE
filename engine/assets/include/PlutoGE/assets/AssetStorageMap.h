#pragma once

#include "PlutoGE/platform/ContentDigest.h"
#include <filesystem>
#include <optional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace PlutoGE::assets
{
    class ArtifactGenerationLock;
    struct ImportedAssetStorage
    {
        std::string reference;
        std::filesystem::path path;
        content::ContentDigest digest{};
        // Database recovery scans retain identities while disabling unsafe reads.
        bool available = true;
        // Retained by immutable snapshots and resources borrowing these bytes.
        std::shared_ptr<const ArtifactGenerationLock> generationLease{};
    };

    // Immutable snapshot once shared with consumers. Keeps project-relative
    // virtual locations independent of editor Library files and runtime packs.
    // Replace validates syntax only; AssetDatabase verifies ownership, Library
    // containment, ordinary files, and generation digests before publication.
    class AssetStorageMap
    {
    public:
        bool Replace(std::vector<ImportedAssetStorage> entries, std::string *errorMessage = nullptr);
        const ImportedAssetStorage *Find(std::string_view reference) const;
        // IO-free reverse lookup; shared physical locations require an explicit identity.
        std::optional<std::string> FindReferenceByPath(const std::filesystem::path &path) const;
        const std::vector<ImportedAssetStorage> &GetEntries() const noexcept { return m_entries; }
    private:
        std::vector<ImportedAssetStorage> m_entries;
        std::unordered_map<std::string, std::size_t> m_byReference;
    };
}
