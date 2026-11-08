#pragma once

#include "PlutoGE/render/HumanoidRigAnalysis.h"
#include "PlutoGE/ui/meshedit/HumanoidRigDiagram.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace PlutoGE::assets
{
    class Project;
}

namespace PlutoGE::ui
{
    class MeshAssetSession;
}

// The mesh editor's pages. Pages keep their own UI state and edit through MeshAssetSession, so every change is
// undoable.
namespace PlutoGE::ui::meshedit
{
    enum class MeshEditorPage
    {
        Overview,
        Materials,
        HumanoidRig,
        LevelOfDetail,
    };

    // The humanoid map and the rig, rebuilt only when the session changes.
    struct HumanoidRigAnalysisCache
    {
        std::uint64_t revision = 0;
        render::HumanoidRigStatus status;

        void Update(const MeshAssetSession &session);
        // Problems about `bone`'s slot.
        [[nodiscard]] std::vector<const render::HumanoidRigProblem *> ProblemsFor(render::HumanoidBone bone) const;
    };

    struct HumanoidRigViewState
    {
        render::HumanoidBone selected = render::HumanoidBone::Hips; // Count when none
        RigView view = RigView::Front;
        bool showAllJointNames = false;
        bool advanceAfterAssign = true; // select the next empty slot after assigning a joint

        // Canvas navigation: zoom multiplies the fitted scale, pan is in diagram units.
        float zoom = 1.0f;
        glm::vec2 pan{0.0f};

        // Joint being previewed from the joint picker or the slot list, -1 for none.
        int highlightedJoint = -1;
        // Joints sharing the spot that was clicked, waiting for the user to choose one.
        std::vector<int> pickChoices;
        int contextJoint = -1;
        std::string notice; // the last assignment's side effect ("Moved hand_l from Left Lower Arm.")
        std::string jointSearch;

        // Derived from the geometry, rebuilt when it or the view changes.
        RigDiagram diagram;
        std::uint64_t diagramRevision = 0;
        RigView diagramView = RigView::Front;
        std::vector<std::pair<int, int>> jointTree; // (joint, depth) in hierarchy order
    };

    void DrawOverviewPage(const MeshAssetSession &session, const HumanoidRigAnalysisCache &rig, MeshEditorPage &page);
    void DrawMaterialsPage(MeshAssetSession &session, const assets::Project *project);
    void DrawHumanoidRigPage(MeshAssetSession &session, HumanoidRigViewState &state, const HumanoidRigAnalysisCache &rig);
    // `error` receives a failed reimport's message.
    void DrawLevelOfDetailPage(MeshAssetSession &session, std::string &error);
}
