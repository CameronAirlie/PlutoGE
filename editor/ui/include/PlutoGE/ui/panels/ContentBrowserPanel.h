#pragma once

#include "PlutoGE/ui/panels/Panel.h"

#include <array>
#include <string>
#include <vector>

namespace PlutoGE::assets
{
    class Project;
}

namespace PlutoGE::scene
{
    class Entity;
}

namespace PlutoGE::ui
{
    inline constexpr const char *kContentBrowserAssetDragDropPayload = "PLUTOGE_CONTENT_BROWSER_ASSET";
    inline constexpr const char *kContentBrowserMeshSubassetDragDropPayload = "PLUTOGE_MESH_SUBASSET";

    struct ContentBrowserMeshSubassetPayload
    {
        char sourceReference[512]{};
        int submeshIndex = -1;
        int submeshCount = 1;
        int materialSlot = -1;
    };

    bool InstantiateMeshAssetIntoScene(std::string reference, scene::Entity *parent = nullptr, int submeshIndex = -1, int submeshCount = 1, int materialSlot = -1);

    class ContentBrowserPanel : public Panel
    {
    public:
        ContentBrowserPanel(const PanelConfig &config) : Panel(config) {}
        ~ContentBrowserPanel() override = default;

        void Render() override;

    private:
        enum class PendingMenuAction
        {
            None,
            ImportModel,
            CreateMaterial,
            CreateParticleSystem,
            CreateShaderGraph,
            CreateAnimationGraph,
        };

        std::array<char, 160> m_filterBuffer{};
        std::array<char, 96> m_newMaterialNameBuffer{};
        std::array<char, 96> m_newParticleSystemNameBuffer{};
        std::array<char, 96> m_newShaderGraphNameBuffer{};
        std::array<char, 96> m_newAnimationGraphNameBuffer{};
        int m_selectedAssetIndex = -1;
        std::string m_selectedFolder;
        bool m_assetCacheDirty = true;
        const assets::Project *m_cachedProject = nullptr;
        std::string m_cachedFilter;
        std::string m_cachedFolder;
        std::vector<std::string> m_cachedAssetReferences;
        std::vector<std::string> m_cachedAssetFolders;
        std::vector<std::string> m_cachedAssetFileNames;
        std::vector<std::string> m_cachedAssetRelativePaths;
        std::vector<std::string> m_cachedFolders;
        std::vector<std::string> m_cachedFolderParents;
        std::vector<std::string> m_cachedChildFolders;
        std::vector<std::string> m_cachedChildFolderLabels;
        std::vector<std::string> m_cachedChildFolderDisplayNames;
        std::vector<std::string> m_cachedFolderLabels;
        std::vector<bool> m_cachedFolderHasChildren;
        std::vector<std::vector<int>> m_cachedFolderChildIndices;
        std::vector<int> m_cachedRootFolderIndices;
        std::vector<int> m_filteredAssetIndices;
        std::vector<std::string> m_filteredAssetDisplayNames;
        PendingMenuAction m_pendingMenuAction = PendingMenuAction::None;
    };
}
