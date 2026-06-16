#include "PlutoGE/scene/components/AnimationComponent.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace PlutoGE::scene
{
    namespace
    {
        constexpr const char *kClipPrefix = "Clips.";

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
            if (scale.x > 0.0f) basisX /= scale.x;
            if (scale.y > 0.0f) basisY /= scale.y;
            if (scale.z > 0.0f) basisZ /= scale.z;
            glm::mat3 rotationMatrix(1.0f);
            rotationMatrix[0] = basisX;
            rotationMatrix[1] = basisY;
            rotationMatrix[2] = basisZ;
            const glm::quat q = glm::normalize(glm::quat_cast(rotationMatrix));
            rotation = glm::vec4(q.x, q.y, q.z, q.w);
        }
    }

    void AnimationComponent::Update(float deltaTime)
    {
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

        SetTime(m_time + deltaTime * m_speed);
        m_jointMatricesDirty = true;
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

        return properties;
    }

    void AnimationComponent::Deserialize(const std::vector<Property> &properties)
    {
        int clipCount = 0;
        for (const auto &property : properties)
        {
            if (property.name == "ClipCount")
            {
                clipCount = std::max(0, std::stoi(property.value));
            }
        }

        if (clipCount > 0)
        {
            m_clips.assign(static_cast<size_t>(clipCount), render::AnimationClip{});
        }

        for (const auto &property : properties)
        {
            if (property.name == "SourceAnimation")
            {
                m_sourceAnimationPath = property.value;
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
        }

        ClampCurrentClipIndex();
        SetTime(m_time);
        m_playing = m_playing && HasCurrentClip();
    }

    void AnimationComponent::SetClipsFromImportedAnimations(const std::vector<render::AnimationClip> &animations)
    {
        m_clips = animations;
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
        SetTime(0.0f);
        m_startedAutoplay = false;
        m_playing = m_autoplay && HasCurrentClip();
        m_jointMatricesDirty = true;
    }

    void AnimationComponent::SetCurrentClipIndex(int clipIndex)
    {
        m_currentClipIndex = clipIndex;
        ClampCurrentClipIndex();
        SetTime(0.0f);
        m_startedAutoplay = false;
        m_jointMatricesDirty = true;
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
        m_playing = HasCurrentClip();
        m_startedAutoplay = true;
    }

    void AnimationComponent::Stop()
    {
        m_playing = false;
        SetTime(0.0f);
        m_jointMatricesDirty = true;
    }

    void AnimationComponent::SetTime(float time)
    {
        if (!HasCurrentClip())
        {
            m_time = 0.0f;
            m_jointMatricesDirty = true;
            return;
        }

        const float duration = GetCurrentClipDuration();
        if (duration <= 0.0f)
        {
            m_time = 0.0f;
            m_playing = false;
            m_jointMatricesDirty = true;
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
            return;
        }

        m_time = std::clamp(time, 0.0f, duration);
        if (time >= duration || time < 0.0f)
        {
            m_playing = false;
        }
        m_jointMatricesDirty = m_jointMatricesDirty || std::abs(previousTime - m_time) > 0.00001f;
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

    const std::vector<glm::mat4> &AnimationComponent::GetJointMatrices(const render::Skeleton &skeleton)
    {
        if (m_jointMatricesDirty || m_jointMatrices.size() != skeleton.joints.size())
        {
            EvaluateJointMatrices(skeleton);
        }

        return m_jointMatrices;
    }

    void AnimationComponent::EvaluateJointMatrices(const render::Skeleton &skeleton)
    {
        m_jointMatrices.assign(skeleton.joints.size(), glm::mat4(1.0f));
        if (skeleton.joints.empty())
        {
            m_jointMatricesDirty = false;
            return;
        }

        std::vector<glm::vec3> translations(skeleton.joints.size(), glm::vec3(0.0f));
        std::vector<glm::vec4> rotations(skeleton.joints.size(), glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        std::vector<glm::vec3> scales(skeleton.joints.size(), glm::vec3(1.0f));
        for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
        {
            DecomposeTransform(skeleton.joints[jointIndex].localBindTransform, translations[jointIndex], rotations[jointIndex], scales[jointIndex]);
        }

        if (HasCurrentClip())
        {
            const auto &clip = m_clips[static_cast<size_t>(m_currentClipIndex)];
            for (const auto &channel : clip.channels)
            {
                if (channel.jointIndex < 0 || channel.jointIndex >= static_cast<int>(skeleton.joints.size()))
                {
                    continue;
                }

                const auto sample = SampleChannelValue(channel, m_time);
                const size_t jointIndex = static_cast<size_t>(channel.jointIndex);
                switch (channel.path)
                {
                case render::AnimationTargetPath::Translation:
                    translations[jointIndex] = glm::vec3(sample);
                    break;
                case render::AnimationTargetPath::Rotation:
                    rotations[jointIndex] = sample;
                    break;
                case render::AnimationTargetPath::Scale:
                    scales[jointIndex] = glm::vec3(sample);
                    break;
                }
            }
        }

        std::vector<glm::mat4> globalTransforms(skeleton.joints.size(), glm::mat4(1.0f));
        for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex)
        {
            const glm::mat4 localTransform = ComposeTransform(translations[jointIndex], rotations[jointIndex], scales[jointIndex]);
            const int parentIndex = skeleton.joints[jointIndex].parentJointIndex;
            globalTransforms[jointIndex] = parentIndex >= 0 && parentIndex < static_cast<int>(globalTransforms.size())
                                               ? globalTransforms[static_cast<size_t>(parentIndex)] * localTransform
                                               : localTransform;
            m_jointMatrices[jointIndex] = skeleton.joints[jointIndex].inverseRootMatrix * globalTransforms[jointIndex] * skeleton.joints[jointIndex].inverseBindMatrix;
        }

        m_jointMatricesDirty = false;
    }
}
