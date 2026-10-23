#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/AnimationComponent.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace PlutoGE;
using namespace PlutoGE::scene;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }

    struct SourceNode
    {
        const char *name;
        int parent;
        glm::mat4 local;
    };

    // Y-up target: Hips at 1m, Head above, legs to either side, +X is left.
    render::Skeleton MakeTargetSkeleton()
    {
        render::Skeleton skeleton;
        const std::vector<std::pair<const char *, glm::vec3>> joints = {
            {"Hips", {0.0f, 1.0f, 0.0f}},
            {"Head", {0.0f, 0.6f, 0.0f}},
            {"LeftUpperLeg", {0.1f, -0.05f, 0.0f}},
            {"RightUpperLeg", {-0.1f, -0.05f, 0.0f}},
        };
        for (size_t index = 0; index < joints.size(); ++index)
        {
            render::SkeletonJoint joint;
            joint.name = joints[index].first;
            joint.nodeIndex = static_cast<int>(index);
            joint.parentJointIndex = index == 0 ? -1 : 0;
            joint.localBindTransform = glm::translate(glm::mat4(1.0f), joints[index].second);
            const glm::mat4 global = index == 0 ? joint.localBindTransform
                                                : skeleton.joints[0].localBindTransform * joint.localBindTransform;
            joint.inverseBindMatrix = glm::inverse(global);
            skeleton.joints.push_back(joint);
        }
        const std::vector<render::HumanoidBone> bones = {
            render::HumanoidBone::Hips, render::HumanoidBone::Head,
            render::HumanoidBone::LeftUpperLeg, render::HumanoidBone::RightUpperLeg};
        for (size_t index = 0; index < bones.size(); ++index)
        {
            skeleton.humanoidBoneMappings.push_back(render::HumanoidBoneMapping{
                .bone = bones[index],
                .targetJointIndex = static_cast<int>(index),
                .copyTranslation = bones[index] == render::HumanoidBone::Hips,
            });
        }
        return skeleton;
    }

    void AddChannel(render::AnimationClip &clip, const std::vector<SourceNode> &nodes, const std::vector<glm::mat4> &globals,
                    int node, render::AnimationTargetPath path, glm::vec4 value)
    {
        render::AnimationChannel channel;
        channel.nodeIndex = node;
        channel.sourceParentNodeIndex = nodes[static_cast<size_t>(node)].parent;
        channel.targetName = nodes[static_cast<size_t>(node)].name;
        channel.sourceLocalBindTransform = nodes[static_cast<size_t>(node)].local;
        channel.sourceGlobalBindTransform = globals[static_cast<size_t>(node)];
        channel.hasSourceLocalBindTransform = true;
        channel.hasSourceGlobalBindTransform = true;
        channel.path = path;
        channel.times = {0.0f, 1.0f};
        channel.values = {value, value};
        clip.channels.push_back(std::move(channel));
    }

    // Source rig in an arbitrary world frame. upAxis/forwardAxis describe
    // where that frame points the character; the clip yaws the pelvis 90
    // degrees about the source up axis, lowers it 0.1m and moves the
    // (unmapped) root two metres forward.
    render::AnimationClip MakeSourceClip(glm::vec3 up, glm::vec3 left, float rootLift = 0.0f, float rootYawDegrees = 0.0f)
    {
        const std::vector<SourceNode> nodes = {
            {"root", -1, glm::mat4(1.0f)},
            {"pelvis", 0, glm::translate(glm::mat4(1.0f), up * 0.95f)},
            {"head", 1, glm::translate(glm::mat4(1.0f), up * 0.6f)},
            {"thigh_l", 1, glm::translate(glm::mat4(1.0f), left * 0.1f - up * 0.05f)},
            {"thigh_r", 1, glm::translate(glm::mat4(1.0f), -left * 0.1f - up * 0.05f)},
        };
        std::vector<glm::mat4> globals;
        for (const auto &node : nodes)
            globals.push_back(node.parent < 0 ? node.local : globals[static_cast<size_t>(node.parent)] * node.local);

        const glm::vec3 forward = glm::cross(left, up);
        const glm::quat yaw = glm::angleAxis(glm::radians(90.0f), up);
        render::AnimationClip clip;
        clip.name = "Turn";
        clip.duration = 1.0f;
        AddChannel(clip, nodes, globals, 0, render::AnimationTargetPath::Translation,
                   glm::vec4(forward * 2.0f + up * rootLift, 0.0f));
        const glm::quat rootYaw = glm::angleAxis(glm::radians(rootYawDegrees), up);
        AddChannel(clip, nodes, globals, 0, render::AnimationTargetPath::Rotation, glm::vec4(rootYaw.x, rootYaw.y, rootYaw.z, rootYaw.w));
        AddChannel(clip, nodes, globals, 1, render::AnimationTargetPath::Translation, glm::vec4(up * 0.85f, 0.0f));
        AddChannel(clip, nodes, globals, 1, render::AnimationTargetPath::Rotation, glm::vec4(yaw.x, yaw.y, yaw.z, yaw.w));
        for (int node = 2; node < 5; ++node)
            AddChannel(clip, nodes, globals, node, render::AnimationTargetPath::Rotation, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        clip.channelCount = static_cast<int>(clip.channels.size());
        return clip;
    }

    glm::mat4 EvaluateHips(const render::AnimationClip &clip, const render::Skeleton &skeleton, bool stripRootMotion = false)
    {
        Scene scene;
        auto *owner = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Character"}));
        auto *animator = owner->CreateComponent<AnimationComponent>();
        animator->SetClipsFromImportedAnimations({clip});
        animator->SetStripRootMotion(stripRootMotion);
        const auto &palette = animator->GetJointMatrices(skeleton);
        return palette[0] * glm::inverse(skeleton.joints[0].inverseBindMatrix);
    }

    void CheckReferencePose(float targetRest, float sourceRest, float animatedAngle, float roll, bool zUp)
    {
        auto target = MakeTargetSkeleton();
        std::vector<SourceNode> nodes = {
            {"pelvis", -1, target.joints[0].localBindTransform},
            {"head", 0, target.joints[1].localBindTransform},
            {"thigh_l", 0, target.joints[2].localBindTransform},
            {"thigh_r", 0, target.joints[3].localBindTransform}};
        const glm::mat4 frame = zUp ? glm::rotate(glm::mat4(1), glm::radians(90.f), glm::vec3(1,0,0)) : glm::mat4(1);
        nodes[0].local = frame * nodes[0].local;
        const auto rotation = [](float degrees, float twist)
        { return glm::rotate(glm::mat4(1), glm::radians(degrees), glm::vec3(0,0,1)) *
                 glm::rotate(glm::mat4(1), glm::radians(twist), glm::vec3(1,0,0)); };
        for (int side = 0; side < 2; ++side)
        {
            const float sign = side == 0 ? 1.f : -1.f;
            const int start = static_cast<int>(target.joints.size());
            const std::array<const char*,3> sourceNames = side == 0
                ? std::array{"upperarm_l","lowerarm_l","hand_l"}
                : std::array{"upperarm_r","lowerarm_r","hand_r"};
            const std::array<render::HumanoidBone,3> bones = side == 0
                ? std::array{render::HumanoidBone::LeftUpperArm,render::HumanoidBone::LeftLowerArm,render::HumanoidBone::LeftHand}
                : std::array{render::HumanoidBone::RightUpperArm,render::HumanoidBone::RightLowerArm,render::HumanoidBone::RightHand};
            for (int part = 0; part < 3; ++part)
            {
                render::SkeletonJoint joint;
                joint.name = "Target_" + std::to_string(side) + "_" + std::to_string(part);
                joint.nodeIndex = start + part;
                joint.parentJointIndex = part == 0 ? 0 : start + part - 1;
                joint.localBindTransform = glm::translate(glm::mat4(1), part == 0 ? glm::vec3(sign*.2f,.4f,0) : glm::vec3(sign*.3f,0,0));
                if (part == 0) joint.localBindTransform *= rotation(-sign*targetRest,roll);
                target.joints.push_back(joint);
                target.humanoidBoneMappings.push_back({.bone=bones[part], .targetJointIndex=start+part});
                glm::mat4 local = glm::translate(glm::mat4(1), part == 0 ? glm::vec3(sign*.2f,.4f,0) : glm::vec3(sign*.5f,0,0));
                if (part == 0) local *= rotation(-sign*sourceRest, -roll);
                nodes.push_back({sourceNames[part], joint.parentJointIndex, local});
            }
        }
        std::vector<glm::mat4> targetGlobals, sourceBind, sourceAnimated;
        for (auto &joint : target.joints)
        {
            const auto global = joint.parentJointIndex < 0 ? joint.localBindTransform : targetGlobals[joint.parentJointIndex] * joint.localBindTransform;
            targetGlobals.push_back(global);
            joint.inverseBindMatrix = glm::inverse(global);
        }
        render::AnimationClip clip;
        clip.name="Reference pose"; clip.duration=1;
        for (size_t i=0;i<nodes.size();++i)
        {
            sourceBind.push_back(nodes[i].parent < 0 ? nodes[i].local : sourceBind[nodes[i].parent] * nodes[i].local);
            auto animated = nodes[i].local;
            if (i==4 || i==7)
                animated = glm::translate(glm::mat4(1),glm::vec3(nodes[i].local[3])) * rotation(i==4 ? -animatedAngle : animatedAngle,-roll);
            sourceAnimated.push_back(nodes[i].parent < 0 ? animated : sourceAnimated[nodes[i].parent] * animated);
            const auto q=glm::normalize(glm::quat_cast(glm::mat3(animated)));
            AddChannel(clip,nodes,sourceBind,static_cast<int>(i),render::AnimationTargetPath::Rotation,{q.x,q.y,q.z,q.w});
        }
        Scene scene;
        auto *owner=scene.AddEntity(std::make_unique<Entity>());
        auto *animation=owner->CreateComponent<AnimationComponent>();
        animation->SetClipsFromImportedAnimations({clip});
        const auto palette=animation->GetJointMatrices(target);
        std::vector<glm::vec3> positions;
        for (size_t i=0;i<target.joints.size();++i)
            positions.emplace_back((palette[i]*glm::inverse(target.joints[i].inverseBindMatrix))[3]);
        for (int i : {4,5,7,8})
        {
            const auto actual=glm::normalize(positions[i+1]-positions[i]);
            const auto expected=glm::normalize(glm::mat3(glm::inverse(frame))*glm::vec3(sourceAnimated[i+1][3]-sourceAnimated[i][3]));
            Require(glm::length(actual-expected)<.001f,"Reference pose limb direction mismatch: joint " + std::to_string(i) +
                "; target="+std::to_string(targetRest)+" source="+std::to_string(sourceRest)+" pose="+std::to_string(animatedAngle));
            Require(std::abs(glm::length(positions[i+1]-positions[i])-.3f)<.001f,"Retargeting changed target limb length");
        }
        if (targetRest==30 && sourceRest==45 && animatedAngle==90 && roll==0 && !zUp)
        {
            const auto positionOf = [&](const auto &p, int i)
            { return glm::vec3((p[i]*glm::inverse(target.joints[i].inverseBindMatrix))[3]); };
            const auto originalDirection=glm::normalize(positions[5]-positions[4]);
            target.humanoidBoneMappings[4].rotationOffsetDegrees.z=10;
            animation->Seek(0);
            const auto offsetPalette=animation->GetJointMatrices(target);
            const auto offsetDirection=glm::normalize(positionOf(offsetPalette,5)-positionOf(offsetPalette,4));
            Require(glm::length(offsetDirection-originalDirection)>.15f,"Automatic alignment swallowed a manual rotation offset");
            target.humanoidBoneMappings[4].rotationOffsetDegrees.z=0;
            target.joints[4].localBindTransform*=rotation(25,0);
            const auto refreshBind=[&]()
            {
                for(size_t i=0;i<target.joints.size();++i)
                {
                    const auto &j=target.joints[i];
                    targetGlobals[i]=j.parentJointIndex<0 ? j.localBindTransform : targetGlobals[j.parentJointIndex]*j.localBindTransform;
                    target.joints[i].inverseBindMatrix=glm::inverse(targetGlobals[i]);
                }
            };
            refreshBind();
            animation->Seek(0);
            const auto editedPalette=animation->GetJointMatrices(target);
            const auto editedDirection=glm::normalize(positionOf(editedPalette,5)-positionOf(editedPalette,4));
            Require(glm::length(editedDirection-originalDirection)<.001f,"Edited target bind pose reused stale alignment");
            const auto finitePalette=[](const auto &p)
            {
                for(const auto &matrix:p) for(int c=0;c<4;++c) for(int r=0;r<4;++r)
                    Require(std::isfinite(matrix[c][r]),"Unsupported reference data produced a nonfinite pose");
            };
            auto legacyClip=clip;
            for(auto &channel:legacyClip.channels.Mutable())
                channel.hasSourceGlobalBindTransform=channel.hasSourceLocalBindTransform=false;
            animation->SetClipsFromImportedAnimations({legacyClip});
            finitePalette(animation->GetJointMatrices(target));
            animation->SetClipsFromImportedAnimations({clip});
            target.joints[5].localBindTransform[3]={0,0,0,1};
            refreshBind();animation->Seek(0);
            finitePalette(animation->GetJointMatrices(target));
        }
    }

    // Optional read-only validation against an actual extracted mesh and native clip.
    void CheckImportedPose(const char *meshPath, const char *clipPath)
    {
        assets::AssetManager reader;
        render::MeshConfig target;
        std::vector<std::string> materials;
        assets::MeshAssetMetadata metadata;
        std::string error;
        Require(reader.LoadMeshAssetData(meshPath,target,materials,metadata,&error),"Cannot read target: "+error);
        render::AnimationClip clip;
        Require(reader.LoadAnimationClipAsset(clipPath,clip),"Cannot read source clip");
        std::array<int,render::kHumanoidBoneCount> sourceBones,targetBones;
        sourceBones.fill(-1); targetBones.fill(-1);
        int count=0;
        for (const auto &c:clip.channels) count=std::max(count,c.nodeIndex+1);
        std::vector<glm::mat4> locals(count,glm::mat4(1)),localBinds(count,glm::mat4(1)),binds(count,glm::mat4(1)),globals(count,glm::mat4(1));
        std::vector<int> parents(count,-1);
        std::vector<uint8_t> present(count,0),evaluated(count,0);
        for (const auto &c:clip.channels)
        {
            if (c.nodeIndex<0 || !c.hasSourceGlobalBindTransform || !c.hasSourceLocalBindTransform) continue;
            const auto i=static_cast<size_t>(c.nodeIndex);
            locals[i]=localBinds[i]=c.sourceLocalBindTransform; binds[i]=c.sourceGlobalBindTransform;
            parents[i]=c.sourceParentNodeIndex;present[i]=1;
            if (auto bone=render::GuessHumanoidBone(c.targetName)) sourceBones[static_cast<size_t>(*bone)]=c.nodeIndex;
        }
        for (const auto &c:clip.channels)
        {
            if (c.nodeIndex<0 || c.values.empty() || !present[c.nodeIndex])continue;
            auto &local=locals[c.nodeIndex];
            const auto v=c.values.front();
            // Test clip start, where the first authored keys can be sampled exactly.
            if (c.path==render::AnimationTargetPath::Translation) local[3]=glm::vec4(glm::vec3(v),1);
            else if(c.path==render::AnimationTargetPath::Rotation)
            {
                const glm::vec3 scale(glm::length(glm::vec3(local[0])),glm::length(glm::vec3(local[1])),glm::length(glm::vec3(local[2])));
                const auto position=local[3];local=glm::mat4_cast(glm::normalize(glm::quat(v.w,v.x,v.y,v.z)));
                for(int axis=0;axis<3;++axis)local[axis]*=scale[axis];local[3]=position;
            }
        }
        std::function<glm::mat4(int)> evaluate=[&](int i)
        {
            if(evaluated[i])return globals[i];
            evaluated[i]=1;
            globals[i]=parents[i]>=0 && parents[i]<count && present[parents[i]]
                ? evaluate(parents[i])*locals[i] : binds[i]*glm::inverse(localBinds[i])*locals[i];
            return globals[i];
        };
        for(int i=0;i<count;++i)if(present[i])evaluate(i);
        std::vector<glm::mat4> targetBind(target.skeleton.joints.size(),glm::mat4(1));
        std::vector<uint8_t> ready(targetBind.size(),0);
        std::function<glm::mat4(int)> targetGlobal=[&](int i)
        {
            if(ready[i])return targetBind[i];ready[i]=1;
            const auto &j=target.skeleton.joints[i];
            return targetBind[i]=j.parentJointIndex>=0 ? targetGlobal(j.parentJointIndex)*j.localBindTransform : j.localBindTransform;
        };
        for(size_t i=0;i<targetBind.size();++i)targetGlobal(static_cast<int>(i));
        for(const auto &m:target.skeleton.humanoidBoneMappings)
            targetBones[static_cast<size_t>(m.bone)]=m.targetJointIndex;
        const auto frame=[](const auto &bones,const auto &transforms)
        {
            using B=render::HumanoidBone;
            const auto pos=[&](B b){const int i=bones[static_cast<size_t>(b)];Require(i>=0,"Missing frame landmark");return glm::vec3(transforms[i][3]);};
            const auto up=glm::normalize(pos(B::Head)-pos(B::Hips));
            auto side=pos(B::LeftUpperLeg)-pos(B::RightUpperLeg);side=glm::normalize(side-up*glm::dot(up,side));
            return glm::mat3(side,up,glm::cross(side,up));
        };
        const auto basis=frame(targetBones,targetBind)*glm::transpose(frame(sourceBones,binds));
        Scene scene;auto *owner=scene.AddEntity(std::make_unique<Entity>());
        auto *animation=owner->CreateComponent<AnimationComponent>();
        animation->SetClipsFromImportedAnimations({clip});
        const auto palette=animation->GetJointMatrices(target.skeleton);
        using B=render::HumanoidBone;
        float worst=0;
        for(auto [a,b]:{std::pair{B::LeftUpperArm,B::LeftLowerArm}, {B::LeftLowerArm,B::LeftHand},
                        {B::RightUpperArm,B::RightLowerArm},{B::RightLowerArm,B::RightHand}})
        {
            const int ta=targetBones[static_cast<size_t>(a)],tb=targetBones[static_cast<size_t>(b)],
                      sa=sourceBones[static_cast<size_t>(a)],sb=sourceBones[static_cast<size_t>(b)];
            Require(ta>=0 && tb>=0 && sa>=0 && sb>=0,"Missing arm mapping");
            const auto aWorld=palette[ta]*glm::inverse(target.skeleton.joints[ta].inverseBindMatrix);
            const auto bWorld=palette[tb]*glm::inverse(target.skeleton.joints[tb].inverseBindMatrix);
            const auto actual=glm::normalize(glm::vec3(bWorld[3]-aWorld[3]));
            const auto expected=glm::normalize(basis*glm::vec3(globals[sb][3]-globals[sa][3]));
            const float radians=std::acos(std::clamp(glm::dot(actual,expected),-1.f,1.f));
            worst=std::max(worst,glm::degrees(radians));
        }
        std::cout<<"Imported arm pose max error: "<<worst<<" degrees\n";
        Require(worst<5.f,"Imported arms do not follow the source pose");
    }

    // A clip authored on the target rig itself: channel names are the target joint names and
    // the source binds are the target binds. The Head channel nods the head 90 degrees.
    glm::vec3 EvaluateSameRigHeadUp(render::Skeleton skeleton)
    {
        const std::vector<SourceNode> nodes = {
            {"Hips", -1, skeleton.joints[0].localBindTransform},
            {"Head", 0, skeleton.joints[1].localBindTransform},
            {"LeftUpperLeg", 0, skeleton.joints[2].localBindTransform},
            {"RightUpperLeg", 0, skeleton.joints[3].localBindTransform},
        };
        std::vector<glm::mat4> globals;
        for (const auto &node : nodes)
            globals.push_back(node.parent < 0 ? node.local : globals[static_cast<size_t>(node.parent)] * node.local);
        render::AnimationClip clip;
        clip.name = "Nod";
        clip.duration = 1.0f;
        const glm::quat nod = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
        AddChannel(clip, nodes, globals, 1, render::AnimationTargetPath::Rotation, glm::vec4(nod.x, nod.y, nod.z, nod.w));
        for (int node : {0, 2, 3})
            AddChannel(clip, nodes, globals, node, render::AnimationTargetPath::Rotation, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        clip.channelCount = static_cast<int>(clip.channels.size());

        Scene scene;
        auto *owner = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Character"}));
        auto *animator = owner->CreateComponent<AnimationComponent>();
        animator->SetClipsFromImportedAnimations({clip});
        const auto &palette = animator->GetJointMatrices(skeleton);
        const glm::mat4 head = palette[1] * glm::inverse(skeleton.joints[1].inverseBindMatrix);
        return glm::normalize(glm::mat3(head) * glm::vec3(0.0f, 1.0f, 0.0f));
    }

    void RequireHipsPose(const glm::mat4 &hips, const char *label, float expectedHeight = 0.9f)
    {
        const std::string prefix = std::string(label) + ": ";
        const glm::vec3 position(hips[3]);
        Require(std::abs(position.y - expectedHeight) < 0.001f,
                prefix + "unexpected hips height " + std::to_string(position.y));
        Require(glm::length(glm::vec2(position.x, position.z)) < 0.001f,
                prefix + "root motion leaked into the hips");
        // A 90 degree yaw about +Y turns the character's left (+X) towards -Z.
        const glm::vec3 left = glm::normalize(glm::mat3(hips) * glm::vec3(1.0f, 0.0f, 0.0f));
        Require(glm::length(left - glm::vec3(0.0f, 0.0f, -1.0f)) < 0.001f,
                prefix + "pelvis yaw was not applied about the target up axis");
    }
}

int main(int argc, char **argv)
{
    try
    {
        if(argc==3) CheckImportedPose(argv[1],argv[2]);
        for (bool zUp : {false,true})
            for (float targetRest : {0.f,30.f,60.f})
                for (float sourceRest : {0.f,45.f})
                    for (float animated : {0.f,45.f,90.f,140.f})
                        for (float roll : {0.f,90.f})
                            CheckReferencePose(targetRest,sourceRest,animated,roll,zUp);
        CheckReferencePose(180.f,0.f,90.f,0.f,false);
        const auto skeleton = MakeTargetSkeleton();
        // Unreal FBX: Z-up with +X left (forward -Y).
        RequireHipsPose(EvaluateHips(MakeSourceClip({0, 0, 1}, {1, 0, 0}), skeleton), "Z-up source");
        // Matching Y-up frame keeps the previous identity behaviour.
        RequireHipsPose(EvaluateHips(MakeSourceClip({0, 1, 0}, {1, 0, 0}), skeleton), "Y-up source");
        // A source facing the opposite way (left = -X) is a 180 degree yaw.
        RequireHipsPose(EvaluateHips(MakeSourceClip({0, 1, 0}, {-1, 0, 0}), skeleton), "Rotated source");

        // Root vertical travel folds into the hips by default and is ignored
        // when the rig's root carries root motion.
        const auto lifted = MakeSourceClip({0, 0, 1}, {1, 0, 0}, 1.0f);
        RequireHipsPose(EvaluateHips(lifted, skeleton), "Folded root lift", 1.9f);
        RequireHipsPose(EvaluateHips(lifted, skeleton, true), "Stripped root lift", 0.9f);
        // A turning root (for example a 180 turn clip) must not rotate the body
        // once root motion is stripped; the controller owns facing.
        const auto turning = MakeSourceClip({0, 0, 1}, {1, 0, 0}, 1.0f, 90.0f);
        RequireHipsPose(EvaluateHips(turning, skeleton, true), "Stripped root turn", 0.9f);
        const glm::vec3 unstrippedLeft = glm::normalize(glm::mat3(EvaluateHips(turning, skeleton)) * glm::vec3(1.0f, 0.0f, 0.0f));
        Require(glm::length(unstrippedLeft - glm::vec3(0.0f, 0.0f, -1.0f)) > 0.5f, "Root yaw should reach the body when root motion is kept");

        // Transition offsets start the destination state part-way through.
        {
            Scene scene;
            auto *owner = scene.AddEntity(std::make_unique<Entity>(EntityConfig{.name = "Graph"}));
            auto *animator = owner->CreateComponent<AnimationComponent>();
            auto first = MakeSourceClip({0, 1, 0}, {1, 0, 0});
            auto second = first;
            first.name = "Fall";
            second.name = "Land";
            second.duration = 2.0f;
            animator->SetClipsFromImportedAnimations({first, second});
            animator->AddParameter("Grounded", AnimationComponent::AnimationParameterType::Bool);
            Require(animator->AddTransition(0, 1), "Could not add transition");
            auto &transition = animator->GetGraphStates()[0].transitions.back();
            transition.duration = 0.0f;
            transition.offset = 0.25f;
            transition.conditions.push_back({.parameterName = "Grounded", .mode = AnimationComponent::AnimationConditionMode::If});
            animator->Update(0.01f);
            Require(animator->GetCurrentStateIndex() == 0, "Transition fired without its condition");
            animator->SetBool("Grounded", true);
            animator->Update(0.01f);
            Require(animator->GetCurrentStateIndex() == 1 && std::abs(animator->GetTime() - 0.5f) < 0.0001f,
                    "Transition offset did not start the destination at 25% (" + std::to_string(animator->GetTime()) + "s)");
        }
        // Same-rig clips keep driving a slot whose source override names a foreign bone
        // (for example Chest -> spine_03 for Unreal clips on this soldier).
        {
            auto overridden = skeleton;
            for (auto &mapping : overridden.humanoidBoneMappings)
                if (mapping.bone == render::HumanoidBone::Head)
                    mapping.sourceBoneName = "neck_02";
            const glm::vec3 headUp = EvaluateSameRigHeadUp(overridden);
            Require(glm::length(headUp - glm::vec3(0.0f, 0.0f, 1.0f)) < 0.001f,
                    "Same-rig Head channel was dropped by a foreign source override");
        }
        std::cout << "Humanoid retarget basis tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
