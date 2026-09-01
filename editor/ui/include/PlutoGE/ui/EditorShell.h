#pragma once

#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/render/postprocess/PostProcessEffectFactory.h"
#include "PlutoGE/scene/SceneBaker.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/ui/EditorProfiler.h"
#include "PlutoGE/ui/PanelManager.h"

#include <algorithm>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace PlutoGE::assets
{
    class Project;
}

namespace PlutoGE::scene
{
    class IblCaptureComponent;
}

namespace PlutoGE::ui
{
    class ProfilerPanel;
    class EditorSceneRenderService;

    class EditorShell
    {
    public:
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

                auto &window = core::Engine::GetInstance().GetWindow();
                if (window.IsOpen() && !window.EnsureOpenGLContextCurrent(true))
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

        bool Initialize(const std::filesystem::path &startupProject = {});
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
        void SetSelectedEntity(scene::Entity *entity)
        {
            m_selectedEntity = entity;
            m_isEditorCameraSelected = false;
        }
        void SelectEditorCamera()
        {
            m_selectedEntity = nullptr;
            m_isEditorCameraSelected = true;
        }
        [[nodiscard]] bool IsEditorCameraSelected() const { return m_isEditorCameraSelected; }
        [[nodiscard]] EditorViewportCamera &GetEditorCamera() { return m_editorCamera; }
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
        void MarkSceneDirty();
        void MarkProjectDirty();
        [[nodiscard]] bool IsSceneDirty() const { return m_sceneDirty; }
        [[nodiscard]] bool IsProjectDirty() const { return m_projectDirty; }
        [[nodiscard]] bool CanUndo() const { return !m_undoStack.empty(); }
        [[nodiscard]] bool CanRedo() const { return !m_redoStack.empty(); }
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
        struct SceneHistoryEntry
        {
            std::string label;
            std::string beforeState;
            std::string afterState;
            std::function<bool()> undo;
            std::function<bool()> redo;
            std::size_t retainedBytes = 0;
        };

        EditorShell();
        ~EditorShell();

        void InitializeEditorCamera();
        void ApplyProjectContext();
        std::filesystem::path ResolveProjectScriptAssemblyPath() const;
        bool ReloadProjectScriptAssembly(std::string *errorMessage = nullptr);
        std::filesystem::path GetProjectScriptSourceDirectory() const;
        std::filesystem::path GetProjectScriptProjectPath() const;
        std::filesystem::path GetProjectScriptAssemblyOutputPath() const;
        bool EnsureProjectScriptBuildScaffold(std::string *errorMessage = nullptr);
        bool SaveProjectManifest(std::string *errorMessage = nullptr);
        void UpdateWindowTitle();
        void ResetSelection();
        void SetScene(std::unique_ptr<scene::Scene> scene);
        std::filesystem::path GetDefaultProjectScenePath() const;
        std::filesystem::path GetDefaultExportExecutablePath() const;
        bool SaveSceneToPath(const std::filesystem::path &scenePath);
        bool SaveActiveSceneIntoProject();
        bool CreateProjectAtPath(const std::filesystem::path &manifestPath);
        bool SaveProjectToDisk();
        bool BuildProjectToPath(const std::filesystem::path &destinationExecutablePath);
        bool BuildAndRunProjectToPath(const std::filesystem::path &destinationExecutablePath);
        bool ExportScriptAuthoringSdk(const std::filesystem::path &destinationExecutablePath, std::string *errorMessage = nullptr) const;
        bool CaptureSceneState(std::string &state, std::string *errorMessage = nullptr) const;
        void PushSceneHistoryEntry(SceneHistoryEntry entry);
        bool RestoreSceneState(const std::string &state, std::string *errorMessage = nullptr, bool markDirty = true);
        bool StartEditorRuntime();
        bool StopEditorRuntime();
        void HandleRuntimeSceneLoadRequest();
        bool ConfirmContinueWithUnsavedChanges();
        void MarkSceneClean();
        void MarkProjectClean();
        void HandleEditorShortcuts(bool isRuntimeRunning, ProfilerPanel *profilerPanel);
        void LoadRecentProjects();
        void SaveRecentProjects() const;
        void AddRecentProject(const std::filesystem::path &manifestPath);

        core::Engine &m_engine = core::Engine::GetInstance();
        PanelManager m_panelManager;
        std::unique_ptr<EditorSceneRenderService> m_editorSceneRenderService;
        std::unique_ptr<EditorSceneRenderService> m_gameSceneRenderService;
        EditorProfiler m_profiler;

        scene::Entity *m_selectedEntity = nullptr;
        bool m_isEditorCameraSelected = false;
        EditorViewportCamera m_editorCamera;
        std::unique_ptr<assets::Project> m_project;
        std::unique_ptr<scene::Scene> m_scene;
        std::unique_ptr<scene::Scene> m_entityClipboardScene;
        scene::EntityID m_entityClipboardRootId = 0;
        std::unique_ptr<scene::SceneBakeTask> m_activeBakeTask;
        scene::SceneBakeSettings m_customBakeSettings = scene::SceneBakeSettings::BalancedPreview();
        std::vector<scene::EntityID> m_pendingIblCaptureEntities;
        std::string m_statusMessage;
        bool m_sceneDirty = false;
        bool m_projectDirty = false;
        std::vector<SceneHistoryEntry> m_undoStack;
        std::vector<SceneHistoryEntry> m_redoStack;
        std::vector<std::filesystem::path> m_recentProjects;
        bool m_sceneEditInProgress = false;
        std::string m_sceneEditLabel;
        std::string m_sceneEditBeforeState;
        std::vector<ConsoleMessage> m_consoleMessages;
        mutable std::mutex m_consoleMessagesMutex;
        std::string m_activeMaterialAssetReference;
        std::string m_activeMeshAssetReference;
        std::string m_activeShaderGraphAssetReference;
        std::string m_activeAnimationGraphAssetReference;
        std::string m_activeAnimationClipAssetReference;
        std::string m_activeParticleSystemAssetReference;
        std::string m_activeInputMappingAssetReference;
        std::string m_runtimeSceneSnapshot;
        std::string m_runtimeSceneSnapshotPath;
        bool m_runtimeSceneWasDirty = false;
        bool m_openMaterialEditorRequested = false;
        bool m_openMeshEditorRequested = false;
        bool m_openShaderGraphEditorRequested = false;
        bool m_openAnimationGraphEditorRequested = false;
        bool m_openAnimationClipEditorRequested = false;
        bool m_openParticleSystemEditorRequested = false;
        bool m_openInputMappingEditorRequested = false;
    };
}
