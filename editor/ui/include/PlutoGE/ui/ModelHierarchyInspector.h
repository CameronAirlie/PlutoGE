#pragma once
#include <memory>
#include <string>

namespace PlutoGE::assets { class Project; class AssetCatalog; struct ModelAsset; }
namespace PlutoGE::ui
{
    // Owns a read-only verified snapshot, independent of scene selection/history.
    class ModelHierarchyInspector
    {
    public:
        ModelHierarchyInspector();
        ~ModelHierarchyInspector();
        void Render(const assets::Project &project, const std::string &reference, const assets::ModelAsset &package,
            std::shared_ptr<const assets::AssetCatalog> catalog, bool importRunning);
    private:
        struct State;
        std::unique_ptr<State> m_state;
    };
}
