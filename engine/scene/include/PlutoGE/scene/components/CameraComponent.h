#pragma once

#include "Component.h"

namespace PlutoGE::render
{
    class Camera;
    struct CameraData;
}

namespace PlutoGE::scene
{
    class CameraComponent : public Component
    {
    public:
        CameraComponent(render::Camera *camera = nullptr) : m_camera(camera) {}
        ~CameraComponent() override = default;

        void Update(float deltaTime) override;

        void SetCamera(render::Camera *camera) { m_camera = camera; }
        render::Camera *GetCamera() const { return m_camera; }

        render::CameraData GetCameraData(int width, int height) const;

    private:
        render::Camera *m_camera = nullptr;
    };
}