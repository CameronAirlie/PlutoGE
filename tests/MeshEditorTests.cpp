#include "PlutoGE/render/HumanoidRigAnalysis.h"
#include "PlutoGE/ui/EditorFonts.h"
#include "PlutoGE/ui/EditorTheme.h"
#include "PlutoGE/ui/meshedit/HumanoidRigDiagram.h"
#include "PlutoGE/ui/meshedit/HumanoidRigEditing.h"
#include "PlutoGE/ui/meshedit/MeshAssetSession.h"
#include "PlutoGE/ui/meshedit/MeshEditorViews.h"
#include "PlutoGE/ui/panels/MeshEditorPanel.h"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Drives the mesh editor: humanoid map analysis and auto mapping (Rigify, a pelvis named like a spine, a pelvis
// split into separate roots), the rig editing helpers, the rig diagram, the editing session (undo, saving,
// reimport) and every page in a hidden OpenGL window. With an output prefix argument, writes a .ppm of each page
// for visual QA.
namespace
{
    using namespace PlutoGE;
    using Bone = render::HumanoidBone;

    constexpr int kWidth = 1280;
    constexpr int kHeight = 800;

    void Require(bool value, const std::string &message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    struct JointSpec
    {
        const char *name;
        const char *parent;
        glm::vec3 position; // bind position in mesh space
    };

    // A skeleton from bind positions, with inverse bind matrices as an importer writes them.
    render::Skeleton MakeSkeleton(std::initializer_list<JointSpec> specs, const glm::mat4 &basis = glm::mat4(1.0f))
    {
        render::Skeleton skeleton;
        for (const auto &spec : specs)
        {
            render::SkeletonJoint joint;
            joint.name = spec.name;
            joint.nodeIndex = static_cast<int>(skeleton.joints.size());
            if (spec.parent)
                for (std::size_t index = 0; index < skeleton.joints.size(); ++index)
                    if (skeleton.joints[index].name == spec.parent)
                        joint.parentJointIndex = static_cast<int>(index);
            const glm::vec3 position = glm::vec3(basis * glm::vec4(spec.position, 1.0f));
            const glm::mat4 global = glm::translate(glm::mat4(1.0f), position);
            joint.inverseBindMatrix = glm::inverse(global);
            const glm::mat4 parentGlobal = joint.parentJointIndex >= 0
                                               ? glm::inverse(skeleton.joints[static_cast<std::size_t>(joint.parentJointIndex)].inverseBindMatrix)
                                               : glm::mat4(1.0f);
            joint.localBindTransform = glm::inverse(parentGlobal) * global;
            skeleton.joints.push_back(joint);
        }
        return skeleton;
    }

    int Joint(const render::Skeleton &skeleton, const std::string &name)
    {
        for (std::size_t index = 0; index < skeleton.joints.size(); ++index)
            if (skeleton.joints[index].name == name)
                return static_cast<int>(index);
        throw std::runtime_error("Test joint " + name + " exists");
    }

    std::string MappedName(const render::Skeleton &skeleton, Bone bone)
    {
        const int joint = render::GetHumanoidTargetJoint(skeleton, bone);
        return joint >= 0 ? skeleton.joints[static_cast<std::size_t>(joint)].name : std::string("<none>");
    }

    // Blender Rigify deform bones (prefix removed on purpose: names come with and without "DEF-"). Y up, facing +Z,
    // the character's left on +X.
    render::Skeleton MakeRigifySkeleton()
    {
        return MakeSkeleton({
            {"DEF-spine", nullptr, {0.0f, 1.00f, 0.0f}},
            {"DEF-spine.001", "DEF-spine", {0.0f, 1.12f, 0.0f}},
            {"DEF-spine.002", "DEF-spine.001", {0.0f, 1.25f, 0.0f}},
            {"DEF-spine.003", "DEF-spine.002", {0.0f, 1.38f, 0.0f}},
            {"DEF-spine.004", "DEF-spine.003", {0.0f, 1.52f, 0.0f}},
            {"DEF-spine.005", "DEF-spine.004", {0.0f, 1.58f, 0.0f}},
            {"DEF-spine.006", "DEF-spine.005", {0.0f, 1.65f, 0.0f}},
            {"DEF-pelvis.L", "DEF-spine", {0.05f, 1.00f, 0.02f}},
            {"DEF-pelvis.R", "DEF-spine", {-0.05f, 1.00f, 0.02f}},
            {"DEF-thigh.L", "DEF-spine", {0.10f, 0.95f, 0.0f}},
            {"DEF-shin.L", "DEF-thigh.L", {0.10f, 0.52f, 0.0f}},
            {"DEF-foot.L", "DEF-shin.L", {0.10f, 0.08f, 0.0f}},
            {"DEF-toe.L", "DEF-foot.L", {0.10f, 0.02f, 0.12f}},
            {"DEF-thigh.R", "DEF-spine", {-0.10f, 0.95f, 0.0f}},
            {"DEF-shin.R", "DEF-thigh.R", {-0.10f, 0.52f, 0.0f}},
            {"DEF-foot.R", "DEF-shin.R", {-0.10f, 0.08f, 0.0f}},
            {"DEF-toe.R", "DEF-foot.R", {-0.10f, 0.02f, 0.12f}},
            {"DEF-shoulder.L", "DEF-spine.003", {0.04f, 1.45f, 0.0f}},
            {"DEF-upper_arm.L", "DEF-shoulder.L", {0.18f, 1.45f, 0.0f}},
            {"DEF-forearm.L", "DEF-upper_arm.L", {0.45f, 1.45f, 0.0f}},
            {"DEF-hand.L", "DEF-forearm.L", {0.70f, 1.45f, 0.0f}},
            {"DEF-shoulder.R", "DEF-spine.003", {-0.04f, 1.45f, 0.0f}},
            {"DEF-upper_arm.R", "DEF-shoulder.R", {-0.18f, 1.45f, 0.0f}},
            {"DEF-forearm.R", "DEF-upper_arm.R", {-0.45f, 1.45f, 0.0f}},
            {"DEF-hand.R", "DEF-forearm.R", {-0.70f, 1.45f, 0.0f}},
        });
    }

    // The ThirdPersonProject "skeleton": torso and each leg hang from separate roots.
    render::Skeleton MakeSplitRootSkeleton(const glm::mat4 &basis = glm::mat4(1.0f))
    {
        return MakeSkeleton({
                                {"Torso", nullptr, {0.0f, 1.0f, 0.0f}},
                                {"Neck", "Torso", {0.0f, 1.5f, 0.0f}},
                                {"Head", "Neck", {0.0f, 1.62f, 0.0f}},
                                {"Shoulder.L", "Torso", {0.06f, 1.45f, 0.0f}},
                                {"UpperArm.L", "Shoulder.L", {0.18f, 1.45f, 0.0f}},
                                {"LowerArm.L", "UpperArm.L", {0.45f, 1.45f, 0.0f}},
                                {"Hand.L", "LowerArm.L", {0.7f, 1.45f, 0.0f}},
                                {"Shoulder.R", "Torso", {-0.06f, 1.45f, 0.0f}},
                                {"UpperArm.R", "Shoulder.R", {-0.18f, 1.45f, 0.0f}},
                                {"LowerArm.R", "UpperArm.R", {-0.45f, 1.45f, 0.0f}},
                                {"Hand.R", "LowerArm.R", {-0.7f, 1.45f, 0.0f}},
                                {"pelvis.L", nullptr, {0.0f, 1.0f, 0.0f}},
                                {"UpperLeg.L", "pelvis.L", {0.1f, 0.95f, 0.0f}},
                                {"LowerLeg.L", "UpperLeg.L", {0.1f, 0.52f, 0.0f}},
                                {"Foot.L", "LowerLeg.L", {0.1f, 0.08f, 0.0f}},
                                {"pelvis.R", nullptr, {0.0f, 1.0f, 0.0f}},
                                {"UpperLeg.R", "pelvis.R", {-0.1f, 0.95f, 0.0f}},
                                {"LowerLeg.R", "UpperLeg.R", {-0.1f, 0.52f, 0.0f}},
                                {"Foot.R", "LowerLeg.R", {-0.1f, 0.08f, 0.0f}},
                            },
                            basis);
    }

    void AddBox(render::MeshConfig &mesh, glm::vec3 a, glm::vec3 b, float radius)
    {
        const glm::vec3 lo = glm::min(a, b) - glm::vec3(radius), hi = glm::max(a, b) + glm::vec3(radius);
        const auto base = static_cast<unsigned int>(mesh.data.vertices.size());
        for (int corner = 0; corner < 8; ++corner)
        {
            render::MeshVertexData vertex{};
            vertex.position = {corner & 1 ? hi.x : lo.x, corner & 2 ? hi.y : lo.y, corner & 4 ? hi.z : lo.z};
            mesh.data.vertices.push_back(vertex);
        }
        constexpr unsigned int faces[12][3] = {{0, 1, 3}, {0, 3, 2}, {4, 6, 7}, {4, 7, 5}, {0, 4, 5}, {0, 5, 1},
                                               {2, 3, 7}, {2, 7, 6}, {0, 2, 6}, {0, 6, 4}, {1, 5, 7}, {1, 7, 3}};
        for (const auto &face : faces)
            for (const unsigned int index : face)
                mesh.data.indices.push_back(base + index);
    }

    // A mesh skinned to `skeleton`: a box along every bone, one submesh per material slot and two LODs.
    render::MeshConfig MakeCharacterMesh(render::Skeleton skeleton)
    {
        render::MeshConfig mesh;
        const auto positions = ui::meshedit::ComputeJointBindPositions(skeleton);
        for (std::size_t index = 0; index < skeleton.joints.size(); ++index)
        {
            const int parent = skeleton.joints[index].parentJointIndex;
            if (parent >= 0)
                AddBox(mesh, positions[static_cast<std::size_t>(parent)], positions[index], 0.035f);
        }
        const auto count = static_cast<std::uint32_t>(mesh.data.indices.size());
        const std::uint32_t half = (count / 6) * 3;
        render::Submesh body;
        body.indexOffset = 0;
        body.indexCount = half;
        body.materialIndex = 0;
        body.lods = {{.indexOffset = 0, .indexCount = half, .maxScreenRadiusPixels = std::numeric_limits<float>::max()},
                     {.indexOffset = 0, .indexCount = half / 3 - (half / 3) % 3, .maxScreenRadiusPixels = 120.0f}};
        render::Submesh limbs = body;
        limbs.indexOffset = half;
        limbs.indexCount = count - half;
        limbs.materialIndex = 1;
        limbs.lods = {{.indexOffset = half, .indexCount = count - half, .maxScreenRadiusPixels = std::numeric_limits<float>::max()}};
        mesh.submeshes = {body, limbs};
        mesh.skeleton = std::move(skeleton);
        return mesh;
    }

    // ---------------------------------------------------------------------------------------------- engine rig

    void TestAutoMap()
    {
        auto rigify = MakeRigifySkeleton();
        render::AutoMapHumanoidRig(rigify);
        const std::pair<Bone, const char *> expected[] = {
            {Bone::Hips, "DEF-spine"}, {Bone::Spine, "DEF-spine.001"}, {Bone::Chest, "DEF-spine.002"}, {Bone::UpperChest, "DEF-spine.003"},
            {Bone::Neck, "DEF-spine.004"}, {Bone::Head, "DEF-spine.006"}, {Bone::LeftLowerArm, "DEF-forearm.L"},
            {Bone::RightLowerLeg, "DEF-shin.R"}, {Bone::LeftToes, "DEF-toe.L"}, {Bone::RightUpperArm, "DEF-upper_arm.R"},
        };
        for (const auto &[bone, name] : expected)
            Require(MappedName(rigify, bone) == name, std::string("Rigify ") + render::HumanoidBoneName(bone) + " maps to " + name + ", got " + MappedName(rigify, bone));
        Require(ui::meshedit::FindHumanoidBoneForJoint(rigify, Joint(rigify, "DEF-pelvis.L")) == Bone::Count, "Rigify side pelvis bones take no slot");
        const auto rigifyStatus = render::AnalyzeHumanoidRig(rigify);
        Require(rigifyStatus.IsReady(), "An auto-mapped Rigify rig is ready, problems: " + std::to_string(rigifyStatus.problems.size()));
        Require(rigifyStatus.hipsFollowers.empty(), "Rigify's pelvis carries the legs, so nothing follows it");

        // A pelvis named "spine" outside Rigify: the joint carrying both legs moves from Spine to Hips.
        auto spineHips = MakeSkeleton({{"spine", nullptr, {0, 1, 0}}, {"chest", "spine", {0, 1.3f, 0}}, {"neck", "chest", {0, 1.5f, 0}},
                                       {"head", "neck", {0, 1.6f, 0}}, {"thigh.L", "spine", {0.1f, 0.95f, 0}}, {"thigh.R", "spine", {-0.1f, 0.95f, 0}}});
        render::AutoMapHumanoidRig(spineHips);
        Require(MappedName(spineHips, Bone::Hips) == "spine" && MappedName(spineHips, Bone::Spine) == "chest" &&
                    render::GetHumanoidTargetJoint(spineHips, Bone::Chest) < 0,
                "A pelvis named spine becomes Hips and the spine slots move down, got Hips " + MappedName(spineHips, Bone::Hips) +
                    ", Spine " + MappedName(spineHips, Bone::Spine));

        // Split roots: Hips goes on the torso root and the leg roots follow it.
        auto split = MakeSplitRootSkeleton();
        render::AutoMapHumanoidRig(split);
        Require(MappedName(split, Bone::Hips) == "Torso", "A split pelvis maps Hips to the torso root, got " + MappedName(split, Bone::Hips));
        const auto splitStatus = render::AnalyzeHumanoidRig(split);
        Require(splitStatus.IsReady(), "An auto-mapped split pelvis is ready");
        Require(splitStatus.hipsFollowers == std::vector<int>{Joint(split, "pelvis.L"), Joint(split, "pelvis.R")},
                "Both leg roots follow Hips");

        // Hips on one side of the split pelvis is explained once, with the joint to use instead.
        render::FindHumanoidMapping(split, Bone::Hips)->targetJointIndex = Joint(split, "pelvis.L");
        const auto problems = render::AnalyzeHumanoidRigProblems(split);
        Require(problems.size() == 1 && problems[0].bone == Bone::Hips &&
                    problems[0].message.find("only the left leg") != std::string::npos && problems[0].message.find("(Torso)") != std::string::npos,
                "Hips on pelvis.L reports one split-pelvis problem naming Torso, got " + std::to_string(problems.size()) +
                    (problems.empty() ? std::string{} : ": " + problems[0].message));

        // A joint in two slots is a problem on the second slot.
        auto duplicate = MakeSplitRootSkeleton();
        render::AutoMapHumanoidRig(duplicate);
        render::FindHumanoidMapping(duplicate, Bone::RightHand)->targetJointIndex = Joint(duplicate, "Hand.L");
        const auto duplicateProblems = render::AnalyzeHumanoidRigProblems(duplicate);
        Require(std::ranges::any_of(duplicateProblems, [](const auto &problem) { return problem.bone == Bone::RightHand && problem.message.find("both use Hand.L") != std::string::npos; }),
                "A joint used twice is reported on the later slot");
        Require(!render::AnalyzeHumanoidRig(duplicate).IsReady(), "A rig with problems is not ready");

        // The map keeps one slot per bone.
        render::Skeleton messy;
        messy.humanoidBoneMappings = {{.bone = Bone::Head, .targetJointIndex = 3}, {.bone = Bone::Head, .targetJointIndex = 4}, {.bone = Bone::Count}};
        render::EnsureHumanoidMappings(messy);
        Require(messy.humanoidBoneMappings.size() == render::kHumanoidBoneCount && messy.humanoidBoneMappings[0].targetJointIndex == 3,
                "EnsureHumanoidMappings drops duplicates and invalid slots and keeps the first choice");
    }

    // ---------------------------------------------------------------------------------------------- editing helpers

    void TestEditing()
    {
        namespace me = ui::meshedit;
        const std::pair<const char *, const char *> mirrored[] = {
            {"hand.L", "hand.R"}, {"DEF-thigh.R", "DEF-thigh.L"}, {"mixamorig:LeftArm", "mixamorig:RightArm"}, {"hand_l", "hand_r"},
            {"Bip01 L Thigh", "Bip01 R Thigh"}, {"lShldr", "rShldr"}, {"L_Elbow", "R_Elbow"}, {"upperarm_twist_01_l", "upperarm_twist_01_r"},
        };
        for (const auto &[from, to] : mirrored)
            Require(me::MirrorJointName(from) == std::optional<std::string>(to), std::string("Mirror of ") + from + " is " + to + ", got " + me::MirrorJointName(from).value_or("<none>"));
        Require(!me::MirrorJointName("Head") && !me::MirrorJointName("spine.003") && !me::MirrorJointName("L"), "Names without a side do not mirror");

        Require(me::MirrorHumanoidBone(Bone::LeftToes) == Bone::RightToes && me::MirrorHumanoidBone(Bone::RightShoulder) == Bone::LeftShoulder &&
                    me::MirrorHumanoidBone(Bone::Neck) == Bone::Neck,
                "Bones mirror across the body");
        std::size_t listed = 0;
        for (const auto &part : me::GetHumanoidBodyParts())
            listed += part.bones.size();
        Require(listed == render::kHumanoidBoneCount, "Every bone is listed once");

        auto skeleton = MakeSplitRootSkeleton();
        Require(me::AssignHumanoidJoint(skeleton, Bone::LeftHand, Joint(skeleton, "Hand.L")) == Bone::Count, "Assigning a free joint moves nothing");
        Require(skeleton.humanoidBoneMappings.size() == render::kHumanoidBoneCount, "Assigning creates the map");
        Require(me::AssignHumanoidJoint(skeleton, Bone::RightHand, Joint(skeleton, "Hand.L")) == Bone::LeftHand &&
                    render::GetHumanoidTargetJoint(skeleton, Bone::LeftHand) < 0 && MappedName(skeleton, Bone::RightHand) == "Hand.L",
                "Assigning a used joint moves it from its old slot");
        Require(me::NextUnassignedHumanoidBone(skeleton, Bone::RightHand) == Bone::LeftUpperLeg, "The next empty slot follows list order");
        Require(me::NextUnassignedHumanoidBone(skeleton, Bone::RightToes) == Bone::Hips, "The search wraps round");

        me::AssignHumanoidJoint(skeleton, Bone::RightHand, -1);
        for (const auto &[bone, name] : {std::pair{Bone::LeftShoulder, "Shoulder.L"}, {Bone::LeftUpperArm, "UpperArm.L"}, {Bone::LeftLowerArm, "LowerArm.L"},
                                         {Bone::LeftHand, "Hand.L"}, {Bone::RightUpperLeg, "UpperLeg.R"}})
            me::AssignHumanoidJoint(skeleton, bone, Joint(skeleton, name));
        Require(me::MirrorHumanoidRig(skeleton) == 5, "Mirroring fills the four right arm slots and the left upper leg");
        Require(MappedName(skeleton, Bone::RightLowerArm) == "LowerArm.R" && MappedName(skeleton, Bone::LeftUpperLeg) == "UpperLeg.L",
                "Mirrored slots get the other side's joints");
        Require(me::MirrorHumanoidRig(skeleton) == 0, "Mirroring again changes nothing");
    }

    // ---------------------------------------------------------------------------------------------- diagram

    void TestDiagram()
    {
        namespace me = ui::meshedit;
        const auto mesh = MakeCharacterMesh(MakeSplitRootSkeleton());
        const auto &skeleton = mesh.skeleton;
        const auto front = me::BuildRigDiagram(mesh, me::RigView::Front);
        const auto at = [&](const me::RigDiagram &diagram, const char *name) { return diagram.joints[static_cast<std::size_t>(Joint(skeleton, name))]; };
        Require(at(front, "Hand.L").x > 0.5f && at(front, "Hand.R").x < -0.5f, "Front view puts the character's left on the viewer's right");
        Require(at(front, "Head").y > at(front, "Torso").y + 0.5f && at(front, "Foot.L").y < 0.2f, "Front view keeps the head up");
        Require(front.min.y < 0.1f && front.max.y > 1.6f && front.max.x > 0.7f, "Bounds cover the joints and mesh");

        // Silhouette: covered along the arm, empty between the legs below the pelvis.
        const auto &silhouette = front.silhouette;
        Require(silhouette.rows == 160 && silhouette.columns > 0, "The silhouette grid has the requested rows");
        const auto cellOf = [&](glm::vec2 point)
        {
            const float cell = front.Size().y / static_cast<float>(silhouette.rows);
            return std::pair{static_cast<int>((point.x - front.min.x) / cell), static_cast<int>((front.max.y - point.y) / cell)};
        };
        const auto [armColumn, armRow] = cellOf({0.3f, 1.45f});
        const auto [gapColumn, gapRow] = cellOf({0.0f, 0.4f});
        Require(silhouette.IsCovered(armColumn, armRow), "The silhouette covers the arm");
        Require(!silhouette.IsCovered(gapColumn, gapRow), "The silhouette leaves the gap between the legs empty");

        // Side view: the character faces the viewer's right, and joints sharing a spot all pick.
        auto toes = MakeRigifySkeleton();
        const auto side = me::BuildRigDiagram(MakeCharacterMesh(toes), me::RigView::Side, 0);
        Require(side.joints[static_cast<std::size_t>(Joint(toes, "DEF-toe.L"))].x > side.joints[static_cast<std::size_t>(Joint(toes, "DEF-foot.L"))].x + 0.05f,
                "Side view shows the toes ahead of the feet");
        Require(side.silhouette.rows == 0, "No silhouette when it is not asked for");
        const auto picked = me::PickRigJoints(front, at(front, "Torso"), 0.01f);
        Require(picked.size() == 3, "The torso and both pelvis roots share a spot and all pick, got " + std::to_string(picked.size()));

        // A Z-up rig (Blender's FBX export) draws the same upright picture.
        const glm::mat4 zUp = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        const auto rotatedMesh = MakeCharacterMesh(MakeSplitRootSkeleton(zUp));
        const auto rotated = me::BuildRigDiagram(rotatedMesh, me::RigView::Front);
        for (const char *name : {"Head", "Hand.L", "Foot.R"})
            Require(glm::distance(at(rotated, name), at(front, name)) < 1e-3f, std::string("A Z-up rig projects the same, ") + name);

        // Without skinning data the local bind transforms place the joints.
        auto unskinned = MakeSplitRootSkeleton();
        for (auto &joint : unskinned.joints)
            joint.inverseBindMatrix = glm::mat4(1.0f);
        const auto positions = me::ComputeJointBindPositions(unskinned);
        Require(glm::distance(positions[static_cast<std::size_t>(Joint(unskinned, "Hand.L"))], glm::vec3(0.7f, 1.45f, 0.0f)) < 1e-4f,
                "Local bind transforms place unskinned joints");
    }

    // ---------------------------------------------------------------------------------------------- session

    struct StorageState
    {
        ui::MeshAssetContent stored;
        render::MeshConfig reimported;
        std::string readOnlyReason;
        std::string source = "S:/Source/Hero.glb";
        int writes = 0;
    };

    class MemoryStorage final : public ui::MeshAssetStorage
    {
    public:
        explicit MemoryStorage(std::shared_ptr<StorageState> state) : m_state(std::move(state)) {}
        std::string GetLabel() const override { return "Hero.plutomesh"; }
        std::string GetReference() const override { return "project://Characters/Hero.plutomesh"; }
        std::string GetReadOnlyReason() const override { return m_state->readOnlyReason; }
        bool Read(ui::MeshAssetContent &content, std::string &) override
        {
            content = m_state->stored;
            return true;
        }
        bool Write(const ui::MeshAssetContent &content, std::string &) override
        {
            m_state->stored = content;
            ++m_state->writes;
            return true;
        }
        std::string GetSourcePath() const override { return m_state->source; }
        bool Reimport(const assetimport::MeshImportOptions &, render::MeshConfig &config, std::string &sourceReference, std::string &) override
        {
            config = m_state->reimported;
            sourceReference = "project://Source/Hero.glb";
            return true;
        }

    private:
        std::shared_ptr<StorageState> m_state;
    };

    std::shared_ptr<StorageState> MakeStorageState(render::Skeleton skeleton)
    {
        auto state = std::make_shared<StorageState>();
        state->stored.config = MakeCharacterMesh(std::move(skeleton));
        state->stored.materialReferences = {"project://Materials/Body.plutomaterial"}; // one slot short of the submeshes
        state->reimported = MakeCharacterMesh(MakeSplitRootSkeleton());
        state->reimported.submeshes.resize(1);
        return state;
    }

    void TestSession()
    {
        auto state = MakeStorageState(MakeSplitRootSkeleton());
        std::string error;
        auto session = ui::MeshAssetSession::Open(std::make_unique<MemoryStorage>(state), error);
        Require(session != nullptr, "The session opens: " + error);
        Require(!session->IsDirty() && session->GetEdits().materialReferences.size() == 2, "Opening shows every material slot without changing anything");
        Require(session->GetLodCount() == 2 && session->GetEdits().lodMaxScreenRadii[1] == 120.0f, "LOD thresholds come from the submeshes");

        session->ModifyRig(render::AutoMapHumanoidRig);
        session->EndFrame(false);
        Require(session->IsDirty() && MappedName(session->GetSkeleton(), Bone::Hips) == "Torso", "Rig edits show on the session's skeleton");
        session->Modify([](ui::MeshAssetEdits &edits) { edits.lodMaxScreenRadii[1] = 64.0f; });
        session->EndFrame(false);
        Require(session->GetMesh().submeshes[0].lods[1].maxScreenRadiusPixels == 64.0f, "LOD edits apply to the mesh");

        // A held control is one undo step.
        for (float value : {0.5f, 1.0f, 1.5f})
        {
            session->ModifyRig([&](render::Skeleton &skeleton) { render::FindHumanoidMapping(skeleton, Bone::Hips)->translationScale = value; });
            session->EndFrame(true);
        }
        session->EndFrame(false);
        session->Undo();
        Require(render::FindHumanoidMapping(session->GetSkeleton(), Bone::Hips)->translationScale == 1.0f, "A drag undoes in one step");
        session->Undo();
        Require(session->GetMesh().submeshes[0].lods[1].maxScreenRadiusPixels == 120.0f, "Undo restores the LOD threshold");
        session->Redo();
        const auto revision = session->GetRevision();
        session->Modify([](ui::MeshAssetEdits &) {});
        Require(session->GetRevision() == revision && session->CanRedo(), "An edit that changes nothing records nothing");

        Require(session->Save(error) && state->writes == 1 && !session->IsDirty(), "Saving writes the asset");
        Require(state->stored.config.skeleton.humanoidBoneMappings.size() == render::kHumanoidBoneCount &&
                    state->stored.config.submeshes[0].lods[1].maxScreenRadiusPixels == 64.0f && state->stored.materialReferences.size() == 2,
                "The saved file has the map, the threshold and both slots");

        session->Modify([](ui::MeshAssetEdits &edits) { edits.materialReferences[1] = "project://Materials/Limbs.plutomaterial"; });
        session->EndFrame(false);
        Require(session->Revert(error) && !session->IsDirty() && session->GetEdits().materialReferences[1].empty(), "Revert discards edits");

        // Reimport replaces the geometry, keeps the map, and cannot be undone except by reverting.
        const auto geometry = session->GetGeometryRevision();
        Require(session->CanReimport() && session->Reimport(error), "Reimport succeeds: " + error);
        Require(session->GetGeometryRevision() != geometry && session->IsDirty() && !session->CanUndo(), "Reimport is a new, unsaved geometry");
        Require(session->GetMesh().submeshes.size() == 1 && session->GetEdits().materialReferences.size() == 1 && session->GetLodCount() == 2,
                "Slots and levels follow the new geometry");
        Require(MappedName(session->GetSkeleton(), Bone::Hips) == "Torso", "Reimport keeps the humanoid map");
        Require(session->Revert(error) && !session->IsDirty() && session->GetMesh().submeshes.size() == 2, "Revert after a reimport reloads the saved file");

        // Read-only assets show but do not change.
        auto locked = MakeStorageState(MakeSplitRootSkeleton());
        locked->readOnlyReason = "Engine meshes are read-only.";
        auto readOnly = ui::MeshAssetSession::Open(std::make_unique<MemoryStorage>(locked), error);
        readOnly->ModifyRig(render::AutoMapHumanoidRig);
        Require(!readOnly->IsDirty() && !readOnly->CanReimport() && !readOnly->Save(error), "Read-only sessions refuse edits, reimports and saves");
    }

    // ---------------------------------------------------------------------------------------------- views

    struct Window
    {
        GLFWwindow *handle = nullptr;
        ui::fonts::EditorFontLibrary fonts;

        bool Create()
        {
            if (!glfwInit())
                return false;
            glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
            glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
            glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
            handle = glfwCreateWindow(kWidth, kHeight, "Mesh editor test", nullptr, nullptr);
            if (!handle)
                return false;
            glfwMakeContextCurrent(handle);
            if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
                return false;
            ImGui::CreateContext();
            ImGui::GetIO().IniFilename = nullptr;
            fonts.Load(ImGui::GetIO());
            ui::ApplyActiveEditorTheme();
            ImGui_ImplGlfw_InitForOpenGL(handle, false);
            ImGui_ImplOpenGL3_Init("#version 330");
            return true;
        }

        ~Window()
        {
            if (!handle)
                return;
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            glfwDestroyWindow(handle);
            glfwTerminate();
        }

        void Frame(const std::function<void()> &draw)
        {
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplGlfw_NewFrame();
            ImGui::GetIO().DisplaySize = ImVec2(kWidth, kHeight);
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(0, 0));
            ImGui::SetNextWindowSize(ImVec2(kWidth, kHeight));
            draw();
            ImGui::Render();
            glViewport(0, 0, kWidth, kHeight);
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }

        void Capture(const std::string &path) const
        {
            std::vector<unsigned char> pixels(kWidth * kHeight * 3);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, kWidth, kHeight, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
            std::ofstream file(path, std::ios::binary);
            file << "P6\n" << kWidth << " " << kHeight << "\n255\n";
            for (int y = kHeight - 1; y >= 0; --y)
                file.write(reinterpret_cast<const char *>(pixels.data() + y * kWidth * 3), kWidth * 3);
        }
    };

