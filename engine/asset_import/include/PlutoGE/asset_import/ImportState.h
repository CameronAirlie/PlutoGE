#pragma once

#include "PlutoGE/asset_import/ArtifactCache.h"
#include "PlutoGE/assets/AssetMetadata.h"
#include "PlutoGE/assets/Project.h"

#include <string>
#include <vector>
#include <utility>
#include <stop_token>

namespace PlutoGE::assetimport
{
    struct ImportState
    {
        std::string ownerId;
        std::string sourceReference;
        content::ContentDigest generation{};
        // Digests of the accepted files, including canonically rewritten settings.
        std::vector<ArtifactInput> inputs;
    };

    bool CaptureImportState(const std::string &ownerId, const std::string &sourceReference,
                            const ArtifactManifest &generation, const std::vector<ArtifactInput> &publicationInputs, ImportState &state,
                            std::string *errorMessage = nullptr);
    bool SerializeImportState(const ImportState &state, std::string &text, std::string *errorMessage = nullptr);
    bool ParseImportState(std::string_view text, ImportState &state, std::string *errorMessage = nullptr);

    // Disposable Library state. Atomic metadata publication preserves the previous
    // document on failure. Store callers serialize with the project asset lock.
    // Store is optional after a successful import; a missing
    // or invalid state schedules reconciliation rather than losing asset identity.
    class ImportStateStore
    {
    public:
        explicit ImportStateStore(std::filesystem::path projectRoot) : m_projectRoot(std::move(projectRoot)) {}
        bool Store(const ImportState &state, std::string *errorMessage = nullptr) const;
        assets::AssetMetadataStatus Load(const std::string &ownerId, ImportState &state,
                                         std::string *errorMessage = nullptr) const;
    private:
        std::filesystem::path StatePath(const std::string &ownerId) const;
        std::filesystem::path m_projectRoot;
    };

    // Best-effort post-acceptance cache bookkeeping; never changes import success.
    void RememberAcceptedImport(const assets::Project &project, const ImportState &state) noexcept;

    enum class ImportReconciliationStatus { Current, NeedsImport, Blocked };
    struct ImportAssessment
    {
        std::string sourceReference;
        ImportReconciliationStatus status = ImportReconciliationStatus::NeedsImport;
        std::string reason;
        // Automatic import is restricted to new sources or verified packages.
        bool automaticImportSafe = false;
    };

    // Read-only authoritative content checks; no parser, sidecar creation, or
    // catalog publication. File watching can schedule this after debouncing.
    bool ReconcileModelImports(const assets::Project &project, std::vector<ImportAssessment> &assessments,
                               std::string *errorMessage = nullptr, std::stop_token stop = {});
    // Restrict expensive checks to known input owners. Any unindexed event
    // (including generated outputs/cache deletion) falls back to a full audit.
    bool ReconcileChangedModelImports(const assets::Project &project, const std::vector<std::filesystem::path> &changedPaths,
                                      std::vector<ImportAssessment> &assessments, std::string *errorMessage = nullptr,
                                      std::stop_token stop = {});
}
