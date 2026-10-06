#include "PlutoGE/ui/IKInspector.h"
#include "PlutoGE/ui/EditorShell.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/IKComponent.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/SkeletonAttachmentComponent.h"
#include <imgui.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/euler_angles.hpp>

namespace PlutoGE::ui
{
    void RenderIKInspectorTools(scene::IKComponent &rig, EditorShell &shell)
    {
        ImGui::TextWrapped("Assign a skinned mesh, choose a bone chain, then create or assign Target and Hint entities. Move targets with the normal viewport gizmo.");
        ImGui::BeginDisabled(rig.GetConstraints().size()>=16);
        if (ImGui::Button("Add Constraint"))
            shell.ExecuteSceneEdit("Add IK Constraint", [&] {
                auto constraints=rig.GetConstraints();
                constraints.emplace_back(); constraints.back().name="Arm "+std::to_string(constraints.size());
                rig.SetConstraints(std::move(constraints));
                rig.GetOwner()->AddPrefabOverride("Component:IKComponent:ConstraintCount");
            });
        ImGui::EndDisabled();
        for (size_t i=0; i<rig.GetConstraints().size(); ++i)
        {
            ImGui::PushID(static_cast<int>(i));
            const auto constraint=rig.GetConstraints()[i];
            ImGui::SeparatorText(constraint.name.c_str());
            const auto status=rig.GetConstraintStatus(i);
            if (!status.empty()) ImGui::TextWrapped("%s",status.c_str());
            auto *mesh=rig.ResolveMesh(); auto *owner=rig.GetOwner();
            auto *scene=owner ? owner->GetScene() : nullptr;
            auto *animator=owner ? owner->GetComponent<scene::AnimationComponent>() : nullptr;
            const auto *skeleton=mesh && mesh->GetMesh() ? &mesh->GetMesh()->GetSkeleton() : nullptr;
            int tip=-1,middle=-1;
            if (skeleton) for (int j=0; j<static_cast<int>(skeleton->joints.size()); ++j)
            { if (skeleton->joints[j].name==constraint.tip) tip=j; if (skeleton->joints[j].name==constraint.middle) middle=j; }
            ImGui::BeginDisabled(!animator || tip<0 || middle<0 || (scene && scene->FindEntityByID(constraint.target) && scene->FindEntityByID(constraint.hint)));
            if (ImGui::Button("Create Missing Targets"))
                shell.ExecuteSceneEdit("Create IK Targets", [&] {
                    if (!scene) return;
                    const auto &palette=animator->GetJointMatrices(*skeleton, mesh->GetMesh()->GetAnimationNodes());
                    const auto world=mesh->GetOwner()->GetWorldTransform()*mesh->GetMeshOffsetTransform();
                    auto constraints=rig.GetConstraints(); auto &c=constraints[i];
                    const auto create=[&](std::string name,int joint) {
                        auto *entity=scene->AddEntity(std::make_unique<scene::Entity>(scene::EntityConfig{.name=std::move(name)}), owner);
                        const auto pose=world*palette[joint]*glm::inverse(skeleton->joints[joint].inverseBindMatrix);
                        entity->SetWorldPosition(glm::vec3(pose[3]));
                        glm::mat4 basis(1);
                        for (int axis=0; axis<3; ++axis) basis[axis]=glm::vec4(glm::normalize(glm::vec3(pose[axis])),0);
                        float x=0,y=0,z=0; glm::extractEulerAngleXYZ(basis,x,y,z);
                        entity->SetWorldRotation(glm::degrees(glm::vec3(x,y,z)));
                        return entity->GetID();
                    };
                    if (!scene->FindEntityByID(c.target)) c.target=create(c.name+" Target",tip);
                    if (!scene->FindEntityByID(c.hint)) c.hint=create(c.name+" Hint",middle);
                    rig.SetConstraints(std::move(constraints));
                    owner->AddPrefabOverride("Component:IKComponent:Constraints."+std::to_string(i)+".Target");
                    owner->AddPrefabOverride("Component:IKComponent:Constraints."+std::to_string(i)+".Hint");
                });
            ImGui::EndDisabled();
            ImGui::SameLine(); ImGui::BeginDisabled(!scene || !scene->FindEntityByID(constraint.target));
            if (ImGui::Button("Select Target")) shell.SetSelectedEntity(scene->FindEntityByID(constraint.target));
            ImGui::EndDisabled(); ImGui::SameLine();
            ImGui::BeginDisabled(!scene || !scene->FindEntityByID(constraint.hint));
            if (ImGui::Button("Select Hint")) shell.SetSelectedEntity(scene->FindEntityByID(constraint.hint));
            ImGui::EndDisabled();
            if (ImGui::Button("Remove Constraint"))
            {
                shell.ExecuteSceneEdit("Remove IK Constraint", [&] {
                    auto constraints=rig.GetConstraints(); constraints.erase(constraints.begin()+i);
                    rig.SetConstraints(std::move(constraints));
                    for (const auto &property : rig.Serialize()) owner->AddPrefabOverride("Component:IKComponent:"+property.name);
                });
                ImGui::PopID(); break;
            }
            ImGui::PopID();
        }
    }
    void RenderAttachmentInspectorTools(scene::SkeletonAttachmentComponent &attachment)
    {
        auto *mesh=attachment.GetSourceMesh();
        if (!mesh || !mesh->GetMesh() || mesh->GetMesh()->GetSkeleton().joints.empty())
            ImGui::TextWrapped("Place this socket beneath a skinned mesh to choose a bone. Put weapon geometry beneath the socket, or use the grip offsets below.");
        else ImGui::TextWrapped("JointName follows the animated bone. Grip offsets are relative to that bone; rotation is in degrees.");
    }
}
