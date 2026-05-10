#include "PlutoGE/render/passes/ShadowPass.h"

#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace
{
    constexpr int kProjectedShadowPassMode = 0;
    constexpr int kPointShadowPassMode = 1;
    constexpr float kDirectionalShadowPadding = 2.0f;

    struct FrustumPlane
    {
        glm::vec3 normal{0.0f};
        float distance = 0.0f;
    };

    glm::vec3 ResolveUpVector(const glm::vec3 &direction)
    {
        return std::abs(direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    }

    struct ShadowCasterBounds
    {
        glm::vec3 center{0.0f};
        float radius = 10.0f;
    };

    int GetDirectionalCascadeCount(const PlutoGE::scene::Light &light)
    {
        return std::clamp(light.activeShadowCascadeCount, 1, PlutoGE::scene::kMaxDirectionalShadowCascades);
    }

    int GetShadowResolution(const PlutoGE::scene::Light &light)
    {
        return std::max(light.directionalShadowSettings.resolution, 256);
    }

    PlutoGE::render::MeshBounds GetWorldBounds(const PlutoGE::render::RenderCommand &command)
    {
        if (!command.mesh)
        {
            return {};
        }

        const auto &bounds = command.submeshIndex < command.mesh->GetSubmeshCount()
                                 ? command.mesh->GetSubmesh(command.submeshIndex).bounds
                                 : command.mesh->GetBounds();
        const glm::vec3 worldCenter = glm::vec3(command.model * glm::vec4(bounds.center, 1.0f));
        const float scaleX = glm::length(glm::vec3(command.model[0]));
        const float scaleY = glm::length(glm::vec3(command.model[1]));
        const float scaleZ = glm::length(glm::vec3(command.model[2]));

        return PlutoGE::render::MeshBounds{
            .center = worldCenter,
            .radius = bounds.radius * std::max(scaleX, std::max(scaleY, scaleZ)),
        };
    }

    std::array<glm::vec3, 8> BuildCameraFrustumCorners(const PlutoGE::render::CameraData &cameraData)
    {
        const glm::mat4 inverseViewProjection = glm::inverse(cameraData.projection * cameraData.view);
        std::array<glm::vec3, 8> corners{};
        std::size_t index = 0;

        for (int z = 0; z < 2; ++z)
        {
            const float clipZ = z == 0 ? -1.0f : 1.0f;
            for (int y = 0; y < 2; ++y)
            {
                const float clipY = y == 0 ? -1.0f : 1.0f;
                for (int x = 0; x < 2; ++x)
                {
                    const float clipX = x == 0 ? -1.0f : 1.0f;
                    const glm::vec4 worldCorner = inverseViewProjection * glm::vec4(clipX, clipY, clipZ, 1.0f);
                    corners[index++] = glm::vec3(worldCorner) / worldCorner.w;
                }
            }
        }

        return corners;
    }

    std::array<glm::vec3, 8> BuildCascadeFrustumCorners(const PlutoGE::render::CameraData &cameraData, float cascadeNear, float cascadeFar)
    {
        const auto frustumCorners = BuildCameraFrustumCorners(cameraData);
        std::array<glm::vec3, 8> cascadeCorners{};
        const float clipRange = glm::max(cameraData.farPlane - cameraData.nearPlane, 0.0001f);
        const float nearFactor = glm::clamp((cascadeNear - cameraData.nearPlane) / clipRange, 0.0f, 1.0f);
        const float farFactor = glm::clamp((cascadeFar - cameraData.nearPlane) / clipRange, 0.0f, 1.0f);

        for (std::size_t index = 0; index < 4; ++index)
        {
            const glm::vec3 &cameraNearCorner = frustumCorners[index];
            const glm::vec3 &cameraFarCorner = frustumCorners[index + 4];
            cascadeCorners[index] = cameraNearCorner + (cameraFarCorner - cameraNearCorner) * nearFactor;
            cascadeCorners[index + 4] = cameraNearCorner + (cameraFarCorner - cameraNearCorner) * farFactor;
        }

        return cascadeCorners;
    }

    std::array<float, PlutoGE::scene::kMaxDirectionalShadowCascades> BuildDirectionalCascadeSplits(
        const PlutoGE::render::CameraData &cameraData,
        const PlutoGE::scene::DirectionalShadowSettings &settings,
        int cascadeCount)
    {
        std::array<float, PlutoGE::scene::kMaxDirectionalShadowCascades> splits{};
        const float nearPlane = cameraData.nearPlane;
        const float farPlane = glm::min(cameraData.farPlane, glm::max(settings.maxDistance, nearPlane + 0.1f));
        const float lambda = glm::clamp(settings.splitLambda, 0.0f, 1.0f);

        for (int cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
        {
            const float splitFactor = static_cast<float>(cascadeIndex + 1) / static_cast<float>(cascadeCount);
            const float logarithmicSplit = nearPlane * std::pow(farPlane / nearPlane, splitFactor);
            const float uniformSplit = nearPlane + (farPlane - nearPlane) * splitFactor;
            splits[cascadeIndex] = glm::mix(uniformSplit, logarithmicSplit, lambda);
        }

        for (int cascadeIndex = cascadeCount; cascadeIndex < PlutoGE::scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
        {
            splits[cascadeIndex] = farPlane;
        }

        return splits;
    }

    glm::mat4 BuildDirectionalCascadeMatrix(
        const PlutoGE::scene::Light &light,
        const PlutoGE::render::CameraData &cameraData,
        float cascadeNear,
        float cascadeFar,
        int shadowResolution)
    {
        const glm::vec3 lightDirection = glm::normalize(light.direction);
        const auto cascadeCorners = BuildCascadeFrustumCorners(cameraData, cascadeNear, cascadeFar);
        glm::vec3 frustumCenter(0.0f);
        for (const glm::vec3 &corner : cascadeCorners)
        {
            frustumCenter += corner;
        }
        frustumCenter /= static_cast<float>(cascadeCorners.size());

        float radius = 0.0f;
        for (const glm::vec3 &corner : cascadeCorners)
        {
            radius = glm::max(radius, glm::length(corner - frustumCenter));
        }

        radius = glm::max(radius + kDirectionalShadowPadding, 10.0f);
        const glm::vec3 eye = frustumCenter - lightDirection * (radius * 2.0f);
        const glm::mat4 view = glm::lookAt(eye, frustumCenter, ResolveUpVector(lightDirection));

        glm::vec3 minBounds(std::numeric_limits<float>::max());
        glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
        for (const glm::vec3 &corner : cascadeCorners)
        {
            const glm::vec3 lightSpaceCorner = glm::vec3(view * glm::vec4(corner, 1.0f));
            minBounds = glm::min(minBounds, lightSpaceCorner);
            maxBounds = glm::max(maxBounds, lightSpaceCorner);
        }

        minBounds -= glm::vec3(kDirectionalShadowPadding);
        maxBounds += glm::vec3(kDirectionalShadowPadding);

        const glm::vec2 extents = glm::max(glm::vec2(maxBounds - minBounds), glm::vec2(1.0f));
        const glm::vec2 texelSize = extents / static_cast<float>(shadowResolution);
        glm::vec2 centerXY = (glm::vec2(minBounds) + glm::vec2(maxBounds)) * 0.5f;
        centerXY = glm::floor(centerXY / texelSize) * texelSize;
        const glm::vec2 halfExtents = extents * 0.5f;

        minBounds.x = centerXY.x - halfExtents.x;
        maxBounds.x = centerXY.x + halfExtents.x;
        minBounds.y = centerXY.y - halfExtents.y;
        maxBounds.y = centerXY.y + halfExtents.y;

        const float nearPlane = glm::max(0.1f, -maxBounds.z);
        const float farPlane = glm::max(nearPlane + 0.1f, -minBounds.z);
        const glm::mat4 projection = glm::ortho(minBounds.x, maxBounds.x, minBounds.y, maxBounds.y, nearPlane, farPlane);
        return projection * view;
    }

    glm::mat4 BuildSpotShadowMatrix(const PlutoGE::scene::Light &light)
    {
        const float farPlane = glm::max(light.range, 0.1f);
        const glm::vec3 lightDirection = glm::normalize(light.direction);
        const glm::mat4 view = glm::lookAt(light.position, light.position + lightDirection, ResolveUpVector(lightDirection));
        const glm::mat4 projection = glm::perspective(glm::radians(50.0f), 1.0f, 0.1f, farPlane);
        return projection * view;
    }

    std::array<glm::mat4, 6> BuildPointShadowMatrices(const PlutoGE::scene::Light &light, float farPlane)
    {
        const glm::mat4 projection = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, farPlane);
        const glm::vec3 position = light.position;

        return {
            projection * glm::lookAt(position, position + glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
            projection * glm::lookAt(position, position + glm::vec3(-1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
            projection * glm::lookAt(position, position + glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f)),
            projection * glm::lookAt(position, position + glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f)),
            projection * glm::lookAt(position, position + glm::vec3(0.0f, 0.0f, 1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
            projection * glm::lookAt(position, position + glm::vec3(0.0f, 0.0f, -1.0f), glm::vec3(0.0f, -1.0f, 0.0f)),
        };
    }

    std::array<FrustumPlane, 6> ExtractFrustumPlanes(const glm::mat4 &viewProjection)
    {
        std::array<FrustumPlane, 6> planes = {
            FrustumPlane{glm::vec3(viewProjection[0][3] + viewProjection[0][0], viewProjection[1][3] + viewProjection[1][0], viewProjection[2][3] + viewProjection[2][0]), viewProjection[3][3] + viewProjection[3][0]},
            FrustumPlane{glm::vec3(viewProjection[0][3] - viewProjection[0][0], viewProjection[1][3] - viewProjection[1][0], viewProjection[2][3] - viewProjection[2][0]), viewProjection[3][3] - viewProjection[3][0]},
            FrustumPlane{glm::vec3(viewProjection[0][3] + viewProjection[0][1], viewProjection[1][3] + viewProjection[1][1], viewProjection[2][3] + viewProjection[2][1]), viewProjection[3][3] + viewProjection[3][1]},
            FrustumPlane{glm::vec3(viewProjection[0][3] - viewProjection[0][1], viewProjection[1][3] - viewProjection[1][1], viewProjection[2][3] - viewProjection[2][1]), viewProjection[3][3] - viewProjection[3][1]},
            FrustumPlane{glm::vec3(viewProjection[0][3] + viewProjection[0][2], viewProjection[1][3] + viewProjection[1][2], viewProjection[2][3] + viewProjection[2][2]), viewProjection[3][3] + viewProjection[3][2]},
            FrustumPlane{glm::vec3(viewProjection[0][3] - viewProjection[0][2], viewProjection[1][3] - viewProjection[1][2], viewProjection[2][3] - viewProjection[2][2]), viewProjection[3][3] - viewProjection[3][2]},
        };

        for (auto &plane : planes)
        {
            const float length = glm::length(plane.normal);
            if (length > 1e-6f)
            {
                plane.normal /= length;
                plane.distance /= length;
            }
        }

        return planes;
    }

    bool IsBoundsVisible(const PlutoGE::render::MeshBounds &bounds, const std::array<FrustumPlane, 6> &planes)
    {
        for (const auto &plane : planes)
        {
            if (glm::dot(plane.normal, bounds.center) + plane.distance < -bounds.radius)
            {
                return false;
            }
        }

        return true;
    }

    bool IsCommandRelevantForPointLight(const PlutoGE::render::RenderCommand &command, const PlutoGE::scene::Light &light)
    {
        const auto bounds = GetWorldBounds(command);
        const float maxDistance = glm::max(light.range, 0.1f) + bounds.radius;
        const glm::vec3 offset = bounds.center - light.position;
        return glm::dot(offset, offset) <= maxDistance * maxDistance;
    }

    bool IsCommandRelevantForProjectedLight(const PlutoGE::render::RenderCommand &command, const std::array<FrustumPlane, 6> &planes)
    {
        return IsBoundsVisible(GetWorldBounds(command), planes);
    }
}

namespace PlutoGE::render
{
    void ShadowPass::Initialize()
    {
        m_shadowPassShader = Shader::CreateShadowPassShader();
        glGenFramebuffers(1, &m_shadowFramebuffer);
    }

    void ShadowPass::Execute(const RenderContext &ctx)
    {
        if (!m_shadowPassShader || !ctx.lights || !ctx.renderCommands || m_shadowFramebuffer == 0)
        {
            return;
        }

        GLint previousViewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, previousViewport);

        glBindFramebuffer(GL_FRAMEBUFFER, m_shadowFramebuffer);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glDisable(GL_POLYGON_OFFSET_FILL);

        m_shadowPassShader->Bind();

        for (auto *light : *ctx.lights)
        {
            if (!light || !light->castsShadows)
            {
                continue;
            }

            const bool needsUpdate = light->type == scene::LightType::Directional
                                         ? ctx.cameraComponent != nullptr
                                         : light->isDirty;
            if (!needsUpdate)
            {
                continue;
            }

            if (light->type == scene::LightType::Point)
            {
                auto *shadowMap = light->shadowMap.get();
                if (!shadowMap)
                {
                    continue;
                }

                glDisable(GL_POLYGON_OFFSET_FILL);
                const float farPlane = glm::max(light->range, 0.1f);
                light->shadowFarPlane = farPlane;
                const auto shadowMatrices = BuildPointShadowMatrices(*light, farPlane);
                glViewport(0, 0, shadowMap->GetWidth(), shadowMap->GetHeight());

                m_shadowPassShader->SetUniform("uShadowPassMode", kPointShadowPassMode);
                m_shadowPassShader->SetUniform("uLightPosition", light->position);
                m_shadowPassShader->SetUniform("uFarPlane", farPlane);
                Material *boundMaterial = nullptr;

                for (unsigned int face = 0; face < shadowMatrices.size(); ++face)
                {
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, shadowMap->GetTextureID(), 0);
                    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                    {
                        break;
                    }

                    glClear(GL_DEPTH_BUFFER_BIT);
                    m_shadowPassShader->SetUniform("uLightSpaceMatrix", shadowMatrices[face]);

                    for (const auto &command : *ctx.renderCommands)
                    {
                        if (!command.mesh || !command.material || !IsCommandRelevantForPointLight(command, *light))
                        {
                            continue;
                        }

                        m_shadowPassShader->SetUniform("uModel", command.model);
                        if (command.material != boundMaterial)
                        {
                            command.material->Bind(m_shadowPassShader);
                            boundMaterial = command.material;
                        }

                        command.mesh->DrawSubmesh(command.submeshIndex);
                    }
                }

                light->isDirty = false;
                continue;
            }

            if (light->type == scene::LightType::Directional)
            {
                if (!ctx.cameraComponent)
                {
                    continue;
                }

                const int cascadeCount = GetDirectionalCascadeCount(*light);
                const auto cascadeSplits = BuildDirectionalCascadeSplits(ctx.cameraData, light->directionalShadowSettings, cascadeCount);

                light->shadowMatrix = glm::mat4(1.0f);
                light->shadowFarPlane = cascadeSplits[cascadeCount - 1];
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(2.0f, 4.0f);
                glCullFace(GL_FRONT);
                m_shadowPassShader->SetUniform("uShadowPassMode", kProjectedShadowPassMode);

                for (int cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex)
                {
                    auto *cascadeMap = light->shadowCascadeMaps[cascadeIndex].get();
                    if (!cascadeMap)
                    {
                        continue;
                    }

                    const float cascadeNear = cascadeIndex == 0 ? ctx.cameraData.nearPlane : cascadeSplits[cascadeIndex - 1];
                    const float cascadeFar = cascadeSplits[cascadeIndex];
                    const int shadowResolution = cascadeMap->GetWidth() > 0 ? cascadeMap->GetWidth() : GetShadowResolution(*light);
                    const glm::mat4 cascadeMatrix = BuildDirectionalCascadeMatrix(*light, ctx.cameraData, cascadeNear, cascadeFar, shadowResolution);
                    light->shadowCascadeMatrices[cascadeIndex] = cascadeMatrix;
                    light->shadowCascadeSplits[cascadeIndex] = cascadeFar;

                    glViewport(0, 0, shadowResolution, shadowResolution);
                    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, cascadeMap->GetTextureID(), 0);
                    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                    {
                        continue;
                    }

                    glClear(GL_DEPTH_BUFFER_BIT);
                    m_shadowPassShader->SetUniform("uLightSpaceMatrix", cascadeMatrix);
                    Material *boundMaterial = nullptr;

                    for (const auto &command : *ctx.renderCommands)
                    {
                        if (!command.mesh || !command.material)
                        {
                            continue;
                        }

                        m_shadowPassShader->SetUniform("uModel", command.model);
                        if (command.material != boundMaterial)
                        {
                            command.material->Bind(m_shadowPassShader);
                            boundMaterial = command.material;
                        }

                        command.mesh->DrawSubmesh(command.submeshIndex);
                    }
                }

                for (int cascadeIndex = cascadeCount; cascadeIndex < scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
                {
                    light->shadowCascadeMatrices[cascadeIndex] = glm::mat4(1.0f);
                    light->shadowCascadeSplits[cascadeIndex] = light->shadowFarPlane;
                }

                light->isDirty = false;
                glCullFace(GL_BACK);
                continue;
            }

            auto *shadowMap = light->shadowMap.get();
            if (!shadowMap)
            {
                continue;
            }

            const glm::mat4 shadowMatrix = BuildSpotShadowMatrix(*light);
            const auto shadowFrustumPlanes = ExtractFrustumPlanes(shadowMatrix);

            light->shadowMatrix = shadowMatrix;
            light->shadowFarPlane = glm::max(light->range, 0.1f);
            glViewport(0, 0, shadowMap->GetWidth(), shadowMap->GetHeight());

            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(2.0f, 4.0f);
            glCullFace(GL_FRONT);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMap->GetTextureID(), 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            {
                glCullFace(GL_BACK);
                continue;
            }

            glClear(GL_DEPTH_BUFFER_BIT);
            m_shadowPassShader->SetUniform("uShadowPassMode", kProjectedShadowPassMode);
            m_shadowPassShader->SetUniform("uLightSpaceMatrix", shadowMatrix);
            Material *boundMaterial = nullptr;

            for (const auto &command : *ctx.renderCommands)
            {
                if (!command.mesh || !command.material)
                {
                    continue;
                }

                if (light->type == scene::LightType::Spot && !IsCommandRelevantForProjectedLight(command, shadowFrustumPlanes))
                {
                    continue;
                }

                m_shadowPassShader->SetUniform("uModel", command.model);
                if (command.material != boundMaterial)
                {
                    command.material->Bind(m_shadowPassShader);
                    boundMaterial = command.material;
                }

                command.mesh->DrawSubmesh(command.submeshIndex);
            }

            light->isDirty = false;
            glCullFace(GL_BACK);
        }

        m_shadowPassShader->Unbind();
        glDisable(GL_POLYGON_OFFSET_FILL);
        glCullFace(GL_BACK);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    }
}