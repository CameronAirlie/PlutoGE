#pragma once

#include "PlutoGE/import/MeshImportOptions.h"

#include "PlutoGE/ui/panels/Panel.h"
#include "PlutoGE/ui/contentbrowser/ContentCommands.h"
#include "PlutoGE/ui/contentbrowser/ContentFileOperations.h"
#include "PlutoGE/ui/contentbrowser/ContentNavigationHistory.h"
#include "PlutoGE/ui/contentbrowser/ContentSelection.h"
#include "PlutoGE/ui/contentbrowser/ContentSorting.h"
#include "PlutoGE/assets/ModelAsset.h"
#include "PlutoGE/assets/Project.h"
#include "PlutoGE/assets/SurfaceResponseAsset.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace PlutoGE::assets
{
    class AssetCatalog;
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
    std::uint64_t GetCachedMaterialPreview(core::Engine &engine, const std::string &cacheKey,
                                           const render::MaterialConfig &config, std::uint64_t revision);
    void ClearCachedMaterialPreviews();
    class AssetThumbnailCache;
    class AssetReferenceSearchPanel;
    class ModelImportDependenciesInspector;
    class ModelHierarchyInspector;
    class StaticModelHierarchyDialog;
    inline constexpr const char *kContentBrowserAssetDragDropPayload = "PLUTOGE_CONTENT_BROWSER_ASSET";
    // Asset-relative folder path (UTF-8, '/'-separated) of a dragged folder.
    inline constexpr const char *kContentBrowserFolderDragDropPayload = "PLUTOGE_CONTENT_BROWSER_FOLDER";
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
        void OnProjectChanged() override;
        std::string RevealAsset(std::string reference);

    private:
        // One row of the explorer view: a folder or an asset in the current
        // folder (or, while searching, anywhere beneath it).
        struct ContentItem
        {
            bool folder = false;
            std::string key;    // Selection key: an asset reference or a folder key.
            std::string folderPath; // Asset-relative folder for folders; the containing folder for assets.
            std::string name;
            std::string typeName;
            int assetIndex = -1; // Index into the manifest for assets.
            assets::ProjectAssetType type{};
            std::uint64_t size = 0;
            std::int64_t modified = 0;
            bool editable = false;
        };

        // What a command acts on: explicit items (usually the selection) and
        // the folder that receives new or pasted items.
        struct CommandScope
        {
            std::vector<std::string> keys;
            std::string folder;
        };

        enum class ViewMode
        {
            Grid,
            List,
        };

        struct InlineRename
        {
            std::string key;
            std::array<char, 260> buffer{};
            std::string extension; // Kept for files so renames cannot change the asset type.
            bool focus = false;
        };

        // Explorer
        void RenderToolbar(assets::Project &project);
        void RenderAddMenu();
        void RefreshRegistryCache(const assets::Project &project);
        void RebuildItems(const assets::Project &project);
        void RenderFolderTree(const assets::Project &project);
        void RenderFolderTreeNode(const assets::Project &project, std::size_t folderIndex);
        void RenderItemGrid(assets::Project &project);
        void RenderItemList(assets::Project &project);
        void RenderModelObjectGrid(assets::Project &project);
        void RenderStatusBar();
        void RenderItemContextMenu(assets::Project &project, const ContentItem &item);
        void RenderFolderContextMenu(const std::string &folder);
        void RenderBackgroundContextMenu();
        bool RenderInlineRename(float width);
        void HandleItemInteraction(assets::Project &project, const ContentItem &item);
        void HandleItemDragSource(const ContentItem &item);
        // Makes the last item accept moves (or copies, with Ctrl) into `folder`.
        void AcceptDropsInto(const std::string &folder);
        void HandleShortcuts(assets::Project &project);
        void HandleArrowNavigation();
        void ProcessDroppedFiles(assets::Project &project);
        void RenderDeleteConfirmation(assets::Project &project);
        // File edits rebuild m_items, so anything triggered while items are
        // being drawn is queued and runs once the item views have finished.
        void Defer(std::function<void()> edit) { m_deferredEdits.push_back(std::move(edit)); }
        void RunDeferredEdits();

        // Navigation and selection
        void NavigateTo(const std::string &folder, bool recordHistory = true);
        void SelectKeys(const std::vector<std::string> &keys);
        void SyncDetailsSelection();
        [[nodiscard]] std::vector<std::string> ShownKeys() const;
        [[nodiscard]] const ContentItem *FindItem(const std::string &key) const;

        // Commands
        [[nodiscard]] CommandScope SelectionScope() const;
        [[nodiscard]] bool CanExecute(ContentCommand command, const CommandScope &scope) const;
        void Execute(ContentCommand command, const CommandScope &scope);
        void ExecuteNow(ContentCommand command, const CommandScope &scope);
        bool CommandMenuItem(ContentCommand command, const CommandScope &scope, const char *icon = nullptr);
        void BeginRename(const std::string &key);
        void CommitRename();
        void MoveOrCopyInto(const std::vector<std::string> &keys, const std::string &folder, bool copy);
        void ApplyReport(const ContentOperationReport &report, const std::string &summary);
        void OpenModelPackage(assets::Project &project, const ContentItem &item);

        // Keys identify folders as kFolderKeyPrefix + asset-relative path and
        // assets by their reference, so virtual engine folders work too.
        [[nodiscard]] static std::string FolderKey(const std::string &folder);
        [[nodiscard]] static bool IsFolderKey(const std::string &key);
        [[nodiscard]] static std::string FolderFromKey(const std::string &key);
        [[nodiscard]] std::filesystem::path PathForKey(const std::string &key) const;
        [[nodiscard]] std::string KeyForPath(const std::filesystem::path &path) const;
        [[nodiscard]] std::vector<std::filesystem::path> PathsForKeys(const std::vector<std::string> &keys) const;
        [[nodiscard]] bool IsKeyEditable(const std::string &key) const;

        std::unique_ptr<AssetReferenceSearchPanel> m_referenceSearch;
        std::unique_ptr<ModelImportDependenciesInspector> m_importDependenciesInspector;
        std::unique_ptr<ModelHierarchyInspector> m_modelHierarchyInspector;
        std::unique_ptr<StaticModelHierarchyDialog> m_staticHierarchyDialog;
        enum class PendingMenuAction
        {
            None,
            ImportModel,
            CreateMaterial,
            CreateSurfaceResponse,
            CreateParticleSystem,
            CreatePostProcessPreset,
            CreateRenderTexture,
            CreateShaderGraph,
            CreateAnimationGraph,
            CreateScriptableObject,
            CreateRmlDocument,
            CreateInputMapping,
        };

        std::array<char, 160> m_filterBuffer{};
        std::array<char, 96> m_newMaterialNameBuffer{};
        std::array<char, 96> m_newSurfaceNameBuffer{};
        std::string m_surfaceEditorReference;
        assets::SurfaceResponseAsset m_surfaceDraft;
        std::string m_surfaceError;
        std::array<char, 96> m_newParticleSystemNameBuffer{};
        std::array<char, 96> m_newPostProcessPresetNameBuffer{};
        std::array<char, 96> m_newRenderTextureNameBuffer{};
        int m_newRenderTextureSize[2] = {512, 512};
        std::array<char, 96> m_newShaderGraphNameBuffer{};
        std::array<char, 96> m_newAnimationGraphNameBuffer{};
        std::array<char, 96> m_newScriptableObjectNameBuffer{};
        std::array<char, 96> m_newRmlDocumentNameBuffer{};
        std::array<char, 96> m_newInputMappingNameBuffer{};
        std::string m_rmlDocumentCreateError;
        int m_newScriptableObjectClassIndex = 0;
        // Primary selected asset, shown in the details pane (-1 for none).
        int m_selectedAssetIndex = -1;
        // Current folder, relative to the asset directory.
        std::string m_selectedFolder;
        std::string m_modelExtractionReference;
        std::string m_modelExtractionStatus;
        std::string m_modelSettingsReference;
        assetimport::MeshImportOptions m_modelSettingsDraft{true, true, true};
        assetimport::MeshImportOptions m_modelSettingsCommitted{true, true, true};
        std::string m_modelSettingsError;
        bool m_modelSettingsValid = false;
        bool m_modelSettingsAwaitingImport = false;
        std::string m_openModelReference;
        std::string m_openModelName;
        std::vector<assets::ModelSubAsset> m_openModelObjects;
        PendingMenuAction m_pendingMenuAction = PendingMenuAction::None;

        // Registry cache, rebuilt when the manifest or catalog changes.
        bool m_assetCacheDirty = true;
        const assets::Project *m_cachedProject = nullptr;
        std::shared_ptr<const assets::AssetCatalog> m_cachedCatalog;
        std::vector<std::string> m_cachedAssetReferences;
        std::vector<std::string> m_cachedAssetFolders;
        std::vector<std::string> m_cachedAssetFileNames;
        std::vector<std::string> m_cachedAssetRelativePaths;
        std::vector<std::string> m_cachedFolders;
        std::vector<std::string> m_cachedFolderLabels;
        std::vector<std::vector<int>> m_cachedFolderChildIndices;
        std::vector<int> m_cachedRootFolderIndices;

        // Items shown for the current folder, filter and sort order.
        bool m_itemsDirty = true;
        std::string m_itemsFilter;
        std::string m_itemsFolder;
        std::vector<ContentItem> m_items;
        ContentSelection m_selection;
        ContentNavigationHistory m_history;
        ContentSortOrder m_sortOrder;
        ViewMode m_viewMode = ViewMode::Grid;
        int m_gridColumns = 1;
        std::string m_scrollToKey;
        bool m_revealFolderInTree = false;
        // A press on an already-selected item selects it alone on release,
        // so the press can start dragging the whole selection instead.
        std::string m_releaseSelectKey;
        std::string m_hoveredFolder; // Folder under the cursor this frame, for OS drops.
        bool m_hoveringFolderItem = false;

        std::vector<std::function<void()>> m_deferredEdits;
        // Keys rather than paths so renames and moves can remap them.
        std::vector<std::string> m_clipboardKeys;
        bool m_clipboardCut = false;
        InlineRename m_rename;
        std::string m_renameWhenShown; // Starts a rename once the item appears (after creation).
        std::vector<std::filesystem::path> m_pendingDeletePaths;
        bool m_openDeletePopup = false;

        std::unique_ptr<AssetThumbnailCache> m_thumbnailCache;
        float m_assetGridScale = 1.0f;
        float m_folderTreeWidth = 200.0f;
    };
}
