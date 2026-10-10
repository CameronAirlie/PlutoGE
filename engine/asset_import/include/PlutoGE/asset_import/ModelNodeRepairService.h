#pragma once
#include "PlutoGE/platform/ContentDigest.h"
#include <cstddef>
#include <cstdint>
#include <string>

namespace PlutoGE::assets { class Project; }
namespace PlutoGE::assetimport
{
    struct ModelNodeRepairProposal
    {
        std::string sourceReference;
        std::uint64_t incomingNodeId = 0, retiredNodeId = 0;
        content::ContentDigest metadataDigest{}, generation{}, hierarchyDigest{}, settingsDigest{};
        std::size_t changedNodeCount = 0;
    };

    // Explicit correspondence settings publication, independent of scene state.
    // Prepare is read-only. Apply re-prepares under the writer lock, rejects stale
    // or altered review evidence and commits only the source metadata through a
    // recoverable transaction. The caller queues reimport after Apply returns;
    // importing while holding this service's lock would deadlock. Failed import
    // keeps the authored repair and the scene's accepted generation intact.
    class ModelNodeRepairService
    {
    public:
        bool Prepare(const assets::Project &project, const std::string &sourceReference,
            std::uint64_t incomingNodeId, std::uint64_t retiredNodeId,
            ModelNodeRepairProposal &proposal, std::string *errorMessage = nullptr) const;
        bool Apply(const assets::Project &project, const ModelNodeRepairProposal &proposal,
            std::string *errorMessage = nullptr) const;
    };
}
