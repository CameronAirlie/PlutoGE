#include "PlutoGE/ui/TimelinePreview.h"
#pragma once

#include "PlutoGE/ui/EntitySelection.h"
#include "PlutoGE/ui/AuthoringRegistry.h"
#include "PlutoGE/ui/ScriptSourceWatch.h"
#include "PlutoGE/scripting/ScriptEngine.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"
#include "PlutoGE/scene/SceneBaker.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/ui/EditorProfiler.h"
#include "PlutoGE/ui/PanelManager.h"
#include "PlutoGE/ui/PlayModeChanges.h"
#include "PlutoGE/ui/ViewportBookmarks.h"
#include "PlutoGE/ui/GroundPlacement.h"
#include "PlutoGE/ui/SceneHistory.h"
#include "PlutoGE/ui/SceneRecovery.h"
#include "PlutoGE/ui/ModelInstanceRefresh.h"
#include "PlutoGE/assets/ProjectValidation.h"
#include "PlutoGE/import/MeshImportOptions.h"
#include "PlutoGE/asset_import/ModelNodeRepairService.h"
#include "PlutoGE/asset_import/ImportState.h"
#include "PlutoGE/asset_import/ImportWatch.h"
#include <chrono>

#include <algorithm>
#include <array>
#include <filesystem>
#include <functional>
#include <future>
#include <deque>
#include <unordered_map>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace PlutoGE::assets
{
    class Project;
}

namespace PlutoGE::assetimport { class ModelImportTask; struct ModelImportResult; }

namespace PlutoGE::scene
{
    class IblCaptureComponent;
    class RenderTextureViewBuilder;
}

namespace PlutoGE::ui
{
    class RmlDocumentEditorPanel;
    class ProfilerPanel;
    class ViewportPanel;
    class EditorSceneRenderService;

    class EditorShell
    {
    public:
        AuthoringRegistry &GetAuthoringRegistry() { return m_authoring; }
        void RenderExtensionInspectors();
        void RenderScriptBuildDiagnostics();
        void UpdateAuthoring() { PollModelImport(); PollScriptSources(); }
        bool IsScriptBuildRunning() const { return m_scriptBuildFuture.valid(); }
        enum class ConsoleSeverity
        {
            Info,
            Warning,
            Error,
        };

        struct ConsoleMessage
        {
            ConsoleSeverity severity = ConsoleSeverity::Info;
            std::string text;
        };

        struct EditorViewportCamera
        {
            render::Camera camera{render::CameraConfig{
                .fovY = 45.0f,
                .nearPlane = 0.1f,
                .farPlane = 100.0f,
            }};
            glm::vec3 position{0.0f, 2.0f, 6.0f};
            float moveSpeed = 6.0f;
            float speedAdjustment = 1.0f;
            float yawDegrees = 0.0f;
            float pitchDegrees = 0.0f;
            bool orthographic = false;
            bool renderRuntimeUI = false;
            float orthographicSize = 10.0f;
            glm::vec3 perspectivePosition{0.0f, 2.0f, 6.0f};
            bool hasPerspectivePosition = false;
            std::vector<std::unique_ptr<render::IPostProcessEffect>> postProcessEffects;
            std::string postProcessPresetAssetReference;

            bool SetPostProcessPresetAssetReference(std::string assetReference);

            void AddPostProcessEffect(std::unique_ptr<render::IPostProcessEffect> effect)
            {
                if (!effect)
                {
                    return;
                }

                postProcessEffects.push_back(std::move(effect));
            }

            bool AddPostProcessEffectByType(std::string_view typeName)
            {
                auto effect = render::CreatePostProcessEffect(typeName);
                if (!effect)
                {
                    return false;
                }

                AddPostProcessEffect(std::move(effect));
                return true;
            }

            bool RemovePostProcessEffect(size_t index)
            {
                if (index >= postProcessEffects.size())
                {
                    return false;
                }

                auto &engine = core::Engine::GetInstance();
                auto &window = core::Engine::GetInstance().GetWindow();
                if (engine.GetConfig().graphicsApi == render::rhi::GraphicsApi::OpenGL &&
                    window.IsOpen() && !window.EnsureOpenGLContextCurrent(true))
                {
                    return false;
                }

                postProcessEffects.erase(postProcessEffects.begin() + static_cast<std::ptrdiff_t>(index));
                return true;
            }

