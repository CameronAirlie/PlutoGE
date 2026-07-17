#pragma once

#include <glm/glm.hpp>

#include <cstdint>
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
}

namespace PlutoGE::render
{
    class IPostProcessEffect;
}

class EditorSession
{
public:
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
    std::string BuildSnapshotEvent() const;
    PlutoGE::scene::Scene *GetScene() const;
    EditorCameraState &GetEditorCamera() { return m_editorCamera; }
    const EditorCameraState &GetEditorCamera() const { return m_editorCamera; }
    const std::vector<std::unique_ptr<PlutoGE::render::IPostProcessEffect>> &GetEditorPostProcessEffects() const { return m_editorPostProcessEffects; }
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
    PlutoGE::scene::Entity *FindEntity(std::uint32_t id) const;

    PlutoGE::core::Engine &m_engine;
    std::unique_ptr<PlutoGE::scene::Scene> m_scene;
    std::unique_ptr<PlutoGE::assets::Project> m_project;
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
};
