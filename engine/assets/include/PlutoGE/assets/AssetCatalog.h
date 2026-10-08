#pragma once

#include "PlutoGE/assets/AssetReference.h"
#include "PlutoGE/assets/AssetOwnership.h"
#include "PlutoGE/assets/AssetType.h"

#include <cstddef>
#include <string>
#include <optional>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace PlutoGE::assets
{
    struct AssetObjectDescriptor
    {
        AssetReference identity;
        ProjectAssetType type = ProjectAssetType::Unknown;
        AssetOwnership ownership = AssetOwnership::Unclassified;
        std::string name;
        // Editor project:// path today; runtime packed location in a runtime
        // catalog. Never serialize this location as the object's identity.
        std::string location;
    };

    // Storage-neutral object index. Construct a replacement off-thread, then
    // share a const snapshot with consumers. It performs no file or GPU IO.
    class AssetCatalog
    {
    public:
        bool Replace(std::vector<AssetObjectDescriptor> objects, std::string *errorMessage = nullptr);
        const AssetObjectDescriptor *Find(const AssetReference &reference) const;
        // Prefer a unique sub-object. Shared sub-object locations fall back to
        // their unique standalone main identity; unresolved ambiguity returns null.
        std::optional<AssetReference> FindIdentityByLocation(std::string_view location) const;
        const std::vector<AssetObjectDescriptor> &GetObjects() const noexcept { return m_objects; }

    private:
        std::vector<AssetObjectDescriptor> m_objects;
        std::unordered_map<std::string, AssetReference> m_byLocation;
        std::unordered_map<std::string, std::unordered_map<std::uint64_t, std::size_t>> m_byOwner;
    };
}
