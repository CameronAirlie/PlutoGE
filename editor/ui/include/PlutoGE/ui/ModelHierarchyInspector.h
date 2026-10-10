#pragma once
#include <functional>
#include <memory>
#include "PlutoGE/asset_import/ModelNodeRepairService.h"
#include <string>

namespace PlutoGE::assets { class Project; class AssetCatalog; struct ModelAsset; }
namespace PlutoGE::scene { class Scene; }
namespace PlutoGE::ui
{
    // Owns a read-only verified snapshot, independent of scene selection/history.
    class ModelHierarchyInspector
    {
    public:
        ModelHierarchyInspector();
        ~ModelHierarchyInspector();
        void Render(const assets::Project &project, const std::string &reference, const assets::ModelAsset &package,
            std::shared_ptr<const assets::AssetCatalog> catalog, bool importRunning,
            const scene::Scene *scene = nullptr,
            const std::function<bool(const assetimport::ModelNodeRepairProposal &, std::string *)> &applyRepair = {});
    private:
        struct State;
        std::unique_ptr<State> m_state;
    };
}
