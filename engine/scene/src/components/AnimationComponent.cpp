#include "PlutoGE/scene/components/AnimationComponent.h"
#include "PlutoGE/core/Engine.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <limits>
#include <string_view>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr const char *kClipPrefix = "Clips.";
        constexpr const char *kStatePrefix = "Graph.States.";
        constexpr const char *kParameterPrefix = "Graph.Parameters.";

        bool ParseBool(const std::string &value)
        {
            return value == "true" || value == "1";
        }

        glm::vec4 SampleChannelValue(const render::AnimationChannel &channel, float time)
        {
            if (channel.times.empty() || channel.values.empty())
            {
                return {};
            }

            if (time <= channel.times.front() || channel.times.size() == 1)
            {
                return channel.values.front();
            }

            if (time >= channel.times.back())
            {
                return channel.values.back();
            }

            auto upper = std::upper_bound(channel.times.begin(), channel.times.end(), time);
            const size_t nextIndex = static_cast<size_t>(std::distance(channel.times.begin(), upper));
            const size_t previousIndex = nextIndex > 0 ? nextIndex - 1 : 0;
            if (nextIndex >= channel.times.size() || nextIndex >= channel.values.size() || previousIndex >= channel.values.size())
            {
                return channel.values.back();
            }

            if (channel.interpolation == render::AnimationInterpolation::Step)
            {
                return channel.values[previousIndex];
            }

            const float startTime = channel.times[previousIndex];
            const float endTime = channel.times[nextIndex];
            const float t = endTime > startTime ? std::clamp((time - startTime) / (endTime - startTime), 0.0f, 1.0f) : 0.0f;
            if (channel.path == render::AnimationTargetPath::Rotation)
            {
                const glm::quat a(channel.values[previousIndex].w, channel.values[previousIndex].x, channel.values[previousIndex].y, channel.values[previousIndex].z);
                const glm::quat b(channel.values[nextIndex].w, channel.values[nextIndex].x, channel.values[nextIndex].y, channel.values[nextIndex].z);
                const glm::quat q = glm::normalize(glm::slerp(a, b, t));
                return glm::vec4(q.x, q.y, q.z, q.w);
            }

            return glm::mix(channel.values[previousIndex], channel.values[nextIndex], t);
        }

        glm::mat4 ComposeTransform(const glm::vec3 &translation, const glm::vec4 &rotation, const glm::vec3 &scale)
        {
            const glm::quat q = glm::normalize(glm::quat(rotation.w, rotation.x, rotation.y, rotation.z));
            return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(q) * glm::scale(glm::mat4(1.0f), scale);
        }

        void DecomposeTransform(const glm::mat4 &transform, glm::vec3 &translation, glm::vec4 &rotation, glm::vec3 &scale)
        {
            translation = glm::vec3(transform[3]);
            glm::vec3 basisX(transform[0]);
            glm::vec3 basisY(transform[1]);
            glm::vec3 basisZ(transform[2]);
            scale = glm::vec3(glm::length(basisX), glm::length(basisY), glm::length(basisZ));
            constexpr float epsilon = 0.000001f;
            if (scale.x <= epsilon || scale.y <= epsilon || scale.z <= epsilon)
            {
                rotation = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
                scale = glm::vec3(1.0f);
                return;
            }

            basisX /= scale.x;
            basisY /= scale.y;
            basisZ /= scale.z;

            // Preserve reflections. Treating all scale components as positive
            // turns a mirrored FBX basis into an invalid rotation and commonly
            // leaves arms or legs pointing sideways after retargeting.
            if (glm::dot(basisX, glm::cross(basisY, basisZ)) < 0.0f)
            {
                basisX = -basisX;
                basisY = -basisY;
                basisZ = -basisZ;
                scale = -scale;
            }

            glm::mat3 rotationMatrix(1.0f);
            rotationMatrix[0] = basisX;
            rotationMatrix[1] = basisY;
            rotationMatrix[2] = basisZ;
            const glm::quat orientation = glm::normalize(glm::quat_cast(rotationMatrix));
            rotation = glm::vec4(orientation.x, orientation.y, orientation.z, orientation.w);
        }

        std::string_view CanonicalTargetName(std::string_view name)
        {
            const size_t separator = name.find_last_of(":|/");
            return separator == std::string_view::npos ? name : name.substr(separator + 1);
        }

        bool TargetNamesMatch(std::string_view a, std::string_view b)
        {
            if (a.empty() || b.empty())
            {
                return false;
            }

            return a == b || CanonicalTargetName(a) == CanonicalTargetName(b);
        }

        int ResolveChannelNodeIndex(const render::AnimationChannel &channel, const std::vector<render::AnimationNode> &nodes)
        {
            if (!channel.targetName.empty())
            {
                for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
                {
                    if (nodes[nodeIndex].name == channel.targetName)
                    {
                        return static_cast<int>(nodeIndex);
                    }
                }

                for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
                {
                    if (TargetNamesMatch(nodes[nodeIndex].name, channel.targetName))
                    {
                        return static_cast<int>(nodeIndex);
                    }
                }
            }

            return channel.nodeIndex >= 0 && channel.nodeIndex < static_cast<int>(nodes.size()) ? channel.nodeIndex : -1;
        }

        const render::HumanoidBoneMapping *FindHumanoidMappingForChannel(const render::AnimationChannel &channel,
                                                                         const render::Skeleton &skeleton)
        {
            if (channel.targetName.empty())
                return nullptr;

            for (const auto &mapping : skeleton.humanoidBoneMappings)
            {
                if (!mapping.sourceBoneName.empty() && TargetNamesMatch(mapping.sourceBoneName, channel.targetName))
                    return &mapping;
            }

            if (const auto humanoidBone = render::GuessHumanoidBone(channel.targetName))
            {
                for (const auto &mapping : skeleton.humanoidBoneMappings)
                {
                    // A source override opts this slot out of automatic name
                    // matching; otherwise a similarly named channel can steal
                    // a mapping the user explicitly assigned.
                    if (mapping.sourceBoneName.empty() && mapping.bone == *humanoidBone)
                        return &mapping;
                }
            }
            return nullptr;
        }

        bool IsReservedRetargetJoint(int jointIndex, const render::Skeleton &skeleton)
        {
            if (jointIndex < 0 || jointIndex >= static_cast<int>(skeleton.joints.size()))
                return false;

            // A source rig may animate an extra conversion/root bone which is
            // deliberately absent from the humanoid map. Never copy that
            // bone's absolute local rotation onto a same-named target root: it
            // would rotate the entire retargeted character. Unmapped children
            // such as fingers remain eligible for exact-name passthrough.
            for (const auto &mapping : skeleton.humanoidBoneMappings)
            {
                int mappedJointIndex = mapping.targetJointIndex;
                while (mappedJointIndex >= 0 && mappedJointIndex < static_cast<int>(skeleton.joints.size()))
                {
                    if (mappedJointIndex == jointIndex)
                        return true;

                    const int parentIndex = skeleton.joints[static_cast<size_t>(mappedJointIndex)].parentJointIndex;
                    if (parentIndex == mappedJointIndex)
                        break;
                    mappedJointIndex = parentIndex;
                }
            }
            return false;
        }

        int ResolveChannelJointIndex(const render::AnimationChannel &channel, const render::Skeleton &skeleton)
        {
            if (!skeleton.humanoidBoneMappings.empty() && !channel.targetName.empty())
            {
                if (const auto *mapping = FindHumanoidMappingForChannel(channel, skeleton);
                    mapping && mapping->targetJointIndex >= 0 &&
                    mapping->targetJointIndex < static_cast<int>(skeleton.joints.size()))
                {
                    return mapping->targetJointIndex;
                }
            }

            if (!channel.targetName.empty())
            {
                for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
                {
                    if (skeleton.joints[jointIndex].name == channel.targetName)
                    {
                        const int resolvedIndex = static_cast<int>(jointIndex);
                        return !skeleton.humanoidBoneMappings.empty() && IsReservedRetargetJoint(resolvedIndex, skeleton)
                                   ? -1
                                   : resolvedIndex;
                    }
                }

                for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
                {
                    if (TargetNamesMatch(skeleton.joints[jointIndex].name, channel.targetName))
                    {
                        const int resolvedIndex = static_cast<int>(jointIndex);
                        return !skeleton.humanoidBoneMappings.empty() && IsReservedRetargetJoint(resolvedIndex, skeleton)
                                   ? -1
                                   : resolvedIndex;
                    }
                }
            }

            // Indices are only safe when no cross-rig mapping is configured.
            // Source and target skeletons commonly use different joint orders.
            return skeleton.humanoidBoneMappings.empty() &&
                           channel.jointIndex >= 0 && channel.jointIndex < static_cast<int>(skeleton.joints.size())
                       ? channel.jointIndex
                       : -1;
        }

        int ParseIntSafe(const std::string &value, int fallback = 0)
        {
            try
            {
                return std::stoi(value);
            }
            catch (...)
            {
                return fallback;
            }
        }

        float ParseFloatSafe(const std::string &value, float fallback = 0.0f)
        {
            try
            {
                return std::stof(value);
            }
            catch (...)
            {
                return fallback;
            }
        }

        const char *ParameterTypeName(AnimationComponent::AnimationParameterType type)
        {
            switch (type)
            {
            case AnimationComponent::AnimationParameterType::Int:
                return "Int";
            case AnimationComponent::AnimationParameterType::Bool:
                return "Bool";
            case AnimationComponent::AnimationParameterType::Trigger:
                return "Trigger";
            case AnimationComponent::AnimationParameterType::Float:
            default:
                return "Float";
            }
        }

        AnimationComponent::AnimationParameterType ParseParameterType(const std::string &value)
        {
            if (value == "Int")
                return AnimationComponent::AnimationParameterType::Int;
            if (value == "Bool")
                return AnimationComponent::AnimationParameterType::Bool;
            if (value == "Trigger")
                return AnimationComponent::AnimationParameterType::Trigger;
            return AnimationComponent::AnimationParameterType::Float;
        }

        const char *ConditionModeName(AnimationComponent::AnimationConditionMode mode)
        {
            switch (mode)
            {
            case AnimationComponent::AnimationConditionMode::IfNot:
                return "IfNot";
            case AnimationComponent::AnimationConditionMode::Greater:
                return "Greater";
            case AnimationComponent::AnimationConditionMode::Less:
                return "Less";
            case AnimationComponent::AnimationConditionMode::Equals:
                return "Equals";
            case AnimationComponent::AnimationConditionMode::NotEqual:
                return "NotEqual";
            case AnimationComponent::AnimationConditionMode::If:
            default:
                return "If";
            }
        }

        AnimationComponent::AnimationConditionMode ParseConditionMode(const std::string &value)
        {
            if (value == "IfNot")
                return AnimationComponent::AnimationConditionMode::IfNot;
            if (value == "Greater")
                return AnimationComponent::AnimationConditionMode::Greater;
            if (value == "Less")
                return AnimationComponent::AnimationConditionMode::Less;
            if (value == "Equals")
                return AnimationComponent::AnimationConditionMode::Equals;
            if (value == "NotEqual")
                return AnimationComponent::AnimationConditionMode::NotEqual;
            return AnimationComponent::AnimationConditionMode::If;
        }

        AnimationComponent::AnimationParameterType ConvertParameterType(assets::AnimationGraphParameterType type)
        {
            switch (type)
            {
            case assets::AnimationGraphParameterType::Int:
                return AnimationComponent::AnimationParameterType::Int;
            case assets::AnimationGraphParameterType::Bool:
                return AnimationComponent::AnimationParameterType::Bool;
            case assets::AnimationGraphParameterType::Trigger:
                return AnimationComponent::AnimationParameterType::Trigger;
            case assets::AnimationGraphParameterType::Float:
            default:
                return AnimationComponent::AnimationParameterType::Float;
            }
        }

        AnimationComponent::AnimationConditionMode ConvertConditionMode(assets::AnimationGraphConditionMode mode)
        {
            switch (mode)
            {
            case assets::AnimationGraphConditionMode::IfNot:
                return AnimationComponent::AnimationConditionMode::IfNot;
            case assets::AnimationGraphConditionMode::Greater:
                return AnimationComponent::AnimationConditionMode::Greater;
            case assets::AnimationGraphConditionMode::Less:
                return AnimationComponent::AnimationConditionMode::Less;
            case assets::AnimationGraphConditionMode::Equals:
                return AnimationComponent::AnimationConditionMode::Equals;
            case assets::AnimationGraphConditionMode::NotEqual:
                return AnimationComponent::AnimationConditionMode::NotEqual;
            case assets::AnimationGraphConditionMode::If:
            default:
                return AnimationComponent::AnimationConditionMode::If;
            }
        }

        template <typename ResolveIndexFn, typename BindTransformFn>
        std::vector<glm::mat4> SampleLocalTransforms(
            const std::vector<render::AnimationClip> &clips,
            int clipIndex,
            float time,
            size_t transformCount,
            ResolveIndexFn resolveIndex,
            BindTransformFn bindTransform)
        {
            std::vector<glm::mat4> localTransforms(transformCount, glm::mat4(1.0f));
            for (size_t index = 0; index < transformCount; ++index)
            {
                localTransforms[index] = bindTransform(index);
            }

            if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size()))
            {
                return localTransforms;
            }

            std::vector<glm::vec3> translations(transformCount, glm::vec3(0.0f));
            std::vector<glm::vec4> rotations(transformCount, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            std::vector<glm::vec3> scales(transformCount, glm::vec3(1.0f));
            std::vector<uint8_t> animatedLocals(transformCount, 0);

            const auto &clip = clips[static_cast<size_t>(clipIndex)];
            for (const auto &channel : clip.channels)
            {
                const int resolvedIndex = resolveIndex(channel);
                if (resolvedIndex < 0 || resolvedIndex >= static_cast<int>(transformCount))
                {
                    continue;
                }

                const auto sample = SampleChannelValue(channel, time);
                const size_t transformIndex = static_cast<size_t>(resolvedIndex);
                if (!animatedLocals[transformIndex])
                {
                    DecomposeTransform(bindTransform(transformIndex), translations[transformIndex], rotations[transformIndex], scales[transformIndex]);
                    animatedLocals[transformIndex] = 1;
                }

                switch (channel.path)
                {
                case render::AnimationTargetPath::Translation:
                    translations[transformIndex] = glm::vec3(sample);
                    break;
                case render::AnimationTargetPath::Rotation:
                    rotations[transformIndex] = sample;
                    break;
                case render::AnimationTargetPath::Scale:
                    scales[transformIndex] = glm::vec3(sample);
                    break;
                }
            }

            for (size_t index = 0; index < transformCount; ++index)
            {
                if (animatedLocals[index])
                {
                    localTransforms[index] = ComposeTransform(translations[index], rotations[index], scales[index]);
                }
            }

            return localTransforms;
        }

        std::vector<glm::mat4> SampleCachedSourceLocalTransforms(
            const render::AnimationClip &clip,
            float time,
            const AnimationRetargetClipCache &cache)
        {
            std::vector<glm::mat4> localTransforms = cache.sourceLocalBindTransforms;
            std::vector<glm::vec3> translations = cache.sourceBindTranslations;
            std::vector<glm::vec4> rotations = cache.sourceBindRotations;
            std::vector<glm::vec3> scales = cache.sourceBindScales;
            std::vector<uint8_t> animatedLocals(localTransforms.size(), 0);

            for (const auto &channel : clip.channels)
            {
                if (channel.nodeIndex < 0 || channel.nodeIndex >= static_cast<int>(localTransforms.size()))
                    continue;
                const size_t nodeIndex = static_cast<size_t>(channel.nodeIndex);
                const glm::vec4 sample = SampleChannelValue(channel, time);
                switch (channel.path)
                {
                case render::AnimationTargetPath::Translation:
                    translations[nodeIndex] = glm::vec3(sample);
                    break;
                case render::AnimationTargetPath::Rotation:
                    rotations[nodeIndex] = sample;
                    break;
                case render::AnimationTargetPath::Scale:
                    scales[nodeIndex] = glm::vec3(sample);
                    break;
                }
                animatedLocals[nodeIndex] = 1;
            }

            for (size_t nodeIndex = 0; nodeIndex < localTransforms.size(); ++nodeIndex)
            {
                if (animatedLocals[nodeIndex])
                    localTransforms[nodeIndex] = ComposeTransform(translations[nodeIndex], rotations[nodeIndex], scales[nodeIndex]);
            }
            return localTransforms;
        }

        std::vector<glm::mat4> SampleRetargetedJointTransforms(
            const std::vector<render::AnimationClip> &clips,
            int clipIndex,
            float time,
            const render::Skeleton &skeleton,
            const AnimationRetargetClipCache &cache,
            const std::vector<glm::vec3> &targetBindTranslations,
            const std::vector<glm::vec4> &targetBindRotations,
            const std::vector<glm::vec3> &targetBindScales,
            const std::vector<glm::vec4> &targetGlobalBindRotations)
        {
            std::vector<glm::mat4> localTransforms;
            localTransforms.reserve(skeleton.joints.size());
            for (const auto &joint : skeleton.joints)
                localTransforms.push_back(joint.localBindTransform);

            if (clipIndex < 0 || clipIndex >= static_cast<int>(clips.size()))
                return localTransforms;

            std::vector<glm::vec3> translations = targetBindTranslations;
            std::vector<glm::vec4> rotations = targetBindRotations;
            std::vector<glm::vec3> scales = targetBindScales;
            std::vector<uint8_t> animatedLocals(skeleton.joints.size(), 0);

            const auto &clip = clips[static_cast<size_t>(clipIndex)];
            const int sourceNodeCount = static_cast<int>(cache.sourceLocalBindTransforms.size());

            const auto sourceLocalTransforms = SampleCachedSourceLocalTransforms(clip, time, cache);

            std::vector<glm::mat4> sourceGlobalTransforms(static_cast<size_t>(sourceNodeCount), glm::mat4(1.0f));
            std::vector<uint8_t> sourceEvaluationState(static_cast<size_t>(sourceNodeCount), 0);
            std::function<glm::mat4(size_t)> evaluateSourceGlobal = [&](size_t nodeIndex) -> glm::mat4 {
                if (sourceEvaluationState[nodeIndex] == 2)
                    return sourceGlobalTransforms[nodeIndex];
                if (sourceEvaluationState[nodeIndex] == 1)
                    return cache.sourceGlobalBindTransforms[nodeIndex] * cache.sourceInverseLocalBindTransforms[nodeIndex] * sourceLocalTransforms[nodeIndex];

                sourceEvaluationState[nodeIndex] = 1;
                const int parentIndex = cache.sourceParentIndices[nodeIndex];
                if (parentIndex >= 0 && parentIndex < sourceNodeCount &&
                    cache.sourceNodesPresent[static_cast<size_t>(parentIndex)] &&
                    parentIndex != static_cast<int>(nodeIndex))
                {
                    sourceGlobalTransforms[nodeIndex] = evaluateSourceGlobal(static_cast<size_t>(parentIndex)) * sourceLocalTransforms[nodeIndex];
                }
                else
                {
                    // Preserve static ancestors that do not own animation
                    // channels (commonly the FBX/glTF armature conversion root).
                    sourceGlobalTransforms[nodeIndex] = cache.sourceGlobalBindTransforms[nodeIndex] *
                                                        cache.sourceInverseLocalBindTransforms[nodeIndex] *
                                                        sourceLocalTransforms[nodeIndex];
                }
                sourceEvaluationState[nodeIndex] = 2;
                return sourceGlobalTransforms[nodeIndex];
            };
            for (size_t nodeIndex = 0; nodeIndex < static_cast<size_t>(sourceNodeCount); ++nodeIndex)
            {
                if (cache.sourceNodesPresent[nodeIndex])
                    evaluateSourceGlobal(nodeIndex);
            }

            std::vector<glm::quat> desiredTargetGlobalRotations(skeleton.joints.size(), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            std::vector<uint8_t> hasDesiredTargetGlobalRotation(skeleton.joints.size(), 0);

            const auto &channels = clips[static_cast<size_t>(clipIndex)].channels;
            for (size_t channelIndex = 0; channelIndex < channels.size(); ++channelIndex)
            {
                const auto &channel = channels[channelIndex];
                const int resolvedIndex = channelIndex < cache.jointBindings.size() ? cache.jointBindings[channelIndex] : -1;
                if (resolvedIndex < 0 || resolvedIndex >= static_cast<int>(skeleton.joints.size()))
                    continue;

                const int mappingIndex = channelIndex < cache.mappingBindings.size() ? cache.mappingBindings[channelIndex] : -1;
                const auto *mapping = mappingIndex >= 0 && mappingIndex < static_cast<int>(skeleton.humanoidBoneMappings.size())
                                          ? &skeleton.humanoidBoneMappings[static_cast<size_t>(mappingIndex)]
                                          : nullptr;
                if (mapping && channel.path == render::AnimationTargetPath::Translation && !mapping->copyTranslation)
                    continue;

                const size_t jointIndex = static_cast<size_t>(resolvedIndex);
                if (!animatedLocals[jointIndex])
                {
                    DecomposeTransform(skeleton.joints[jointIndex].localBindTransform,
                                       translations[jointIndex], rotations[jointIndex], scales[jointIndex]);
                    animatedLocals[jointIndex] = 1;
                }

                glm::vec4 sample = SampleChannelValue(channel, time);
                glm::vec3 sourceBindTranslation{0.0f};
                glm::vec4 sourceBindRotation{0.0f, 0.0f, 0.0f, 1.0f};
                glm::vec3 sourceBindScale{1.0f};
                const bool applyBindRelativeRetarget = mapping && channel.hasSourceLocalBindTransform;
                if (applyBindRelativeRetarget && channel.nodeIndex >= 0 && channel.nodeIndex < sourceNodeCount)
                {
                    const size_t sourceNodeIndex = static_cast<size_t>(channel.nodeIndex);
                    sourceBindTranslation = cache.sourceBindTranslations[sourceNodeIndex];
                    sourceBindRotation = cache.sourceBindRotations[sourceNodeIndex];
                    sourceBindScale = cache.sourceBindScales[sourceNodeIndex];
                }
                switch (channel.path)
                {
                case render::AnimationTargetPath::Translation:
                    if (applyBindRelativeRetarget)
                    {
                        translations[jointIndex] += (glm::vec3(sample) - sourceBindTranslation) * mapping->translationScale;
                    }
                    else
                    {
                        translations[jointIndex] = glm::vec3(sample) * (mapping ? mapping->translationScale : 1.0f);
                    }
                    break;
                case render::AnimationTargetPath::Rotation:
                    if (applyBindRelativeRetarget && channel.hasSourceGlobalBindTransform &&
                        channel.nodeIndex >= 0 && channel.nodeIndex < sourceNodeCount)
                    {
                        glm::vec3 ignoredTranslation;
                        glm::vec3 ignoredScale;
                        const glm::vec4 &sourceGlobalBindRotation = cache.sourceGlobalBindRotations[static_cast<size_t>(channel.nodeIndex)];
                        glm::vec4 sourceGlobalAnimatedRotation;
                        const glm::vec4 &targetGlobalBindRotation = targetGlobalBindRotations[jointIndex];
                        DecomposeTransform(sourceGlobalTransforms[static_cast<size_t>(channel.nodeIndex)],
                                           ignoredTranslation, sourceGlobalAnimatedRotation, ignoredScale);

                        const glm::quat sourceGlobalBind(sourceGlobalBindRotation.w,
                                                         sourceGlobalBindRotation.x,
                                                         sourceGlobalBindRotation.y,
                                                         sourceGlobalBindRotation.z);
                        const glm::quat sourceGlobalAnimated(sourceGlobalAnimatedRotation.w,
                                                             sourceGlobalAnimatedRotation.x,
                                                             sourceGlobalAnimatedRotation.y,
                                                             sourceGlobalAnimatedRotation.z);
                        const glm::quat targetGlobalBind(targetGlobalBindRotation.w,
                                                         targetGlobalBindRotation.x,
                                                         targetGlobalBindRotation.y,
                                                         targetGlobalBindRotation.z);
                        const glm::quat correction = glm::quat(glm::radians(mapping->rotationOffsetDegrees));
                        const glm::quat sourceGlobalDelta = glm::normalize(sourceGlobalAnimated * glm::inverse(sourceGlobalBind));
                        desiredTargetGlobalRotations[jointIndex] = glm::normalize(sourceGlobalDelta * targetGlobalBind * correction);
                        hasDesiredTargetGlobalRotation[jointIndex] = 1;
                        break;
                    }
                    else if (applyBindRelativeRetarget)
                    {
                        const glm::quat sourceBind(sourceBindRotation.w, sourceBindRotation.x, sourceBindRotation.y, sourceBindRotation.z);
                        const glm::quat sourceAnimated(sample.w, sample.x, sample.y, sample.z);
                        const glm::quat targetBind(rotations[jointIndex].w, rotations[jointIndex].x, rotations[jointIndex].y, rotations[jointIndex].z);
                        const glm::quat correction = glm::quat(glm::radians(mapping->rotationOffsetDegrees));
                        const glm::quat sourceDelta = glm::normalize(glm::inverse(sourceBind) * sourceAnimated);
                        const glm::quat corrected = glm::normalize(targetBind * correction * sourceDelta);
                        sample = glm::vec4(corrected.x, corrected.y, corrected.z, corrected.w);
                    }
                    else if (mapping)
                    {
                        const glm::quat source(sample.w, sample.x, sample.y, sample.z);
                        const glm::quat correction = glm::quat(glm::radians(mapping->rotationOffsetDegrees));
                        const glm::quat corrected = glm::normalize(correction * source);
                        sample = glm::vec4(corrected.x, corrected.y, corrected.z, corrected.w);
                    }
                    rotations[jointIndex] = sample;
                    break;
                case render::AnimationTargetPath::Scale:
                    if (applyBindRelativeRetarget)
                    {
                        const glm::vec3 safeSourceBindScale(
                            std::abs(sourceBindScale.x) > 0.000001f ? sourceBindScale.x : 1.0f,
                            std::abs(sourceBindScale.y) > 0.000001f ? sourceBindScale.y : 1.0f,
                            std::abs(sourceBindScale.z) > 0.000001f ? sourceBindScale.z : 1.0f);
                        scales[jointIndex] *= glm::vec3(sample) / safeSourceBindScale;
                    }
                    else
                    {
                        scales[jointIndex] = glm::vec3(sample);
                    }
                    break;
                }
            }

            std::vector<glm::quat> targetGlobalRotations(skeleton.joints.size(), glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
            std::vector<uint8_t> targetRotationEvaluationState(skeleton.joints.size(), 0);
            std::function<glm::quat(size_t)> evaluateTargetRotation = [&](size_t jointIndex) -> glm::quat {
                if (targetRotationEvaluationState[jointIndex] == 2)
                    return targetGlobalRotations[jointIndex];

                targetRotationEvaluationState[jointIndex] = 1;
                const int parentIndex = skeleton.joints[jointIndex].parentJointIndex;
                const glm::quat parentGlobal = parentIndex >= 0 && parentIndex < static_cast<int>(skeleton.joints.size()) &&
                                                       parentIndex != static_cast<int>(jointIndex) &&
                                                       targetRotationEvaluationState[static_cast<size_t>(parentIndex)] != 1
                                                   ? evaluateTargetRotation(static_cast<size_t>(parentIndex))
                                                   : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

                glm::quat localRotation(rotations[jointIndex].w, rotations[jointIndex].x,
                                        rotations[jointIndex].y, rotations[jointIndex].z);
                if (hasDesiredTargetGlobalRotation[jointIndex])
                {
                    localRotation = glm::normalize(glm::inverse(parentGlobal) * desiredTargetGlobalRotations[jointIndex]);
                    rotations[jointIndex] = glm::vec4(localRotation.x, localRotation.y, localRotation.z, localRotation.w);
                    animatedLocals[jointIndex] = 1;
                }
                targetGlobalRotations[jointIndex] = glm::normalize(parentGlobal * localRotation);
                targetRotationEvaluationState[jointIndex] = 2;
                return targetGlobalRotations[jointIndex];
            };
            for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
                evaluateTargetRotation(jointIndex);

            for (size_t index = 0; index < skeleton.joints.size(); ++index)
            {
                if (animatedLocals[index])
                    localTransforms[index] = ComposeTransform(translations[index], rotations[index], scales[index]);
            }
            return localTransforms;
        }

        std::vector<glm::mat4> BlendLocalTransforms(
            const std::vector<glm::mat4> &source,
            const std::vector<glm::mat4> &destination,
            float blend)
        {
            std::vector<glm::mat4> blended(source.size(), glm::mat4(1.0f));
            for (size_t index = 0; index < source.size(); ++index)
            {
                glm::vec3 sourceTranslation;
                glm::vec4 sourceRotation;
                glm::vec3 sourceScale;
                glm::vec3 destinationTranslation;
                glm::vec4 destinationRotation;
                glm::vec3 destinationScale;
                DecomposeTransform(source[index], sourceTranslation, sourceRotation, sourceScale);
                DecomposeTransform(destination[index], destinationTranslation, destinationRotation, destinationScale);

                const glm::quat sourceQuat(sourceRotation.w, sourceRotation.x, sourceRotation.y, sourceRotation.z);
                const glm::quat destinationQuat(destinationRotation.w, destinationRotation.x, destinationRotation.y, destinationRotation.z);
                const glm::quat rotation = glm::normalize(glm::slerp(sourceQuat, destinationQuat, blend));
                blended[index] = ComposeTransform(
                    glm::mix(sourceTranslation, destinationTranslation, blend),
                    glm::vec4(rotation.x, rotation.y, rotation.z, rotation.w),
                    glm::mix(sourceScale, destinationScale, blend));
            }

            return blended;
        }

        std::vector<glm::mat4> BlendWeightedLocalTransforms(
            const std::vector<std::pair<std::vector<glm::mat4>, float>> &samples)
        {
            std::vector<glm::mat4> result;
            float accumulatedWeight = 0.0f;
            for (const auto &[transforms, weight] : samples)
            {
                if (weight <= 0.00001f)
                    continue;
                if (result.empty())
                {
                    result = transforms;
                    accumulatedWeight = weight;
                    continue;
                }
                const float nextWeight = accumulatedWeight + weight;
                result = BlendLocalTransforms(result, transforms, weight / nextWeight);
                accumulatedWeight = nextWeight;
            }
            return result;
        }

        std::vector<glm::mat4> BlendMaskedLocalTransforms(
            const std::vector<glm::mat4> &base,
            const std::vector<glm::mat4> &layer,
            const std::vector<glm::mat4> &reference,
            const std::vector<float> &mask,
            float layerWeight,
            assets::AnimationGraphLayerBlendMode mode)
        {
            std::vector<glm::mat4> result = base;
            const size_t count = std::min({base.size(), layer.size(), reference.size(), mask.size()});
            for (size_t index = 0; index < count; ++index)
            {
                const float weight = std::clamp(mask[index] * layerWeight, 0.0f, 1.0f);
                if (weight <= 0.00001f)
                    continue;

                glm::vec3 baseTranslation, layerTranslation, referenceTranslation;
                glm::vec4 baseRotation, layerRotation, referenceRotation;
                glm::vec3 baseScale, layerScale, referenceScale;
                DecomposeTransform(base[index], baseTranslation, baseRotation, baseScale);
                DecomposeTransform(layer[index], layerTranslation, layerRotation, layerScale);
                DecomposeTransform(reference[index], referenceTranslation, referenceRotation, referenceScale);

                const glm::quat baseQuat(baseRotation.w, baseRotation.x, baseRotation.y, baseRotation.z);
                const glm::quat layerQuat(layerRotation.w, layerRotation.x, layerRotation.y, layerRotation.z);
                if (mode == assets::AnimationGraphLayerBlendMode::Override)
                {
                    const glm::quat rotation = glm::normalize(glm::slerp(baseQuat, layerQuat, weight));
                    result[index] = ComposeTransform(
                        glm::mix(baseTranslation, layerTranslation, weight),
                        glm::vec4(rotation.x, rotation.y, rotation.z, rotation.w),
                        glm::mix(baseScale, layerScale, weight));
                    continue;
                }

                const glm::quat referenceQuat(referenceRotation.w, referenceRotation.x, referenceRotation.y, referenceRotation.z);
                const glm::quat deltaRotation = glm::normalize(glm::inverse(referenceQuat) * layerQuat);
                const glm::quat weightedDelta = glm::normalize(glm::slerp(glm::quat(1.0f, 0.0f, 0.0f, 0.0f), deltaRotation, weight));
                const glm::vec3 safeReferenceScale(
                    std::abs(referenceScale.x) > 0.000001f ? referenceScale.x : 1.0f,
                    std::abs(referenceScale.y) > 0.000001f ? referenceScale.y : 1.0f,
                    std::abs(referenceScale.z) > 0.000001f ? referenceScale.z : 1.0f);
                result[index] = ComposeTransform(
                    baseTranslation + (layerTranslation - referenceTranslation) * weight,
                    glm::vec4((baseQuat * weightedDelta).x, (baseQuat * weightedDelta).y,
                              (baseQuat * weightedDelta).z, (baseQuat * weightedDelta).w),
                    baseScale * glm::mix(glm::vec3(1.0f), layerScale / safeReferenceScale, weight));
            }
            return result;
        }

        bool IsDescendantOf(int candidate, int ancestor, const std::function<int(int)> &parentOf, int count)
        {
            int current = candidate;
            for (int guard = 0; guard < count && current >= 0 && current < count; ++guard)
            {
                if (current == ancestor)
                    return true;
                const int parent = parentOf(current);
                if (parent == current)
                    break;
                current = parent;
            }
            return false;
        }
    }

    void AnimationComponent::Update(float deltaTime)
    {
        EnsureDefaultGraph();
        if (!HasCurrentClip())
        {
            m_playing = false;
            return;
        }

        if (m_autoplay && !m_startedAutoplay)
        {
            m_playing = true;
            m_startedAutoplay = true;
        }

        if (!m_playing)
        {
            return;
        }

        m_layerTriggersToReset.clear();
        UpdateLayers(deltaTime);
        if (!m_states.empty())
        {
            UpdateGraph(deltaTime);
        }
        else
        {
            SetTime(m_time + deltaTime * m_speed);
        }
        for (const size_t parameterIndex : m_layerTriggersToReset)
        {
            if (parameterIndex < m_parameters.size() && m_parameters[parameterIndex].type == AnimationParameterType::Trigger)
                m_parameters[parameterIndex].boolValue = false;
        }
        m_jointMatricesDirty = true;
        m_nodeMatricesDirty = true;
    }

    std::vector<Property> AnimationComponent::Serialize() const
    {
        std::vector<std::string> clipNames;
        clipNames.reserve(m_clips.size());
        for (const auto &clip : m_clips)
        {
            clipNames.push_back(clip.name.empty() ? "Unnamed" : clip.name);
        }
        if (clipNames.empty())
        {
            clipNames.push_back("None");
        }

        std::vector<Property> properties{
            {"SourceAnimation", PropertyType::String, m_sourceAnimationPath},
            {"AnimationGraph", PropertyType::String, m_animationGraphAssetReference},
            {"CurrentClipIndex", PropertyType::Enum, std::to_string(std::clamp(m_currentClipIndex, 0, static_cast<int>(clipNames.size()) - 1)), clipNames},
            {"Time", PropertyType::Float, std::to_string(m_time)},
            {"Speed", PropertyType::Float, std::to_string(m_speed)},
            {"Playing", PropertyType::Bool, m_playing ? "true" : "false"},
            {"Looping", PropertyType::Bool, m_looping ? "true" : "false"},
            {"Autoplay", PropertyType::Bool, m_autoplay ? "true" : "false"},
            {"ClipCount", PropertyType::Int, std::to_string(m_clips.size())},
        };

        for (size_t clipIndex = 0; clipIndex < m_clips.size(); ++clipIndex)
        {
            const auto &clip = m_clips[clipIndex];
            const std::string prefix = std::string(kClipPrefix) + std::to_string(clipIndex) + ".";
            properties.push_back({prefix + "Name", PropertyType::String, clip.name});
            properties.push_back({prefix + "Duration", PropertyType::Float, std::to_string(clip.duration)});
            properties.push_back({prefix + "ChannelCount", PropertyType::Int, std::to_string(clip.channelCount)});
        }

        if (m_animationGraphAssetReference.empty())
        {
            properties.push_back({"Graph.DefaultStateIndex", PropertyType::Int, std::to_string(m_defaultStateIndex)});
            properties.push_back({"Graph.CurrentStateIndex", PropertyType::Int, std::to_string(m_graphCurrentStateIndex)});
            properties.push_back({"Graph.StateTime", PropertyType::Float, std::to_string(m_graphStateTime)});
            properties.push_back({"Graph.StateCount", PropertyType::Int, std::to_string(m_states.size())});
            for (size_t stateIndex = 0; stateIndex < m_states.size(); ++stateIndex)
            {
                const auto &state = m_states[stateIndex];
                const std::string prefix = std::string(kStatePrefix) + std::to_string(stateIndex) + ".";
                properties.push_back({prefix + "Name", PropertyType::String, state.name});
                properties.push_back({prefix + "ClipIndex", PropertyType::Int, std::to_string(state.clipIndex)});
                properties.push_back({prefix + "Speed", PropertyType::Float, std::to_string(state.speed)});
                properties.push_back({prefix + "Loop", PropertyType::Bool, state.loop ? "true" : "false"});
                properties.push_back({prefix + "TransitionCount", PropertyType::Int, std::to_string(state.transitions.size())});
                for (size_t transitionIndex = 0; transitionIndex < state.transitions.size(); ++transitionIndex)
                {
                    const auto &transition = state.transitions[transitionIndex];
                    const std::string transitionPrefix = prefix + "Transitions." + std::to_string(transitionIndex) + ".";
                    properties.push_back({transitionPrefix + "DestinationStateIndex", PropertyType::Int, std::to_string(transition.destinationStateIndex)});
                    properties.push_back({transitionPrefix + "Duration", PropertyType::Float, std::to_string(transition.duration)});
                    properties.push_back({transitionPrefix + "HasExitTime", PropertyType::Bool, transition.hasExitTime ? "true" : "false"});
                    properties.push_back({transitionPrefix + "ExitTime", PropertyType::Float, std::to_string(transition.exitTime)});
                    properties.push_back({transitionPrefix + "ConditionCount", PropertyType::Int, std::to_string(transition.conditions.size())});
                    for (size_t conditionIndex = 0; conditionIndex < transition.conditions.size(); ++conditionIndex)
                    {
                        const auto &condition = transition.conditions[conditionIndex];
                        const std::string conditionPrefix = transitionPrefix + "Conditions." + std::to_string(conditionIndex) + ".";
                        properties.push_back({conditionPrefix + "Parameter", PropertyType::String, condition.parameterName});
                        properties.push_back({conditionPrefix + "Mode", PropertyType::String, ConditionModeName(condition.mode)});
                        properties.push_back({conditionPrefix + "Threshold", PropertyType::Float, std::to_string(condition.threshold)});
                    }
                }
            }

            properties.push_back({"Graph.ParameterCount", PropertyType::Int, std::to_string(m_parameters.size())});
            for (size_t parameterIndex = 0; parameterIndex < m_parameters.size(); ++parameterIndex)
            {
                const auto &parameter = m_parameters[parameterIndex];
                const std::string prefix = std::string(kParameterPrefix) + std::to_string(parameterIndex) + ".";
                properties.push_back({prefix + "Name", PropertyType::String, parameter.name});
                properties.push_back({prefix + "Type", PropertyType::String, ParameterTypeName(parameter.type)});
                properties.push_back({prefix + "Float", PropertyType::Float, std::to_string(parameter.floatValue)});
                properties.push_back({prefix + "Int", PropertyType::Int, std::to_string(parameter.intValue)});
                properties.push_back({prefix + "Bool", PropertyType::Bool, parameter.boolValue ? "true" : "false"});
            }
        }

        return properties;
    }

    void AnimationComponent::Deserialize(const std::vector<Property> &properties)
    {
        int clipCount = 0;
        int stateCount = -1;
        int parameterCount = -1;
        for (const auto &property : properties)
        {
            if (property.name == "ClipCount")
            {
                clipCount = std::max(0, std::stoi(property.value));
            }
            else if (property.name == "Graph.StateCount")
            {
                stateCount = std::max(0, ParseIntSafe(property.value));
            }
            else if (property.name == "Graph.ParameterCount")
            {
                parameterCount = std::max(0, ParseIntSafe(property.value));
            }
        }

        if (clipCount > 0)
        {
            m_clips.assign(static_cast<size_t>(clipCount), render::AnimationClip{});
        }
        if (stateCount >= 0)
        {
            m_states.assign(static_cast<size_t>(stateCount), AnimationState{});
        }
        if (parameterCount >= 0)
        {
            m_parameters.assign(static_cast<size_t>(parameterCount), AnimationParameter{});
        }

        for (const auto &property : properties)
        {
            if (property.name == "SourceAnimation")
            {
                m_sourceAnimationPath = property.value;
            }
            else if (property.name == "AnimationGraph")
            {
                m_animationGraphAssetReference = property.value;
            }
            else if (property.name == "CurrentClipIndex")
            {
                m_currentClipIndex = std::stoi(property.value);
            }
            else if (property.name == "Time")
            {
                m_time = std::stof(property.value);
            }
            else if (property.name == "Speed")
            {
                m_speed = std::stof(property.value);
            }
            else if (property.name == "Playing")
            {
                m_playing = ParseBool(property.value);
            }
            else if (property.name == "Looping")
            {
                m_looping = ParseBool(property.value);
            }
            else if (property.name == "Autoplay")
            {
                m_autoplay = ParseBool(property.value);
            }
            else if (property.name == "Graph.DefaultStateIndex")
            {
                m_defaultStateIndex = ParseIntSafe(property.value);
            }
            else if (property.name == "Graph.CurrentStateIndex")
            {
                m_graphCurrentStateIndex = ParseIntSafe(property.value);
            }
            else if (property.name == "Graph.StateTime")
            {
                m_graphStateTime = ParseFloatSafe(property.value);
            }
            else if (property.name.rfind(kClipPrefix, 0) == 0)
            {
                const std::string remainder = property.name.substr(std::char_traits<char>::length(kClipPrefix));
                const auto separatorIndex = remainder.find('.');
                if (separatorIndex == std::string::npos)
                {
                    continue;
                }

                const size_t clipIndex = static_cast<size_t>(std::stoul(remainder.substr(0, separatorIndex)));
                if (clipIndex >= m_clips.size())
                {
                    continue;
                }

                const std::string fieldName = remainder.substr(separatorIndex + 1);
                if (fieldName == "Name")
                {
                    m_clips[clipIndex].name = property.value;
                }
                else if (fieldName == "Duration")
                {
                    m_clips[clipIndex].duration = std::stof(property.value);
                }
                else if (fieldName == "ChannelCount")
                {
                    m_clips[clipIndex].channelCount = std::stoi(property.value);
                }
            }
            else if (property.name.rfind(kStatePrefix, 0) == 0)
            {
                const std::string remainder = property.name.substr(std::char_traits<char>::length(kStatePrefix));
                const auto separatorIndex = remainder.find('.');
                if (separatorIndex == std::string::npos)
                {
                    continue;
                }

                const size_t stateIndex = static_cast<size_t>(std::stoul(remainder.substr(0, separatorIndex)));
                if (stateIndex >= m_states.size())
                {
                    continue;
                }

                const std::string fieldPath = remainder.substr(separatorIndex + 1);
                auto &state = m_states[stateIndex];
                if (fieldPath == "Name")
                {
                    state.name = property.value;
                }
                else if (fieldPath == "ClipIndex")
                {
                    state.clipIndex = ParseIntSafe(property.value);
                }
                else if (fieldPath == "Speed")
                {
                    state.speed = ParseFloatSafe(property.value, 1.0f);
                }
                else if (fieldPath == "Loop")
                {
                    state.loop = ParseBool(property.value);
                }
                else if (fieldPath == "TransitionCount")
                {
                    state.transitions.assign(static_cast<size_t>(std::max(0, ParseIntSafe(property.value))), AnimationTransition{});
                }
                else if (fieldPath.rfind("Transitions.", 0) == 0)
                {
                    const std::string transitionPath = fieldPath.substr(std::char_traits<char>::length("Transitions."));
                    const auto transitionSeparator = transitionPath.find('.');
                    if (transitionSeparator == std::string::npos)
                    {
                        continue;
                    }

                    const size_t transitionIndex = static_cast<size_t>(std::stoul(transitionPath.substr(0, transitionSeparator)));
                    if (transitionIndex >= state.transitions.size())
                    {
                        continue;
                    }

                    auto &transition = state.transitions[transitionIndex];
                    const std::string transitionField = transitionPath.substr(transitionSeparator + 1);
                    if (transitionField == "DestinationStateIndex")
                    {
                        transition.destinationStateIndex = ParseIntSafe(property.value, -1);
                    }
                    else if (transitionField == "Duration")
                    {
                        transition.duration = ParseFloatSafe(property.value, 0.15f);
                    }
                    else if (transitionField == "HasExitTime")
                    {
                        transition.hasExitTime = ParseBool(property.value);
                    }
                    else if (transitionField == "ExitTime")
                    {
                        transition.exitTime = ParseFloatSafe(property.value, 0.9f);
                    }
                    else if (transitionField == "ConditionCount")
                    {
                        transition.conditions.assign(static_cast<size_t>(std::max(0, ParseIntSafe(property.value))), AnimationCondition{});
                    }
                    else if (transitionField.rfind("Conditions.", 0) == 0)
                    {
                        const std::string conditionPath = transitionField.substr(std::char_traits<char>::length("Conditions."));
                        const auto conditionSeparator = conditionPath.find('.');
                        if (conditionSeparator == std::string::npos)
                        {
                            continue;
                        }

                        const size_t conditionIndex = static_cast<size_t>(std::stoul(conditionPath.substr(0, conditionSeparator)));
                        if (conditionIndex >= transition.conditions.size())
                        {
                            continue;
                        }

                        auto &condition = transition.conditions[conditionIndex];
                        const std::string conditionField = conditionPath.substr(conditionSeparator + 1);
                        if (conditionField == "Parameter")
                        {
                            condition.parameterName = property.value;
                        }
                        else if (conditionField == "Mode")
                        {
                            condition.mode = ParseConditionMode(property.value);
                        }
                        else if (conditionField == "Threshold")
                        {
                            condition.threshold = ParseFloatSafe(property.value);
                        }
                    }
                }
            }
            else if (property.name.rfind(kParameterPrefix, 0) == 0)
            {
                const std::string remainder = property.name.substr(std::char_traits<char>::length(kParameterPrefix));
                const auto separatorIndex = remainder.find('.');
                if (separatorIndex == std::string::npos)
                {
                    continue;
                }

                const size_t parameterIndex = static_cast<size_t>(std::stoul(remainder.substr(0, separatorIndex)));
                if (parameterIndex >= m_parameters.size())
                {
                    continue;
                }

                auto &parameter = m_parameters[parameterIndex];
                const std::string fieldName = remainder.substr(separatorIndex + 1);
                if (fieldName == "Name")
                {
                    parameter.name = property.value;
                }
                else if (fieldName == "Type")
                {
                    parameter.type = ParseParameterType(property.value);
                }
                else if (fieldName == "Float")
                {
                    parameter.floatValue = ParseFloatSafe(property.value);
                }
                else if (fieldName == "Int")
                {
                    parameter.intValue = ParseIntSafe(property.value);
                }
                else if (fieldName == "Bool")
                {
                    parameter.boolValue = ParseBool(property.value);
                }
            }
        }

        ClampCurrentClipIndex();
        ClampGraph();
        SetTime(m_time);
        m_playing = m_playing && HasCurrentClip();

        const bool hasSerializedGraph = stateCount >= 0 || parameterCount >= 0;
        auto serializedStates = m_states;
        auto serializedParameters = m_parameters;
        const int serializedDefaultStateIndex = m_defaultStateIndex;
        const int serializedCurrentStateIndex = m_graphCurrentStateIndex;
        const float serializedGraphStateTime = m_graphStateTime;

        if (!m_sourceAnimationPath.empty())
        {
            SetAnimationAssetReference(m_sourceAnimationPath);
        }

        if (!m_animationGraphAssetReference.empty())
        {
            SetAnimationGraphAssetReference(m_animationGraphAssetReference);
        }
        else if (hasSerializedGraph)
        {
            m_states = std::move(serializedStates);
            m_parameters = std::move(serializedParameters);
            m_defaultStateIndex = serializedDefaultStateIndex;
            m_graphCurrentStateIndex = serializedCurrentStateIndex;
            m_graphStateTime = serializedGraphStateTime;
            m_transition = {};
            m_graphStarted = true;
            ClampGraph();
            if (!m_states.empty())
            {
                m_currentClipIndex = m_states[static_cast<size_t>(m_graphCurrentStateIndex)].clipIndex;
                m_time = m_graphStateTime;
            }
        }

        EnsureDefaultGraph();
        m_jointMatricesDirty = true;
        m_nodeMatricesDirty = true;
    }

    bool AnimationComponent::SetAnimationAssetReference(std::string animationAssetReference)
    {
        if (animationAssetReference.empty())
        {
            m_sourceAnimationPath.clear();
            m_clips.clear();
            m_states.clear();
            m_parameters.clear();
            m_parameterLookup.clear();
            m_boneMasks.clear();
            m_layers.clear();
            m_retargetBindingSkeleton = nullptr;
            m_retargetClipCaches.clear();
            m_transition = {};
            m_currentClipIndex = 0;
            m_defaultStateIndex = 0;
            m_graphCurrentStateIndex = 0;
            m_graphStateTime = 0.0f;
            SetTime(0.0f);
            m_playing = false;
            m_jointMatricesDirty = true;
            m_nodeMatricesDirty = true;
            return true;
        }

        auto &engine = core::Engine::GetInstance();
        std::vector<render::AnimationClip> animationClips;
        if (!engine.GetAssetManager().LoadAnimationAsset(animationAssetReference, animationClips) || animationClips.empty())
        {
            const std::string resolvedPath = engine.GetAssetManager().ResolveMeshAssetSourcePath(animationAssetReference);
            const auto importedMeshAsset = engine.ImportMeshAsset(resolvedPath);
            if (importedMeshAsset.animations)
            {
                animationClips = *importedMeshAsset.animations;
            }
        }

        if (animationClips.empty())
        {
            return false;
        }

        const int clipIndex = m_currentClipIndex;
        const float time = m_time;
        const bool playing = m_playing;
        m_sourceAnimationPath = std::move(animationAssetReference);
        SetClipsFromImportedAnimations(animationClips);
        if (!m_animationGraphAssetReference.empty())
        {
            SetAnimationGraphAssetReference(m_animationGraphAssetReference);
        }
        else
        {
            SetCurrentClipIndex(clipIndex);
        }
        SetTime(time);
        m_playing = playing && HasCurrentClip();
        return true;
    }

    bool AnimationComponent::SetAnimationGraphAssetReference(std::string animationGraphAssetReference)
    {
        if (animationGraphAssetReference.empty())
        {
            m_animationGraphAssetReference.clear();
            m_states.clear();
            m_parameters.clear();
            m_parameterLookup.clear();
            EnsureDefaultGraph();
            ResetGraphPlayback();
            m_jointMatricesDirty = true;
            m_nodeMatricesDirty = true;
            return true;
        }

        auto &engine = core::Engine::GetInstance();
        bool loaded = false;
        const auto graph = engine.GetAssetManager().LoadAnimationGraphAsset(animationGraphAssetReference, &loaded);
        if (!loaded)
        {
            return false;
        }

        m_animationGraphAssetReference = std::move(animationGraphAssetReference);
        ApplyAnimationGraphAsset(graph);
        ResetGraphPlayback();
        m_jointMatricesDirty = true;
        m_nodeMatricesDirty = true;
        return true;
    }

    void AnimationComponent::SetClipsFromImportedAnimations(const std::vector<render::AnimationClip> &animations)
    {
        m_clips = animations;
        m_retargetBindingSkeleton = nullptr;
        m_retargetClipCaches.clear();
        for (size_t index = 0; index < m_clips.size(); ++index)
        {
            auto &clip = m_clips[index];
            if (clip.name.empty())
            {
                clip.name = "Animation " + std::to_string(index);
            }
            clip.duration = std::max(0.0f, clip.duration);
            clip.channelCount = std::max(clip.channelCount, static_cast<int>(clip.channels.size()));
        }

        ClampCurrentClipIndex();
        EnsureDefaultGraph();
        ClampGraph();
        SetTime(0.0f);
        ResetGraphPlayback();
        m_startedAutoplay = false;
        m_playing = m_autoplay && HasCurrentClip();
        m_jointMatricesDirty = true;
        m_nodeMatricesDirty = true;
    }

    void AnimationComponent::SetCurrentClipIndex(int clipIndex)
    {
        m_currentClipIndex = clipIndex;
        ClampCurrentClipIndex();
        for (size_t stateIndex = 0; stateIndex < m_states.size(); ++stateIndex)
        {
            if (m_states[stateIndex].clipIndex == m_currentClipIndex)
            {
                SetCurrentStateIndex(static_cast<int>(stateIndex));
                break;
            }
        }
        SetTime(0.0f);
        m_startedAutoplay = false;
        m_jointMatricesDirty = true;
        m_nodeMatricesDirty = true;
    }

    int AnimationComponent::FindClipIndex(std::string_view clipName) const
    {
        for (size_t index = 0; index < m_clips.size(); ++index)
        {
            if (m_clips[index].name == clipName)
            {
                return static_cast<int>(index);
            }
        }

        return -1;
    }

    bool AnimationComponent::Play(std::string_view clipName)
    {
        const int clipIndex = FindClipIndex(clipName);
        if (clipIndex < 0)
        {
            return false;
        }

        SetCurrentClipIndex(clipIndex);
        Play();
        return true;
    }

    void AnimationComponent::Play()
    {
        EnsureDefaultGraph();
        if (!m_graphStarted)
        {
            ResetGraphPlayback();
        }
        m_playing = HasCurrentClip();
        m_startedAutoplay = true;
    }

    void AnimationComponent::Stop()
    {
        m_playing = false;
        SetTime(0.0f);
        ResetGraphPlayback();
        m_jointMatricesDirty = true;
        m_nodeMatricesDirty = true;
    }

    void AnimationComponent::SetTime(float time)
    {
        if (!HasCurrentClip())
        {
            m_time = 0.0f;
            m_jointMatricesDirty = true;
            m_nodeMatricesDirty = true;
            return;
        }

        const float duration = GetCurrentClipDuration();
        if (duration <= 0.0f)
        {
            m_time = 0.0f;
            m_playing = false;
            m_jointMatricesDirty = true;
            m_nodeMatricesDirty = true;
            return;
        }

        const float previousTime = m_time;
        if (m_looping)
        {
            m_time = std::fmod(time, duration);
            if (m_time < 0.0f)
            {
                m_time += duration;
            }
            m_jointMatricesDirty = m_jointMatricesDirty || std::abs(previousTime - m_time) > 0.00001f;
            m_nodeMatricesDirty = m_nodeMatricesDirty || std::abs(previousTime - m_time) > 0.00001f;
            return;
        }

        m_time = std::clamp(time, 0.0f, duration);
        if (time >= duration || time < 0.0f)
        {
            m_playing = false;
        }
        m_jointMatricesDirty = m_jointMatricesDirty || std::abs(previousTime - m_time) > 0.00001f;
        m_nodeMatricesDirty = m_nodeMatricesDirty || std::abs(previousTime - m_time) > 0.00001f;
    }

    float AnimationComponent::GetCurrentClipDuration() const
    {
        return HasCurrentClip() ? m_clips[static_cast<size_t>(m_currentClipIndex)].duration : 0.0f;
    }

    bool AnimationComponent::HasCurrentClip() const
    {
        return m_currentClipIndex >= 0 && m_currentClipIndex < static_cast<int>(m_clips.size());
    }

    void AnimationComponent::ClampCurrentClipIndex()
    {
        if (m_clips.empty())
        {
            m_currentClipIndex = 0;
            return;
        }

        m_currentClipIndex = std::clamp(m_currentClipIndex, 0, static_cast<int>(m_clips.size()) - 1);
    }

    void AnimationComponent::EnsureDefaultGraph()
    {
        if (!m_states.empty() || m_clips.empty())
        {
            return;
        }

        m_states.reserve(m_clips.size());
        for (size_t clipIndex = 0; clipIndex < m_clips.size(); ++clipIndex)
        {
            AnimationState state;
            state.name = m_clips[clipIndex].name.empty() ? "State " + std::to_string(clipIndex) : m_clips[clipIndex].name;
            state.clipIndex = static_cast<int>(clipIndex);
            state.speed = 1.0f;
            state.loop = m_looping;
            m_states.push_back(std::move(state));
        }

        m_defaultStateIndex = std::clamp(m_currentClipIndex, 0, static_cast<int>(m_states.size()) - 1);
        m_graphCurrentStateIndex = m_defaultStateIndex;
        m_graphStateTime = m_time;
        m_graphStarted = false;
    }

    void AnimationComponent::ClampGraph()
    {
        if (m_states.empty())
        {
            m_defaultStateIndex = 0;
            m_graphCurrentStateIndex = 0;
            m_transition = {};
        }
        else
        {
            m_defaultStateIndex = std::clamp(m_defaultStateIndex, 0, static_cast<int>(m_states.size()) - 1);
            m_graphCurrentStateIndex = std::clamp(m_graphCurrentStateIndex, 0, static_cast<int>(m_states.size()) - 1);
        }

        for (auto &state : m_states)
        {
            if (m_clips.empty())
            {
                state.clipIndex = 0;
            }
            else
            {
                state.clipIndex = std::clamp(state.clipIndex, 0, static_cast<int>(m_clips.size()) - 1);
            }
            for (auto &point : state.blendSpacePoints)
                point.clipIndex = m_clips.empty() ? 0 : std::clamp(point.clipIndex, 0, static_cast<int>(m_clips.size()) - 1);
            if (!state.blendSpacePoints.empty())
                state.clipIndex = state.blendSpacePoints.front().clipIndex;

            state.speed = std::max(0.0f, state.speed);
            for (auto &transition : state.transitions)
            {
                transition.duration = std::max(0.0f, transition.duration);
                transition.exitTime = std::max(0.0f, transition.exitTime);
                if (transition.destinationStateIndex >= static_cast<int>(m_states.size()))
                {
                    transition.destinationStateIndex = -1;
                }
            }
        }

        for (auto &layer : m_layers)
        {
            if (!layer.graphReference.empty())
            {
                layer.graphDefaultStateIndex = layer.graphStates.empty() ? 0 : std::clamp(
                    layer.graphDefaultStateIndex, 0, static_cast<int>(layer.graphStates.size()) - 1);
                layer.graphCurrentStateIndex = layer.graphStates.empty() ? 0 : std::clamp(
                    layer.graphCurrentStateIndex, 0, static_cast<int>(layer.graphStates.size()) - 1);
                for (auto &state : layer.graphStates)
                {
                    state.clipIndex = m_clips.empty() ? 0 : std::clamp(state.clipIndex, 0, static_cast<int>(m_clips.size()) - 1);
                    for (auto &point : state.blendSpacePoints)
                        point.clipIndex = m_clips.empty() ? 0 : std::clamp(point.clipIndex, 0, static_cast<int>(m_clips.size()) - 1);
                    if (!state.blendSpacePoints.empty())
                        state.clipIndex = state.blendSpacePoints.front().clipIndex;
                    state.speed = std::max(0.0f, state.speed);
                    for (auto &transition : state.transitions)
                    {
                        transition.duration = std::max(0.0f, transition.duration);
                        transition.exitTime = std::max(0.0f, transition.exitTime);
                        if (transition.destinationStateIndex < 0 ||
                            transition.destinationStateIndex >= static_cast<int>(layer.graphStates.size()))
                            transition.destinationStateIndex = -1;
                    }
                }
                layer.clipValid = !layer.graphStates.empty() && !m_clips.empty();
            }
            else
            {
                layer.clipValid = layer.clipIndex >= 0 && layer.clipIndex < static_cast<int>(m_clips.size());
            }
            layer.weight = std::clamp(layer.weight, 0.0f, 1.0f);
            layer.speed = std::max(0.0f, layer.speed);
            layer.fadeIn = std::max(0.0f, layer.fadeIn);
            layer.fadeOut = std::max(0.0f, layer.fadeOut);
        }

        m_parameterLookup.clear();
        for (size_t parameterIndex = 0; parameterIndex < m_parameters.size(); ++parameterIndex)
        {
            if (!m_parameters[parameterIndex].name.empty())
            {
                m_parameterLookup[m_parameters[parameterIndex].name] = parameterIndex;
            }
        }
    }

    void AnimationComponent::ApplyAnimationGraphAsset(const assets::AnimationGraphAsset &graph)
    {
        m_states.clear();
        m_parameters.clear();
        m_parameterLookup.clear();
        m_boneMasks = graph.boneMasks;
        m_layers.clear();

        auto &assetManager = core::Engine::GetInstance().GetAssetManager();
        std::unordered_map<std::string, int> clipIndexByReference;
        auto addParameterIfMissing = [this](const assets::AnimationGraphParameter &assetParameter)
        {
            const bool exists = std::any_of(m_parameters.begin(), m_parameters.end(),
                                            [&assetParameter](const AnimationParameter &parameter)
                                            { return parameter.name == assetParameter.name; });
            if (exists || assetParameter.name.empty())
                return;
            m_parameters.push_back(AnimationParameter{
                .name = assetParameter.name,
                .type = ConvertParameterType(assetParameter.type),
                .floatValue = assetParameter.floatValue,
                .intValue = assetParameter.intValue,
                .boolValue = assetParameter.boolValue,
            });
        };
        for (const auto &assetParameter : graph.parameters)
            addParameterIfMissing(assetParameter);

        auto resolveClipIndex = [this, &assetManager, &clipIndexByReference](
                                    const std::string &clipReference,
                                    const std::string &clipName,
                                    int fallbackIndex)
        {
            int clipIndex = fallbackIndex;
            if (!clipReference.empty())
            {
                const auto cached = clipIndexByReference.find(clipReference);
                if (cached != clipIndexByReference.end())
                    return cached->second;
                render::AnimationClip clip;
                if (assetManager.LoadAnimationClipAsset(clipReference, clip))
                {
                    if (!clipName.empty())
                        clip.name = clipName;
                    m_clips.push_back(std::move(clip));
                    clipIndex = static_cast<int>(m_clips.size()) - 1;
                    clipIndexByReference[clipReference] = clipIndex;
                }
            }
            else if (!clipName.empty())
            {
                const int namedClipIndex = FindClipIndex(clipName);
                if (namedClipIndex >= 0)
                    clipIndex = namedClipIndex;
            }
            return clipIndex;
        };

        std::unordered_map<int, int> stateIndexById;
        m_states.reserve(graph.states.size());
        for (const auto &assetState : graph.states)
        {
            AnimationState state;
            state.name = assetState.name.empty() ? "State " + std::to_string(m_states.size()) : assetState.name;
            state.clipIndex = resolveClipIndex(assetState.clipReference, assetState.clipName, assetState.clipIndex);
            state.speed = assetState.speed;
            state.loop = assetState.loop;
            state.blendSpaceParameterX = assetState.blendSpaceParameterX;
            state.blendSpaceParameterY = assetState.blendSpaceParameterY;
            for (const auto &assetPoint : assetState.blendSpacePoints)
            {
                state.blendSpacePoints.push_back(AnimationState::BlendSpacePoint{
                    .clipIndex = resolveClipIndex(assetPoint.clipReference, assetPoint.clipName, assetPoint.clipIndex),
                    .positionX = assetPoint.positionX,
                    .positionY = assetPoint.positionY,
                });
            }
            if (!state.blendSpacePoints.empty())
                state.clipIndex = state.blendSpacePoints.front().clipIndex;
            stateIndexById[assetState.id] = static_cast<int>(m_states.size());
            m_states.push_back(std::move(state));
        }

        m_layers.reserve(graph.layers.size());
        for (const auto &assetLayer : graph.layers)
        {
            AnimationLayer layer;
            layer.name = assetLayer.name.empty() ? "Layer " + std::to_string(m_layers.size() + 1) : assetLayer.name;
            layer.graphReference = assetLayer.graphReference;
            layer.clipIndex = layer.graphReference.empty()
                                  ? resolveClipIndex(assetLayer.clipReference, assetLayer.clipName, assetLayer.clipIndex)
                                  : assetLayer.clipIndex;
            if (!layer.graphReference.empty())
            {
                bool graphLoaded = false;
                const auto layerGraph = assetManager.LoadAnimationGraphAsset(layer.graphReference, &graphLoaded);
                if (graphLoaded)
                {
                    for (const auto &parameter : layerGraph.parameters)
                        addParameterIfMissing(parameter);

                    std::unordered_map<int, int> layerStateIndexById;
                    layer.graphStates.reserve(layerGraph.states.size());
                    for (const auto &assetState : layerGraph.states)
                    {
                        AnimationState state;
                        state.name = assetState.name.empty() ? "State " + std::to_string(layer.graphStates.size()) : assetState.name;
                        state.clipIndex = resolveClipIndex(assetState.clipReference, assetState.clipName, assetState.clipIndex);
                        state.speed = assetState.speed;
                        state.loop = assetState.loop;
                        state.blendSpaceParameterX = assetState.blendSpaceParameterX;
                        state.blendSpaceParameterY = assetState.blendSpaceParameterY;
                        for (const auto &assetPoint : assetState.blendSpacePoints)
                        {
                            state.blendSpacePoints.push_back(AnimationState::BlendSpacePoint{
                                .clipIndex = resolveClipIndex(assetPoint.clipReference, assetPoint.clipName, assetPoint.clipIndex),
                                .positionX = assetPoint.positionX,
                                .positionY = assetPoint.positionY,
                            });
                        }
                        if (!state.blendSpacePoints.empty())
                            state.clipIndex = state.blendSpacePoints.front().clipIndex;
                        layerStateIndexById[assetState.id] = static_cast<int>(layer.graphStates.size());
                        layer.graphStates.push_back(std::move(state));
                    }
                    for (const auto &assetTransition : layerGraph.transitions)
                    {
                        const auto fromIt = layerStateIndexById.find(assetTransition.fromStateId);
                        const auto toIt = layerStateIndexById.find(assetTransition.toStateId);
                        if (fromIt == layerStateIndexById.end() || toIt == layerStateIndexById.end())
                            continue;
                        AnimationTransition transition;
                        transition.destinationStateIndex = toIt->second;
                        transition.duration = assetTransition.duration;
                        transition.hasExitTime = assetTransition.hasExitTime;
                        transition.exitTime = assetTransition.exitTime;
                        for (const auto &condition : assetTransition.conditions)
                            transition.conditions.push_back(AnimationCondition{
                                .parameterName = condition.parameterName,
                                .mode = ConvertConditionMode(condition.mode),
                                .threshold = condition.threshold,
                            });
                        layer.graphStates[static_cast<size_t>(fromIt->second)].transitions.push_back(std::move(transition));
                    }
                    const auto defaultState = layerStateIndexById.find(layerGraph.defaultStateId);
                    layer.graphDefaultStateIndex = defaultState == layerStateIndexById.end() ? 0 : defaultState->second;
                    layer.graphCurrentStateIndex = layer.graphDefaultStateIndex;
                }
            }
            layer.maskId = assetLayer.maskId;
            layer.blendMode = assetLayer.blendMode;
            layer.weight = std::clamp(assetLayer.weight, 0.0f, 1.0f);
            layer.weightParameter = assetLayer.weightParameter;
            layer.activationParameter = assetLayer.activationParameter;
            layer.speed = std::max(0.0f, assetLayer.speed);
            layer.fadeIn = std::max(0.0f, assetLayer.fadeIn);
            layer.fadeOut = std::max(0.0f, assetLayer.fadeOut);
            layer.loop = assetLayer.loop;
            layer.restartOnActivation = assetLayer.restartOnActivation;
            layer.enabled = assetLayer.enabled;
            m_layers.push_back(std::move(layer));
        }

        for (const auto &assetTransition : graph.transitions)
        {
            const auto fromIt = stateIndexById.find(assetTransition.fromStateId);
            const auto toIt = stateIndexById.find(assetTransition.toStateId);
            if (fromIt == stateIndexById.end() || toIt == stateIndexById.end())
            {
                continue;
            }

            AnimationTransition transition;
            transition.destinationStateIndex = toIt->second;
            transition.duration = assetTransition.duration;
            transition.hasExitTime = assetTransition.hasExitTime;
            transition.exitTime = assetTransition.exitTime;
            transition.conditions.reserve(assetTransition.conditions.size());
            for (const auto &assetCondition : assetTransition.conditions)
            {
                transition.conditions.push_back(AnimationCondition{
                    .parameterName = assetCondition.parameterName,
                    .mode = ConvertConditionMode(assetCondition.mode),
                    .threshold = assetCondition.threshold,
                });
            }
            m_states[static_cast<size_t>(fromIt->second)].transitions.push_back(std::move(transition));
        }

        const auto defaultIt = stateIndexById.find(graph.defaultStateId);
        m_defaultStateIndex = defaultIt == stateIndexById.end() ? 0 : defaultIt->second;
        m_graphCurrentStateIndex = m_defaultStateIndex;
        m_graphStateTime = 0.0f;
        m_transition = {};
        m_graphStarted = false;
        ClampGraph();
    }

    void AnimationComponent::ResetGraphPlayback()
    {
        ClampGraph();
        m_graphCurrentStateIndex = m_defaultStateIndex;
        m_graphStateTime = 0.0f;
        m_transition = {};
        m_graphStarted = true;
        for (auto &layer : m_layers)
        {
            layer.time = 0.0f;
            layer.graphCurrentStateIndex = layer.graphDefaultStateIndex;
            layer.graphStateTime = 0.0f;
            layer.graphTransitionActive = false;
            layer.graphTransitionSourceStateIndex = -1;
            layer.graphTransitionDestinationStateIndex = -1;
            layer.graphTransitionSourceTime = 0.0f;
            layer.graphTransitionDestinationTime = 0.0f;
            layer.graphTransitionElapsed = 0.0f;
            layer.currentWeight = 0.0f;
            layer.playing = layer.enabled && layer.clipValid && layer.activationParameter.empty();
            layer.wasActive = false;
        }
        if (!m_states.empty())
        {
            m_currentClipIndex = m_states[static_cast<size_t>(m_graphCurrentStateIndex)].clipIndex;
            m_time = m_graphStateTime;
        }
    }

    float AnimationComponent::GetClipDuration(int clipIndex) const
    {
        return clipIndex >= 0 && clipIndex < static_cast<int>(m_clips.size()) ? m_clips[static_cast<size_t>(clipIndex)].duration : 0.0f;
    }

    std::vector<AnimationComponent::BlendSpaceSample> AnimationComponent::ResolveBlendSpaceSamples(const AnimationState &state) const
    {
        const auto &points = state.blendSpacePoints;
        if (points.empty())
            return {{state.clipIndex, 1.0f}};

        const float x = GetFloat(state.blendSpaceParameterX);
        const float y = GetFloat(state.blendSpaceParameterY);
        constexpr float epsilon = 0.00001f;

        for (const auto &point : points)
        {
            const float dx = x - point.positionX;
            const float dy = y - point.positionY;
            if (dx * dx + dy * dy <= epsilon * epsilon)
                return {{point.clipIndex, 1.0f}};
        }

        float bestTriangleArea = std::numeric_limits<float>::max();
        std::vector<BlendSpaceSample> bestTriangle;
        for (size_t first = 0; first < points.size(); ++first)
        {
            for (size_t second = first + 1; second < points.size(); ++second)
            {
                for (size_t third = second + 1; third < points.size(); ++third)
                {
                    const auto &a = points[first];
                    const auto &b = points[second];
                    const auto &c = points[third];
                    const float denominator = (b.positionY - c.positionY) * (a.positionX - c.positionX) +
                                              (c.positionX - b.positionX) * (a.positionY - c.positionY);
                    const float area = std::abs(denominator);
                    if (area <= epsilon || area >= bestTriangleArea)
                        continue;

                    const float weightA = ((b.positionY - c.positionY) * (x - c.positionX) +
                                           (c.positionX - b.positionX) * (y - c.positionY)) /
                                          denominator;
                    const float weightB = ((c.positionY - a.positionY) * (x - c.positionX) +
                                           (a.positionX - c.positionX) * (y - c.positionY)) /
                                          denominator;
                    const float weightC = 1.0f - weightA - weightB;
                    if (weightA < -epsilon || weightB < -epsilon || weightC < -epsilon)
                        continue;

                    const float clampedA = std::max(0.0f, weightA);
                    const float clampedB = std::max(0.0f, weightB);
                    const float clampedC = std::max(0.0f, weightC);
                    const float total = clampedA + clampedB + clampedC;
                    bestTriangleArea = area;
                    bestTriangle = {
                        {a.clipIndex, clampedA / total},
                        {b.clipIndex, clampedB / total},
                        {c.clipIndex, clampedC / total},
                    };
                }
            }
        }
        if (!bestTriangle.empty())
            return bestTriangle;

        if (points.size() == 1)
            return {{points.front().clipIndex, 1.0f}};

        float bestDistanceSquared = std::numeric_limits<float>::max();
        size_t bestFirst = 0;
        size_t bestSecond = 1;
        float bestT = 0.0f;
        for (size_t first = 0; first < points.size(); ++first)
        {
            for (size_t second = first + 1; second < points.size(); ++second)
            {
                const float edgeX = points[second].positionX - points[first].positionX;
                const float edgeY = points[second].positionY - points[first].positionY;
                const float lengthSquared = edgeX * edgeX + edgeY * edgeY;
                if (lengthSquared <= epsilon)
                    continue;
                const float t = std::clamp(((x - points[first].positionX) * edgeX +
                                            (y - points[first].positionY) * edgeY) /
                                               lengthSquared,
                                           0.0f, 1.0f);
                const float closestX = points[first].positionX + edgeX * t;
                const float closestY = points[first].positionY + edgeY * t;
                const float dx = x - closestX;
                const float dy = y - closestY;
                const float distanceSquared = dx * dx + dy * dy;
                if (distanceSquared < bestDistanceSquared)
                {
                    bestDistanceSquared = distanceSquared;
                    bestFirst = first;
                    bestSecond = second;
                    bestT = t;
                }
            }
        }
        return {
            {points[bestFirst].clipIndex, 1.0f - bestT},
            {points[bestSecond].clipIndex, bestT},
        };
    }

    float AnimationComponent::GetBlendSpaceClipTime(const AnimationState &state, int clipIndex, float stateTime) const
    {
        if (state.blendSpacePoints.empty())
            return stateTime;
        const float stateDuration = GetClipDuration(state.clipIndex);
        const float clipDuration = GetClipDuration(clipIndex);
        if (stateDuration <= 0.00001f || clipDuration <= 0.00001f)
            return stateTime;
        const float phase = state.loop ? std::fmod(std::max(0.0f, stateTime) / stateDuration, 1.0f)
                                       : std::clamp(stateTime / stateDuration, 0.0f, 1.0f);
        return phase * clipDuration;
    }

    float AnimationComponent::GetBlendSpaceDuration(const AnimationState &state) const
    {
        if (state.blendSpacePoints.empty())
            return GetClipDuration(state.clipIndex);
        float duration = 0.0f;
        float totalWeight = 0.0f;
        for (const auto &sample : ResolveBlendSpaceSamples(state))
        {
            const float clipDuration = GetClipDuration(sample.clipIndex);
            if (clipDuration <= 0.00001f || sample.weight <= 0.00001f)
                continue;
            duration += clipDuration * sample.weight;
            totalWeight += sample.weight;
        }
        return totalWeight > 0.00001f ? duration / totalWeight : GetClipDuration(state.clipIndex);
    }

    float AnimationComponent::AdvanceStateTime(
        const AnimationState &state, float time, float deltaTime, float speedMultiplier, bool *finished) const
    {
        float speed = state.speed * speedMultiplier;
        if (!state.blendSpacePoints.empty())
        {
            const float referenceDuration = GetClipDuration(state.clipIndex);
            const float blendedDuration = GetBlendSpaceDuration(state);
            if (referenceDuration > 0.00001f && blendedDuration > 0.00001f)
                speed *= referenceDuration / blendedDuration;
        }
        return AdvanceClipTime(state.clipIndex, time, deltaTime, speed, state.loop, finished);
    }

    float AnimationComponent::AdvanceClipTime(int clipIndex, float time, float deltaTime, float speed, bool looping, bool *finished) const
    {
        if (finished)
        {
            *finished = false;
        }

        const float duration = GetClipDuration(clipIndex);
        if (duration <= 0.0f)
        {
            if (finished)
            {
                *finished = true;
            }
            return 0.0f;
        }

        float nextTime = time + deltaTime * speed * m_speed;
        if (looping)
        {
            nextTime = std::fmod(nextTime, duration);
            if (nextTime < 0.0f)
            {
                nextTime += duration;
            }
            return nextTime;
        }

        if (nextTime >= duration)
        {
            nextTime = duration;
            if (finished)
            {
                *finished = true;
            }
        }
        else if (nextTime < 0.0f)
        {
            nextTime = 0.0f;
            if (finished)
            {
                *finished = true;
            }
        }

        return nextTime;
    }

    bool AnimationComponent::IsTransitionReady(const AnimationTransition &transition, const AnimationState &state) const
    {
        if (transition.destinationStateIndex < 0 || transition.destinationStateIndex >= static_cast<int>(m_states.size()))
            return false;

        return IsTransitionReadyAtTime(transition, state, m_graphStateTime);
    }

    bool AnimationComponent::IsTransitionReadyAtTime(const AnimationTransition &transition, const AnimationState &state, float stateTime) const
    {
        if (transition.hasExitTime)
        {
            const float duration = GetClipDuration(state.clipIndex);
            if (duration > 0.0f && (stateTime / duration) < transition.exitTime)
            {
                return false;
            }
        }

        for (const auto &condition : transition.conditions)
        {
            const auto parameterIt = m_parameterLookup.find(condition.parameterName);
            if (parameterIt == m_parameterLookup.end())
            {
                return false;
            }

            const auto &parameter = m_parameters[parameterIt->second];
            const bool parameterAsBool = parameter.type == AnimationParameterType::Bool ||
                                                 parameter.type == AnimationParameterType::Trigger
                                             ? parameter.boolValue
                                             : parameter.type == AnimationParameterType::Int
                                                   ? parameter.intValue != 0
                                                   : std::abs(parameter.floatValue) > 0.0001f;
            bool matched = false;
            switch (condition.mode)
            {
            case AnimationConditionMode::If:
                matched = parameterAsBool;
                break;
            case AnimationConditionMode::IfNot:
                matched = !parameterAsBool;
                break;
            case AnimationConditionMode::Greater:
                matched = parameter.type == AnimationParameterType::Int ? parameter.intValue > static_cast<int>(condition.threshold) : parameter.floatValue > condition.threshold;
                break;
            case AnimationConditionMode::Less:
                matched = parameter.type == AnimationParameterType::Int ? parameter.intValue < static_cast<int>(condition.threshold) : parameter.floatValue < condition.threshold;
                break;
            case AnimationConditionMode::Equals:
                matched = parameter.type == AnimationParameterType::Int ? parameter.intValue == static_cast<int>(condition.threshold) : std::abs(parameter.floatValue - condition.threshold) <= 0.0001f;
                break;
            case AnimationConditionMode::NotEqual:
                matched = parameter.type == AnimationParameterType::Int ? parameter.intValue != static_cast<int>(condition.threshold) : std::abs(parameter.floatValue - condition.threshold) > 0.0001f;
                break;
            }

            if (!matched)
            {
                return false;
            }
        }

        return transition.hasExitTime || !transition.conditions.empty();
    }

    void AnimationComponent::ConsumeTransitionTriggers(const AnimationTransition &transition)
    {
        for (const auto &condition : transition.conditions)
        {
            const auto parameterIt = m_parameterLookup.find(condition.parameterName);
            if (parameterIt == m_parameterLookup.end())
            {
                continue;
            }

            const auto &parameter = m_parameters[parameterIt->second];
            if (parameter.type == AnimationParameterType::Trigger)
                m_layerTriggersToReset.push_back(parameterIt->second);
        }
    }

    void AnimationComponent::StartTransition(int sourceStateIndex, const AnimationTransition &transition)
    {
        if (sourceStateIndex < 0 || sourceStateIndex >= static_cast<int>(m_states.size()))
        {
            return;
        }

        m_transition.active = true;
        m_transition.sourceStateIndex = sourceStateIndex;
        m_transition.destinationStateIndex = transition.destinationStateIndex;
        m_transition.sourceTime = m_graphStateTime;
        m_transition.destinationTime = 0.0f;
        m_transition.elapsed = 0.0f;
        m_transition.duration = std::max(0.0f, transition.duration);
        ConsumeTransitionTriggers(transition);

        if (m_transition.duration <= 0.0f)
        {
            m_graphCurrentStateIndex = m_transition.destinationStateIndex;
            m_graphStateTime = 0.0f;
            m_transition = {};
        }
    }

    void AnimationComponent::UpdateGraph(float deltaTime)
    {
        ClampGraph();
        if (m_states.empty())
        {
            SetTime(m_time + deltaTime * m_speed);
            return;
        }

        if (!m_graphStarted)
        {
            ResetGraphPlayback();
        }

        if (m_transition.active)
        {
            const auto &source = m_states[static_cast<size_t>(m_transition.sourceStateIndex)];
            const auto &destination = m_states[static_cast<size_t>(m_transition.destinationStateIndex)];
            m_transition.sourceTime = AdvanceStateTime(source, m_transition.sourceTime, deltaTime);
            m_transition.destinationTime = AdvanceStateTime(destination, m_transition.destinationTime, deltaTime);
            m_transition.elapsed += std::max(0.0f, deltaTime);

            if (m_transition.elapsed >= m_transition.duration)
            {
                m_graphCurrentStateIndex = m_transition.destinationStateIndex;
                m_graphStateTime = m_transition.destinationTime;
                m_transition = {};
            }
        }
        else
        {
            auto &state = m_states[static_cast<size_t>(m_graphCurrentStateIndex)];
            bool finished = false;
            m_graphStateTime = AdvanceStateTime(state, m_graphStateTime, deltaTime, 1.0f, &finished);

            bool startedTransition = false;
            for (const auto &transition : state.transitions)
            {
                if (IsTransitionReady(transition, state))
                {
                    StartTransition(m_graphCurrentStateIndex, transition);
                    startedTransition = true;
                    break;
                }
            }

            if (finished && !state.loop && !startedTransition)
            {
                m_playing = false;
            }
        }

        const AnimationState &visibleState = m_transition.active
                                                 ? m_states[static_cast<size_t>(m_transition.destinationStateIndex)]
                                                 : m_states[static_cast<size_t>(m_graphCurrentStateIndex)];
        m_currentClipIndex = visibleState.clipIndex;
        m_time = m_transition.active ? m_transition.destinationTime : m_graphStateTime;
    }

    void AnimationComponent::UpdateLayers(float deltaTime)
    {
        for (auto &layer : m_layers)
        {
            const auto activationIt = m_parameterLookup.find(layer.activationParameter);
            const AnimationParameter *activation = activationIt != m_parameterLookup.end()
                                                       ? &m_parameters[activationIt->second]
                                                       : nullptr;
            const bool triggerFired = activation && activation->type == AnimationParameterType::Trigger && activation->boolValue;
            bool active = layer.activationParameter.empty();
            if (activation)
            {
                active = activation->type == AnimationParameterType::Bool || activation->type == AnimationParameterType::Trigger
                             ? activation->boolValue
                             : activation->type == AnimationParameterType::Int
                                   ? activation->intValue != 0
                                   : std::abs(activation->floatValue) > 0.0001f;
            }
            if (triggerFired)
                m_layerTriggersToReset.push_back(activationIt->second);

            const bool activatedThisFrame = active && !layer.wasActive;
            if (layer.enabled && layer.clipValid && (triggerFired || activatedThisFrame))
            {
                if (layer.restartOnActivation || !layer.playing)
                {
                    layer.time = 0.0f;
                    layer.graphCurrentStateIndex = layer.graphDefaultStateIndex;
                    layer.graphStateTime = 0.0f;
                    layer.graphTransitionActive = false;
                }
                layer.playing = true;
            }

            bool finished = false;
            if (layer.playing && layer.enabled && layer.clipValid)
            {
                if (layer.graphReference.empty())
                    layer.time = AdvanceClipTime(layer.clipIndex, layer.time, deltaTime, layer.speed, layer.loop, &finished);
                else
                    UpdateLayerGraph(layer, deltaTime);
            }
            if (layer.graphReference.empty() && finished && !layer.loop)
                layer.playing = false;

            float targetWeight = layer.enabled && layer.clipValid && layer.playing ? layer.weight : 0.0f;
            // Bool parameters describe sustained layers; releasing the bool
            // fades the layer out even when its clip itself is non-looping.
            // Triggers intentionally continue their one-shot to completion.
            if (layer.enabled && activation && activation->type == AnimationParameterType::Bool && !active)
            {
                layer.playing = false;
                targetWeight = 0.0f;
            }
            if (!layer.weightParameter.empty())
            {
                const auto weightIt = m_parameterLookup.find(layer.weightParameter);
                if (weightIt != m_parameterLookup.end())
                    targetWeight *= std::clamp(GetFloat(layer.weightParameter), 0.0f, 1.0f);
            }

            const float fadeDuration = targetWeight > layer.currentWeight ? layer.fadeIn : layer.fadeOut;
            if (fadeDuration <= 0.00001f)
                layer.currentWeight = targetWeight;
            else
            {
                const float step = std::max(0.0f, deltaTime) / fadeDuration;
                layer.currentWeight += std::clamp(targetWeight - layer.currentWeight, -step, step);
            }
            layer.wasActive = active;
        }
    }

    void AnimationComponent::StartLayerTransition(AnimationLayer &layer, int sourceStateIndex, const AnimationTransition &transition)
    {
        if (sourceStateIndex < 0 || sourceStateIndex >= static_cast<int>(layer.graphStates.size()) ||
            transition.destinationStateIndex < 0 || transition.destinationStateIndex >= static_cast<int>(layer.graphStates.size()))
            return;

        layer.graphTransitionActive = true;
        layer.graphTransitionSourceStateIndex = sourceStateIndex;
        layer.graphTransitionDestinationStateIndex = transition.destinationStateIndex;
        layer.graphTransitionSourceTime = layer.graphStateTime;
        layer.graphTransitionDestinationTime = 0.0f;
        layer.graphTransitionElapsed = 0.0f;
        layer.graphTransitionDuration = std::max(0.0f, transition.duration);
        ConsumeTransitionTriggers(transition);
        if (layer.graphTransitionDuration <= 0.0f)
        {
            layer.graphCurrentStateIndex = transition.destinationStateIndex;
            layer.graphStateTime = 0.0f;
            layer.graphTransitionActive = false;
        }
    }

    void AnimationComponent::UpdateLayerGraph(AnimationLayer &layer, float deltaTime)
    {
        if (layer.graphStates.empty())
            return;

        if (layer.graphTransitionActive)
        {
            const auto &source = layer.graphStates[static_cast<size_t>(layer.graphTransitionSourceStateIndex)];
            const auto &destination = layer.graphStates[static_cast<size_t>(layer.graphTransitionDestinationStateIndex)];
            layer.graphTransitionSourceTime = AdvanceStateTime(source, layer.graphTransitionSourceTime, deltaTime, layer.speed);
            layer.graphTransitionDestinationTime = AdvanceStateTime(destination, layer.graphTransitionDestinationTime, deltaTime, layer.speed);
            layer.graphTransitionElapsed += std::max(0.0f, deltaTime);
            if (layer.graphTransitionElapsed >= layer.graphTransitionDuration)
            {
                layer.graphCurrentStateIndex = layer.graphTransitionDestinationStateIndex;
                layer.graphStateTime = layer.graphTransitionDestinationTime;
                layer.graphTransitionActive = false;
            }
            return;
        }

        auto &state = layer.graphStates[static_cast<size_t>(layer.graphCurrentStateIndex)];
        bool finished = false;
        layer.graphStateTime = AdvanceStateTime(state, layer.graphStateTime, deltaTime, layer.speed, &finished);
        bool startedTransition = false;
        for (const auto &transition : state.transitions)
        {
            if (transition.destinationStateIndex >= 0 &&
                transition.destinationStateIndex < static_cast<int>(layer.graphStates.size()) &&
                IsTransitionReadyAtTime(transition, state, layer.graphStateTime))
            {
                StartLayerTransition(layer, layer.graphCurrentStateIndex, transition);
                startedTransition = true;
                break;
            }
        }

        // Referenced graph layers are one-shots when they reach a terminal
        // non-looping state, just like simple clip layers. Stopping playback
        // lets UpdateLayers fade the layer out and reveal the base graph.
        if (finished && !state.loop && !startedTransition)
            layer.playing = false;
    }

    void AnimationComponent::SetCurrentStateIndex(int stateIndex)
    {
        EnsureDefaultGraph();
        if (m_states.empty())
        {
            return;
        }

        m_graphCurrentStateIndex = std::clamp(stateIndex, 0, static_cast<int>(m_states.size()) - 1);
        m_graphStateTime = 0.0f;
        m_transition = {};
        m_graphStarted = true;
        m_currentClipIndex = m_states[static_cast<size_t>(m_graphCurrentStateIndex)].clipIndex;
        SetTime(0.0f);
        m_jointMatricesDirty = true;
        m_nodeMatricesDirty = true;
    }

    void AnimationComponent::SetDefaultStateIndex(int stateIndex)
    {
        EnsureDefaultGraph();
        if (m_states.empty())
        {
            return;
        }

        m_defaultStateIndex = std::clamp(stateIndex, 0, static_cast<int>(m_states.size()) - 1);
        if (!m_graphStarted)
        {
            m_graphCurrentStateIndex = m_defaultStateIndex;
        }
    }

    int AnimationComponent::FindStateIndex(std::string_view stateName) const
    {
        for (size_t index = 0; index < m_states.size(); ++index)
        {
            if (m_states[index].name == stateName)
            {
                return static_cast<int>(index);
            }
        }

        return -1;
    }

    int AnimationComponent::FindParameterIndex(std::string_view parameterName) const
    {
        for (size_t index = 0; index < m_parameters.size(); ++index)
        {
            if (m_parameters[index].name == parameterName)
            {
                return static_cast<int>(index);
            }
        }

        return -1;
    }

    int AnimationComponent::AddState(std::string name, int clipIndex)
    {
        if (name.empty())
        {
            name = "State " + std::to_string(m_states.size());
        }

        AnimationState state;
        state.name = std::move(name);
        state.clipIndex = clipIndex;
        state.loop = m_looping;
        m_states.push_back(std::move(state));
        ClampGraph();
        return static_cast<int>(m_states.size()) - 1;
    }

    void AnimationComponent::RemoveState(int stateIndex)
    {
        if (stateIndex < 0 || stateIndex >= static_cast<int>(m_states.size()))
        {
            return;
        }

        m_states.erase(m_states.begin() + stateIndex);
        for (auto &state : m_states)
        {
            state.transitions.erase(
                std::remove_if(state.transitions.begin(), state.transitions.end(),
                               [stateIndex](const AnimationTransition &transition)
                               {
                                   return transition.destinationStateIndex == stateIndex;
                               }),
                state.transitions.end());
            for (auto &transition : state.transitions)
            {
                if (transition.destinationStateIndex > stateIndex)
                {
                    --transition.destinationStateIndex;
                }
            }
        }
        ClampGraph();
        ResetGraphPlayback();
    }

    int AnimationComponent::AddParameter(std::string name, AnimationParameterType type)
    {
        if (name.empty())
        {
            name = "Parameter " + std::to_string(m_parameters.size());
        }

        AnimationParameter parameter;
        parameter.name = std::move(name);
        parameter.type = type;
        m_parameters.push_back(std::move(parameter));
        ClampGraph();
        return static_cast<int>(m_parameters.size()) - 1;
    }

    void AnimationComponent::RemoveParameter(int parameterIndex)
    {
        if (parameterIndex < 0 || parameterIndex >= static_cast<int>(m_parameters.size()))
        {
            return;
        }

        const std::string removedName = m_parameters[static_cast<size_t>(parameterIndex)].name;
        m_parameters.erase(m_parameters.begin() + parameterIndex);
        for (auto &state : m_states)
        {
            for (auto &transition : state.transitions)
            {
                transition.conditions.erase(
                    std::remove_if(transition.conditions.begin(), transition.conditions.end(),
                                   [&removedName](const AnimationCondition &condition)
                                   {
                                       return condition.parameterName == removedName;
                                   }),
                    transition.conditions.end());
            }
        }
        ClampGraph();
    }

    bool AnimationComponent::AddTransition(int sourceStateIndex, int destinationStateIndex)
    {
        if (sourceStateIndex < 0 || sourceStateIndex >= static_cast<int>(m_states.size()) ||
            destinationStateIndex < 0 || destinationStateIndex >= static_cast<int>(m_states.size()) ||
            sourceStateIndex == destinationStateIndex)
        {
            return false;
        }

        AnimationTransition transition;
        transition.destinationStateIndex = destinationStateIndex;
        m_states[static_cast<size_t>(sourceStateIndex)].transitions.push_back(std::move(transition));
        return true;
    }

    void AnimationComponent::RemoveTransition(int sourceStateIndex, int transitionIndex)
    {
        if (sourceStateIndex < 0 || sourceStateIndex >= static_cast<int>(m_states.size()))
        {
            return;
        }

        auto &transitions = m_states[static_cast<size_t>(sourceStateIndex)].transitions;
        if (transitionIndex < 0 || transitionIndex >= static_cast<int>(transitions.size()))
        {
            return;
        }

        transitions.erase(transitions.begin() + transitionIndex);
    }

    void AnimationComponent::SetBool(std::string_view parameterName, bool value)
    {
        const int index = FindParameterIndex(parameterName);
        if (index >= 0)
        {
            m_parameters[static_cast<size_t>(index)].boolValue = value;
        }
    }

    bool AnimationComponent::GetBool(std::string_view parameterName) const
    {
        const int index = FindParameterIndex(parameterName);
        if (index < 0)
        {
            return false;
        }

        const auto &parameter = m_parameters[static_cast<size_t>(index)];
        switch (parameter.type)
        {
        case AnimationParameterType::Bool:
        case AnimationParameterType::Trigger:
            return parameter.boolValue;
        case AnimationParameterType::Int:
            return parameter.intValue != 0;
        case AnimationParameterType::Float:
        default:
            return std::abs(parameter.floatValue) > 0.0001f;
        }
    }

    void AnimationComponent::SetTrigger(std::string_view parameterName)
    {
        const int index = FindParameterIndex(parameterName);
        if (index >= 0 && m_parameters[static_cast<size_t>(index)].type == AnimationParameterType::Trigger)
        {
            m_parameters[static_cast<size_t>(index)].boolValue = true;
        }
    }

    void AnimationComponent::ResetTrigger(std::string_view parameterName)
    {
        const int index = FindParameterIndex(parameterName);
        if (index >= 0 && m_parameters[static_cast<size_t>(index)].type == AnimationParameterType::Trigger)
        {
            m_parameters[static_cast<size_t>(index)].boolValue = false;
        }
    }

    void AnimationComponent::SetFloat(std::string_view parameterName, float value)
    {
        const int index = FindParameterIndex(parameterName);
        if (index >= 0)
        {
            m_parameters[static_cast<size_t>(index)].floatValue = value;
        }
    }

    float AnimationComponent::GetFloat(std::string_view parameterName) const
    {
        const int index = FindParameterIndex(parameterName);
        return index >= 0 ? m_parameters[static_cast<size_t>(index)].floatValue : 0.0f;
    }

    void AnimationComponent::SetInt(std::string_view parameterName, int value)
    {
        const int index = FindParameterIndex(parameterName);
        if (index >= 0)
        {
            m_parameters[static_cast<size_t>(index)].intValue = value;
        }
    }

    int AnimationComponent::GetInt(std::string_view parameterName) const
    {
        const int index = FindParameterIndex(parameterName);
        return index >= 0 ? m_parameters[static_cast<size_t>(index)].intValue : 0;
    }

    bool AnimationComponent::PlayState(std::string_view stateName)
    {
        const int stateIndex = FindStateIndex(stateName);
        if (stateIndex < 0)
        {
            return false;
        }

        SetCurrentStateIndex(stateIndex);
        Play();
        return true;
    }

    int AnimationComponent::FindLayerIndex(std::string_view layerName) const
    {
        for (size_t index = 0; index < m_layers.size(); ++index)
            if (m_layers[index].name == layerName)
                return static_cast<int>(index);
        return -1;
    }

    bool AnimationComponent::PlayLayer(std::string_view layerName, bool restart)
    {
        const int index = FindLayerIndex(layerName);
        if (index < 0)
            return false;
        auto &layer = m_layers[static_cast<size_t>(index)];
        if (restart)
            layer.time = 0.0f;
        layer.playing = layer.enabled && layer.clipValid;
        m_jointMatricesDirty = true;
        m_nodeMatricesDirty = true;
        return layer.playing;
    }

    bool AnimationComponent::StopLayer(std::string_view layerName)
    {
        const int index = FindLayerIndex(layerName);
        if (index < 0)
            return false;
        m_layers[static_cast<size_t>(index)].playing = false;
        m_jointMatricesDirty = true;
        m_nodeMatricesDirty = true;
        return true;
    }

    float AnimationComponent::GetLayerWeight(std::string_view layerName) const
    {
        const int index = FindLayerIndex(layerName);
        return index >= 0 ? m_layers[static_cast<size_t>(index)].currentWeight : 0.0f;
    }

    const std::vector<glm::mat4> &AnimationComponent::GetJointMatrices(const render::Skeleton &skeleton)
    {
        if (m_jointMatricesDirty || m_jointMatrices.size() != skeleton.joints.size())
        {
            EvaluateJointMatrices(skeleton);
        }

        return m_jointMatrices;
    }

    const std::vector<glm::mat4> &AnimationComponent::GetJointMatrices(const render::Skeleton &skeleton, const std::vector<render::AnimationNode> &nodes)
    {
        if (nodes.empty() || !skeleton.humanoidBoneMappings.empty())
        {
            return GetJointMatrices(skeleton);
        }

        if (m_jointMatricesDirty || m_jointMatrices.size() != skeleton.joints.size())
        {
            if (m_nodeMatricesDirty || m_nodeMatrices.size() != nodes.size())
            {
                EvaluateNodeMatrices(nodes);
            }

            m_jointMatrices.assign(skeleton.joints.size(), glm::mat4(1.0f));
            bool usedNodeHierarchy = false;
            for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
            {
                const auto &joint = skeleton.joints[jointIndex];
                if (joint.nodeIndex < 0 || joint.nodeIndex >= static_cast<int>(m_nodeMatrices.size()))
                {
                    continue;
                }

                m_jointMatrices[jointIndex] = joint.inverseRootMatrix *
                                              m_nodeMatrices[static_cast<size_t>(joint.nodeIndex)] *
                                              joint.inverseBindMatrix;
                usedNodeHierarchy = true;
            }

            if (!usedNodeHierarchy)
            {
                EvaluateJointMatrices(skeleton);
            }
            else
            {
                m_jointMatricesDirty = false;
            }
        }

        return m_jointMatrices;
    }

    glm::mat4 AnimationComponent::GetNodeMatrix(const std::vector<render::AnimationNode> &nodes, int nodeIndex)
    {
        if (nodeIndex < 0 || nodeIndex >= static_cast<int>(nodes.size()))
        {
            return glm::mat4(1.0f);
        }

        if (m_nodeMatricesDirty || m_nodeMatrices.size() != nodes.size())
        {
            EvaluateNodeMatrices(nodes);
        }

        return nodeIndex < static_cast<int>(m_nodeMatrices.size()) ? m_nodeMatrices[static_cast<size_t>(nodeIndex)] : glm::mat4(1.0f);
    }

    void AnimationComponent::EvaluateNodeMatrices(const std::vector<render::AnimationNode> &nodes)
    {
        m_nodeMatrices.assign(nodes.size(), glm::mat4(1.0f));
        if (nodes.empty())
        {
            m_nodeMatricesDirty = false;
            return;
        }

        auto sampleClip = [&](int clipIndex, float time)
        {
            return SampleLocalTransforms(
                m_clips, clipIndex, time, nodes.size(),
                [&nodes](const render::AnimationChannel &channel) { return ResolveChannelNodeIndex(channel, nodes); },
                [&nodes](size_t nodeIndex) { return nodes[nodeIndex].localBindTransform; });
        };
        auto sampleState = [&](const AnimationState &state, float time)
        {
            std::vector<std::pair<std::vector<glm::mat4>, float>> poses;
            for (const auto &sample : ResolveBlendSpaceSamples(state))
                poses.emplace_back(sampleClip(sample.clipIndex, GetBlendSpaceClipTime(state, sample.clipIndex, time)), sample.weight);
            return BlendWeightedLocalTransforms(poses);
        };

        auto localTransforms = !m_states.empty()
                                   ? sampleState(m_states[static_cast<size_t>(m_graphCurrentStateIndex)], m_graphStateTime)
                                   : sampleClip(m_currentClipIndex, m_time);

        if (m_transition.active)
        {
            const auto &source = m_states[static_cast<size_t>(m_transition.sourceStateIndex)];
            const auto &destination = m_states[static_cast<size_t>(m_transition.destinationStateIndex)];
            const auto sourceTransforms = sampleState(source, m_transition.sourceTime);
            const auto destinationTransforms = sampleState(destination, m_transition.destinationTime);
            const float blend = m_transition.duration > 0.0f ? std::clamp(m_transition.elapsed / m_transition.duration, 0.0f, 1.0f) : 1.0f;
            localTransforms = BlendLocalTransforms(sourceTransforms, destinationTransforms, blend);
        }

        std::vector<glm::mat4> bindTransforms;
        bindTransforms.reserve(nodes.size());
        for (const auto &node : nodes)
            bindTransforms.push_back(node.localBindTransform);
        for (const auto &layer : m_layers)
        {
            if (!layer.enabled || layer.currentWeight <= 0.00001f)
                continue;
            std::vector<glm::mat4> layerTransforms;
            if (!layer.graphReference.empty() && !layer.graphStates.empty())
            {
                if (layer.graphTransitionActive)
                {
                    const auto &source = layer.graphStates[static_cast<size_t>(layer.graphTransitionSourceStateIndex)];
                    const auto &destination = layer.graphStates[static_cast<size_t>(layer.graphTransitionDestinationStateIndex)];
                    const float blend = layer.graphTransitionDuration > 0.0f
                                            ? std::clamp(layer.graphTransitionElapsed / layer.graphTransitionDuration, 0.0f, 1.0f)
                                            : 1.0f;
                    layerTransforms = BlendLocalTransforms(
                        sampleState(source, layer.graphTransitionSourceTime),
                        sampleState(destination, layer.graphTransitionDestinationTime), blend);
                }
                else
                {
                    const auto &state = layer.graphStates[static_cast<size_t>(layer.graphCurrentStateIndex)];
                    layerTransforms = sampleState(state, layer.graphStateTime);
                }
            }
            else
            {
                layerTransforms = sampleClip(layer.clipIndex, layer.time);
            }
            localTransforms = BlendMaskedLocalTransforms(
                localTransforms, layerTransforms, bindTransforms,
                ResolveNodeMask(layer.maskId, nodes), layer.currentWeight, layer.blendMode);
        }

        std::vector<uint8_t> evaluated(nodes.size(), 0);
        std::function<glm::mat4(size_t)> evaluateNode = [&](size_t nodeIndex) -> glm::mat4 {
            if (evaluated[nodeIndex])
            {
                return m_nodeMatrices[nodeIndex];
            }

            const int parentIndex = nodes[nodeIndex].parentNodeIndex;
            m_nodeMatrices[nodeIndex] = parentIndex >= 0 && parentIndex < static_cast<int>(nodes.size())
                                            ? evaluateNode(static_cast<size_t>(parentIndex)) * localTransforms[nodeIndex]
                                            : localTransforms[nodeIndex];
            evaluated[nodeIndex] = 1;
            return m_nodeMatrices[nodeIndex];
        };

        for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
        {
            evaluateNode(nodeIndex);
        }

        m_nodeMatricesDirty = false;
    }

    std::vector<float> AnimationComponent::ResolveNodeMask(int maskId, const std::vector<render::AnimationNode> &nodes) const
    {
        const auto maskIt = std::find_if(m_boneMasks.begin(), m_boneMasks.end(),
                                         [maskId](const assets::AnimationGraphBoneMask &mask) { return mask.id == maskId; });
        if (maskId == 0)
            return std::vector<float>(nodes.size(), 1.0f);
        if (maskIt == m_boneMasks.end())
            return std::vector<float>(nodes.size(), 0.0f);

        std::vector<float> weights(nodes.size(), std::clamp(maskIt->defaultWeight, 0.0f, 1.0f));
        for (const auto &entry : maskIt->entries)
        {
            int rootIndex = -1;
            for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
            {
                if (render::GuessHumanoidBone(nodes[nodeIndex].name) == entry.bone)
                {
                    rootIndex = static_cast<int>(nodeIndex);
                    break;
                }
            }
            if (rootIndex < 0)
                continue;
            for (int nodeIndex = 0; nodeIndex < static_cast<int>(nodes.size()); ++nodeIndex)
            {
                if (nodeIndex == rootIndex || (entry.includeChildren && IsDescendantOf(
                                                   nodeIndex, rootIndex,
                                                   [&nodes](int index) { return nodes[static_cast<size_t>(index)].parentNodeIndex; },
                                                   static_cast<int>(nodes.size()))))
                    weights[static_cast<size_t>(nodeIndex)] = std::clamp(entry.weight, 0.0f, 1.0f);
            }
        }
        return weights;
    }

    std::vector<float> AnimationComponent::ResolveJointMask(int maskId, const render::Skeleton &skeleton) const
    {
        const auto maskIt = std::find_if(m_boneMasks.begin(), m_boneMasks.end(),
                                         [maskId](const assets::AnimationGraphBoneMask &mask) { return mask.id == maskId; });
        if (maskId == 0)
            return std::vector<float>(skeleton.joints.size(), 1.0f);
        if (maskIt == m_boneMasks.end())
            return std::vector<float>(skeleton.joints.size(), 0.0f);

        std::vector<float> weights(skeleton.joints.size(), std::clamp(maskIt->defaultWeight, 0.0f, 1.0f));
        for (const auto &entry : maskIt->entries)
        {
            int rootIndex = -1;
            for (const auto &mapping : skeleton.humanoidBoneMappings)
            {
                if (mapping.bone == entry.bone && mapping.targetJointIndex >= 0 &&
                    mapping.targetJointIndex < static_cast<int>(skeleton.joints.size()))
                {
                    rootIndex = mapping.targetJointIndex;
                    break;
                }
            }
            if (rootIndex < 0)
            {
                for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
                {
                    if (render::GuessHumanoidBone(skeleton.joints[jointIndex].name) == entry.bone)
                    {
                        rootIndex = static_cast<int>(jointIndex);
                        break;
                    }
                }
            }
            if (rootIndex < 0)
                continue;
            for (int jointIndex = 0; jointIndex < static_cast<int>(skeleton.joints.size()); ++jointIndex)
            {
                if (jointIndex == rootIndex || (entry.includeChildren && IsDescendantOf(
                                                     jointIndex, rootIndex,
                                                     [&skeleton](int index) { return skeleton.joints[static_cast<size_t>(index)].parentJointIndex; },
                                                     static_cast<int>(skeleton.joints.size()))))
                    weights[static_cast<size_t>(jointIndex)] = std::clamp(entry.weight, 0.0f, 1.0f);
            }
        }
        return weights;
    }

    void AnimationComponent::EnsureRetargetBindingCache(const render::Skeleton &skeleton)
    {
        auto hashCombine = [](std::size_t &seed, std::size_t value)
        {
            seed ^= value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        };

        std::size_t signature = skeleton.joints.size();
        for (const auto &joint : skeleton.joints)
        {
            hashCombine(signature, std::hash<std::string>{}(joint.name));
            hashCombine(signature, static_cast<std::size_t>(joint.parentJointIndex + 1));
        }
        for (const auto &mapping : skeleton.humanoidBoneMappings)
        {
            hashCombine(signature, std::hash<std::string>{}(mapping.sourceBoneName));
            hashCombine(signature, static_cast<std::size_t>(mapping.targetJointIndex + 1));
            hashCombine(signature, static_cast<std::size_t>(mapping.bone));
        }
        bool bindingSizesMatch = m_retargetClipCaches.size() == m_clips.size();
        if (bindingSizesMatch)
        {
            for (size_t clipIndex = 0; clipIndex < m_clips.size(); ++clipIndex)
            {
                if (m_retargetClipCaches[clipIndex].jointBindings.size() != m_clips[clipIndex].channels.size())
                {
                    bindingSizesMatch = false;
                    break;
                }
            }
        }
        if (m_retargetBindingSkeleton == &skeleton &&
            m_retargetBindingSignature == signature &&
            bindingSizesMatch)
        {
            return;
        }

        m_retargetBindingSkeleton = &skeleton;
        m_retargetBindingSignature = signature;
        m_retargetClipCaches.clear();
        m_retargetClipCaches.resize(m_clips.size());

        m_targetBindTranslations.resize(skeleton.joints.size());
        m_targetBindRotations.resize(skeleton.joints.size());
        m_targetBindScales.resize(skeleton.joints.size());
        m_targetGlobalBindTransforms.assign(skeleton.joints.size(), glm::mat4(1.0f));
        m_targetGlobalBindRotations.assign(skeleton.joints.size(), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        std::vector<uint8_t> targetEvaluationState(skeleton.joints.size(), 0);
        std::function<glm::mat4(size_t)> evaluateTargetBind = [&](size_t jointIndex) -> glm::mat4 {
            if (targetEvaluationState[jointIndex] == 2)
                return m_targetGlobalBindTransforms[jointIndex];
            if (targetEvaluationState[jointIndex] == 1)
                return skeleton.joints[jointIndex].localBindTransform;
            targetEvaluationState[jointIndex] = 1;
            const int parentIndex = skeleton.joints[jointIndex].parentJointIndex;
            m_targetGlobalBindTransforms[jointIndex] = parentIndex >= 0 && parentIndex < static_cast<int>(skeleton.joints.size()) &&
                                                              parentIndex != static_cast<int>(jointIndex)
                                                          ? evaluateTargetBind(static_cast<size_t>(parentIndex)) * skeleton.joints[jointIndex].localBindTransform
                                                          : skeleton.joints[jointIndex].localBindTransform;
            targetEvaluationState[jointIndex] = 2;
            return m_targetGlobalBindTransforms[jointIndex];
        };
        for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
        {
            DecomposeTransform(skeleton.joints[jointIndex].localBindTransform,
                               m_targetBindTranslations[jointIndex], m_targetBindRotations[jointIndex], m_targetBindScales[jointIndex]);
            evaluateTargetBind(jointIndex);
            glm::vec3 ignoredTranslation;
            glm::vec3 ignoredScale;
            DecomposeTransform(m_targetGlobalBindTransforms[jointIndex], ignoredTranslation, m_targetGlobalBindRotations[jointIndex], ignoredScale);
        }

        for (size_t clipIndex = 0; clipIndex < m_clips.size(); ++clipIndex)
        {
            const auto &channels = m_clips[clipIndex].channels;
            auto &cache = m_retargetClipCaches[clipIndex];
            cache.jointBindings.reserve(channels.size());
            cache.mappingBindings.reserve(channels.size());
            int sourceNodeCount = 0;
            for (const auto &channel : channels)
                sourceNodeCount = channel.nodeIndex >= 0 ? std::max(sourceNodeCount, channel.nodeIndex + 1) : sourceNodeCount;
            cache.sourceLocalBindTransforms.assign(static_cast<size_t>(sourceNodeCount), glm::mat4(1.0f));
            cache.sourceGlobalBindTransforms.assign(static_cast<size_t>(sourceNodeCount), glm::mat4(1.0f));
            cache.sourceInverseLocalBindTransforms.assign(static_cast<size_t>(sourceNodeCount), glm::mat4(1.0f));
            cache.sourceGlobalBindRotations.assign(static_cast<size_t>(sourceNodeCount), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            cache.sourceBindTranslations.assign(static_cast<size_t>(sourceNodeCount), glm::vec3(0.0f));
            cache.sourceBindRotations.assign(static_cast<size_t>(sourceNodeCount), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            cache.sourceBindScales.assign(static_cast<size_t>(sourceNodeCount), glm::vec3(1.0f));
            cache.sourceParentIndices.assign(static_cast<size_t>(sourceNodeCount), -1);
            cache.sourceNodesPresent.assign(static_cast<size_t>(sourceNodeCount), 0);
            for (const auto &channel : channels)
            {
                cache.jointBindings.push_back(ResolveChannelJointIndex(channel, skeleton));
                const auto *mapping = FindHumanoidMappingForChannel(channel, skeleton);
                cache.mappingBindings.push_back(mapping ? static_cast<int>(mapping - skeleton.humanoidBoneMappings.data()) : -1);
                if (channel.nodeIndex < 0 || channel.nodeIndex >= sourceNodeCount)
                    continue;
                const size_t nodeIndex = static_cast<size_t>(channel.nodeIndex);
                cache.sourceParentIndices[nodeIndex] = channel.sourceParentNodeIndex;
                if (channel.hasSourceLocalBindTransform)
                    cache.sourceLocalBindTransforms[nodeIndex] = channel.sourceLocalBindTransform;
                if (channel.hasSourceGlobalBindTransform)
                {
                    cache.sourceGlobalBindTransforms[nodeIndex] = channel.sourceGlobalBindTransform;
                    cache.sourceNodesPresent[nodeIndex] = 1;
                }
            }
            for (size_t nodeIndex = 0; nodeIndex < static_cast<size_t>(sourceNodeCount); ++nodeIndex)
            {
                cache.sourceInverseLocalBindTransforms[nodeIndex] = glm::inverse(cache.sourceLocalBindTransforms[nodeIndex]);
                DecomposeTransform(cache.sourceLocalBindTransforms[nodeIndex], cache.sourceBindTranslations[nodeIndex],
                                   cache.sourceBindRotations[nodeIndex], cache.sourceBindScales[nodeIndex]);
                glm::vec3 ignoredTranslation;
                glm::vec3 ignoredScale;
                DecomposeTransform(cache.sourceGlobalBindTransforms[nodeIndex], ignoredTranslation,
                                   cache.sourceGlobalBindRotations[nodeIndex], ignoredScale);
            }
        }
    }

    void AnimationComponent::EvaluateJointMatrices(const render::Skeleton &skeleton)
    {
        m_jointMatrices.assign(skeleton.joints.size(), glm::mat4(1.0f));
        if (skeleton.joints.empty())
        {
            m_jointMatricesDirty = false;
            return;
        }

        EnsureRetargetBindingCache(skeleton);
        auto sampleClip = [&](int clipIndex, float time)
        {
            static const AnimationRetargetClipCache emptyCache;
            const bool validBindings = clipIndex >= 0 && clipIndex < static_cast<int>(m_retargetClipCaches.size());
            return SampleRetargetedJointTransforms(
                m_clips, clipIndex, time, skeleton,
                validBindings ? m_retargetClipCaches[static_cast<size_t>(clipIndex)] : emptyCache,
                m_targetBindTranslations, m_targetBindRotations, m_targetBindScales, m_targetGlobalBindRotations);
        };
        auto sampleState = [&](const AnimationState &state, float time)
        {
            std::vector<std::pair<std::vector<glm::mat4>, float>> poses;
            for (const auto &sample : ResolveBlendSpaceSamples(state))
                poses.emplace_back(sampleClip(sample.clipIndex, GetBlendSpaceClipTime(state, sample.clipIndex, time)), sample.weight);
            return BlendWeightedLocalTransforms(poses);
        };

        std::vector<glm::mat4> localTransforms;
        if (m_transition.active)
        {
            const auto &source = m_states[static_cast<size_t>(m_transition.sourceStateIndex)];
            const auto &destination = m_states[static_cast<size_t>(m_transition.destinationStateIndex)];
            const auto sourceTransforms = sampleState(source, m_transition.sourceTime);
            const auto destinationTransforms = sampleState(destination, m_transition.destinationTime);
            const float blend = m_transition.duration > 0.0f ? std::clamp(m_transition.elapsed / m_transition.duration, 0.0f, 1.0f) : 1.0f;
            localTransforms = BlendLocalTransforms(sourceTransforms, destinationTransforms, blend);
        }
        else
        {
            localTransforms = !m_states.empty()
                                  ? sampleState(m_states[static_cast<size_t>(m_graphCurrentStateIndex)], m_graphStateTime)
                                  : sampleClip(m_currentClipIndex, m_time);
        }

        std::vector<glm::mat4> bindTransforms;
        bindTransforms.reserve(skeleton.joints.size());
        for (const auto &joint : skeleton.joints)
            bindTransforms.push_back(joint.localBindTransform);
        for (const auto &layer : m_layers)
        {
            if (!layer.enabled || layer.currentWeight <= 0.00001f)
                continue;
            std::vector<glm::mat4> layerTransforms;
            if (!layer.graphReference.empty() && !layer.graphStates.empty())
            {
                if (layer.graphTransitionActive)
                {
                    const auto &source = layer.graphStates[static_cast<size_t>(layer.graphTransitionSourceStateIndex)];
                    const auto &destination = layer.graphStates[static_cast<size_t>(layer.graphTransitionDestinationStateIndex)];
                    const float blend = layer.graphTransitionDuration > 0.0f
                                            ? std::clamp(layer.graphTransitionElapsed / layer.graphTransitionDuration, 0.0f, 1.0f)
                                            : 1.0f;
                    layerTransforms = BlendLocalTransforms(
                        sampleState(source, layer.graphTransitionSourceTime),
                        sampleState(destination, layer.graphTransitionDestinationTime), blend);
                }
                else
                {
                    const auto &state = layer.graphStates[static_cast<size_t>(layer.graphCurrentStateIndex)];
                    layerTransforms = sampleState(state, layer.graphStateTime);
                }
            }
            else
            {
                layerTransforms = sampleClip(layer.clipIndex, layer.time);
            }
            localTransforms = BlendMaskedLocalTransforms(
                localTransforms, layerTransforms, bindTransforms,
                ResolveJointMask(layer.maskId, skeleton), layer.currentWeight, layer.blendMode);
        }

        // Skin joint arrays are not required to be topologically sorted. In
        // particular, glTF permits a child to appear before its parent and
        // Assimp preserves the source mesh's bone order. Evaluate parents on
        // demand instead of accidentally using an identity parent transform.
        std::vector<glm::mat4> globalTransforms(skeleton.joints.size(), glm::mat4(1.0f));
        std::vector<uint8_t> evaluationState(skeleton.joints.size(), 0);
        std::function<glm::mat4(size_t)> evaluateJoint = [&](size_t jointIndex) -> glm::mat4 {
            if (evaluationState[jointIndex] == 2)
            {
                return globalTransforms[jointIndex];
            }

            // Malformed cyclic hierarchies should not recurse forever. Treat
            // the joint at which the cycle closes as a root.
            if (evaluationState[jointIndex] == 1)
            {
                return localTransforms[jointIndex];
            }

            evaluationState[jointIndex] = 1;
            const int parentIndex = skeleton.joints[jointIndex].parentJointIndex;
            globalTransforms[jointIndex] = parentIndex >= 0 &&
                                                   parentIndex < static_cast<int>(globalTransforms.size()) &&
                                                   parentIndex != static_cast<int>(jointIndex)
                                               ? evaluateJoint(static_cast<size_t>(parentIndex)) * localTransforms[jointIndex]
                                               : localTransforms[jointIndex];
            evaluationState[jointIndex] = 2;
            return globalTransforms[jointIndex];
        };

        for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
        {
            m_jointMatrices[jointIndex] = skeleton.joints[jointIndex].inverseRootMatrix *
                                          evaluateJoint(jointIndex) *
                                          skeleton.joints[jointIndex].inverseBindMatrix;
        }

        m_jointMatricesDirty = false;
    }
}
