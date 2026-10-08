#include "PlutoGE/assets/AssetCatalog.h"

#include <algorithm>
#include <utility>

namespace PlutoGE::assets
{
    bool AssetCatalog::Replace(std::vector<AssetObjectDescriptor> objects, std::string *errorMessage)
    {
        decltype(m_byOwner) byOwner;
        decltype(m_byLocation) byLocation;
        std::unordered_map<std::string, std::vector<std::size_t>> locations;
        std::sort(objects.begin(), objects.end(), [](const auto &a, const auto &b)
        {
            if (a.identity.assetId != b.identity.assetId) return a.identity.assetId < b.identity.assetId;
            return a.identity.localObjectId < b.identity.localObjectId;
        });
        for (std::size_t index = 0; index < objects.size(); ++index)
        {
            const auto &object = objects[index];
            std::string encoded;
            if (object.identity.IsEmpty() || !SerializeAssetReference(object.identity, encoded) || object.location.empty())
            {
                if (errorMessage) *errorMessage = "Catalog objects require a valid identity and location.";
                return false;
            }
            locations[object.location].push_back(index);
            if (!byOwner[object.identity.assetId].emplace(object.identity.localObjectId, index).second)
            {
                if (errorMessage) *errorMessage = "Duplicate catalog object: " + encoded;
                return false;
            }
        }
        for (const auto &[location, indices] : locations)
        {
            const AssetReference *main = nullptr;
            const AssetReference *subObject = nullptr;
            bool ambiguousMain = false;
            bool ambiguousSubObject = false;
            for (const auto index : indices)
            {
                const auto &identity = objects[index].identity;
                auto &selected = identity.localObjectId == 0 ? main : subObject;
                auto &ambiguous = identity.localObjectId == 0 ? ambiguousMain : ambiguousSubObject;
                if (selected) ambiguous = true;
                else selected = &identity;
            }
            if (subObject && !ambiguousSubObject) byLocation.emplace(location, *subObject);
            else if (main && !ambiguousMain) byLocation.emplace(location, *main);
        }
        m_byLocation.swap(byLocation);
        m_objects.swap(objects);
        m_byOwner.swap(byOwner);
        if (errorMessage) errorMessage->clear();
        return true;
    }

    std::optional<AssetReference> AssetCatalog::FindIdentityByLocation(std::string_view location) const
    {
        const auto found = m_byLocation.find(std::string(location));
        return found == m_byLocation.end() ? std::nullopt : std::optional<AssetReference>{found->second};
    }

    const AssetObjectDescriptor *AssetCatalog::Find(const AssetReference &reference) const
    {
        const auto owner = m_byOwner.find(reference.assetId);
        if (owner == m_byOwner.end()) return nullptr;
        const auto object = owner->second.find(reference.localObjectId);
        return object == owner->second.end() ? nullptr : &m_objects[object->second];
    }
}
