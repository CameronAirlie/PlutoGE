#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace PlutoGE::render
{
    struct CameraData
    {
        glm::vec3 position;   // Camera position in world space
        glm::mat4 view;       // View matrix
        glm::mat4 projection; // Projection matrix
    };

    struct CameraConfig
    {
        float fovY = 45.0f;              // Field of view in the Y direction (degrees)
        float aspectRatio = 4.0f / 3.0f; // Aspect ratio (width/height)
        float nearPlane = 0.1f;          // Near clipping plane
        float farPlane = 100.0f;         // Far clipping plane
    };

    class Camera
    {
    public:
        Camera(const CameraConfig &config)
        {
            // Set up the projection matrix based on the config
            m_cameraData.projection = glm::perspective(
                glm::radians(config.fovY),
                config.aspectRatio,
                config.nearPlane,
                config.farPlane);
            m_cameraData.view = glm::mat4(1.0f); // Start with an identity view matrix
        }
        ~Camera() = default;

        void SetViewMatrix(const glm::mat4 &view) { m_cameraData.view = view; }
        void SetProjectionMatrix(const glm::mat4 &projection) { m_cameraData.projection = projection; }

        CameraData &GetCameraData() { return m_cameraData; }

    private:
        CameraData m_cameraData; // Camera data containing view and projection matrices
    };
}