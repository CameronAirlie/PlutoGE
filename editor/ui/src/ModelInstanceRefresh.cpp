#include "PlutoGE/ui/ModelInstanceRefresh.h"
#include "PlutoGE/assets/ModelActiveGeneration.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/assets/ProjectAssetLock.h"
#include "PlutoGE/assets/AssetPathPolicy.h"
#include "PlutoGE/scene/Scene.h"
#include <map>
#include <algorithm>

namespace PlutoGE::ui
{
    PreparedModelInstanceRefresh::PreparedModelInstanceRefresh() = default;
    PreparedModelInstanceRefresh::~PreparedModelInstanceRefresh() = default;
    PreparedModelInstanceRefresh::PreparedModelInstanceRefresh(PreparedModelInstanceRefresh &&) noexcept = default;
    PreparedModelInstanceRefresh &PreparedModelInstanceRefresh::operator=(PreparedModelInstanceRefresh &&) noexcept = default;

    bool PrepareModelInstanceRefresh(const scene::Scene &source, const assets::Project &project,
        assets::AssetManager &sharedAssets, PreparedModelInstanceRefresh &output, std::string *error)
    {
        PreparedModelInstanceRefresh candidate;
        candidate.publicationLock = std::make_unique<assets::ProjectAssetLock>();
        if (!candidate.publicationLock->TryAcquire(project.GetRootDirectory(), error) ||
            !assets::ValidateNoPendingAssetTransactions(project.GetRootDirectory(), error)) return false;
        std::map<std::string, std::vector<std::uint32_t>> owners;
        for (const auto &[root, instance] : source.GetStaticModelInstances())
            owners[instance.state.accepted.layout.sourceAssetId].push_back(root);
        const scene::Scene *current = &source;
        for (auto &[owner, roots] : owners)
        {
            std::sort(roots.begin(), roots.end());
            assets::ModelGenerationSnapshot artifacts;
            assets::StaticModelGenerationSnapshot incoming;
            std::string diagnostic;
            if (!assets::ReadActiveModelGenerationSnapshot(project, owner,
                    sharedAssets.GetAssetCatalog(), sharedAssets.GetAssetStorageMap(), artifacts, &diagnostic) ||
                !assets::PrepareStaticModelGenerationSnapshot(project, artifacts, incoming, &diagnostic))
            {
                candidate.diagnostics.push_back({owner, roots, std::move(diagnostic)});
                continue;
            }
            scene::PreparedStaticModelSceneReconciliation prepared;
            if (!scene::PrepareStaticModelSceneReconciliation(*current, project, incoming, sharedAssets, prepared, &diagnostic))
            {
                candidate.diagnostics.push_back({owner, roots, std::move(diagnostic)});
                continue;
            }
            candidate.updatedRoots.insert(candidate.updatedRoots.end(), prepared.updatedRoots.begin(), prepared.updatedRoots.end());
            candidate.conflicts.insert(candidate.conflicts.end(), prepared.conflicts.begin(), prepared.conflicts.end());
            if (prepared.scene)
            {
                candidate.scene = std::move(prepared.scene);
                current = candidate.scene.get();
            }
        }
        output = std::move(candidate);
        if (error) error->clear();
        return true;
    }
}
