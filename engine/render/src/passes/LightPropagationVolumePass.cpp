#include "PlutoGE/render/passes/LightPropagationVolumePass.h"

#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <algorithm>
#include <cassert>
#include <cstring>

#include <glm/gtc/matrix_inverse.hpp>

namespace PlutoGE::render
{
    namespace
    {
        constexpr float kFarPlaneUpdateThreshold = 1.0f;
        constexpr float kPropagationSelfWeight = 0.36f;
        constexpr float kPropagationNeighborWeight = 0.07f;
        constexpr std::size_t kReadbackBufferCount = 2;
        constexpr int kReadbackSamplesPerAxisScale = 4;
        constexpr int kMinReadbackTargetDimension = 72;
        constexpr int kMaxReadbackTargetDimension = 96;

        std::size_t FlattenCellIndex(const glm::ivec3 &resolution, int x, int y, int z)
        {
            return static_cast<std::size_t>(x + resolution.x * (y + resolution.y * z));
        }

        void HashCombine(std::uint64_t &seed, std::uint64_t value)
        {
            seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
        }

        std::uint64_t HashQuantizedFloat(float value, float quantum)
        {
            const float safeQuantum = glm::max(quantum, 0.000001f);
            const auto quantized = static_cast<std::int64_t>(std::llround(static_cast<double>(value) / static_cast<double>(safeQuantum)));
            return static_cast<std::uint64_t>(quantized);
        }

        std::uint64_t ComputeLightSignature(const std::vector<scene::Light *> &lights)
        {
            std::uint64_t signature = 1469598103934665603ULL;
            HashCombine(signature, static_cast<std::uint64_t>(lights.size()));
            for (const auto *light : lights)
            {
                if (!light)
                {
                    HashCombine(signature, 0ULL);
                    continue;
                }

                HashCombine(signature, static_cast<std::uint64_t>(light->type));
                HashCombine(signature, light->castsShadows ? 1ULL : 0ULL);
                HashCombine(signature, HashQuantizedFloat(light->position.x, 0.02f));
                HashCombine(signature, HashQuantizedFloat(light->position.y, 0.02f));
                HashCombine(signature, HashQuantizedFloat(light->position.z, 0.02f));
                HashCombine(signature, HashQuantizedFloat(light->direction.x, 0.002f));
                HashCombine(signature, HashQuantizedFloat(light->direction.y, 0.002f));
                HashCombine(signature, HashQuantizedFloat(light->direction.z, 0.002f));
                HashCombine(signature, HashQuantizedFloat(light->color.x, 0.01f));
                HashCombine(signature, HashQuantizedFloat(light->color.y, 0.01f));
                HashCombine(signature, HashQuantizedFloat(light->color.z, 0.01f));
                HashCombine(signature, HashQuantizedFloat(light->intensity, 0.01f));
                HashCombine(signature, HashQuantizedFloat(light->range, 0.02f));
            }

            return signature;
        }

        bool ParseBool(const std::string &value)
        {
            return value == "true" || value == "1";
        }

        glm::vec3 GetCameraPosition(const CameraData &cameraData)
        {
            return glm::vec3(glm::inverse(cameraData.view)[3]);
        }

        glm::vec3 GetCameraForward(const CameraData &cameraData)
        {
            const glm::mat4 inverseView = glm::inverse(cameraData.view);
            return -glm::normalize(glm::vec3(inverseView[2]));
        }

        bool WorldToCell(const glm::vec3 &worldPosition, const glm::vec3 &origin, const glm::vec3 &size, const glm::ivec3 &resolution, glm::ivec3 &cell)
        {
            const glm::vec3 safeSize = glm::max(size, glm::vec3(0.0001f));
            const glm::vec3 normalized = (worldPosition - origin) / safeSize;
            if (glm::any(glm::lessThan(normalized, glm::vec3(0.0f))) || glm::any(glm::greaterThanEqual(normalized, glm::vec3(1.0f))))
            {
                return false;
            }

            const glm::vec3 scaled = normalized * glm::vec3(resolution);
            cell = glm::clamp(glm::ivec3(scaled), glm::ivec3(0), resolution - glm::ivec3(1));
            return true;
        }

        glm::vec3 GetCellCenterWorldPosition(const glm::vec3 &origin, const glm::vec3 &size, const glm::ivec3 &resolution, int x, int y, int z)
        {
            const glm::vec3 cellUv = (glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) + glm::vec3(0.5f)) / glm::vec3(resolution);
            return origin + cellUv * size;
        }

        glm::vec3 SnapGridOriginToVoxel(const glm::vec3 &origin, const glm::vec3 &size, const glm::ivec3 &resolution)
        {
            const glm::vec3 cellSize = glm::max(size / glm::vec3(resolution), glm::vec3(0.0001f));
            return glm::floor(origin / cellSize) * cellSize;
        }

        glm::vec3 ClampOriginToKeepPointInside(const glm::vec3 &origin, const glm::vec3 &size, const glm::vec3 &point, float marginFraction)
        {
            const glm::vec3 margin = glm::clamp(size * marginFraction, glm::vec3(0.0f), size * 0.49f);
            const glm::vec3 minOrigin = point - (size - margin);
            const glm::vec3 maxOrigin = point - margin;
            return glm::clamp(origin, minOrigin, maxOrigin);
        }

        bool IsPointInsideInnerBox(const glm::vec3 &point, const glm::vec3 &origin, const glm::vec3 &size, float marginFraction)
        {
            const glm::vec3 margin = glm::clamp(size * marginFraction, glm::vec3(0.0f), size * 0.49f);
            const glm::vec3 minBounds = origin + margin;
            const glm::vec3 maxBounds = origin + size - margin;
            return glm::all(glm::greaterThanEqual(point, minBounds)) && glm::all(glm::lessThanEqual(point, maxBounds));
        }

