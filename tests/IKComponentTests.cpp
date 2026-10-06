#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/Prefab.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/IKComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/SkeletonAttachmentComponent.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>
#include <limits>
using namespace PlutoGE;
using namespace PlutoGE::scene;
void Require(bool condition,const char *message) { if (!condition) throw std::runtime_error(message); }
int main()
{
    try
    {
        render::MeshConfig config;
        config.skeleton.joints.resize(3); config.animationNodes.resize(3);
        for (int i=0; i<3; ++i)
        {
            auto &joint=config.skeleton.joints[i];
            joint.name=std::array{"upper","lower","hand"}[i]; joint.nodeIndex=i; joint.parentJointIndex=i-1;
            joint.localBindTransform=glm::translate(glm::mat4(1),glm::vec3(i ? 1 : 0,0,0));
            joint.inverseBindMatrix=glm::translate(glm::mat4(1),glm::vec3(-i,0,0));
            config.animationNodes[i]={.name=joint.name,.parentNodeIndex=i-1,.localBindTransform=joint.localBindTransform};
        }
        render::Mesh mesh(config);
        Scene scene;
        auto *owner=scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name="Soldier"}));
        auto *animator=owner->CreateComponent<AnimationComponent>();
        auto *meshEntity=scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name="Mesh"}),owner);
        meshEntity->SetPosition({3,0,0});
        auto *meshComponent=meshEntity->CreateComponent<MeshComponent>(MeshComponentConfig{.mesh=&mesh});
        auto *target=scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name="Target"}),owner); target->SetPosition({4,1,0});
        auto *hint=scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name="Hint"}),owner); hint->SetPosition({3,2,0});
        auto *rig=owner->CreateComponent<IKComponent>(); rig->SetMeshEntity(meshEntity->GetID());
        IKConstraint constraint{.name="Arm",.root="upper",.middle="lower",.tip="hand",.target=target->GetID(),.hint=hint->GetID()};
        Require(rig->SetConstraints({constraint}),"Configure failed");
        Require(rig->GetConstraintStatus(0).empty(),"Valid chain rejected");
        auto tip=[&] { const auto &palette=animator->GetJointMatrices(mesh.GetSkeleton(),mesh.GetAnimationNodes()); return glm::vec3((palette[2]*glm::inverse(mesh.GetSkeleton().joints[2].inverseBindMatrix))[3]); };
        Require(glm::length(tip()-glm::vec3(1,1,0))<.0001f,"Mesh space target conversion failed");
        target->SetPosition({4,-1,0});
        Require(glm::length(tip()-glm::vec3(1,-1,0))<.0001f,"Target movement did not invalidate cached pose");
        animator->GetGraphParameters().push_back({.name="GripIK",.floatValue=1.0f});
        constraint.weightParameter="GripIK";
        Require(rig->SetConstraints({constraint}),"Parameterized IK configuration failed");
        animator->SetFloat("GripIK",0);
        Require(glm::length(tip()-glm::vec3(2,0,0))<.0001f,"Animator weight did not release IK");
        animator->SetFloat("GripIK",2);
        Require(glm::length(tip()-glm::vec3(1,-1,0))<.0001f,"Animator weight did not clamp/reapply IK");
        rig->SetEnabled(false); Require(glm::length(tip()-glm::vec3(2,0,0))<.0001f,"Disabling rig retained IK");
        rig->SetEnabled(true); rig->SetPreviewInEditor(false);
        Require(glm::length(tip()-glm::vec3(2,0,0))<.0001f,"Editor preview toggle ignored");
        rig->SetPreviewInEditor(true);
        auto bad=constraint; bad.weight=std::numeric_limits<float>::quiet_NaN();
        Require(!rig->SetConstraints({bad}) && rig->GetConstraints()[0].weight==1,"Invalid settings replaced valid rig");
        rig->Deserialize({{"ConstraintCount",PropertyType::Int,"999999"}});
        Require(rig->GetConstraints().size()==1,"Invalid count accepted");
        // Verify persistence and internal entity references survive duplicate/prefab cloning.
        std::string saved,error;
        Require(SceneSerializer::SaveToString(scene,saved,&error),"Scene serialization failed");
        auto restored=SceneSerializer::LoadFromString(saved,&error);
        Require(restored!=nullptr,"Scene reload failed");
        auto *restoredRig=restored->FindEntityByID(owner->GetID())->GetComponent<IKComponent>();
        Require(restoredRig && restoredRig->GetMeshEntity()==meshEntity->GetID() && restoredRig->GetConstraints()[0].target==target->GetID() && restoredRig->GetConstraints()[0].weightParameter=="GripIK","Scene reload lost IK settings");
        auto *duplicate=Prefab::DuplicateEntity(scene,*owner);
        auto *copiedRig=duplicate ? duplicate->GetComponent<IKComponent>() : nullptr;
        Require(copiedRig && copiedRig->GetMeshEntity()!=meshEntity->GetID() && copiedRig->GetConstraints()[0].target!=target->GetID() &&
            scene.FindEntityByID(copiedRig->GetConstraints()[0].target)->GetParent()==duplicate,"Prefab duplication retained original IK references");
        auto *socket=scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name="Socket"}),meshEntity);
        auto *attachment=socket->CreateComponent<SkeletonAttachmentComponent>(0,"hand");
        attachment->Deserialize({{"PositionOffset",PropertyType::Vec3,"0,0,0.2"}});
        attachment->Update(0);
        Require(attachment->GetTargetNodeIndex()==2,"Bone name did not override stale node index");
        const auto &palette=animator->GetJointMatrices(mesh.GetSkeleton(),mesh.GetAnimationNodes());
        const auto expected=meshEntity->GetWorldTransform()*meshComponent->GetMeshOffsetTransform()*palette[2]*glm::inverse(mesh.GetSkeleton().joints[2].inverseBindMatrix)*glm::vec4(0,0,.2f,1);
        Require(glm::length(socket->GetWorldPosition()-glm::vec3(expected))<.0001f,"Attachment grip offset differs from skinning pose");
        auto *otherMeshEntity=scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name="Other mesh"}),owner);
        auto *otherMesh=otherMeshEntity->CreateComponent<MeshComponent>(MeshComponentConfig{.mesh=&mesh});
        otherMeshEntity->SetPosition({8,0,0}); socket->SetParent(otherMeshEntity);
        attachment->Update(0);
        Require(attachment->GetSourceMesh()==otherMesh,"Reparented socket retained stale source mesh");
        owner->RemoveComponent(rig);
        Require(glm::length(tip()-glm::vec3(2,0,0))<.0001f,"Removing rig retained cached IK");
        std::cout << "PASS: authored IK evaluation, preview, invalid data, persistence, prefab references and attachments\n";
        return 0;
    }
    catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