            bool MovePostProcessEffect(size_t fromIndex, size_t toIndex)
            {
                if (fromIndex >= postProcessEffects.size() || toIndex >= postProcessEffects.size() || fromIndex == toIndex)
                {
                    return false;
                }

                auto effect = std::move(postProcessEffects[fromIndex]);
                postProcessEffects.erase(postProcessEffects.begin() + static_cast<std::ptrdiff_t>(fromIndex));
                postProcessEffects.insert(postProcessEffects.begin() + static_cast<std::ptrdiff_t>(toIndex), std::move(effect));
                return true;
            }

            render::IPostProcessEffect *GetPostProcessEffect(size_t index)
            {
                if (index >= postProcessEffects.size())
                {
                    return nullptr;
                }

                return postProcessEffects[index].get();
            }

            const std::vector<std::unique_ptr<render::IPostProcessEffect>> &GetPostProcessEffects() const { return postProcessEffects; }
            const std::string &GetPostProcessPresetAssetReference() const { return postProcessPresetAssetReference; }
        };

        bool Initialize(const std::filesystem::path &startupProject = {}, bool visible = true);
        bool CreateProjectAtPath(const std::filesystem::path &manifestPath, const std::string &templateId = "pluto.empty");
        void Render();
        void Shutdown();

        [[nodiscard]] core::Engine &GetEngine() { return m_engine; }
        [[nodiscard]] PanelManager &GetPanelManager() { return m_panelManager; }
        [[nodiscard]] EditorProfiler &GetProfiler() { return m_profiler; }

        [[nodiscard]] static EditorShell &GetInstance()
        {
            static EditorShell instance;
            return instance;
        }

