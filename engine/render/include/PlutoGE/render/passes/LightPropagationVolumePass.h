#pragma once

#include "PlutoGE/render/passes/IRenderPass.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"

#include <glad/glad.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <glm/glm.hpp>

#include <memory>
#include <unordered_map>
#include <vector>

namespace PlutoGE::render
{
    class RenderTarget;
    class Texture;

    class LightPropagationVolumePass : public IRenderPass
    {
    public:
        ~LightPropagationVolumePass() override;
        void Initialize() override;
        void Execute(const RenderContext &ctx) override;
        const char *GetName() const override { return "LPV"; }

        Texture *GetVolumeTexture() const
        {
            if (!m_enabled || !m_activeVolumeState || !m_activeVolumeState->hasValidVolume || !m_activeVolumeState->hasPublishedVolume)
            {
                return nullptr;
            }

            return m_activeVolumeState->volumeTextures[m_activeVolumeState->activeVolumeTextureIndex].get();
        }
        glm::vec3 GetGridOrigin() const { return m_activeVolumeState ? m_activeVolumeState->gridOrigin : glm::vec3(0.0f); }
        glm::vec3 GetGridSize() const { return m_activeVolumeState ? m_activeVolumeState->gridSize : glm::vec3(1.0f); }
        bool IsEnabled() const { return m_enabled; }
        std::vector<PostProcessParameter> GetParameters() const;
        void SetParameters(const std::vector<PostProcessParameter> &parameters);

    private:
        struct VolumeState
        {
            glm::vec3 gridOrigin{0.0f};
            glm::vec3 gridSize{32.0f, 20.0f, 32.0f};
            glm::vec3 capturedGridOrigin{0.0f};
            glm::vec3 capturedGridSize{32.0f, 20.0f, 32.0f};
            glm::vec3 pendingGridOrigin{0.0f};
            glm::vec3 pendingGridSize{32.0f, 20.0f, 32.0f};
            glm::vec3 historyGridOrigin{0.0f};
            glm::vec3 historyGridSize{32.0f, 20.0f, 32.0f};
            std::array<std::unique_ptr<Texture>, 2> volumeTextures;
            std::vector<glm::vec3> currentRadiance;
            std::vector<glm::vec3> nextRadiance;
            std::vector<glm::vec3> historyRadiance;
            std::vector<float> injectionWeights;
            std::vector<float> positionReadback;
            std::vector<float> normalReadback;
            std::vector<unsigned char> albedoReadback;
            std::chrono::steady_clock::time_point lastUpdateTime{};
            std::chrono::steady_clock::time_point lastCaptureTime{};
            glm::ivec2 lastViewportSize{0};
            glm::ivec2 lastReadbackSize{0};
            float lastFarPlane = 0.0f;
            std::uint64_t lastLightSignature = 0;
            std::uint64_t lastSettingsRevision = 0;
            std::size_t activeVolumeTextureIndex = 0;
            std::size_t pendingVolumeTextureIndex = 0;
            bool hasValidVolume = false;
            bool hasHistory = false;
            bool hasCapturedSamples = false;
            bool hasPublishedVolume = false;
            bool hasPendingPublishedVolume = false;
        };

        void EnsureResources(VolumeState &state);
        void EnsureReadbackResources(int width, int height);
        void DestroyReadbackResources();
        void QueueReadback(const RenderContext &ctx,
                           const RenderTarget *renderTarget,
                           const glm::ivec2 &readbackSize,
                           const glm::vec3 &gridOrigin,
                           const glm::vec3 &gridSize,
                           std::chrono::steady_clock::time_point captureTime);
        void TryResolveQueuedReadback();
        bool CopyReadbackPbo(GLuint pbo, void *destination, GLsizeiptr byteCount) const;
        void ClearVolume(VolumeState &state);
        VolumeState &GetOrCreateVolumeState(const RenderTarget *renderTarget);

        glm::ivec3 m_resolution{16, 16, 16};
        std::vector<float> m_positionReadback;
        std::vector<float> m_normalReadback;
        std::vector<unsigned char> m_albedoReadback;
        std::unordered_map<const RenderTarget *, VolumeState> m_volumeStates;
        VolumeState *m_activeVolumeState = nullptr;
        GLuint m_readbackFbo = 0;
        GLuint m_readbackPositionTexture = 0;
        GLuint m_readbackNormalTexture = 0;
        GLuint m_readbackAlbedoTexture = 0;
        std::array<GLuint, 2> m_positionReadbackPbos{};
        std::array<GLuint, 2> m_normalReadbackPbos{};
        std::array<GLuint, 2> m_albedoReadbackPbos{};
        std::size_t m_readbackPboIndex = 0;
        bool m_hasQueuedReadback = false;
        GLsync m_queuedReadbackFence = nullptr;
        const RenderTarget *m_queuedReadbackTarget = nullptr;
        std::chrono::steady_clock::time_point m_queuedReadbackTime{};
        glm::ivec2 m_queuedReadbackSize{0};
        glm::vec3 m_queuedReadbackGridOrigin{0.0f};
        glm::vec3 m_queuedReadbackGridSize{32.0f, 20.0f, 32.0f};
        std::size_t m_queuedReadbackPboIndex = 0;
        int m_readbackWidth = 0;
        int m_readbackHeight = 0;
        bool m_enabled = true;
        int m_propagationIterations = 6;
        float m_injectionBoost = 1.8f;
        float m_temporalBlendWeight = 0.85f;
        float m_lookAnchorDistanceScale = 0.22f;
        float m_lookAnchorDistanceMin = 6.0f;
        float m_lookAnchorDistanceMax = 18.0f;
        float m_focusDeadZone = 0.2f;
        float m_cameraSafeMargin = 0.18f;
        float m_activeUpdateIntervalMs = 16.0f;
        float m_recenterUpdateIntervalMs = 120.0f;
        float m_steadyStateUpdateIntervalMs = 50.0f;
        float m_steadyStateCaptureIntervalMs = 180.0f;
        std::uint64_t m_settingsRevision = 1;
    };
}