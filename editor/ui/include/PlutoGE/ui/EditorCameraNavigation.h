#pragma once

#include <glm/glm.hpp>

namespace PlutoGE::ui
{
    // Editor viewport camera moves, free of input and rendering so they can be tested. Yaw turns about world up,
    // pitch about the camera's right; at zero both the camera looks down -Z.
    struct EditorCameraPose
    {
        glm::vec3 position{0.0f};
        float yawDegrees = 0.0f;
        float pitchDegrees = 0.0f;
    };

    [[nodiscard]] glm::vec3 GetEditorCameraForward(float yawDegrees, float pitchDegrees);

    // Swings the camera around `pivot` as if rigidly attached to it: the pivot keeps its place on screen and its
    // distance. Pitch stops at +-pitchLimitDegrees.
    [[nodiscard]] EditorCameraPose OrbitEditorCamera(const EditorCameraPose &pose, const glm::vec3 &pivot, float deltaYawDegrees,
                                                     float deltaPitchDegrees, float pitchLimitDegrees);

    // True when `pivot` is a sensible orbit centre: in front of the camera, within `maxAngleDegrees` of the view
    // direction and closer than `maxDistance`. Otherwise the caller picks the point under the view centre.
    [[nodiscard]] bool IsUsableOrbitPivot(const EditorCameraPose &pose, const glm::vec3 &pivot, float maxAngleDegrees, float maxDistance);

    struct EditorCameraDolly
    {
        glm::vec3 position{0.0f};
        glm::vec3 pivot{0.0f};
    };

    // Mouse-wheel zoom in perspective: each notch covers a share of the distance to the pivot, so steps are fine up
    // close and quick far away. Pushing past the pivot carries it forward instead of stopping.
    [[nodiscard]] EditorCameraDolly DollyEditorCamera(const EditorCameraPose &pose, const glm::vec3 &pivot, float wheelSteps);
}
