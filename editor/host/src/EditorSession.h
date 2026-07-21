#pragma once

#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/render/Mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace PlutoGE::core
{
    class Engine;
}

namespace PlutoGE::assets
{
    class Project;
}

namespace PlutoGE::scene
{
    class Entity;
    class Scene;
    class SceneBakeTask;
}

namespace PlutoGE::render
{
    class IPostProcessEffect;
}

class EditorSession
{
public:
    using OperationProgressCallback = std::function<void(int, const std::string &)>;

    enum class GizmoOperation { Translate, Rotate, Scale };
    enum class GizmoSpace { Local, World };

    struct EditorCameraState
    {
        glm::vec3 position{0.0f, 2.0f, 6.0f};
        float yawDegrees = 0.0f;
        float pitchDegrees = 0.0f;
        float fovY = 50.0f;
        float nearPlane = 0.1f;
        float farPlane = 1000.0f;
        float moveSpeed = 6.0f;
        float speedAdjustment = 1.0f;
        bool gridVisible = true;
    };

    explicit EditorSession(PlutoGE::core::Engine &engine);
    ~EditorSession();

    bool Initialize();
    void Shutdown();
    bool HandleCommand(const std::string &commandLine, std::string &errorMessage);
    void SetOperationProgressCallback(OperationProgressCallback callback);
    std::string BuildSnapshotEvent() const;
    PlutoGE::scene::Scene *GetScene() const;
    PlutoGE::scene::Entity *GetSelectedEntity() const;
    void SetSelectedEntity(PlutoGE::scene::Entity *entity);
    bool BeginGizmoEdit();
    bool EndGizmoEdit();
    GizmoOperation GetGizmoOperation() const { return m_gizmoOperation; }
    GizmoSpace GetGizmoSpace() const { return m_gizmoSpace; }
    void SetGizmoOperation(GizmoOperation operation) { m_gizmoOperation = operation; }
    void SetGizmoSpace(GizmoSpace space) { m_gizmoSpace = space; }
    EditorCameraState &GetEditorCamera() { return m_editorCamera; }
    const EditorCameraState &GetEditorCamera() const { return m_editorCamera; }
    const std::vector<std::unique_ptr<PlutoGE::render::IPostProcessEffect>> &GetEditorPostProcessEffects() const { return m_editorPostProcessEffects; }
    bool GetDebugShapesVisible() const { return m_debugShapes; }
    bool IsSnapEnabled() const { return m_snapEnabled; }
    float GetTranslateSnap() const { return m_translateSnap; }
    float GetRotateSnap() const { return m_rotateSnap; }
    float GetScaleSnap() const { return m_scaleSnap; }
    bool SetViewportStats(int submittedRenderCommands, int visibleRenderCommands);
    void Update(float deltaTime);

private:
    struct HistoryEntry
    {
        std::string before;
        std::string after;
    };

    bool SetScene(std::unique_ptr<PlutoGE::scene::Scene> scene, bool dirty);
    bool CaptureScene(std::string &state) const;
    bool RestoreScene(const std::string &state, bool dirty);
    bool CommitEdit(const std::string &before);
    bool LoadMeshEditorAsset(const std::string &reference, std::string &errorMessage);
    void ClearMeshEditorAsset();
    PlutoGE::scene::Entity *FindEntity(std::uint32_t id) const;
    void ReportOperationProgress(int percent, const std::string &detail) const;

    PlutoGE::core::Engine &m_engine;
    std::unique_ptr<PlutoGE::scene::Scene> m_scene;
    std::unique_ptr<PlutoGE::assets::Project> m_project;
    OperationProgressCallback m_operationProgressCallback;
    std::uint32_t m_selectedEntityId = 0;
    std::string m_projectPath;
    std::string m_scenePath;
    std::string m_runtimeSnapshot;
    bool m_dirty = false;
    EditorCameraState m_editorCamera;
    std::vector<std::unique_ptr<PlutoGE::render::IPostProcessEffect>> m_editorPostProcessEffects;
    std::string m_editorPostProcessPresetReference;
    int m_submittedRenderCommands = 0;
    int m_visibleRenderCommands = 0;
    std::vector<HistoryEntry> m_undo;
    std::vector<HistoryEntry> m_redo;
    std::unique_ptr<PlutoGE::scene::SceneBakeTask> m_bakeTask;
    std::string m_bakeStatus;
    std::uint32_t m_copiedEntityId = 0;
    GizmoOperation m_gizmoOperation = GizmoOperation::Translate;
    GizmoSpace m_gizmoSpace = GizmoSpace::Local;
    std::string m_gizmoEditBefore;
    bool m_gizmoEditActive = false;
    bool m_debugShapes = true;
    bool m_snapEnabled = false;
    float m_translateSnap = 1.0f;
    float m_rotateSnap = 15.0f;
    float m_scaleSnap = 0.1f;
    std::string m_meshEditorReference;
    std::string m_meshEditorSourcePath;
    PlutoGE::render::MeshConfig m_meshEditorConfig;
    PlutoGE::render::MeshBounds m_meshEditorBounds;
    std::vector<std::string> m_meshEditorMaterialReferences;
    PlutoGE::assets::MeshAssetMetadata m_meshEditorMetadata;
    bool m_meshEditorDirty = false;
};
