#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/Scene.h"

#include <glm/glm.hpp>
#include <memory>
#include <vector>

namespace PlutoGE::render
{
    class Texture;
}

namespace PlutoGE::scene
{
    class IblCaptureComponent : public TypedComponent<IblCaptureComponent>
    {
    public:
        IblCaptureComponent() = default;
        ~IblCaptureComponent() override = default;

        void Update(float deltaTime) override;
        std::vector<Property> Serialize() const override;
        std::vector<Property> SerializeEditableProperties() const;
        void Deserialize(const std::vector<Property> &properties) override;

        void SetSize(const glm::vec3 &size);
        void SetIntensity(float intensity);
        void SetBlendDistance(float blendDistance);
        void SetResolution(int resolution);
        void SetFarPlane(float farPlane);
        void MarkDirty() { m_dirty = true; }
        void ClearDirty() { m_dirty = false; }

        const glm::vec3 &GetSize() const { return m_size; }
        float GetIntensity() const { return m_intensity; }
        float GetBlendDistance() const { return m_blendDistance; }
        int GetResolution() const { return m_resolution; }
        float GetFarPlane() const { return m_farPlane; }
        bool IsDirty() const { return m_dirty; }
        render::Texture *GetCaptureTexture() const { return m_captureTexture.get(); }
        render::Texture *EnsureCaptureTexture();
        void DiscardCaptureResult();
        bool StoreCapturePixelsFromTexture();
        glm::mat4 GetVolumeTransform() const;
        IblCaptureVolume BuildCaptureVolume() const;

    private:
        glm::vec3 m_size{10.0f};
        float m_intensity = 1.0f;
        float m_blendDistance = 1.0f;
        int m_resolution = 256;
        float m_farPlane = 100.0f;
        bool m_dirty = true;
        std::unique_ptr<render::Texture> m_captureTexture;
        std::vector<float> m_capturePixels;

        void UploadStoredCapturePixels();
    };
}
