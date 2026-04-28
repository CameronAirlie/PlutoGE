#pragma once

#include "Component.h"

namespace PlutoGE::render
{
    class Camera;
}

namespace PlutoGE::scene
{
    class CameraComponent : public Component
    {
    public:
        CameraComponent() = default;
        ~CameraComponent() override = default;

        void Update(float deltaTime) override;

        void SetCamera(render::Camera *camera) { m_camera = camera; }
        render::Camera *GetCamera() const { return m_camera; }

    private:
        render::Camera *m_camera = nullptr;
    };
}