        float ComputePointAttenuation(const glm::vec3 &fragPos, const scene::Light &light)
        {
            const float distanceToLight = glm::length(light.position - fragPos);
            const float normalizedDistance = light.range > 0.0001f ? distanceToLight / light.range : 1.0f;
            const float attenuation = glm::clamp(1.0f - normalizedDistance, 0.0f, 1.0f);
            return attenuation * attenuation;
        }

        float ComputeSpotAttenuation(const glm::vec3 &fragPos, const glm::vec3 &lightDir, const scene::Light &light)
        {
            const float distanceAttenuation = ComputePointAttenuation(fragPos, light);
            const float spotEffect = glm::dot(-lightDir, glm::normalize(light.direction));
            return distanceAttenuation * glm::smoothstep(0.9f, 0.975f, spotEffect);
        }

        glm::vec3 ComputeInjectedRadiance(const glm::vec3 &fragPos, const glm::vec3 &normal, const glm::vec3 &albedo, float metallic, const std::vector<scene::Light *> &lights)
        {
            glm::vec3 totalRadiance(0.0f);
            const glm::vec3 surfaceNormal = glm::normalize(normal);
            const float diffuseReflectance = glm::clamp(1.0f - metallic, 0.0f, 1.0f);

            for (auto *light : lights)
            {
                if (!light)
                {
                    continue;
                }

                glm::vec3 lightDir(0.0f);
                float attenuation = 1.0f;
                if (light->type == scene::LightType::Directional)
                {
                    lightDir = glm::normalize(-light->direction);
                }
                else if (light->type == scene::LightType::Point)
                {
                    lightDir = glm::normalize(light->position - fragPos);
                    attenuation = ComputePointAttenuation(fragPos, *light);
                }
                else
                {
                    lightDir = glm::normalize(light->position - fragPos);
                    attenuation = ComputeSpotAttenuation(fragPos, lightDir, *light);
                }

                const float ndotl = glm::max(glm::dot(surfaceNormal, lightDir), 0.0f);
                if (ndotl <= 0.0f || attenuation <= 0.0f)
                {
                    continue;
                }

                totalRadiance += light->color * light->intensity * attenuation * ndotl;
            }

            return totalRadiance * albedo * diffuseReflectance;
        }
    }

    LightPropagationVolumePass::~LightPropagationVolumePass()
    {
        DestroyReadbackResources();
    }

    void LightPropagationVolumePass::Initialize()
    {
        auto &defaultState = GetOrCreateVolumeState(nullptr);
        EnsureResources(defaultState);
        ClearVolume(defaultState);
    }

    std::vector<PostProcessParameter> LightPropagationVolumePass::GetParameters() const
    {
        return {
            PostProcessParameter{.name = "Enabled", .type = PostProcessParameterType::Bool, .value = m_enabled ? "true" : "false"},
            PostProcessParameter{.name = "Grid Resolution", .type = PostProcessParameterType::Int, .value = std::to_string(m_resolution.x)},
            PostProcessParameter{.name = "Propagation Iterations", .type = PostProcessParameterType::Int, .value = std::to_string(m_propagationIterations)},
            PostProcessParameter{.name = "Injection Boost", .type = PostProcessParameterType::Float, .value = std::to_string(m_injectionBoost)},
            PostProcessParameter{.name = "Temporal Blend", .type = PostProcessParameterType::Float, .value = std::to_string(m_temporalBlendWeight)},
            PostProcessParameter{.name = "Look Ahead Scale", .type = PostProcessParameterType::Float, .value = std::to_string(m_lookAnchorDistanceScale)},
            PostProcessParameter{.name = "Look Ahead Min", .type = PostProcessParameterType::Float, .value = std::to_string(m_lookAnchorDistanceMin)},
            PostProcessParameter{.name = "Look Ahead Max", .type = PostProcessParameterType::Float, .value = std::to_string(m_lookAnchorDistanceMax)},
            PostProcessParameter{.name = "Focus Dead Zone", .type = PostProcessParameterType::Float, .value = std::to_string(m_focusDeadZone)},
            PostProcessParameter{.name = "Camera Safe Margin", .type = PostProcessParameterType::Float, .value = std::to_string(m_cameraSafeMargin)},
            PostProcessParameter{.name = "Active Update Interval", .type = PostProcessParameterType::Float, .value = std::to_string(m_activeUpdateIntervalMs)},
            PostProcessParameter{.name = "Recenter Update Interval", .type = PostProcessParameterType::Float, .value = std::to_string(m_recenterUpdateIntervalMs)},
            PostProcessParameter{.name = "Steady Update Interval", .type = PostProcessParameterType::Float, .value = std::to_string(m_steadyStateUpdateIntervalMs)},
            PostProcessParameter{.name = "Steady Capture Interval", .type = PostProcessParameterType::Float, .value = std::to_string(m_steadyStateCaptureIntervalMs)},
        };
    }

    void LightPropagationVolumePass::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        bool changed = false;
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Enabled")
            {
                const bool enabled = ParseBool(parameter.value);
                if (m_enabled != enabled)
                {
                    m_enabled = enabled;
                    changed = true;
                }
            }
            else if (parameter.name == "Grid Resolution")
            {
                const int resolution = std::clamp(std::stoi(parameter.value), 8, 48);
                if (m_resolution.x != resolution)
                {
                    m_resolution = glm::ivec3(resolution);
                    changed = true;
                }
            }
            else if (parameter.name == "Propagation Iterations")
            {
                const int iterations = std::clamp(std::stoi(parameter.value), 1, 12);
                if (m_propagationIterations != iterations)
                {
                    m_propagationIterations = iterations;
                    changed = true;
                }
            }
            else if (parameter.name == "Injection Boost")
            {
                const float injectionBoost = std::clamp(std::stof(parameter.value), 0.1f, 8.0f);
                if (m_injectionBoost != injectionBoost)
                {
                    m_injectionBoost = injectionBoost;
                    changed = true;
                }
            }
            else if (parameter.name == "Temporal Blend")
            {
                const float temporalBlend = std::clamp(std::stof(parameter.value), 0.0f, 0.98f);
                if (m_temporalBlendWeight != temporalBlend)
                {
                    m_temporalBlendWeight = temporalBlend;
                    changed = true;
                }
            }
            else if (parameter.name == "Look Ahead Scale")
            {
                const float lookScale = std::clamp(std::stof(parameter.value), 0.0f, 1.0f);
                if (m_lookAnchorDistanceScale != lookScale)
                {
                    m_lookAnchorDistanceScale = lookScale;
                    changed = true;
                }
            }
            else if (parameter.name == "Look Ahead Min")
            {
                const float lookMin = std::clamp(std::stof(parameter.value), 0.0f, 128.0f);
                if (m_lookAnchorDistanceMin != lookMin)
                {
                    m_lookAnchorDistanceMin = lookMin;
                    changed = true;
                }
            }
            else if (parameter.name == "Look Ahead Max")
            {
                const float lookMax = std::clamp(std::stof(parameter.value), 0.0f, 256.0f);
                if (m_lookAnchorDistanceMax != lookMax)
                {
                    m_lookAnchorDistanceMax = lookMax;
                    changed = true;
                }
            }
            else if (parameter.name == "Focus Dead Zone")
            {
                const float deadZone = std::clamp(std::stof(parameter.value), 0.0f, 0.45f);
                if (m_focusDeadZone != deadZone)
                {
                    m_focusDeadZone = deadZone;
                    changed = true;
                }
            }
            else if (parameter.name == "Camera Safe Margin")
            {
                const float safeMargin = std::clamp(std::stof(parameter.value), 0.0f, 0.45f);
                if (m_cameraSafeMargin != safeMargin)
                {
                    m_cameraSafeMargin = safeMargin;
                    changed = true;
                }
            }
            else if (parameter.name == "Active Update Interval")
            {
                const float intervalMs = std::clamp(std::stof(parameter.value), 1.0f, 1000.0f);
                if (m_activeUpdateIntervalMs != intervalMs)
                {
                    m_activeUpdateIntervalMs = intervalMs;
                    changed = true;
                }
            }
            else if (parameter.name == "Recenter Update Interval")
            {
                const float intervalMs = std::clamp(std::stof(parameter.value), 1.0f, 2000.0f);
                if (m_recenterUpdateIntervalMs != intervalMs)
                {
                    m_recenterUpdateIntervalMs = intervalMs;
                    changed = true;
                }
            }
            else if (parameter.name == "Steady Update Interval")
            {
                const float intervalMs = std::clamp(std::stof(parameter.value), 1.0f, 2000.0f);
                if (m_steadyStateUpdateIntervalMs != intervalMs)
                {
                    m_steadyStateUpdateIntervalMs = intervalMs;
                    changed = true;
                }
            }
            else if (parameter.name == "Steady Capture Interval")
            {
                const float intervalMs = std::clamp(std::stof(parameter.value), 1.0f, 5000.0f);
                if (m_steadyStateCaptureIntervalMs != intervalMs)
                {
                    m_steadyStateCaptureIntervalMs = intervalMs;
                    changed = true;
                }
            }
        }

        if (m_lookAnchorDistanceMax < m_lookAnchorDistanceMin)
        {
            m_lookAnchorDistanceMax = m_lookAnchorDistanceMin;
        }

        if (changed)
        {
            ++m_settingsRevision;
        }
    }

    auto LightPropagationVolumePass::GetOrCreateVolumeState(const RenderTarget *renderTarget) -> VolumeState &
    {
        return m_volumeStates[renderTarget];
    }

    void LightPropagationVolumePass::DestroyReadbackResources()
    {
        if (m_queuedReadbackFence != nullptr)
        {
            glDeleteSync(m_queuedReadbackFence);
            m_queuedReadbackFence = nullptr;
        }

        if (std::any_of(m_positionReadbackPbos.begin(), m_positionReadbackPbos.end(), [](GLuint pbo)
                        { return pbo != 0; }))
        {
            glDeleteBuffers(static_cast<GLsizei>(m_positionReadbackPbos.size()), m_positionReadbackPbos.data());
            m_positionReadbackPbos = {};
        }

        if (std::any_of(m_normalReadbackPbos.begin(), m_normalReadbackPbos.end(), [](GLuint pbo)
                        { return pbo != 0; }))
        {
            glDeleteBuffers(static_cast<GLsizei>(m_normalReadbackPbos.size()), m_normalReadbackPbos.data());
            m_normalReadbackPbos = {};
        }

        if (std::any_of(m_albedoReadbackPbos.begin(), m_albedoReadbackPbos.end(), [](GLuint pbo)
                        { return pbo != 0; }))
        {
            glDeleteBuffers(static_cast<GLsizei>(m_albedoReadbackPbos.size()), m_albedoReadbackPbos.data());
            m_albedoReadbackPbos = {};
        }

        if (m_readbackPositionTexture != 0)
        {
            glDeleteTextures(1, &m_readbackPositionTexture);
            m_readbackPositionTexture = 0;
        }

        if (m_readbackNormalTexture != 0)
        {
            glDeleteTextures(1, &m_readbackNormalTexture);
            m_readbackNormalTexture = 0;
        }

        if (m_readbackAlbedoTexture != 0)
        {
            glDeleteTextures(1, &m_readbackAlbedoTexture);
            m_readbackAlbedoTexture = 0;
        }

        if (m_readbackFbo != 0)
        {
            glDeleteFramebuffers(1, &m_readbackFbo);
            m_readbackFbo = 0;
        }

        m_readbackPboIndex = 0;
        m_hasQueuedReadback = false;
        m_queuedReadbackTarget = nullptr;
        m_queuedReadbackTime = {};
        m_queuedReadbackSize = glm::ivec2(0);
        m_queuedReadbackGridOrigin = glm::vec3(0.0f);
        m_queuedReadbackGridSize = glm::vec3(32.0f, 20.0f, 32.0f);
        m_queuedReadbackPboIndex = 0;
        m_readbackWidth = 0;
        m_readbackHeight = 0;
    }

    void LightPropagationVolumePass::EnsureReadbackResources(int width, int height)
    {
        if (width <= 0 || height <= 0)
        {
            DestroyReadbackResources();
            return;
        }

        if (m_readbackFbo != 0 && m_readbackWidth == width && m_readbackHeight == height)
        {
            return;
        }

        DestroyReadbackResources();

        m_readbackWidth = width;
        m_readbackHeight = height;

        glGenFramebuffers(1, &m_readbackFbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_readbackFbo);

        glGenTextures(1, &m_readbackPositionTexture);
        glBindTexture(GL_TEXTURE_2D, m_readbackPositionTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_readbackPositionTexture, 0);

        glGenTextures(1, &m_readbackNormalTexture);
        glBindTexture(GL_TEXTURE_2D, m_readbackNormalTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, m_readbackNormalTexture, 0);

        glGenTextures(1, &m_readbackAlbedoTexture);
        glBindTexture(GL_TEXTURE_2D, m_readbackAlbedoTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT2, GL_TEXTURE_2D, m_readbackAlbedoTexture, 0);

        glGenBuffers(static_cast<GLsizei>(m_positionReadbackPbos.size()), m_positionReadbackPbos.data());
        glGenBuffers(static_cast<GLsizei>(m_normalReadbackPbos.size()), m_normalReadbackPbos.data());
        glGenBuffers(static_cast<GLsizei>(m_albedoReadbackPbos.size()), m_albedoReadbackPbos.data());

        const GLsizeiptr positionBytes = static_cast<GLsizeiptr>(width) * static_cast<GLsizeiptr>(height) * 3 * static_cast<GLsizeiptr>(sizeof(float));
        const GLsizeiptr normalBytes = static_cast<GLsizeiptr>(width) * static_cast<GLsizeiptr>(height) * 4 * static_cast<GLsizeiptr>(sizeof(float));
        const GLsizeiptr albedoBytes = static_cast<GLsizeiptr>(width) * static_cast<GLsizeiptr>(height) * 4 * static_cast<GLsizeiptr>(sizeof(unsigned char));

        for (std::size_t index = 0; index < kReadbackBufferCount; ++index)
        {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_positionReadbackPbos[index]);
            glBufferData(GL_PIXEL_PACK_BUFFER, positionBytes, nullptr, GL_STREAM_READ);

            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_normalReadbackPbos[index]);
            glBufferData(GL_PIXEL_PACK_BUFFER, normalBytes, nullptr, GL_STREAM_READ);

            glBindBuffer(GL_PIXEL_PACK_BUFFER, m_albedoReadbackPbos[index]);
            glBufferData(GL_PIXEL_PACK_BUFFER, albedoBytes, nullptr, GL_STREAM_READ);
        }

        assert(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE && "LPV readback framebuffer is not complete!");

        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    bool LightPropagationVolumePass::CopyReadbackPbo(GLuint pbo, void *destination, GLsizeiptr byteCount) const
    {
        if (pbo == 0 || destination == nullptr || byteCount <= 0)
        {
            return false;
        }

        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
        const void *mappedBuffer = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, byteCount, GL_MAP_READ_BIT);
        if (!mappedBuffer)
        {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            return false;
        }

        std::memcpy(destination, mappedBuffer, static_cast<std::size_t>(byteCount));
        const GLboolean unmapSucceeded = glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        return unmapSucceeded == GL_TRUE;
    }

    void LightPropagationVolumePass::QueueReadback(const RenderContext &ctx,
                                                   const RenderTarget *renderTarget,
                                                   const glm::ivec2 &readbackSize,
                                                   const glm::vec3 &gridOrigin,
                                                   const glm::vec3 &gridSize,
                                                   std::chrono::steady_clock::time_point captureTime)
    {
        if (!ctx.gBuffer || readbackSize.x <= 0 || readbackSize.y <= 0 || m_hasQueuedReadback)
        {
            return;
        }

        const int width = ctx.gBuffer->GetWidth();
        const int height = ctx.gBuffer->GetHeight();
        if (width <= 0 || height <= 0)
        {
            return;
        }

        GLint previousReadFramebuffer = 0;
        GLint previousDrawFramebuffer = 0;
        GLint previousReadBuffer = 0;
        GLint previousDrawBuffer = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
        glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
        glGetIntegerv(GL_DRAW_BUFFER, &previousDrawBuffer);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, ctx.gBuffer->GetFBO());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_readbackFbo);

        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        glBlitFramebuffer(0, 0, width, height, 0, 0, readbackSize.x, readbackSize.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        glReadBuffer(GL_COLOR_ATTACHMENT1);
        glDrawBuffer(GL_COLOR_ATTACHMENT1);
        glBlitFramebuffer(0, 0, width, height, 0, 0, readbackSize.x, readbackSize.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        glReadBuffer(GL_COLOR_ATTACHMENT2);
        glDrawBuffer(GL_COLOR_ATTACHMENT2);
        glBlitFramebuffer(0, 0, width, height, 0, 0, readbackSize.x, readbackSize.y, GL_COLOR_BUFFER_BIT, GL_NEAREST);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, previousReadFramebuffer);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, previousDrawFramebuffer);
        glReadBuffer(previousReadBuffer);
        glDrawBuffer(previousDrawBuffer);

        const std::size_t pboIndex = m_readbackPboIndex;

        glBindTexture(GL_TEXTURE_2D, m_readbackPositionTexture);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_positionReadbackPbos[pboIndex]);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_FLOAT, nullptr);

        glBindTexture(GL_TEXTURE_2D, m_readbackNormalTexture);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_normalReadbackPbos[pboIndex]);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, nullptr);

        glBindTexture(GL_TEXTURE_2D, m_readbackAlbedoTexture);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, m_albedoReadbackPbos[pboIndex]);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glBindTexture(GL_TEXTURE_2D, 0);

        m_queuedReadbackFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if (m_queuedReadbackFence == nullptr)
        {
            return;
        }

        glFlush();

        m_hasQueuedReadback = true;
        m_queuedReadbackTarget = renderTarget;
        m_queuedReadbackTime = captureTime;
        m_queuedReadbackSize = readbackSize;
        m_queuedReadbackGridOrigin = gridOrigin;
        m_queuedReadbackGridSize = gridSize;
        m_queuedReadbackPboIndex = pboIndex;
        m_readbackPboIndex = (pboIndex + 1) % kReadbackBufferCount;
    }

    void LightPropagationVolumePass::TryResolveQueuedReadback()
    {
        if (!m_hasQueuedReadback || m_queuedReadbackFence == nullptr)
        {
            return;
        }

        const GLenum waitResult = glClientWaitSync(m_queuedReadbackFence, 0, 0);
        if (waitResult == GL_TIMEOUT_EXPIRED)
        {
            return;
        }

        glDeleteSync(m_queuedReadbackFence);
        m_queuedReadbackFence = nullptr;

        if (waitResult == GL_WAIT_FAILED)
        {
            m_hasQueuedReadback = false;
            m_queuedReadbackTarget = nullptr;
            m_queuedReadbackTime = {};
            m_queuedReadbackSize = glm::ivec2(0);
            m_queuedReadbackGridOrigin = glm::vec3(0.0f);
            m_queuedReadbackGridSize = glm::vec3(32.0f, 20.0f, 32.0f);
            m_queuedReadbackPboIndex = 0;
            return;
        }

        auto stateIt = m_volumeStates.find(m_queuedReadbackTarget);
        if (stateIt == m_volumeStates.end())
        {
            m_hasQueuedReadback = false;
            m_queuedReadbackTarget = nullptr;
            m_queuedReadbackTime = {};
            m_queuedReadbackSize = glm::ivec2(0);
            m_queuedReadbackGridOrigin = glm::vec3(0.0f);
            m_queuedReadbackGridSize = glm::vec3(32.0f, 20.0f, 32.0f);
            m_queuedReadbackPboIndex = 0;
            return;
        }

        const int readbackWidth = m_queuedReadbackSize.x;
        const int readbackHeight = m_queuedReadbackSize.y;
        const GLsizeiptr positionBytes = static_cast<GLsizeiptr>(readbackWidth) * static_cast<GLsizeiptr>(readbackHeight) * 3 * static_cast<GLsizeiptr>(sizeof(float));
        const GLsizeiptr normalBytes = static_cast<GLsizeiptr>(readbackWidth) * static_cast<GLsizeiptr>(readbackHeight) * 4 * static_cast<GLsizeiptr>(sizeof(float));
        const GLsizeiptr albedoBytes = static_cast<GLsizeiptr>(readbackWidth) * static_cast<GLsizeiptr>(readbackHeight) * 4 * static_cast<GLsizeiptr>(sizeof(unsigned char));

        m_positionReadback.resize(static_cast<std::size_t>(readbackWidth * readbackHeight * 3));
        m_normalReadback.resize(static_cast<std::size_t>(readbackWidth * readbackHeight * 4));
        m_albedoReadback.resize(static_cast<std::size_t>(readbackWidth * readbackHeight * 4));

        const bool positionCopied = CopyReadbackPbo(m_positionReadbackPbos[m_queuedReadbackPboIndex], m_positionReadback.data(), positionBytes);
        const bool normalCopied = CopyReadbackPbo(m_normalReadbackPbos[m_queuedReadbackPboIndex], m_normalReadback.data(), normalBytes);
        const bool albedoCopied = CopyReadbackPbo(m_albedoReadbackPbos[m_queuedReadbackPboIndex], m_albedoReadback.data(), albedoBytes);

        if (positionCopied && normalCopied && albedoCopied)
        {
            auto &state = stateIt->second;
            state.positionReadback = m_positionReadback;
            state.normalReadback = m_normalReadback;
            state.albedoReadback = m_albedoReadback;
            state.lastCaptureTime = m_queuedReadbackTime;
            state.lastReadbackSize = m_queuedReadbackSize;
            state.capturedGridOrigin = m_queuedReadbackGridOrigin;
            state.capturedGridSize = m_queuedReadbackGridSize;
            state.hasCapturedSamples = true;
        }

        m_hasQueuedReadback = false;
        m_queuedReadbackTarget = nullptr;
        m_queuedReadbackTime = {};
        m_queuedReadbackSize = glm::ivec2(0);
        m_queuedReadbackGridOrigin = glm::vec3(0.0f);
        m_queuedReadbackGridSize = glm::vec3(32.0f, 20.0f, 32.0f);
        m_queuedReadbackPboIndex = 0;
    }

    void LightPropagationVolumePass::EnsureResources(VolumeState &state)
    {
        const std::size_t voxelCount = static_cast<std::size_t>(m_resolution.x * m_resolution.y * m_resolution.z);
        for (auto &volumeTexture : state.volumeTextures)
        {
            if (!volumeTexture ||
                volumeTexture->GetType() != GL_TEXTURE_3D ||
                volumeTexture->GetWidth() != m_resolution.x ||
                volumeTexture->GetHeight() != m_resolution.y ||
                volumeTexture->GetDepth() != m_resolution.z)
            {
                volumeTexture.reset(Texture::ColorVolume(m_resolution.x, m_resolution.y, m_resolution.z));
            }
        }

        if (state.currentRadiance.size() != voxelCount)
        {
            state.currentRadiance.assign(voxelCount, glm::vec3(0.0f));
            state.nextRadiance.assign(voxelCount, glm::vec3(0.0f));
            state.historyRadiance.assign(voxelCount, glm::vec3(0.0f));
            state.injectionWeights.assign(voxelCount, 0.0f);
            state.hasHistory = false;
            state.hasValidVolume = false;
            state.hasPublishedVolume = false;
            state.hasPendingPublishedVolume = false;
        }
    }

    void LightPropagationVolumePass::ClearVolume(VolumeState &state)
    {
        EnsureResources(state);
        std::fill(state.currentRadiance.begin(), state.currentRadiance.end(), glm::vec3(0.0f));
        std::fill(state.nextRadiance.begin(), state.nextRadiance.end(), glm::vec3(0.0f));
        std::fill(state.injectionWeights.begin(), state.injectionWeights.end(), 0.0f);
        state.positionReadback.clear();
        state.normalReadback.clear();
        state.albedoReadback.clear();
        state.lastReadbackSize = glm::ivec2(0);
        state.hasCapturedSamples = false;
        for (auto &volumeTexture : state.volumeTextures)
        {
            if (volumeTexture)
            {
                volumeTexture->Upload3D(GL_RGB, GL_FLOAT, state.currentRadiance.data());
            }
        }
        state.activeVolumeTextureIndex = 0;
        state.pendingVolumeTextureIndex = 0;
        state.capturedGridOrigin = state.gridOrigin;
        state.capturedGridSize = state.gridSize;
        state.pendingGridOrigin = state.gridOrigin;
        state.pendingGridSize = state.gridSize;
        state.hasPublishedVolume = false;
        state.hasPendingPublishedVolume = false;

        state.hasValidVolume = false;
        state.hasHistory = false;
    }

    void LightPropagationVolumePass::Execute(const RenderContext &ctx)
    {
        TryResolveQueuedReadback();

        auto &state = GetOrCreateVolumeState(ctx.renderTarget);
        m_activeVolumeState = &state;

        if (state.hasPendingPublishedVolume)
        {
            state.activeVolumeTextureIndex = state.pendingVolumeTextureIndex;
            state.gridOrigin = state.pendingGridOrigin;
            state.gridSize = state.pendingGridSize;
            state.hasPublishedVolume = true;
            state.hasPendingPublishedVolume = false;
        }

        if (!m_enabled)
        {
            if (state.hasValidVolume || state.hasHistory)
            {
                ClearVolume(state);
            }
            state.lastSettingsRevision = m_settingsRevision;
            return;
        }

        if (!ctx.hasCameraData || !ctx.gBuffer || !ctx.lights)
        {
            ClearVolume(state);
            return;
        }

        EnsureResources(state);

        const glm::vec3 cameraPosition = GetCameraPosition(ctx.cameraData);
        const glm::vec3 cameraForward = GetCameraForward(ctx.cameraData);
        const float horizontalExtent = glm::clamp(ctx.cameraData.farPlane * 0.4f, 20.0f, 64.0f);
        const float verticalExtent = glm::clamp(ctx.cameraData.farPlane * 0.22f, 12.0f, 28.0f);
        const glm::vec3 desiredGridSize(horizontalExtent, verticalExtent, horizontalExtent);
        const float lookAnchorDistance = glm::clamp(ctx.cameraData.farPlane * m_lookAnchorDistanceScale, m_lookAnchorDistanceMin, m_lookAnchorDistanceMax);
        const glm::vec3 lookAnchorPoint = cameraPosition + cameraForward * lookAnchorDistance;
        glm::vec3 desiredGridOrigin = lookAnchorPoint - desiredGridSize * 0.5f;

        if (state.hasValidVolume && glm::distance(desiredGridSize, state.gridSize) <= 0.0001f)
        {
            const bool focusInsideDeadZone = IsPointInsideInnerBox(lookAnchorPoint, state.gridOrigin, state.gridSize, m_focusDeadZone);
            const bool cameraInsideSafeRegion = IsPointInsideInnerBox(cameraPosition, state.gridOrigin, state.gridSize, m_cameraSafeMargin);
            if (focusInsideDeadZone && cameraInsideSafeRegion)
            {
                desiredGridOrigin = state.gridOrigin;
            }
        }

        desiredGridOrigin = ClampOriginToKeepPointInside(desiredGridOrigin, desiredGridSize, cameraPosition, m_cameraSafeMargin);
        desiredGridOrigin = SnapGridOriginToVoxel(desiredGridOrigin, desiredGridSize, m_resolution);

        std::fill(state.currentRadiance.begin(), state.currentRadiance.end(), glm::vec3(0.0f));
        std::fill(state.nextRadiance.begin(), state.nextRadiance.end(), glm::vec3(0.0f));
        std::fill(state.injectionWeights.begin(), state.injectionWeights.end(), 0.0f);

        const int width = ctx.gBuffer->GetWidth();
        const int height = ctx.gBuffer->GetHeight();
        if (width <= 0 || height <= 0)
        {
            ClearVolume(state);
            return;
        }

        const auto now = std::chrono::steady_clock::now();
        const glm::ivec2 viewportSize(width, height);
        const bool viewportChanged = viewportSize != state.lastViewportSize;
        const bool farPlaneChanged = std::abs(ctx.cameraData.farPlane - state.lastFarPlane) >= kFarPlaneUpdateThreshold;
        const std::uint64_t lightSignature = ComputeLightSignature(*ctx.lights);
        const bool lightSignatureChanged = !state.hasValidVolume || lightSignature != state.lastLightSignature;
        const bool settingsChanged = !state.hasValidVolume || state.lastSettingsRevision != m_settingsRevision;
        const bool gridOriginChanged = !state.hasValidVolume || glm::distance(desiredGridOrigin, state.gridOrigin) > 0.0001f;
        const bool gridSizeChanged = !state.hasValidVolume || glm::distance(desiredGridSize, state.gridSize) > 0.0001f;
        const bool contentStateChanged = viewportChanged || farPlaneChanged || gridSizeChanged || lightSignatureChanged || settingsChanged;
        const bool recenterStateChanged = gridOriginChanged;
        const float requiredUpdateIntervalMs = contentStateChanged
                                                   ? m_activeUpdateIntervalMs
                                                   : (recenterStateChanged ? m_recenterUpdateIntervalMs : m_steadyStateUpdateIntervalMs);
        const bool updateIntervalElapsed = !state.hasValidVolume ||
                                           std::chrono::duration<float, std::milli>(now - state.lastUpdateTime).count() >= requiredUpdateIntervalMs;
        const bool steadyStateCaptureElapsed = !state.hasCapturedSamples ||
                                               std::chrono::duration<float, std::milli>(now - state.lastCaptureTime).count() >= m_steadyStateCaptureIntervalMs;

        if (state.hasValidVolume && !contentStateChanged && !recenterStateChanged && !updateIntervalElapsed)
        {
            return;
        }

        if (state.hasValidVolume && recenterStateChanged && !contentStateChanged && !updateIntervalElapsed)
        {
            return;
        }

        const glm::vec3 targetGridSize = desiredGridSize;
        const glm::vec3 targetGridOrigin = desiredGridOrigin;

        const int targetReadbackDimension = std::clamp(m_resolution.x * kReadbackSamplesPerAxisScale, kMinReadbackTargetDimension, kMaxReadbackTargetDimension);
        const int sampleStep = glm::max(1, glm::max(width, height) / targetReadbackDimension);
        const int readbackWidth = glm::max(1, (width + sampleStep - 1) / sampleStep);
        const int readbackHeight = glm::max(1, (height + sampleStep - 1) / sampleStep);
        const glm::ivec2 readbackSize(readbackWidth, readbackHeight);
        const bool readbackSizeChanged = state.lastReadbackSize != readbackSize;
        const bool needsFreshCapture = contentStateChanged || recenterStateChanged || readbackSizeChanged || steadyStateCaptureElapsed || !state.hasCapturedSamples;
        const bool hasUnprocessedCapturedSamples = state.hasCapturedSamples && (!state.hasValidVolume || state.lastCaptureTime > state.lastUpdateTime);

        if (state.hasValidVolume &&
            !hasUnprocessedCapturedSamples &&
            !contentStateChanged &&
            !recenterStateChanged &&
            !steadyStateCaptureElapsed)
        {
            return;
        }

        if (needsFreshCapture && !hasUnprocessedCapturedSamples)
        {
            EnsureReadbackResources(readbackWidth, readbackHeight);
        }

        if (needsFreshCapture && !hasUnprocessedCapturedSamples)
        {
            QueueReadback(ctx, ctx.renderTarget, readbackSize, targetGridOrigin, targetGridSize, now);
        }

        if (m_hasQueuedReadback && m_queuedReadbackTarget == ctx.renderTarget)
        {
            return;
        }

        if (!state.hasCapturedSamples || state.positionReadback.empty() || state.normalReadback.empty() || state.albedoReadback.empty())
        {
            return;
        }

        const int sampleReadbackWidth = state.lastReadbackSize.x;
        const int sampleReadbackHeight = state.lastReadbackSize.y;
        if (sampleReadbackWidth <= 0 || sampleReadbackHeight <= 0)
        {
            return;
        }

        const std::size_t expectedPositionSamples = static_cast<std::size_t>(sampleReadbackWidth) * static_cast<std::size_t>(sampleReadbackHeight) * 3;
        const std::size_t expectedNormalSamples = static_cast<std::size_t>(sampleReadbackWidth) * static_cast<std::size_t>(sampleReadbackHeight) * 4;
        const std::size_t expectedAlbedoSamples = static_cast<std::size_t>(sampleReadbackWidth) * static_cast<std::size_t>(sampleReadbackHeight) * 4;
        if (state.positionReadback.size() < expectedPositionSamples ||
            state.normalReadback.size() < expectedNormalSamples ||
            state.albedoReadback.size() < expectedAlbedoSamples)
        {
            state.hasCapturedSamples = false;
            return;
        }

        const bool useCapturedSampleGrid = hasUnprocessedCapturedSamples;
        const glm::vec3 injectionGridOrigin = useCapturedSampleGrid ? state.capturedGridOrigin : targetGridOrigin;
        const glm::vec3 injectionGridSize = useCapturedSampleGrid ? state.capturedGridSize : targetGridSize;

        for (int y = 0; y < sampleReadbackHeight; ++y)
        {
            for (int x = 0; x < sampleReadbackWidth; ++x)
            {
                const std::size_t pixelIndex = static_cast<std::size_t>(y * sampleReadbackWidth + x);
                const std::size_t positionIndex = pixelIndex * 3;
                const std::size_t normalIndex = pixelIndex * 4;
                const std::size_t albedoIndex = pixelIndex * 4;

                const glm::vec3 worldPosition(
                    state.positionReadback[positionIndex + 0],
                    state.positionReadback[positionIndex + 1],
                    state.positionReadback[positionIndex + 2]);
                const glm::vec3 normal(
                    state.normalReadback[normalIndex + 0],
                    state.normalReadback[normalIndex + 1],
                    state.normalReadback[normalIndex + 2]);

                if (glm::dot(normal, normal) <= 0.01f)
                {
                    continue;
                }

                const glm::vec3 albedo(
                    static_cast<float>(state.albedoReadback[albedoIndex + 0]) / 255.0f,
                    static_cast<float>(state.albedoReadback[albedoIndex + 1]) / 255.0f,
                    static_cast<float>(state.albedoReadback[albedoIndex + 2]) / 255.0f);
                const float metallic = static_cast<float>(state.albedoReadback[albedoIndex + 3]) / 255.0f;
                const glm::vec3 injectedRadiance = ComputeInjectedRadiance(worldPosition, normal, albedo, metallic, *ctx.lights);
                if (glm::dot(injectedRadiance, injectedRadiance) <= 0.000001f)
                {
                    continue;
                }

                glm::ivec3 cell(0);
                if (!WorldToCell(worldPosition, injectionGridOrigin, injectionGridSize, m_resolution, cell))
                {
                    continue;
                }

                const std::size_t cellIndex = FlattenCellIndex(m_resolution, cell.x, cell.y, cell.z);
                state.currentRadiance[cellIndex] += injectedRadiance;
                state.injectionWeights[cellIndex] += 1.0f;
            }
        }

        for (std::size_t cellIndex = 0; cellIndex < state.currentRadiance.size(); ++cellIndex)
        {
            if (state.injectionWeights[cellIndex] > 0.0f)
            {
                state.currentRadiance[cellIndex] = (state.currentRadiance[cellIndex] / state.injectionWeights[cellIndex]) * m_injectionBoost;
            }
        }

        for (int iteration = 0; iteration < m_propagationIterations; ++iteration)
        {
            for (int z = 0; z < m_resolution.z; ++z)
            {
                for (int y = 0; y < m_resolution.y; ++y)
                {
                    for (int x = 0; x < m_resolution.x; ++x)
                    {
                        const std::size_t cellIndex = FlattenCellIndex(m_resolution, x, y, z);
                        glm::vec3 propagatedRadiance = state.currentRadiance[cellIndex] * kPropagationSelfWeight;

                        if (x > 0)
                        {
                            propagatedRadiance += state.currentRadiance[FlattenCellIndex(m_resolution, x - 1, y, z)] * kPropagationNeighborWeight;
                        }
                        if (x + 1 < m_resolution.x)
                        {
                            propagatedRadiance += state.currentRadiance[FlattenCellIndex(m_resolution, x + 1, y, z)] * kPropagationNeighborWeight;
                        }
                        if (y > 0)
                        {
                            propagatedRadiance += state.currentRadiance[FlattenCellIndex(m_resolution, x, y - 1, z)] * kPropagationNeighborWeight;
                        }
                        if (y + 1 < m_resolution.y)
                        {
                            propagatedRadiance += state.currentRadiance[FlattenCellIndex(m_resolution, x, y + 1, z)] * kPropagationNeighborWeight;
                        }
                        if (z > 0)
                        {
                            propagatedRadiance += state.currentRadiance[FlattenCellIndex(m_resolution, x, y, z - 1)] * kPropagationNeighborWeight;
                        }
                        if (z + 1 < m_resolution.z)
                        {
                            propagatedRadiance += state.currentRadiance[FlattenCellIndex(m_resolution, x, y, z + 1)] * kPropagationNeighborWeight;
                        }

                        state.nextRadiance[cellIndex] = propagatedRadiance;
                    }
                }
            }

            state.currentRadiance.swap(state.nextRadiance);
        }

        if (state.hasHistory)
        {
            for (int z = 0; z < m_resolution.z; ++z)
            {
                for (int y = 0; y < m_resolution.y; ++y)
                {
                    for (int x = 0; x < m_resolution.x; ++x)
                    {
                        const glm::vec3 worldPosition = GetCellCenterWorldPosition(injectionGridOrigin, injectionGridSize, m_resolution, x, y, z);
                        glm::ivec3 historyCell(0);
                        if (!WorldToCell(worldPosition, state.historyGridOrigin, state.historyGridSize, m_resolution, historyCell))
                        {
                            continue;
                        }

                        const std::size_t cellIndex = FlattenCellIndex(m_resolution, x, y, z);
                        const std::size_t historyIndex = FlattenCellIndex(m_resolution, historyCell.x, historyCell.y, historyCell.z);
                        state.currentRadiance[cellIndex] = glm::mix(state.currentRadiance[cellIndex], state.historyRadiance[historyIndex], m_temporalBlendWeight);
                    }
                }
            }
        }

        const std::size_t uploadTextureIndex = state.hasPublishedVolume ? (1 - state.activeVolumeTextureIndex) : state.activeVolumeTextureIndex;
        auto *uploadTexture = state.volumeTextures[uploadTextureIndex].get();
        if (uploadTexture)
        {
            uploadTexture->Upload3D(GL_RGB, GL_FLOAT, state.currentRadiance.data());
            state.pendingVolumeTextureIndex = uploadTextureIndex;
            state.pendingGridOrigin = injectionGridOrigin;
            state.pendingGridSize = injectionGridSize;
            state.hasPendingPublishedVolume = true;
        }

        state.historyRadiance = state.currentRadiance;
        state.historyGridOrigin = injectionGridOrigin;
        state.historyGridSize = injectionGridSize;
        state.lastUpdateTime = now;
        state.lastViewportSize = viewportSize;
        state.lastFarPlane = ctx.cameraData.farPlane;
        state.lastLightSignature = lightSignature;
        state.lastSettingsRevision = m_settingsRevision;
        state.hasValidVolume = true;
        state.hasHistory = true;
    }
}