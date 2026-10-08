#pragma once
#include "PlutoGE/assets/ModelImportSettings.h"
#include "PlutoGE/import/ImportedModelHierarchy.h"
#include <span>

namespace PlutoGE::assets
{
    enum class ModelNodeIdentityStatus { Matched, Anonymous, DuplicateSiblingName, DuplicateSourceIdentifier, UnresolvedAncestor };
    struct ModelNodeIdentity
    {
        std::uint64_t localId = 0;
        std::string sourceKey;
        ModelNodeIdentityStatus status = ModelNodeIdentityStatus::Anonymous;
    };

    // Source indices only address this snapshot. Optional producer identifiers
    // must be persistent source IDs, never array indices. Otherwise unique named
    // parent paths provide conservative correspondence: rename/reparent creates
    // a new identity, and ambiguous/anonymous paths remain explicitly unresolved.
    // Node keys share the object's ID allocator and retain removed tombstones.
    // No asset catalog entries or prefab entities are created by this operation.
    // Failure preserves settings and output; arbitrary parent ordering is valid.
    bool ReconcileModelNodeIdentities(const assetimport::ImportedModelHierarchy &hierarchy,
        ModelImportSettings &settings, std::vector<ModelNodeIdentity> &identities,
        std::string *errorMessage = nullptr, std::span<const std::string> sourceIdentifiers = {});
}
