#pragma once

#include "PlutoGE/ui/panels/Panel.h"
#include "PlutoGE/assets/ModelAsset.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace PlutoGE::assets
{
    class Project;
}

namespace PlutoGE::core { class Engine; }
namespace PlutoGE::render { struct MaterialConfig; }

namespace PlutoGE::scene
{
    class Entity;
}

namespace PlutoGE::ui
{
    // Renders (and caches) the same lit material sphere used by content-browser thumbnails.
    // `revision` must change whenever any preview input changes.
    unsigned int GetCachedMaterialPreview(core::Engine &engine,
                                          const std::string &cacheKey,
                                          const render::MaterialConfig &config,
                                          std::uint64_t revision);
    class AssetThumbnailCache;
    inline constexpr const char *kContentBrowserAssetDragDropPayload = "PLUTOGE_CONTENT_BROWSER_ASSET";
    bool InstantiateMeshAssetIntoScene(std::string reference,
                                       scene::Entity *parent = nullptr,
                                       std::string materialBindingReference = {});
    bool InstantiateModelAssetIntoScene(const std::string &reference, scene::Entity *parent = nullptr);

    class ContentBrowserPanel : public Panel
    {
    public:
        ContentBrowserPanel(const PanelConfig &config);
        ~ContentBrowserPanel() override;

        void Render() override;

    private:
        enum class PendingMenuAction
        {
            None,
            ImportModel,
            CreateMaterial,
            CreateParticleSystem,
            CreatePostProcessPreset,
            CreateShaderGraph,
            CreateAnimationGraph,
            CreateScriptableObject,
            CreateRmlDocument,
            CreateInputMapping,
        };

        std::array<char, 160> m_filterBuffer{};
        std::array<char, 96> m_newMaterialNameBuffer{};
        std::array<char, 96> m_newParticleSystemNameBuffer{};
        std::array<char, 96> m_newPostProcessPresetNameBuffer{};
        std::array<char, 96> m_newShaderGraphNameBuffer{};
        std::array<char, 96> m_newAnimationGraphNameBuffer{};
        std::array<char, 96> m_newScriptableObjectNameBuffer{};
        std::array<char, 96> m_newRmlDocumentNameBuffer{};
        std::array<char, 96> m_newInputMappingNameBuffer{};
        std::string m_rmlDocumentCreateError;
        int m_newScriptableObjectClassIndex = 0;
        int m_selectedAssetIndex = -1;
        std::string m_selectedFolder;
        std::string m_openModelReference;
        std::string m_openModelName;
        std::vector<assets::ModelSubAsset> m_openModelObjects;
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
        std::unique_ptr<AssetThumbnailCache> m_thumbnailCache;
        float m_thumbnailSize = 96.0f;
    };
}