        [[nodiscard]] scene::Entity *GetSelectedEntity();
        void SetSelectedEntity(scene::Entity *entity);
        void SetSelectedEntities(const std::vector<std::uint32_t> &ids);
        void ClickEntity(scene::Entity *entity, bool control, bool shift,
                         const std::vector<std::uint32_t> &visible = {});
        [[nodiscard]] std::vector<scene::Entity *> GetSelectedEntities(bool rootsOnly = false);
        void SelectEditorCamera()
        {
            SetSelectedEntity(nullptr);
            m_isEditorCameraSelected = true;
        }
        [[nodiscard]] bool IsEditorCameraSelected() const { return m_isEditorCameraSelected; }
        [[nodiscard]] EditorViewportCamera &GetEditorCamera() { return m_editorCamera; }
        [[nodiscard]] std::uint64_t GetSceneRevision() const { return m_sceneRevision; }
        [[nodiscard]] scene::Scene *GetScene() { return m_scene.get(); }
        [[nodiscard]] const scene::Scene *GetScene() const { return m_scene.get(); }
        [[nodiscard]] assets::Project *GetProject() { return m_project.get(); }
        [[nodiscard]] const assets::Project *GetProject() const { return m_project.get(); }
        void RequestIblCapture(scene::IblCaptureComponent *captureComponent);
        [[nodiscard]] bool IsRuntimeExportProject() const;
        bool CreateScriptAsset(std::string_view requestedName,
                               std::string *createdClassName = nullptr,
                               std::string *errorMessage = nullptr);
        bool BuildProjectScripts();
        void SetStatusMessage(std::string message) { m_statusMessage = std::move(message); }
        bool LoadProjectFromPath(const std::filesystem::path &manifestPath);
        bool OpenSceneFromPath(const std::filesystem::path &scenePath);
        void OpenMaterialAsset(std::string materialAssetReference);
        void OpenMeshAsset(std::string meshAssetReference);
        void OpenShaderGraphAsset(std::string shaderGraphAssetReference);
        void OpenAnimationGraphAsset(std::string animationGraphAssetReference);
        void OpenAnimationClipAsset(std::string animationClipAssetReference);
        void OpenParticleSystemAsset(std::string particleSystemAssetReference);
        void OpenInputMappingAsset(std::string inputMappingAssetReference);
        void OpenRmlDocument(std::string reference);
        void OpenLoadingScreenAsset(std::string reference)
        { m_activeLoadingScreenAssetReference = std::move(reference); m_openLoadingScreenEditorRequested = true; }
        const std::string &GetActiveLoadingScreenAssetReference() const { return m_activeLoadingScreenAssetReference; }
        const std::string &GetActiveMaterialAssetReference() const { return m_activeMaterialAssetReference; }
        const std::string &GetActiveMeshAssetReference() const { return m_activeMeshAssetReference; }
        const std::string &GetActiveShaderGraphAssetReference() const { return m_activeShaderGraphAssetReference; }
        const std::string &GetActiveAnimationGraphAssetReference() const { return m_activeAnimationGraphAssetReference; }
        const std::string &GetActiveAnimationClipAssetReference() const { return m_activeAnimationClipAssetReference; }
        const std::string &GetActiveParticleSystemAssetReference() const { return m_activeParticleSystemAssetReference; }
        const std::string &GetActiveInputMappingAssetReference() const { return m_activeInputMappingAssetReference; }
        bool ConsumeMaterialEditorOpenRequest()
        {
            const bool requested = m_openMaterialEditorRequested;
            m_openMaterialEditorRequested = false;
            return requested;
        }
        bool ConsumeMeshEditorOpenRequest()
        {
            const bool requested = m_openMeshEditorRequested;
            m_openMeshEditorRequested = false;
            return requested;
        }
        bool ConsumeShaderGraphEditorOpenRequest()
        {
            const bool requested = m_openShaderGraphEditorRequested;
            m_openShaderGraphEditorRequested = false;
            return requested;
        }
        bool ConsumeAnimationGraphEditorOpenRequest()
        {
            const bool requested = m_openAnimationGraphEditorRequested;
            m_openAnimationGraphEditorRequested = false;
            return requested;
        }
        void OpenSequencerEditor() { m_openSequencerEditorRequested = true; }
        bool ConsumeSequencerEditorOpenRequest()
        {
            const bool requested = m_openSequencerEditorRequested;
            m_openSequencerEditorRequested = false;
            return requested;
        }
        bool ConsumeAnimationClipEditorOpenRequest()
        {
            const bool requested = m_openAnimationClipEditorRequested;
            m_openAnimationClipEditorRequested = false;
            return requested;
        }
        bool ConsumeParticleSystemEditorOpenRequest()
        {
            const bool requested = m_openParticleSystemEditorRequested;
            m_openParticleSystemEditorRequested = false;
            return requested;
        }
        bool ConsumeInputMappingEditorOpenRequest()
        {
            const bool requested = m_openInputMappingEditorRequested;
            m_openInputMappingEditorRequested = false;
            return requested;
        }
        void Log(ConsoleSeverity severity, std::string message);
        std::vector<ConsoleMessage> GetConsoleMessages() const;
        void ClearConsoleMessages();
        TimelinePreview &GetTimelinePreview() { return m_timelinePreview; }
        void MarkSceneDirty();
        // Recovers publications and refreshes a validated catalog; logical writers are format-gated.
        bool RefreshProjectAssets();
        bool BeginModelImport(std::string sourceReference,
                              std::optional<assetimport::MeshImportOptions> options = std::nullopt,
                              std::string *errorMessage = nullptr, bool forceReimport = false);
        // Owning thread only, shared by asynchronous and source-copy imports.
        bool PublishModelImportResult(const std::string &sourceReference, const assetimport::ModelImportResult &result,
                                      const scene::ModelAssetSnapshot &previous, std::string *errorMessage = nullptr);
        const auto &GetModelInstanceConflicts() const { return m_modelInstanceConflicts; }
        const auto &GetModelInstanceMessages() const { return m_modelInstanceMessages; }
        void RenderModelInstanceInspector(scene::Entity &entity);
        bool ApplyReviewedModelNodeRepair(const assetimport::ModelNodeRepairProposal &proposal,
            std::string *errorMessage = nullptr);
        bool IsModelImportRunning() const;
        std::string GetModelImportProgress() const;
        const std::string &GetLastModelImportError() const { return m_lastModelImportError; }
        const std::string &GetModelImportError(const std::string &sourceReference) const
        {
            static const std::string empty;
            const auto found = m_modelImportErrors.find(sourceReference);
            return found == m_modelImportErrors.end() ? empty : found->second;
        }
        void CancelModelImport();
        void MarkProjectDirty();
        [[nodiscard]] bool IsSceneDirty() const { return m_sceneDirty; }
        [[nodiscard]] bool IsProjectDirty() const { return m_projectDirty; }
        [[nodiscard]] bool CanUndo() const { return !m_engine.IsRuntimeRunning() && (!m_undoStack.empty() || m_untrackedSceneEdit); }
        [[nodiscard]] bool CanRedo() const { return !m_engine.IsRuntimeRunning() && !m_redoStack.empty(); }
        void ExecuteSceneEdit(std::string label, const std::function<void()> &edit);
        void PushSceneEditCommand(std::string label,
                                  std::function<bool()> undo,
                                  std::function<bool()> redo,
                                  std::size_t retainedBytes = 0);
        bool BeginSceneEdit(std::string label);
        bool EndSceneEdit();
        void CancelSceneEdit();
        bool Undo();
        bool Redo();
        bool CopySelectedEntity();
        bool PasteCopiedEntity();
        bool DuplicateSelectedEntity();
        bool DeleteSelectedEntity();
        bool HasCopiedEntity() const { return m_entityClipboardScene != nullptr && m_entityClipboardRootId != 0; }

