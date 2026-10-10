#pragma once
#include <memory>
#include "PlutoGE/assets/ModelGenerationSnapshot.h"

namespace PlutoGE::assets { class AssetManager; }

namespace PlutoGE::scene
{
    class Scene;
    struct StaticModelSceneConflict
    {
        std::uint32_t rootEntityId = 0;
        content::ContentDigest acceptedGeneration{}, incomingGeneration{};
        std::vector<assets::StaticModelInstanceConflict> reasons;
    };
    struct PreparedStaticModelSceneReconciliation
    {
        PreparedStaticModelSceneReconciliation();
        ~PreparedStaticModelSceneReconciliation();
        PreparedStaticModelSceneReconciliation(PreparedStaticModelSceneReconciliation &&) noexcept;
        PreparedStaticModelSceneReconciliation &operator=(PreparedStaticModelSceneReconciliation &&) noexcept;
        // Null when nothing can change. The caller publishes this edit-mode
        // snapshot after checking its revision. Imported defaults are derived;
        // authored edit history must reconcile restored snapshots separately.
        std::unique_ptr<Scene> scene;
        std::vector<std::uint32_t> updatedRoots;
        std::vector<StaticModelSceneConflict> conflicts;
    };
    // Resource-owning thread, authored edit scenes only. Captures overrides,
    // verifies incoming bytes and builds an isolated replacement. Conflicted
    // instances retain their entire accepted tree/generation. Source and caller
    // output remain unchanged on failure; no authored files, history or live scene change.
    bool PrepareStaticModelSceneReconciliation(const Scene &source, const assets::Project &project,
        const assets::StaticModelGenerationSnapshot &incoming, assets::AssetManager &sharedAssets,
        PreparedStaticModelSceneReconciliation &output, std::string *errorMessage = nullptr);
}