    void TestViews(const std::string &capturePrefix)
    {
        Window window;
        if (!window.Create())
        {
            std::cout << "MeshEditorTests: no OpenGL context, skipping views\n";
            return;
        }
        const auto capture = [&](const char *name)
        {
            if (!capturePrefix.empty())
                window.Capture(capturePrefix + name + ".ppm");
        };
        const auto frames = [&](const std::function<void()> &draw)
        {
            // A few frames settle layout (tables, child sizes) before capturing.
            for (int frame = 0; frame < 4; ++frame)
                window.Frame(draw);
        };

        auto state = MakeStorageState(MakeSplitRootSkeleton());
        std::string error;
        auto session = ui::MeshAssetSession::Open(std::make_unique<MemoryStorage>(state), error);
        auto *raw = session.get();
        ui::MeshEditorPanel panel({"Mesh Editor", true});
        panel.Open(std::move(session));
        const auto drawPanel = [&] { panel.Update(); };

        using Page = ui::meshedit::MeshEditorPage;
        panel.SetPage(Page::HumanoidRig);
        frames(drawPanel);
        capture("rig-empty");

        raw->ModifyRig(render::AutoMapHumanoidRig);
        raw->EndFrame(false);
        frames(drawPanel);
        capture("rig-split-pelvis");
        Require(panel.HasUnsavedChanges(), "The panel reports unsaved changes");

        // Problems show on the slot, and every page draws.
        raw->ModifyRig([](render::Skeleton &skeleton) { ui::meshedit::AssignHumanoidJoint(skeleton, Bone::Hips, 11); });
        raw->EndFrame(false);
        frames(drawPanel);
        capture("rig-problem");

        auto rigifyState = MakeStorageState(MakeRigifySkeleton());
        auto rigify = ui::MeshAssetSession::Open(std::make_unique<MemoryStorage>(rigifyState), error);
        rigify->ModifyRig(render::AutoMapHumanoidRig);
        rigify->EndFrame(false);
        panel.Open(std::move(rigify));
        panel.SetPage(Page::HumanoidRig);
        frames(drawPanel);
        capture("rig-rigify");

        for (const auto &[page, name] : {std::pair{Page::Overview, "overview"}, {Page::Materials, "materials"}, {Page::LevelOfDetail, "lod"}})
        {
            panel.SetPage(page);
            frames(drawPanel);
            capture(name);
        }

        // The rig page on its own with all names and the side view.
        auto standalone = ui::MeshAssetSession::Open(std::make_unique<MemoryStorage>(MakeStorageState(MakeRigifySkeleton())), error);
        standalone->ModifyRig(render::AutoMapHumanoidRig);
        ui::meshedit::HumanoidRigAnalysisCache analysis;
        ui::meshedit::HumanoidRigViewState rig;
        rig.showAllJointNames = true;
        rig.view = ui::meshedit::RigView::Side;
        frames([&]
               {
                   analysis.Update(*standalone);
                   ImGui::Begin("Rig");
                   ui::meshedit::DrawHumanoidRigPage(*standalone, rig, analysis);
                   ImGui::End();
               });
        capture("rig-side-all-names");
        Require(rig.diagram.view == ui::meshedit::RigView::Side && !rig.jointTree.empty(), "The rig page builds its diagram and joint tree");
    }
}

int main(int argc, char **argv)
{
    try
    {
        TestAutoMap();
        TestEditing();
        TestDiagram();
        TestSession();
        TestViews(argc > 1 ? argv[1] : "");
    }
    catch (const std::exception &error)
    {
        std::cerr << "MeshEditorTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "MeshEditorTests passed\n";
    return 0;
}