    private:
        EditorShell();
        ~EditorShell();

        void InitializeEditorCamera();
        void ApplyProjectContext();
        void PollModelImport();
        void PollImportWatch();
        std::filesystem::path ResolveProjectScriptAssemblyPath() const;
        bool ReloadProjectScriptAssembly(std::string *errorMessage = nullptr);
        std::filesystem::path GetProjectScriptSourceDirectory() const;
        std::filesystem::path GetProjectScriptProjectPath() const;
        std::filesystem::path GetProjectScriptAssemblyOutputPath() const;
        bool EnsureProjectScriptBuildScaffold(std::string *errorMessage = nullptr);
        bool SaveProjectManifest(std::string *errorMessage = nullptr);
        void UpdateWindowTitle();
        void ResetSelection();
        void SetScene(std::unique_ptr<scene::Scene> scene, bool updatePrefabs = true);
        std::filesystem::path GetDefaultProjectScenePath() const;
        std::filesystem::path GetDefaultExportExecutablePath() const;
        bool SaveSceneToPath(const std::filesystem::path &scenePath);
        bool SaveActiveSceneIntoProject();
        void PollScriptSources();
        void RenderAuthoringMenu();
        AuthoringRegistry m_authoring;
        ScriptSourceWatch m_scriptWatch;
        std::future<ScriptSourceWatch::Snapshot> m_scriptWatchFuture;
        std::filesystem::path m_scriptWatchRoot;
        ScriptSourceWatch::Clock::time_point m_nextScriptScan{};
        bool m_autoBuildScripts = true;
        std::future<scripting::ScriptBuildResult> m_scriptBuildFuture;
        std::filesystem::path m_scriptBuildProject;
        std::string m_scriptBuildOutput;
        bool m_showScriptBuildDiagnostics = false;
        bool m_persistEditorSettings = true;
        bool SaveProjectToDisk();
        bool BuildProjectToPath(const std::filesystem::path &destinationExecutablePath);
        bool RunTestBuild();
        bool BuildAndRunProjectToPath(const std::filesystem::path &destinationExecutablePath);
        bool ExportScriptAuthoringSdk(const std::filesystem::path &destinationExecutablePath, std::string *errorMessage = nullptr) const;
        bool CaptureSceneState(std::string &state, std::string *errorMessage = nullptr, SceneGenerationRetention *retained = nullptr) const;
        void PushSceneHistoryEntry(SceneHistoryEntry entry);
        void FlushUntrackedSceneEdit();
        void PollModelInstances();
        bool UnpackModelInstance(std::uint32_t rootEntityId, std::string &error);
        void RecordModelInstanceRefresh(const PreparedModelInstanceRefresh &prepared);
        void SynchronizeHistoryState();
        void UpdateSceneRecovery();
        void RenderSceneRecovery();
        bool RunProjectValidation(bool includeCurrentScene);
        void RenderProjectValidation(const std::function<void(const std::string &)> &reveal);
        void SaveRecoveryBackup();
        bool RestoreSceneState(const std::string &state, std::string *errorMessage = nullptr, bool markDirty = true);
        bool StartEditorRuntime(ViewportPanel &gameViewport);
        bool StopEditorRuntime(bool reviewChanges = false);
        void RenderPlayModeChanges();
        bool ApplyPlayModeChanges();
        void RenderViewportBookmarks();
        void RenderGroundPlacement();
        void HandleRuntimeSceneLoadRequest(ViewportPanel &gameViewport);
        bool ConfirmContinueWithUnsavedChanges();
        void MarkSceneClean();
        void MarkProjectClean();
        void HandleEditorShortcuts(bool isRuntimeRunning, ProfilerPanel *profilerPanel);
        void LoadRecentProjects();
        bool RunProjectLauncher();
        void SaveRecentProjects() const;
        void AddRecentProject(const std::filesystem::path &manifestPath);

