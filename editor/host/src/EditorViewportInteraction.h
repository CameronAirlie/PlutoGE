#pragma once

#include "PlutoGE/render/Camera.h"

#include <glm/glm.hpp>

#include <cstdint>

class EditorSession;

namespace PlutoGE::platform
{
    struct InputState;
}

class EditorViewportInteraction
{
public:
    bool Update(EditorSession &session,
                const PlutoGE::render::CameraData &camera,
                const PlutoGE::platform::InputState &input,
                int viewportWidth,
                int viewportHeight,
                bool runtimeRunning);
    void Render(const EditorSession &session,
                const PlutoGE::render::CameraData &camera,
                int viewportWidth,
                int viewportHeight);
    void Shutdown();

private:
    struct DragState
    {
        std::uint32_t entityId = 0;
        int axis = -1;
        glm::vec2 startMouse{0.0f};
        glm::vec2 screenDirection{1.0f, 0.0f};
        glm::vec3 axisWorld{1.0f, 0.0f, 0.0f};
        glm::vec3 position{0.0f};
        glm::mat4 localRotation{1.0f};
        glm::mat4 worldRotation{1.0f};
        glm::vec3 scale{1.0f};
        float worldUnitsPerPixel = 0.01f;
        bool active = false;
    };

    bool EnsureRenderer();
    unsigned int m_program = 0;
    unsigned int m_vertexArray = 0;
    unsigned int m_vertexBuffer = 0;
    int m_hoveredAxis = -1;
    DragState m_drag;
};
