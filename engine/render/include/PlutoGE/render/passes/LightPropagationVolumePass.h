#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace PlutoGE::render
{
    class Texture;

    class LightPropagationVolumePass : public IRenderPass
    {
    public:
        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "LPV"; }

        Texture *GetVolumeTexture() const { return m_volumeTexture.get(); }
        glm::vec3 GetGridOrigin() const { return m_gridOrigin; }
        glm::vec3 GetGridSize() const { return m_gridSize; }

    private:
        void EnsureResources();
        void ClearVolume();
        bool ShouldUpdateVolume(const RenderContext &ctx,
                                const glm::vec3 &desiredGridOrigin,
                                const glm::vec3 &desiredGridSize,
                                std::size_t sceneSignature,
                                std::size_t lightSignature);

        glm::ivec3 m_resolution{16, 16, 16};
        glm::vec3 m_gridOrigin{0.0f};
        glm::vec3 m_gridSize{32.0f, 20.0f, 32.0f};
        std::unique_ptr<Texture> m_volumeTexture;
        std::vector<glm::vec3> m_currentRadiance;
        std::vector<glm::vec3> m_nextRadiance;
        std::vector<float> m_injectionWeights;
        std::vector<float> m_positionReadback;
        std::vector<float> m_normalReadback;
        std::vector<unsigned char> m_albedoReadback;
        glm::ivec2 m_lastViewportSize{0, 0};
        std::size_t m_lastSceneSignature = 0;
        std::size_t m_lastLightSignature = 0;
        bool m_hasValidVolume = false;
    };
}