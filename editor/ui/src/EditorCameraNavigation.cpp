#include "PlutoGE/ui/EditorCameraNavigation.h"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace PlutoGE::ui
{
    namespace
    {
        constexpr float kDollyShare = 0.15f;      // Of the pivot distance, per wheel notch.
        constexpr float kMinDollyDistance = 0.5f; // Closer than this the pivot moves ahead with the camera.
        constexpr float kDefaultPivotDistance = 8.0f;

        glm::mat3 Rotation(float yawDegrees, float pitchDegrees)
        {
            glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), glm::radians(yawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
            rotation = glm::rotate(rotation, glm::radians(pitchDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
            return glm::mat3(rotation);
        }
    }

    glm::vec3 GetEditorCameraForward(float yawDegrees, float pitchDegrees)
    {
        return glm::normalize(Rotation(yawDegrees, pitchDegrees) * glm::vec3(0.0f, 0.0f, -1.0f));
    }

    EditorCameraPose OrbitEditorCamera(const EditorCameraPose &pose, const glm::vec3 &pivot, float deltaYawDegrees, float deltaPitchDegrees,
                                       float pitchLimitDegrees)
    {
        EditorCameraPose result = pose;
        result.yawDegrees = pose.yawDegrees + deltaYawDegrees;
        result.pitchDegrees = std::clamp(pose.pitchDegrees + deltaPitchDegrees, -pitchLimitDegrees, pitchLimitDegrees);
        // Carry the camera's offset from the pivot through the same change of orientation.
        const glm::mat3 before = Rotation(pose.yawDegrees, pose.pitchDegrees);
        const glm::mat3 after = Rotation(result.yawDegrees, result.pitchDegrees);
        result.position = pivot + after * (glm::transpose(before) * (pose.position - pivot));
        return result;
    }

    bool IsUsableOrbitPivot(const EditorCameraPose &pose, const glm::vec3 &pivot, float maxAngleDegrees, float maxDistance)
    {
        const glm::vec3 offset = pivot - pose.position;
        const float distance = glm::length(offset);
        if (distance < 1e-3f || distance > maxDistance)
            return false;
        const float cosine = glm::dot(offset / distance, GetEditorCameraForward(pose.yawDegrees, pose.pitchDegrees));
        return cosine >= std::cos(glm::radians(maxAngleDegrees));
    }

    EditorCameraDolly DollyEditorCamera(const EditorCameraPose &pose, const glm::vec3 &pivot, float wheelSteps)
    {
        const glm::vec3 forward = GetEditorCameraForward(pose.yawDegrees, pose.pitchDegrees);
        float distance = glm::dot(pivot - pose.position, forward);
        EditorCameraDolly result{pose.position, pivot};
        if (distance <= 1e-3f)
        {
            // The pivot is beside or behind the camera: zoom about a point straight ahead.
            distance = kDefaultPivotDistance;
            result.pivot = pose.position + forward * distance;
        }
        if (wheelSteps < 0.0f && distance < kMinDollyDistance)
        {
            // Backing away from very close: start from the minimum so zooming out speeds up again.
            distance = kMinDollyDistance;
            result.pivot = pose.position + forward * distance;
        }
        const float target = distance * std::pow(1.0f - kDollyShare, wheelSteps);
        if (target >= kMinDollyDistance || wheelSteps < 0.0f)
        {
            result.position += forward * (distance - target);
            return result;
        }
        // Past the closest approach: keep moving at the minimum step and take the pivot along.
        const float travel = (std::max)(distance - kMinDollyDistance, 0.0f) + kMinDollyDistance * kDollyShare * wheelSteps;
        result.position += forward * travel;
        result.pivot = result.position + forward * kMinDollyDistance;
        return result;
    }
}
