#pragma once

#include "PlutoGE/render/Mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <vector>

// A flat picture of a mesh's skeleton for the rig editor: the bind-pose joints projected
// front-on or side-on over the mesh's silhouette. No ImGui here, so it can be tested.
namespace PlutoGE::ui::meshedit
{
    enum class RigView
    {
        Front, // facing the viewer: the character's left is on the viewer's right
        Side,  // seen from the character's right: it faces the viewer's right
    };

    // The character's axes in mesh space, from the bind pose.
    struct CharacterFrame
    {
        glm::vec3 up{0.0f, 1.0f, 0.0f};
        glm::vec3 left{1.0f, 0.0f, 0.0f};
        glm::vec3 forward{0.0f, 0.0f, 1.0f};
    };

    // Where the mesh covers the diagram, as a grid over RigDiagram::min..max. Row 0 is the top.
    struct RigSilhouette
    {
        int columns = 0;
        int rows = 0;
        std::vector<std::uint8_t> covered;

        [[nodiscard]] bool IsCovered(int column, int row) const
        {
            return column >= 0 && row >= 0 && column < columns && row < rows &&
                   covered[static_cast<std::size_t>(row) * static_cast<std::size_t>(columns) + static_cast<std::size_t>(column)] != 0;
        }
    };

    struct RigDiagram
    {
        RigView view = RigView::Front;
        CharacterFrame frame;
        // Per joint, in diagram units (mesh units): x to the viewer's right, y up.
        std::vector<glm::vec2> joints;
        // Per joint, toward the viewer, so nearer joints can draw on top.
        std::vector<float> depth;
        glm::vec2 min{0.0f};
        glm::vec2 max{0.0f};
        RigSilhouette silhouette;

        [[nodiscard]] glm::vec2 Size() const { return max - min; }
    };

    // Joint positions in the space of the mesh's vertices in its bind pose.
    [[nodiscard]] std::vector<glm::vec3> ComputeJointBindPositions(const render::Skeleton &skeleton);

    // Up from the hips to the head, left from the right leg (or arm) to the left one. Uses the humanoid map when
    // it has the joints and joint names otherwise; falls back to Y up with the character facing +Z.
    [[nodiscard]] CharacterFrame EstimateCharacterFrame(const render::Skeleton &skeleton, std::span<const glm::vec3> positions);

    // `silhouetteRows` sets the silhouette grid's resolution; 0 skips it.
    [[nodiscard]] RigDiagram BuildRigDiagram(const render::MeshConfig &mesh, RigView view, int silhouetteRows = 160);

    // Joints within `radius` of `point` (diagram units), nearest first. Joints that share a spot (a split pelvis's
    // roots often do) all come back, so the caller can let the user choose.
    [[nodiscard]] std::vector<int> PickRigJoints(const RigDiagram &diagram, glm::vec2 point, float radius);
}
