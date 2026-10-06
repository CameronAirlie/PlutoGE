#include "PlutoGE/scene/components/IKComponent.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/scene/components/SkeletonAttachmentComponent.h"
#include <cmath>
#include <limits>
#include <sstream>
#include <iomanip>
#include <tuple>

namespace PlutoGE::scene
{
    namespace
    {
        int FindJoint(const render::Skeleton &skeleton, const std::string &name)
        {
            for (int i = 0; i < static_cast<int>(skeleton.joints.size()); ++i)
                if (skeleton.joints[i].name == name) return i;
            return -1;
        }
        std::string Number(float value)
        {
            std::ostringstream out; out.imbue(std::locale::classic());
            out << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
            return out.str();
        }
        glm::quat WorldRotation(const glm::mat4 &world)
        {
            glm::mat3 basis(world);
            for (int i=0; i<3; ++i) basis[i]=glm::normalize(basis[i]);
            return glm::normalize(glm::quat_cast(basis));
        }
    }
    bool IKComponent::SetConstraints(std::vector<IKConstraint> constraints)
    {
        if (constraints.size() > 16) return false;
        for (auto &constraint : constraints)
        {
            if (!std::isfinite(constraint.weight) || !std::isfinite(constraint.rotationWeight)) return false;
            constraint.weight=std::clamp(constraint.weight, 0.0f, 1.0f);
            constraint.rotationWeight=std::clamp(constraint.rotationWeight, 0.0f, 1.0f);
        }
        m_constraints=std::move(constraints);
        return true;
    }
    MeshComponent *IKComponent::ResolveMesh() const
    {
        auto *owner=GetOwner();
        auto *scene=owner ? owner->GetScene() : nullptr;
        auto *entity=scene && m_meshEntity ? scene->FindEntityByID(m_meshEntity) : owner;
        if (!entity) return nullptr;
        // The rig is owned by the animator, and can drive only its descendant meshes.
        for (auto *ancestor=entity; ancestor; ancestor=ancestor->GetParent())
            if (ancestor == owner) return entity->GetComponent<MeshComponent>();
        return nullptr;
    }
    std::string IKComponent::GetConstraintStatus(size_t index) const
    {
        auto *owner=GetOwner();
        if (!owner || !owner->GetComponent<AnimationComponent>()) return "Add Animation to this entity first.";
        auto *mesh=ResolveMesh();
        if (!mesh || !mesh->GetMesh() || mesh->GetMesh()->GetSkeleton().joints.empty()) return "Select a skinned mesh on this entity or a descendant.";
        if (index >= m_constraints.size()) return "Invalid constraint index.";
        const auto &constraint=m_constraints[index];
        const auto &skeleton=mesh->GetMesh()->GetSkeleton();
        const int root=FindJoint(skeleton,constraint.root), middle=FindJoint(skeleton,constraint.middle), tip=FindJoint(skeleton,constraint.tip);
        if (root < 0 || middle < 0 || tip < 0) return "Select all three bones.";
        if (root == middle || middle == tip || root == tip || skeleton.joints[middle].parentJointIndex != root || skeleton.joints[tip].parentJointIndex != middle)
            return "Bones must form a direct root > middle > tip chain.";
        auto *scene=owner->GetScene();
        auto *target=scene ? scene->FindEntityByID(constraint.target) : nullptr;
        if (!target) return "Assign a target entity.";
        auto *hint=scene->FindEntityByID(constraint.hint);
        if (constraint.hint && !hint) return "Elbow hint entity is missing.";
        for (auto *entity : {target,hint})
            for (auto *ancestor=entity; ancestor; ancestor=ancestor->GetParent())
                if (const auto *attachment=ancestor->GetComponent<SkeletonAttachmentComponent>(); attachment && attachment->GetSourceMesh()==mesh)
                {
                    int joint=FindJoint(skeleton,attachment->GetJointName());
                    for (size_t depth=0; joint>=0 && joint<static_cast<int>(skeleton.joints.size()) && depth<skeleton.joints.size(); ++depth)
                    {
                        if (joint==root) return "Target or hint follows this arm: move it outside the constrained bone hierarchy.";
                        joint=skeleton.joints[joint].parentJointIndex;
                    }
                }
        return {};
    }
    std::vector<TwoBoneIKTarget> IKComponent::ResolveTargets(const render::Skeleton &skeleton) const
    {
        std::vector<TwoBoneIKTarget> result;
        auto *owner=GetOwner(); auto *scene=owner ? owner->GetScene() : nullptr;
        auto *mesh=ResolveMesh();
        if (!IsEnabled() || !owner || !owner->IsActive() || !scene || (!scene->IsRuntimeStarted() && !m_previewInEditor) ||
            !mesh || !mesh->GetMesh() || &mesh->GetMesh()->GetSkeleton() != &skeleton) return result;
        const auto world=mesh->GetOwner()->GetWorldTransform()*mesh->GetMeshOffsetTransform();
        const float determinant=glm::determinant(world);
        if (!std::isfinite(determinant) || std::abs(determinant)<1e-8f) return result;
        const auto inverse=glm::inverse(world);
        for (size_t i=0; i<m_constraints.size(); ++i)
        {
            const auto &constraint=m_constraints[i];
            if (!constraint.enabled || constraint.weight<=0 || !GetConstraintStatus(i).empty()) continue;
            auto *target=scene->FindEntityByID(constraint.target);
            auto *hint=scene->FindEntityByID(constraint.hint);
            TwoBoneIKTarget resolved;
            resolved.root=constraint.root; resolved.middle=constraint.middle; resolved.tip=constraint.tip;
            resolved.position=glm::vec3(inverse*glm::vec4(target->GetWorldPosition(),1));
            if (hint) resolved.pole=glm::vec3(inverse*glm::vec4(hint->GetWorldPosition(),1));
            else
            {
                // Bind elbow is a stable optional hint, avoiding feedback from the solved pose.
                int joint=FindJoint(skeleton,constraint.middle);
                resolved.pole=glm::vec3(glm::inverse(skeleton.joints[joint].inverseBindMatrix)[3]);
            }
            resolved.rotation=glm::inverse(WorldRotation(world))*WorldRotation(target->GetWorldTransform());
            float multiplier=1.0f;
            if (!constraint.weightParameter.empty())
                if (const auto *animation=owner->GetComponent<AnimationComponent>())
                    multiplier=animation->GetFloat(constraint.weightParameter);
            if (!std::isfinite(multiplier)) continue;
            resolved.weight=constraint.weight*std::clamp(multiplier,0.0f,1.0f);
            resolved.rotationWeight=constraint.rotationWeight;
            result.push_back(std::move(resolved));
        }
        return result;
    }
    std::vector<Property> IKComponent::Serialize() const
    {
        std::vector<Property> properties{
            {"Mesh",PropertyType::Entity,std::to_string(m_meshEntity)},
            {"PreviewInEditor",PropertyType::Bool,m_previewInEditor ? "true" : "false"},
            {"ConstraintCount",PropertyType::Int,std::to_string(m_constraints.size())}
        };
        auto *mesh=ResolveMesh();
        const auto *skeleton=mesh && mesh->GetMesh() ? &mesh->GetMesh()->GetSkeleton() : nullptr;
        for (size_t i=0; i<m_constraints.size(); ++i)
        {
            const auto &c=m_constraints[i]; const auto prefix="Constraints."+std::to_string(i)+".";
            properties.push_back({prefix+"Name",PropertyType::String,c.name});
            properties.push_back({prefix+"Enabled",PropertyType::Bool,c.enabled ? "true" : "false"});
            for (auto [label,value,parent] : {std::tuple{"RootBone",c.root,std::string{}}, {"MiddleBone",c.middle,c.root}, {"TipBone",c.tip,c.middle}})
            {
                Property property{prefix+label,PropertyType::String,value,{""}};
                if (skeleton) for (const auto &joint : skeleton->joints)
                    if (parent.empty() || (joint.parentJointIndex >= 0 && joint.parentJointIndex < static_cast<int>(skeleton->joints.size()) && skeleton->joints[joint.parentJointIndex].name == parent))
                        property.enumOptions.push_back(joint.name);
                properties.push_back(std::move(property));
            }
            properties.push_back({prefix+"Target",PropertyType::Entity,std::to_string(c.target)});
            properties.push_back({prefix+"Hint",PropertyType::Entity,std::to_string(c.hint)});
            properties.push_back({prefix+"Weight",PropertyType::Float,Number(c.weight)});
            properties.push_back({prefix+"RotationWeight",PropertyType::Float,Number(c.rotationWeight)});
            properties.push_back({prefix+"WeightParameter",PropertyType::String,c.weightParameter});
        }
        return properties;
    }
    void IKComponent::Deserialize(const std::vector<Property> &properties)
    {
        auto constraints=m_constraints; auto mesh=m_meshEntity; auto preview=m_previewInEditor;
        try
        {
            for (const auto &p : properties)
                if (p.name == "ConstraintCount") { int count=std::stoi(p.value); if (count<0 || count>16) return; constraints.resize(count); }
            for (const auto &p : properties)
            {
                if (p.name=="Mesh") mesh=static_cast<uint32_t>(std::stoul(p.value));
                else if (p.name=="PreviewInEditor") preview=p.value=="true" || p.value=="1";
                for (size_t i=0; i<constraints.size(); ++i)
                {
                    const auto prefix="Constraints."+std::to_string(i)+".";
                    if (!p.name.starts_with(prefix)) continue;
                    const auto field=p.name.substr(prefix.size()); auto &c=constraints[i];
                    if (field=="Name") c.name=p.value;
                    else if (field=="RootBone") c.root=p.value;
                    else if (field=="MiddleBone") c.middle=p.value;
                    else if (field=="TipBone") c.tip=p.value;
                    else if (field=="Target") c.target=static_cast<uint32_t>(std::stoul(p.value));
                    else if (field=="Hint") c.hint=static_cast<uint32_t>(std::stoul(p.value));
                    else if (field=="Enabled") c.enabled=p.value=="true" || p.value=="1";
                    else if (field=="Weight") c.weight=std::stof(p.value);
                    else if (field=="RotationWeight") c.rotationWeight=std::stof(p.value);
                    else if (field=="WeightParameter") c.weightParameter=p.value;
                }
            }
            if (SetConstraints(std::move(constraints))) { m_meshEntity=mesh; m_previewInEditor=preview; }
        }
        catch (const std::exception &) { /* Keep previous valid data for malformed scene input. */ }
    }
}
