#pragma once

#include "PlutoGE/render/Camera.h"
#include "PlutoGE/scene/TerrainHeightData.h"
#include "PlutoGE/scene/TerrainHoleMask.h"
#include "PlutoGE/scene/TerrainWeightMap.h"
#include "PlutoGE/scene/TerrainWorldGrid.h"
#include "PlutoGE/ui/BrushStrokeSampler.h"
#include "PlutoGE/ui/TerrainEditTool.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

namespace PlutoGE::scene
{
    class TerrainVolumeComponent;
}

namespace PlutoGE::ui
{
    class EditorShell;

    // The Sculpt / Paint / Holes buttons, wherever the terrain tool is offered. Clicking the active mode turns it off.
    void DrawTerrainToolModeButtons(TerrainEditTool &tool, const TerrainEditTarget &target);
    // The brush settings of the tool's current mode (sculpt mode and strength, the layer to paint...).
    void DrawTerrainBrushSettings(TerrainEditTool &tool, const TerrainEditTarget &target, bool compact);

    struct TerrainViewportContext
    {
        const render::CameraData &camera;
        std::optional<glm::vec3> rayOrigin;
        std::optional<glm::vec3> rayDirection;
        bool hovered = false;
        bool controlsHovered = false;
        float deltaTime = 0.0f;
        // Viewport drawing: a world-space circle from a centre and two axes.
        std::function<void(const glm::vec3 &, const glm::vec3 &, const glm::vec3 &, ImU32)> drawCircle;
    };

    // The terrain tool's viewport half: the tool bar, the brush, and strokes over the selected terrain or world (each
    // stroke one undo step). While the tool is on it owns the mouse, so the viewport's gizmo and picking stand aside.
    class TerrainViewportTool
    {
    public:
        // Every frame, before anything else: follows the selection and finishes a stroke the mouse has let go of.
        void BeginFrame(EditorShell &shell, bool mouseDown);
        // Draws the tool bar over the viewport. Returns true when the mouse is on it.
        bool RenderToolBar(EditorShell &shell, const ImVec2 &viewportMin, const ImVec2 &viewportSize, ImGuiID platformViewport);
        // Runs the brush. Returns false when the tool is off (the viewport carries on as usual).
        bool Update(EditorShell &shell, const TerrainViewportContext &context);
        [[nodiscard]] static bool IsActive(EditorShell &shell);

    private:
        // A terrain a stroke touched, found again later by entity or, for a world's tile, by world and tile.
        struct TerrainRef
        {
            scene::EntityID entity = 0;
            scene::EntityID world = 0;
            scene::TerrainTileCoord tile;
        };
        struct Touched
        {
            scene::TerrainComponent *terrain = nullptr; // valid while the stroke runs
            TerrainRef ref;
            int width = 0;
            std::vector<float> heightsBefore;
            scene::TerrainSampleRect heights;
            std::optional<scene::TerrainWeightMap> weightsBefore;
            scene::TerrainWeightRect weights;
            std::optional<scene::TerrainHoleMask> holesBefore;
        };

        // A terrain volume a Dig stroke touched, with its edits before the stroke.
        struct TouchedVolume
        {
            scene::TerrainVolumeComponent *volume = nullptr; // valid while the stroke runs
            TerrainRef ref;
            std::vector<std::uint8_t> before;
        };

        Touched &Touch(scene::TerrainComponent &terrain, const TerrainEditTarget &target);
        void TouchVolume(EditorShell &shell, scene::TerrainVolumeComponent &volume, const TerrainEditTarget &target);
        // The Dig tool: its cursor (on dug ground too), dabs and stroke.
        bool UpdateDig(EditorShell &shell, const TerrainViewportContext &context, const TerrainEditTarget &target);
        void FinishDigStroke(EditorShell &shell, std::vector<TouchedVolume> touched);
        void FinishStroke(EditorShell &shell);

        std::vector<Touched> m_touched;
        std::vector<TouchedVolume> m_touchedVolumes;
        float m_digTimer = 0.0f;
        TerrainToolMode m_strokeMode = TerrainToolMode::Off;
        scene::EntityID m_strokeRoot = 0;
        bool m_active = false;
        bool m_sceneEditStroke = false; // recorded as a scene edit (holes; heights without a height file)
        bool m_blockedUntilRelease = false;
        BrushStrokeSampler m_sampler;
        std::uint64_t m_sceneRevision = 0;
    };
}
