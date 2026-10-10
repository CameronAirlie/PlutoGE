#pragma once
#include "PlutoGE/scene/ModelInstanceReconciliation.h"

namespace PlutoGE::assets { class ProjectAssetLock; }
namespace PlutoGE::ui
{
    struct StaticModelSourceDiagnostic
    {
        std::string sourceAssetId;
        std::vector<std::uint32_t> roots;
        std::string message;
    };
    struct PreparedModelInstanceRefresh
    {
        PreparedModelInstanceRefresh();
        ~PreparedModelInstanceRefresh();
        PreparedModelInstanceRefresh(PreparedModelInstanceRefresh &&) noexcept;
        PreparedModelInstanceRefresh &operator=(PreparedModelInstanceRefresh &&) noexcept;
        // Held until the caller publishes or discards this preparation.
        std::unique_ptr<assets::ProjectAssetLock> publicationLock;
        std::unique_ptr<scene::Scene> scene;
        std::vector<std::uint32_t> updatedRoots;
        std::vector<scene::StaticModelSceneConflict> conflicts;
        std::vector<StaticModelSourceDiagnostic> diagnostics;
    };
    // Owner-thread preparation against authoritative active source metadata.
    // Invalid/unavailable sources retain their accepted instances and report a
    // diagnostic; independent valid sources may still update. No authored writes.
    // Lock acquisition failure leaves caller output unchanged and is retryable.
    bool PrepareModelInstanceRefresh(const scene::Scene &source, const assets::Project &project,
        assets::AssetManager &sharedAssets, PreparedModelInstanceRefresh &output,
        std::string *errorMessage = nullptr);
}
