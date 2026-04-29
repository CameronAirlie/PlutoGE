#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace PlutoGE::scene
{
    class CameraComponent;
}

namespace PlutoGE::render
{
    struct CameraData
    {
        glm::mat4 view;       // View matrix
        glm::mat4 projection; // Projection matrix
    };

    struct CameraConfig
    {
        float fovY = 45.0f;      // Field of view in the Y direction (degrees)
        float nearPlane = 0.1f;  // Near clipping plane
        float farPlane = 100.0f; // Far clipping plane
    };

    class Camera
    {
    public:
        Camera(const CameraConfig &config) : m_config(config) {}
        ~Camera() = default;

        void SetFOV(float fovY) { m_config.fovY = fovY; }
        void SetNearPlane(float nearPlane) { m_config.nearPlane = nearPlane; }
        void SetFarPlane(float farPlane) { m_config.farPlane = farPlane; }

    protected:
        friend class scene::CameraComponent;

        CameraData &GetCameraData(glm::mat4 &transform, int width, int height) const
        {
            CameraData data;
            const glm::vec3 position = glm::vec3(transform[3]);                 // Extract position from the transform
            const glm::vec3 forward = -glm::normalize(glm::vec3(transform[2])); // Extract forward direction
            const glm::vec3 up = glm::normalize(glm::vec3(transform[1]));       // Extract up direction

            data.view = glm::lookAt(position, position + forward, up);
            data.projection = glm::perspective(glm::radians(m_config.fovY), static_cast<float>(width) / static_cast<float>(height), m_config.nearPlane, m_config.farPlane);
            return data;
        }

    private:
        CameraConfig m_config;
    };
}