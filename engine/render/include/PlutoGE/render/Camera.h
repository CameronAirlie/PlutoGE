#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace PlutoGE::scene
{
    class CameraComponent;
}

namespace PlutoGE::render
{
    enum class CameraProjection { Perspective, Orthographic };

    struct CameraData
    {
        glm::mat4 view;       // View matrix
        glm::mat4 projection; // Projection matrix
        float nearPlane = 0.1f;
        float farPlane = 100.0f;
    };

    struct CameraConfig
    {
        float fovY = 45.0f;      // Field of view in the Y direction (degrees)
        float nearPlane = 0.1f;  // Near clipping plane
        float farPlane = 100.0f; // Far clipping plane
        CameraProjection projection = CameraProjection::Perspective;
        float orthographicHeight = 10.0f; // Full vertical span in world units
    };

    class Camera
    {
    public:
        Camera(const CameraConfig &config) : m_config(config) { SetOrthographicHeight(config.orthographicHeight); }
        ~Camera() = default;

        float GetFOV() const { return m_config.fovY; }
        float GetNearPlane() const { return m_config.nearPlane; }
        float GetFarPlane() const { return m_config.farPlane; }
        CameraProjection GetProjection() const { return m_config.projection; }
        bool IsOrthographic() const { return m_config.projection == CameraProjection::Orthographic; }
        float GetOrthographicHeight() const { return m_config.orthographicHeight; }
        void SetProjection(CameraProjection projection) { m_config.projection = projection; }
        void SetOrthographicHeight(float height)
        {
            m_config.orthographicHeight = std::isfinite(height) ? std::max(height, 0.01f) : 10.0f;
        }

        void SetFOV(float fovY) { m_config.fovY = fovY; }
        void SetNearPlane(float nearPlane) { m_config.nearPlane = nearPlane; }
        void SetFarPlane(float farPlane) { m_config.farPlane = farPlane; }
        CameraData GetCameraDataForTransform(const glm::mat4 &transform, int width, int height) const
        {
            return GetCameraData(transform, width, height);
        }

    protected:
        friend class scene::CameraComponent;

        CameraData GetCameraData(const glm::mat4 &transform, int width, int height) const
        {
            CameraData data;
            const glm::vec3 position = glm::vec3(transform[3]);                 // Extract position from the transform
            const glm::vec3 forward = -glm::normalize(glm::vec3(transform[2])); // Extract forward direction
            const glm::vec3 up = glm::normalize(glm::vec3(transform[1]));       // Extract up direction

            data.view = glm::lookAt(position, position + forward, up);
            // Swap near/far to map near -> 1 and far -> 0. With floating-point
            // depth this preserves substantially more precision at distance.
            const float aspect = static_cast<float>(std::max(width, 1)) / static_cast<float>(std::max(height, 1));
            const float halfHeight = m_config.orthographicHeight * 0.5f;
            data.projection = IsOrthographic()
                ? glm::ortho(-halfHeight * aspect, halfHeight * aspect, -halfHeight, halfHeight, m_config.farPlane, m_config.nearPlane)
                : glm::perspective(glm::radians(m_config.fovY), aspect, m_config.farPlane, m_config.nearPlane);
            data.nearPlane = m_config.nearPlane;
            data.farPlane = m_config.farPlane;
            return data;
        }

    private:
        CameraConfig m_config;
    };
}
