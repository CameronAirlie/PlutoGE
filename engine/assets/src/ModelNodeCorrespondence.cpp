#include "PlutoGE/assets/ModelNodeCorrespondence.h"
#include "PlutoGE/platform/ContentDigest.h"
#include <limits>
#include <map>
#include <stdexcept>
#include <unordered_set>

namespace PlutoGE::assets
{
    namespace
    {
        constexpr std::string_view kNodePrefix = "node/";
        std::string Key(std::string_view kind, std::string_view parent, std::string_view name)
        {
            content::ContentHasher hash;
            for (const auto value : {parent, name})
            {
                const auto length = std::to_string(value.size()) + ":";
                hash.Update(std::as_bytes(std::span(length.data(), length.size())));
                hash.Update(std::as_bytes(std::span(value.data(), value.size())));
            }
            return std::string(kNodePrefix) + std::string(kind) + "/v1/" + content::DigestToHex(hash.Finalize());
        }
    }

    bool ReconcileModelNodeIdentities(const assetimport::ImportedModelHierarchy &hierarchy,
        ModelImportSettings &settings, std::vector<ModelNodeIdentity> &identities,
        std::string *errorMessage, std::span<const std::string> sourceIdentifiers)
    {
        try
        {
            const auto &nodes = hierarchy.nodes;
            if (nodes.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
                (!sourceIdentifiers.empty() && sourceIdentifiers.size() != nodes.size()))
                throw std::runtime_error("Invalid model node correspondence inventory.");
            std::map<std::pair<int, std::string>, std::size_t> siblingCounts;
            std::map<std::string, std::size_t> sourceCounts;
            for (std::size_t index = 0; index < nodes.size(); ++index)
            {
                const auto &node = nodes[index];
                if (node.parentNodeIndex < -1 || node.parentNodeIndex >= static_cast<int>(nodes.size()))
                    throw std::runtime_error("Invalid model node correspondence parent.");
                ++siblingCounts[{node.parentNodeIndex, node.name}];
                if (!sourceIdentifiers.empty() && !sourceIdentifiers[index].empty()) ++sourceCounts[sourceIdentifiers[index]];
            }
            std::vector<ModelNodeIdentity> candidate(nodes.size());
            std::vector<unsigned char> state(nodes.size());
            std::vector<int> chain;
            for (int start = 0; start < static_cast<int>(nodes.size()); ++start)
            {
                if (state[start] == 2) continue;
                chain.clear();
                int current = start;
                while (current >= 0 && state[current] != 2)
                {
                    if (state[current] == 1) throw std::runtime_error("Model node correspondence contains a parent cycle.");
                    state[current] = 1;
                    chain.push_back(current);
                    current = nodes[current].parentNodeIndex;
                }
                while (!chain.empty())
                {
                    const int index = chain.back();
                    chain.pop_back();
                    const auto &node = nodes[index];
                    auto &identity = candidate[index];
                    const auto source = sourceIdentifiers.empty() ? std::string_view{} : std::string_view(sourceIdentifiers[index]);
                    if (!source.empty())
                    {
                        if (sourceCounts.at(std::string(source)) != 1) identity.status = ModelNodeIdentityStatus::DuplicateSourceIdentifier;
                        else identity.sourceKey = Key("source", {}, source);
                    }
                    else if (node.parentNodeIndex >= 0 && candidate[node.parentNodeIndex].sourceKey.empty())
                        identity.status = ModelNodeIdentityStatus::UnresolvedAncestor;
                    else if (node.name.empty()) identity.status = ModelNodeIdentityStatus::Anonymous;
                    else if (siblingCounts.at({node.parentNodeIndex, node.name}) != 1)
                        identity.status = ModelNodeIdentityStatus::DuplicateSiblingName;
                    else identity.sourceKey = Key("path", node.parentNodeIndex < 0 ? std::string_view{} :
                        std::string_view(candidate[node.parentNodeIndex].sourceKey), node.name);
                    if (!identity.sourceKey.empty()) identity.status = ModelNodeIdentityStatus::Matched;
                    state[index] = 2;
                }
            }
            auto updated = settings;
            std::unordered_set<std::uint64_t> ids;
            std::unordered_set<std::string> keys;
            for (auto &object : updated.objects)
            {
                if (!object.localId || object.sourceKey.empty() || object.sourceKey.find_first_of("\t\r\n\0", 0, 4) != std::string::npos || !ids.insert(object.localId).second || !keys.insert(object.sourceKey).second)
                    throw std::runtime_error("Invalid existing model node/object correspondence.");
                if (object.sourceKey.starts_with(kNodePrefix)) object.retired = true;
            }
            for (auto &identity : candidate)
                if (!identity.sourceKey.empty())
                {
                    identity.localId = ResolveModelObjectId(updated, identity.sourceKey, 0, errorMessage);
                    if (!identity.localId) return false;
                }
            settings = std::move(updated);
            identities = std::move(candidate);
            if (errorMessage) errorMessage->clear();
            return true;
        }
        catch (const std::exception &error)
        {
            if (errorMessage) *errorMessage = error.what();
            return false;
        }
    }
}