        core::Engine &m_engine = core::Engine::GetInstance();
        PanelManager m_panelManager;
        std::unique_ptr<EditorSceneRenderService> m_editorSceneRenderService;
        std::unique_ptr<EditorSceneRenderService> m_gameSceneRenderService;
        // Per-frame storage for cameras that render into render textures.
        std::unique_ptr<scene::RenderTextureViewBuilder> m_renderTextureViews;
        bool m_pendingRuntimeStart = false;
        EditorProfiler m_profiler;

        EntitySelection m_entitySelection;
        scene::Entity *m_selectedEntity = nullptr;
        bool m_isEditorCameraSelected = false;
        EditorViewportCamera m_editorCamera;
        std::unique_ptr<assets::Project> m_project;
        std::uint64_t m_sceneRevision = 0;
        std::unique_ptr<scene::Scene> m_scene;
        std::unique_ptr<scene::Scene> m_entityClipboardScene;
        scene::EntityID m_entityClipboardRootId = 0;
        std::unique_ptr<scene::SceneBakeTask> m_activeBakeTask;
        scene::SceneBakeSettings m_customBakeSettings = scene::SceneBakeSettings::BalancedPreview();
        std::vector<scene::EntityID> m_pendingIblCaptureEntities;
        std::string m_statusMessage;
        bool m_sceneDirty = false;
        bool m_untrackedSceneEdit = false;
        std::string m_observedSceneState;
        SceneGenerationRetention m_observedSceneGenerations;
        std::string m_savedSceneState;
        std::filesystem::path m_recoveryDirectory;
        RecoverySettings m_recoverySettings;
        bool m_showSceneRecovery = false;
        bool m_showProjectValidation = false;
        bool m_validationHasRun = false;
        bool m_validationWarnings = true;
        assets::ProjectValidationResult m_validationResult;
        std::filesystem::path m_validationProject;
        std::string m_validationCurrentOwner;
        std::string m_validationCurrentState;
        bool m_recoveredSceneNeedsSaveAs = false;
        std::string m_recoveryError;
        std::vector<RecoveryBackup> m_recoveryBackups;
        std::vector<std::string> m_recoveryScanErrors;
        std::string m_lastRecoveryState;
        std::chrono::steady_clock::time_point m_nextRecovery;
        bool m_projectDirty = false;
        std::vector<SceneHistoryEntry> m_undoStack;
        std::vector<SceneHistoryEntry> m_redoStack;
        std::vector<std::filesystem::path> m_recentProjects;
        bool m_sceneEditInProgress = false;
        std::string m_sceneEditLabel;
        std::string m_sceneEditBeforeState;
        SceneGenerationRetention m_sceneEditBeforeGenerations;
        std::vector<ConsoleMessage> m_consoleMessages;
        mutable std::mutex m_consoleMessagesMutex;
        std::string m_activeMaterialAssetReference;
        std::string m_activeMeshAssetReference;
        std::string m_activeShaderGraphAssetReference;
        std::string m_activeAnimationGraphAssetReference;
        std::string m_activeAnimationClipAssetReference;
        std::string m_activeParticleSystemAssetReference;
        std::string m_activeInputMappingAssetReference;
        std::string m_activeLoadingScreenAssetReference;
        bool m_openLoadingScreenEditorRequested = false;
        RmlDocumentEditorPanel *m_rmlDocumentEditor = nullptr;
        TimelinePreview m_timelinePreview;
        std::string m_runtimeSceneSnapshot;
        SceneGenerationRetention m_runtimeSceneGenerations;
        std::string m_runtimeSceneSnapshotPath;
        bool m_runtimeSceneWasDirty = false;
        bool m_runtimeSceneReplaced = false;
        bool m_openPlayModeChanges = false;
        std::string m_playModeChangesError;
        PlayModeChanges::Snapshot m_playModeBaseline;
        std::vector<PlayModeChanges::Change> m_playModeChanges;
        std::vector<SceneHistoryEntry> m_prePlayUndoStack;
        std::vector<SceneHistoryEntry> m_prePlayRedoStack;
        bool m_showViewportBookmarks = false;
        bool m_bookmarksLoaded = false;
        std::filesystem::path m_bookmarkPath;
        std::vector<ViewportBookmark> m_viewportBookmarks;
        std::array<char, 129> m_bookmarkName{};
        int m_selectedBookmark = -1;
        std::string m_bookmarkError;
        bool m_showGroundPlacement = false;
        GroundPlacementOptions m_groundPlacementOptions;
        std::string m_groundPlacementError;
        bool m_openMaterialEditorRequested = false;
        bool m_openMeshEditorRequested = false;
        bool m_openShaderGraphEditorRequested = false;
        bool m_openAnimationGraphEditorRequested = false;
        bool m_openAnimationClipEditorRequested = false;
        bool m_openSequencerEditorRequested = false;
        bool m_openParticleSystemEditorRequested = false;
        bool m_openInputMappingEditorRequested = false;
        std::optional<std::filesystem::path> m_pendingProjectLoad;
        std::optional<render::rhi::GraphicsApi> m_pendingGraphicsApi;
        struct AssetReconciliationCompletion
        {
            std::filesystem::path projectPath;
            std::uint64_t contextEpoch = 0;
            bool succeeded = false;
            bool cancelled = false;
            std::vector<assetimport::ImportAssessment> assessments;
            std::string error;
        };
        struct ImportWatchCompletion
        {
            std::filesystem::path projectPath;
            std::uint64_t contextEpoch = 0;
            bool succeeded = false;
            bool cancelled = false;
            assetimport::ImportWatchSnapshot snapshot;
            std::string error;
        };
        assetimport::ImportWatchDebouncer m_importWatchDebouncer;
        bool m_importWatchPrimed = false;
        std::stop_source m_importWatchStop;
        std::future<ImportWatchCompletion> m_importWatchFuture;
        std::chrono::steady_clock::time_point m_nextImportWatch{};
        std::string m_importWatchError;
        std::uint64_t m_assetContextEpoch = 0;
        std::uint64_t m_activeModelImportEpoch = 0;
        bool m_modelInstancesNeedRefresh = false;
        std::uint32_t m_requestedModelInstanceUnpack = 0;
        std::uint32_t m_modelInstanceUnpackErrorRoot = 0;
        std::string m_modelInstanceUnpackError;
        std::vector<scene::StaticModelSceneConflict> m_modelInstanceConflicts;
        std::vector<std::string> m_modelInstanceMessages;
        std::vector<StaticModelSourceDiagnostic> m_modelInstanceSourceDiagnostics;
        std::string m_modelInstanceRefreshError;
        bool m_assetRefreshPending = false;
        bool m_assetReconciliationRequested = false;
        std::vector<std::filesystem::path> m_reconciliationChangedPaths;
        std::deque<std::string> m_pendingModelImports;
        std::stop_source m_assetReconciliationStop;
        std::future<AssetReconciliationCompletion> m_assetReconciliation;
        scene::ModelAssetSnapshot m_activeModelAssetSnapshot;
        std::unordered_map<std::string, std::string> m_modelImportErrors;
        std::string m_lastModelImportError;
        std::unique_ptr<assetimport::ModelImportTask> m_modelImportTask;
    };
}
