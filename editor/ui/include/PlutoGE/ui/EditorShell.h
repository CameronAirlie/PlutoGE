#include "PlutoGE/ui/TimelinePreview.h"
#pragma once

#include "PlutoGE/ui/ConsoleLog.h"
#include "PlutoGE/ui/EditorCommands.h"
#include "PlutoGE/ui/EditorDocument.h"
#include "PlutoGE/ui/EntityLocks.h"
#include "PlutoGE/ui/EntitySelection.h"
#include "PlutoGE/ui/FoliageEditTool.h"
#include "PlutoGE/ui/AuthoringRegistry.h"
#include "PlutoGE/ui/ScriptSourceWatch.h"
#include "PlutoGE/ui/AssetHotReload.h"
#include "PlutoGE/scripting/ScriptEngine.h"
#include "PlutoGE/scripting/ScriptBuildProcess.h"
#include "PlutoGE/scripting/ScriptErrors.h"

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"
#include "PlutoGE/scene/SceneBaker.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/ui/EditorProfiler.h"
#include "PlutoGE/ui/PanelManager.h"
#include "PlutoGE/ui/PlayModeChanges.h"
#include "PlutoGE/ui/PlayModeToolbar.h"
#include "PlutoGE/ui/ViewportBookmarks.h"
#include "PlutoGE/ui/GroundPlacement.h"
#include "PlutoGE/ui/SceneEditHistory.h"
#include "PlutoGE/ui/SceneHistory.h"
#include "PlutoGE/ui/SceneRecovery.h"
#include "PlutoGE/ui/ModelInstanceRefresh.h"
#include "PlutoGE/assets/PostProcessStack.h"
#include "PlutoGE/assets/ProjectValidation.h"
#include "PlutoGE/import/MeshImportOptions.h"
#include "PlutoGE/asset_import/ModelNodeRepairService.h"
#include "PlutoGE/asset_import/ImportState.h"
#include "PlutoGE/asset_import/ImportWatch.h"
#include <atomic>
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
    struct ProjectAssetEntry;
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
    class AnimationGraphEditorPanel;
    class UIThemeEditorPanel;
    class InputMappingEditorPanel;
    class PostProcessEditorPanel;
    class FoliageEditorPanel;
    struct UIThemeTarget;
    class ContentBrowserPanel;
    class SceneHierarchyPanel;
    class ViewportPanel;
    class ProfilerPanel;
    class ViewportPanel;
    class EditorSceneRenderService;

    class EditorShell : private ISceneHistoryHost
    {
    public:
        AuthoringRegistry &GetAuthoringRegistry() { return m_authoring; }
        void RenderExtensionInspectors();
        void RenderScriptBuildDiagnostics();
        void UpdateAuthoring() { PollModelImport(); PollScriptSources(); PollAssetHotReload(); }
        bool IsScriptBuildRunning() const { return m_scriptBuildFuture.valid(); }
        using ConsoleSeverity = ui::ConsoleSeverity;
        using ConsoleMessage = ui::ConsoleEntry;

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
            // Frame Selected moves the camera here over a few frames; any navigation input cancels it.
            std::optional<glm::vec3> flyTarget;
            // The point the camera was last framed on, used as the orbit pivot.
            glm::vec3 focusPoint{0.0f};
            // Counts down after the fly speed changes; the viewport shows the speed meanwhile.
            float speedToastSeconds = 0.0f;
            bool orthographic = false;
            bool renderRuntimeUI = false;
            float orthographicSize = 10.0f;
            glm::vec3 perspectivePosition{0.0f, 2.0f, 6.0f};
            bool hasPerspectivePosition = false;
            // Normally follows a preset asset, so saving the preset updates the viewport. Projects from older
            // builds may still carry an inline stack.
            assets::PostProcessStack postProcess;

            bool SetPostProcessPresetAssetReference(std::string assetReference);
            const std::string &GetPostProcessPresetAssetReference() const { return postProcess.GetPresetReference(); }
            // Re-applies the preset if it was saved or previewed since it was last applied.
            bool RefreshPostProcessPreset();
            // Destroys the effects, forgetting the preset.
            void ClearPostProcessEffects() { postProcess.SetInlineEffects({}); }

            bool AddPostProcessEffectByType(std::string_view typeName)
            {
                auto effect = render::CreatePostProcessEffect(typeName);
                if (!effect)
                {
                    return false;
                }

                postProcess.GetEffects().push_back(std::move(effect));
                return true;
            }

            render::IPostProcessEffect *GetPostProcessEffect(size_t index)
            {
                auto &effects = postProcess.GetEffects();
                return index < effects.size() ? effects[index].get() : nullptr;
            }

            const std::vector<std::unique_ptr<render::IPostProcessEffect>> &GetPostProcessEffects() const { return postProcess.GetEffects(); }
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
        [[nodiscard]] std::uint64_t GetSelectionRevision() const override { return m_entitySelection.Revision(); }
        // Moves the editor camera so the primary selected entity, or the foliage instances selected in it, fill the view.
        void FrameSelectedEntity();
        // Where a new scene object should appear: the first surface under the centre of the editor view within
        // 50 m, otherwise a point a few metres in front of the camera.
        [[nodiscard]] glm::vec3 GetSpawnPointInView() const;
        [[nodiscard]] glm::vec3 GetEditorCameraForward() const;
        [[nodiscard]] FoliageEditTool &GetFoliageTool() { return m_foliageTool; }
        // Selects an asset in the Content Browser and shows its details. Returns false if there is no browser.
        bool RevealAsset(const std::string &reference);
        // The Content Browser's cached preview of an asset, or 0 while there is none. An ImTextureID, drawn with V flipped.
        [[nodiscard]] std::uint64_t GetAssetThumbnail(const std::string &reference);
        [[nodiscard]] EntityLocks &GetEntityLocks() { return m_entityLocks; }
        [[nodiscard]] const EntityLocks &GetEntityLocks() const { return m_entityLocks; }
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
        void RequestAmbientBake(scene::EntityID entity) { m_pendingAmbientBake = entity; }
        scene::EntityID m_pendingAmbientBake = 0;
        void RequestIblCapture(scene::IblCaptureComponent *captureComponent);
        [[nodiscard]] bool IsRuntimeExportProject() const;
        bool CreateScriptAsset(std::string_view requestedName,
                               std::string *createdClassName = nullptr,
                               std::string *errorMessage = nullptr);
        // Debug for editing and Development builds; Release for Shipping builds.
        bool BuildProjectScripts(const std::string &configuration = "Debug");
        void SetStatusMessage(std::string message) { m_statusMessage = std::move(message); }
        bool LoadProjectFromPath(const std::filesystem::path &manifestPath);
        bool OpenSceneFromPath(const std::filesystem::path &scenePath);
        // A .plutoprefab opened as the active scene, with preview lighting.
        [[nodiscard]] bool IsEditingPrefab() const;
        [[nodiscard]] const std::filesystem::path &GetPrefabReturnScenePath() const { return m_prefabReturnScenePath; }
        // Reopens the scene that was active when the prefab was opened.
        bool ReturnFromPrefab();
        void OpenMaterialAsset(std::string materialAssetReference);
        // Opens an asset in its editor, as double-clicking it in the Content Browser does. False for assets with no
        // editor (textures, models, sounds).
        bool OpenProjectAsset(const assets::ProjectAssetEntry &asset);
        // Editor commands with their shortcuts; asset editors may register and bind their own.
        [[nodiscard]] EditorCommandRegistry &GetCommands() { return m_commands; }
        // Opens a script source file in the system's default editor for its type.
        bool OpenScriptSource(const std::filesystem::path &sourcePath);
        void OpenMeshAsset(std::string meshAssetReference);
        void OpenShaderGraphAsset(std::string shaderGraphAssetReference);
        void OpenAnimationGraphAsset(std::string animationGraphAssetReference);
        void OpenAnimationClipAsset(std::string animationClipAssetReference);
        void OpenParticleSystemAsset(std::string particleSystemAssetReference);
        void OpenInputMappingAsset(std::string inputMappingAssetReference);
        // Shows a .plutopostprocess preset in the Post Process Editor.
        void OpenPostProcessPresetAsset(std::string postProcessPresetAssetReference);
        // Shows a .plutofoliage foliage type in the Foliage Editor.
        void OpenFoliageTypeAsset(const std::string &reference);
        // Re-applies post process presets that were saved or previewed to the editor camera and scene cameras.
        // Runs every frame, so edits show even while the scene is not updating.
        void RefreshPostProcessPresets();
        void OpenRmlDocument(std::string reference);
        // Shows a UI theme in the theme editor, selecting `style` when given.
        void OpenUIThemeEditor(const UIThemeTarget &target, const std::string &style = {});
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
        const std::string &GetActivePostProcessPresetAssetReference() const { return m_activePostProcessPresetAssetReference; }
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
        bool ConsumePostProcessEditorOpenRequest()
        {
            const bool requested = m_openPostProcessEditorRequested;
            m_openPostProcessEditorRequested = false;
            return requested;
        }
        void Log(ConsoleSeverity severity, std::string message);
        void Log(ConsoleEntry entry);
        [[nodiscard]] const ConsoleLog &GetConsoleLog() const { return m_consoleLog; }
        std::vector<ConsoleMessage> GetConsoleMessages() const;
        void ClearConsoleMessages();
        // Selects the entry's entity, reveals its asset or opens its source file, whichever it carries.
        void NavigateToConsoleEntry(const ConsoleEntry &entry);
        // Scene editing keys (Esc, copy, paste, duplicate, delete, group) act on the scene only while the user is
        // working in it: the hierarchy or scene viewport has focus, the pointer is over the scene viewport, or no
        // window has focus. Other panels (Inspector, Console, asset editors) keep those keys for themselves.
        [[nodiscard]] bool IsSceneShortcutContext() const;
        // Rebuilds prefab instances in the open scene whose prefab changed since they were saved (keeping overrides),
        // as one undoable edit. Runs when a scene opens.
        void UpdateOutdatedPrefabInstances();
        // Brings prefab instances up to date in every scene of the project: the open scene as an undoable edit,
        // other scene files are loaded, updated and saved when anything changed.
        void UpdatePrefabInstancesInAllScenes();
        TimelinePreview &GetTimelinePreview() { return m_timelinePreview; }
        // Reports an edit already made to the scene. Its reach comes from the enclosing SceneEditRegion, and without
        // one it is assumed to be the whole scene; pass one to say otherwise.
        void MarkSceneDirty();
        void MarkSceneDirty(SceneEditReach reach);
        // While alive, edits reported with MarkSceneDirty() are taken to have this reach. Panels whose controls all edit
        // the selection use one instead of annotating each control; a control that reaches further says so explicitly.
        class SceneEditRegion
        {
        public:
            SceneEditRegion(EditorShell &shell, SceneEditReach reach) : m_shell(shell), m_previous(shell.m_sceneEditReach) { shell.m_sceneEditReach = reach; }
            ~SceneEditRegion() { m_shell.m_sceneEditReach = m_previous; }
            SceneEditRegion(const SceneEditRegion &) = delete;
            SceneEditRegion &operator=(const SceneEditRegion &) = delete;
        private:
            EditorShell &m_shell;
            SceneEditReach m_previous;
        };
        // Changes whenever an edit is reported or history moves, for caches of values read from the scene.
        [[nodiscard]] std::uint64_t GetSceneEditRevision() const { return m_sceneEditRevision; }
        // Changes when a different scene is opened, unlike GetSceneRevision, which also changes when the current scene
        // is restored from a snapshot.
        [[nodiscard]] std::uint64_t GetSceneIdentityRevision() const { return m_sceneIdentityRevision; }
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
        [[nodiscard]] bool CanUndo() const { return !m_engine.IsRuntimeRunning() && m_sceneHistory.CanUndo(); }
        [[nodiscard]] bool CanRedo() const { return !m_engine.IsRuntimeRunning() && m_sceneHistory.CanRedo(); }
        // `scope` names what the edit may change, which lets it record only that; see SceneEditScope.
        void ExecuteSceneEdit(std::string label, const std::function<void()> &edit, const SceneEditScope &scope = SceneEditScope::WholeScene());
        [[nodiscard]] SceneEditHistory &GetSceneHistory() { return m_sceneHistory; }
        void PushSceneEditCommand(std::string label,
                                  std::function<bool()> undo,
                                  std::function<bool()> redo,
                                  std::size_t retainedBytes = 0);
        // Records pending inspector edits as their own undo step. Call before capturing state for a command pushed
        // with PushSceneEditCommand, which would otherwise absorb them.
        void FlushPendingSceneEdits() { FlushUntrackedSceneEdit(); }
        bool BeginSceneEdit(std::string label, SceneEditReach reach = SceneEditReach::Scene);
        bool EndSceneEdit();
        void CancelSceneEdit();
        bool Undo();
        bool Redo();
        bool CopySelectedEntity();
        bool PasteCopiedEntity();
        bool DuplicateSelectedEntity();
        bool DeleteSelectedEntity();
        bool HasCopiedEntity() const { return m_entityClipboardScene != nullptr && !m_entityClipboardRootIds.empty(); }

    private:
        EditorShell();
        ~EditorShell();

        void InitializeEditorCamera();
        void FrameWorldSphere(const glm::vec3 &center, float radius);
        // Foliage instances take Escape and Delete while the tool is on. Returns whether they did.
        bool HandleFoliageToolKeys();
        void ApplyProjectContext();
        void PollModelImport();
        void PollImportWatch();
        std::filesystem::path ResolveProjectScriptAssemblyPath() const;
        bool ReloadProjectScriptAssembly(std::string *errorMessage = nullptr);
        std::filesystem::path GetProjectScriptSourceDirectory() const;
        std::filesystem::path GetProjectScriptProjectPath() const;
        std::filesystem::path GetProjectScriptAssemblyOutputPath() const;
        // The PlutoGE.ScriptCore.dll the editor loads project scripts against.
        std::filesystem::path GetEditorScriptCoreAssemblyPath() const;
        bool EnsureProjectScriptBuildScaffold(std::string *errorMessage = nullptr);
        bool SaveProjectManifest(std::string *errorMessage = nullptr);
        void UpdateWindowTitle();
        void ResetSelection();
        enum class SceneHistorySync { Resynchronize, Deferred };
        void SetScene(std::unique_ptr<scene::Scene> scene, bool updatePrefabs = true, SceneHistorySync historySync = SceneHistorySync::Resynchronize);
        std::filesystem::path GetDefaultProjectScenePath() const;
        std::filesystem::path GetDefaultExportExecutablePath() const;
        bool SaveSceneToPath(const std::filesystem::path &scenePath);
        bool SaveActiveSceneIntoProject();
        void PollScriptSources();
        void RenderAuthoringMenuItems();
        AuthoringRegistry m_authoring;
        ScriptSourceWatch m_scriptWatch;
        // Textures, materials and sounds changed outside the editor (EditorHotReload.cpp).
        void PollAssetHotReload();
        void ReloadChangedAssets(const std::vector<std::string> &paths);
        AssetFileWatch m_assetWatch;
        std::future<AssetFileWatch::Snapshot> m_assetWatchFuture;
        AssetFileWatch::Clock::time_point m_nextAssetScan{};
        std::filesystem::path m_assetWatchRoot;
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
        std::function<void()> m_pendingBuild;
        bool m_buildExecuting = false;
        float m_buildProgress = 0;
        double m_buildStartedAt = 0;
        std::string m_buildStage;
        void PresentBuildProgress();
        void SetBuildProgress(float progress, std::string stage);
        bool BuildProjectToPath(const std::filesystem::path &destinationExecutablePath, bool headless = false);
        std::filesystem::path GetServerOutputPath() const;
        bool BuildServer(bool runAfterBuild);
        bool RunServerBuild();
        bool RunTestBuild();
        void RenderProjectBenchmarkMenu();
        void RunProjectBenchmark();
        bool PollProjectBenchmark();
        std::future<scripting::ProcessOutput> m_projectBenchmark;
        std::filesystem::path m_projectBenchmarkOutput;
        bool BuildAndRunProjectToPath(const std::filesystem::path &destinationExecutablePath);
        bool ExportScriptAuthoringSdk(const std::filesystem::path &destinationExecutablePath, std::string *errorMessage = nullptr) const;
        bool CaptureSceneState(std::string &state, std::string *errorMessage = nullptr, SceneGenerationRetention *retained = nullptr) const;
        void FlushUntrackedSceneEdit();
        void RefreshDirtyFromHistory();

        // ISceneHistoryHost
        scene::Scene *GetHistoryScene() override { return m_scene.get(); }
        bool CaptureSceneSnapshot(SceneSnapshot &snapshot, std::string &error) override;
        bool RestoreSceneSnapshot(const SceneSnapshot &snapshot, bool &exact, std::string &error) override;
        void OnEntitiesChangedInPlace() override;
        void OnSceneEdited() override;
        bool IsHistorySuspended() const override;
        std::vector<scene::EntityID> GetEditableSelection() const override;
        void LogHistoryWarning(const std::string &message) override { Log(ConsoleSeverity::Warning, message); }
        void PollModelInstances();
        bool UnpackModelInstance(std::uint32_t rootEntityId, std::string &error);
        void RecordModelInstanceRefresh(const PreparedModelInstanceRefresh &prepared);
        void SynchronizeHistoryState();
        void UpdateSceneRecovery();
        void RenderSceneRecovery();
        bool RunProjectValidation(bool includeCurrentScene, bool headless = false);
        void RenderProjectValidation(const std::function<void(const std::string &)> &reveal);
        void SaveRecoveryBackup();
        // With `exact`, the caller takes over telling history about the new scene, and learns whether it serializes to
        // `state`. Without it, history re-reads the scene.
        bool RestoreSceneState(const std::string &state, std::string *errorMessage = nullptr, bool markDirty = true, bool *exact = nullptr);
        bool StartEditorRuntime(ViewportPanel &gameViewport);
        bool StopEditorRuntime(bool reviewChanges = false);
        [[nodiscard]] PlayState GetPlayState() const;
        void SetRuntimePaused(bool paused);
        void StepRuntime();
        // A script callback threw: log it with its entity and source line, and pause play when the user asked to.
        void OnScriptError(const scripting::ScriptErrorReport &report);
        // Debug.Log and friends: the message with its entity and calling line.
        void OnScriptLog(ConsoleSeverity severity, std::string_view message, uint32_t entityId, std::string_view file, int line);
        // Logs compiler errors and warnings from a script build as navigable Console entries.
        void ReportScriptBuildDiagnostics(std::string_view output, bool succeeded);
        void ApplyPlayModeActions(const PlayModeToolbarActions &actions);
        void RenderPlayModeChanges();
        bool ApplyPlayModeChanges();
        void RenderViewportBookmarks();
        void RenderGroundPlacement();
        void HandleRuntimeSceneLoadRequest(ViewportPanel &gameViewport);
        bool ConfirmContinueWithUnsavedChanges();
        // The scene and project, then every editor panel that holds a document (see IEditorDocument).
        [[nodiscard]] std::vector<IEditorDocument *> CollectEditorDocuments();
        // Ctrl+S: saves the document whose editor has keyboard focus when it has unsaved changes. False when no
        // such editor is focused, so the caller saves the project instead.
        bool SaveFocusedDocument();
        void MarkSceneClean();
        void MarkProjectClean();
        void RegisterEditorCommands();
        // Binds the play, edit and scene commands for this frame and works out which shortcut scopes listen.
        void BindEditorCommands(bool isRuntimeRunning, ProfilerPanel *profilerPanel);
        // Runs the palette's choice and pressed shortcuts, once every command is bound.
        void DispatchEditorCommands();
        void RenderCommandPalette();
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
        FoliageEditTool m_foliageTool;
        EntityLocks m_entityLocks;
        scene::Entity *m_selectedEntity = nullptr;
        bool m_isEditorCameraSelected = false;
        EditorViewportCamera m_editorCamera;
        std::unique_ptr<assets::Project> m_project;
        std::uint64_t m_sceneRevision = 0;
        std::unique_ptr<scene::Scene> m_scene;
        std::unique_ptr<scene::Scene> m_entityClipboardScene;
        // Copied selection roots, in selection order, inside m_entityClipboardScene.
        std::vector<scene::EntityID> m_entityClipboardRootIds;
        std::unique_ptr<scene::SceneBakeTask> m_activeBakeTask;
        scene::SceneBakeSettings m_customBakeSettings = scene::SceneBakeSettings::Final();
        std::vector<scene::EntityID> m_pendingIblCaptureEntities;
        bool m_generateAmbientReflections = false;
        bool m_saveAfterAmbientCaptures = false;
        bool m_autoAmbientReflections = true;
        int m_ambientReflectionCount = 4;
        void GenerateAmbientReflectionCaptures();
        std::string m_statusMessage;
        bool m_sceneDirty = false;
        SceneEditReach m_sceneEditReach = SceneEditReach::Scene;
        std::uint64_t m_sceneEditRevision = 0;
        std::uint64_t m_sceneIdentityRevision = 0;
        std::uint64_t m_idleRefreshEditRevision = 0;
        std::chrono::steady_clock::time_point m_idleRefreshChangedAt;
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
        std::filesystem::path m_prefabReturnScenePath;
        std::string m_recoveryError;
        std::vector<RecoveryBackup> m_recoveryBackups;
        std::vector<std::string> m_recoveryScanErrors;
        std::string m_lastRecoveryState;
        std::chrono::steady_clock::time_point m_nextRecovery;
        bool m_projectDirty = false;
        // The open scene and project settings, as one document for unsaved-change prompts.
        class SceneDocument final : public IEditorDocument
        {
        public:
            explicit SceneDocument(EditorShell &shell) : m_shell(shell) {}
            [[nodiscard]] std::string GetDocumentName() const override;
            [[nodiscard]] bool HasUnsavedChanges() const override;
            bool SaveDocument(std::string &error) override;
            void DiscardChanges() override;

        private:
            EditorShell &m_shell;
        };
        SceneDocument m_sceneDocument{*this};
        SceneEditHistory m_sceneHistory{*this};
        std::vector<std::filesystem::path> m_recentProjects;
        ConsoleLog m_consoleLog;
        std::string m_activeMaterialAssetReference;
        std::string m_activeMeshAssetReference;
        std::string m_activeShaderGraphAssetReference;
        std::string m_activeAnimationGraphAssetReference;
        std::string m_activeAnimationClipAssetReference;
        std::string m_activeParticleSystemAssetReference;
        std::string m_activeInputMappingAssetReference;
        std::string m_activePostProcessPresetAssetReference;
        std::string m_activeLoadingScreenAssetReference;
        bool m_openLoadingScreenEditorRequested = false;
        RmlDocumentEditorPanel *m_rmlDocumentEditor = nullptr;
        AnimationGraphEditorPanel *m_animationGraphEditor = nullptr;
        UIThemeEditorPanel *m_uiThemeEditor = nullptr;
        InputMappingEditorPanel *m_inputMappingEditor = nullptr;
        PostProcessEditorPanel *m_postProcessEditor = nullptr;
        FoliageEditorPanel *m_foliageEditor = nullptr;
        // Owns Ctrl+C/V/D and Delete for files while it has keyboard focus.
        ContentBrowserPanel *m_contentBrowser = nullptr;
        SceneHierarchyPanel *m_sceneHierarchyPanel = nullptr;
        ViewportPanel *m_editorViewportPanel = nullptr;
        ViewportPanel *m_gameViewportPanel = nullptr;
        TimelinePreview m_timelinePreview;
        SceneSnapshot m_runtimeSceneSnapshot;
        std::string m_runtimeSceneSnapshotPath;
        bool m_runtimeSceneWasDirty = false;
        bool m_runtimeSceneReplaced = false;
        bool m_runtimePaused = false;
        // Set from whichever thread ran the failing script; the frame loop pauses play.
        std::atomic<bool> m_scriptErrorPauseRequested{false};
        float m_pausedOutputVolume = 1.0f;
        bool m_openPlayModeChanges = false;
        std::string m_playModeChangesError;
        PlayModeChanges::Snapshot m_playModeBaseline;
        std::vector<PlayModeChanges::Change> m_playModeChanges;
        bool m_showViewportBookmarks = false;
        bool m_showPreferences = false;
        EditorCommandRegistry m_commands;
        std::array<bool, 4> m_commandScopeActive{}; // Indexed by EditorCommandScope.
        bool m_openCommandPalette = false;
        std::array<char, 128> m_commandPaletteQuery{};
        int m_commandPaletteSelection = 0;
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
        bool m_openPostProcessEditorRequested = false;
        std::optional<std::filesystem::path> m_pendingProjectLoad;
        std::optional<render::rhi::GraphicsApi> m_pendingGraphicsApi;
        struct AssetReconciliationCompletion
        {
            std::filesystem::path projectPath;
            std::uint64_t contextEpoch = 0;
            bool succeeded = false;
            bool cancelled = false;
            std::vector<assetimport::ImportAssessment> assessments;
            std::optional<assetimport::ImportWatchSnapshot> baseline;
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
        struct AssetAuditProgress
        {
            std::mutex mutex;
            std::size_t completed = 0, total = 0;
            std::string source;
            std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
        };
        std::shared_ptr<AssetAuditProgress> m_assetAuditProgress;
        bool m_assetRefreshPending = false;
        bool m_assetReconciliationRequested = false;
        std::vector<std::filesystem::path> m_reconciliationChangedPaths;
        std::deque<std::string> m_pendingModelImports;
        std::stop_source m_assetReconciliationStop;
        std::future<AssetReconciliationCompletion> m_assetReconciliation;
        std::string m_activeModelImportSource;
        std::chrono::steady_clock::time_point m_activeModelImportStarted{};
        scene::ModelAssetSnapshot m_activeModelAssetSnapshot;
        std::unordered_map<std::string, std::string> m_modelImportErrors;
        std::string m_lastModelImportError;
        std::unique_ptr<assetimport::ModelImportTask> m_modelImportTask;
    };
}
