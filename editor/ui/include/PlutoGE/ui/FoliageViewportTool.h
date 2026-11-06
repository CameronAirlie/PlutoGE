#pragma once

#include "PlutoGE/render/Camera.h"
#include "PlutoGE/ui/BrushStrokeSampler.h"
#include "PlutoGE/ui/FoliageEditTool.h"

#include <ImGuizmo.h>
#include <glm/glm.hpp>
#include <imgui.h>

#include <functional>
#include <optional>

namespace PlutoGE::scene
{
    class Entity;
    class TerrainComponent;
}

namespace PlutoGE::ui
{
    class EditorShell;

    // The Paint / Erase / Select buttons, wherever the tool is offered. Clicking the active mode turns the tool off.
    void DrawFoliageToolModeButtons(FoliageEditTool &tool, scene::EntityID entity);

    struct FoliageViewportContext
    {
        scene::Entity &entity;
        scene::FoliageComponent &foliage;
        scene::TerrainComponent *terrain = nullptr;
        const render::CameraData &camera;
        const glm::mat4 &gizmoProjection;
        // The ray under the mouse, when the viewport can build one.
        std::optional<glm::vec3> rayOrigin;
        std::optional<glm::vec3> rayDirection;
        bool hovered = false;
        bool controlsHovered = false;
        bool clicked = false;
        ImGuizmo::OPERATION gizmoOperation = ImGuizmo::TRANSLATE;
        ImGuizmo::MODE gizmoMode = ImGuizmo::LOCAL;
        const float *gizmoSnap = nullptr;
        // Viewport drawing: world-space line, and circle from a center and two axes.
        std::function<void(const glm::vec3 &, const glm::vec3 &, ImU32)> drawLine;
        std::function<void(const glm::vec3 &, const glm::vec3 &, const glm::vec3 &, ImU32)> drawCircle;
    };

    // The foliage tool's viewport half: the tool bar, brush strokes, instance picking and the instance gizmo. While the
    // tool is on it owns the mouse, so the viewport's entity gizmo and picking stand aside.
    class FoliageViewportTool
    {
    public:
        // Every frame, before anything else: follows the editor selection and finishes a stroke the mouse has left.
        void BeginFrame(EditorShell &shell, bool mouseDown, bool hovered, bool controlsHovered);
        // Draws the tool bar over the viewport. Returns true when the mouse is on it.
        bool RenderToolBar(EditorShell &shell, const ImVec2 &viewportMin, const ImVec2 &viewportSize, ImGuiID platformViewport);
        // Runs the tool for the selected entity. Returns true when a gizmo is being dragged.
        bool Update(EditorShell &shell, const FoliageViewportContext &context);
        // Whether the tool is on for this entity, so the viewport should hand it the frame.
        [[nodiscard]] static bool IsActiveFor(EditorShell &shell, const scene::Entity &entity);

    private:
        void UpdateBrush(EditorShell &shell, const FoliageViewportContext &context);
        bool UpdateSelection(EditorShell &shell, const FoliageViewportContext &context);
        void FinishStroke(EditorShell &shell, bool mouseDown);

        struct Stroke
        {
            scene::FoliageInstanceSnapshot before;
            BrushStrokeSampler sampler;
            scene::FoliageBrushMode mode = scene::FoliageBrushMode::Add;
            scene::EntityID entity = 0;
            int type = -1;
            bool active = false;
            bool sampledThisFrame = false;
            bool blockedUntilRelease = false;
        } m_stroke;
        std::uint64_t m_sceneRevision = 0;

        bool m_gizmoDragging = false;
        glm::mat4 m_gizmoStart{1.0f};
        glm::mat4 m_gizmoMatrix{1.0f};
    };
}
