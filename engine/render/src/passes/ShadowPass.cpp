#include "PlutoGE/render/passes/ShadowPass.h"

#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <array>
#include <limits>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace
{
    constexpr int kShadowMapResolution = 1024;
    constexpr int kProjectedShadowPassMode = 0;
    constexpr int kPointShadowPassMode = 1;

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

    ShadowCasterBounds BuildShadowCasterBounds(const std::vector<PlutoGE::render::RenderCommand> &renderCommands)
    {
        ShadowCasterBounds bounds;
        if (renderCommands.empty())
        {
            return bounds;
        }

        glm::vec3 minCorner(std::numeric_limits<float>::max());
        glm::vec3 maxCorner(std::numeric_limits<float>::lowest());

        for (const auto &command : renderCommands)
        {
            const glm::vec3 position = glm::vec3(command.model[3]);
            const float xScale = glm::length(glm::vec3(command.model[0]));
            const float yScale = glm::length(glm::vec3(command.model[1]));
            const float zScale = glm::length(glm::vec3(command.model[2]));
            const float maxScale = std::max(1.0f, std::max(xScale, std::max(yScale, zScale)));
            const glm::vec3 extent = glm::vec3(maxScale) * 0.8660254f;
            minCorner = glm::min(minCorner, position - extent);
            maxCorner = glm::max(maxCorner, position + extent);
        }

        bounds.center = (minCorner + maxCorner) * 0.5f;
        bounds.radius = glm::max(glm::length(maxCorner - bounds.center), 10.0f);
        return bounds;
    }

    glm::mat4 BuildDirectionalShadowMatrix(const PlutoGE::scene::Light &light, const std::vector<PlutoGE::render::RenderCommand> &renderCommands)
    {
        const ShadowCasterBounds casterBounds = BuildShadowCasterBounds(renderCommands);
        const glm::vec3 lightDirection = glm::normalize(light.direction);

        const glm::vec3 eye = casterBounds.center - lightDirection * (casterBounds.radius * 2.0f);
        const glm::mat4 view = glm::lookAt(eye, casterBounds.center, ResolveUpVector(lightDirection));

        glm::vec3 minBounds(std::numeric_limits<float>::max());
        glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
        for (const auto &command : renderCommands)
        {
            const glm::vec3 position = glm::vec3(command.model[3]);
            const float xScale = glm::length(glm::vec3(command.model[0]));
            const float yScale = glm::length(glm::vec3(command.model[1]));
            const float zScale = glm::length(glm::vec3(command.model[2]));
            const float maxScale = std::max(1.0f, std::max(xScale, std::max(yScale, zScale)));
            const glm::vec3 extent = glm::vec3(maxScale) * 0.8660254f;

            const std::array<glm::vec3, 8> corners = {
                position + glm::vec3(-extent.x, -extent.y, -extent.z),
                position + glm::vec3(-extent.x, -extent.y, extent.z),
                position + glm::vec3(-extent.x, extent.y, -extent.z),
                position + glm::vec3(-extent.x, extent.y, extent.z),
                position + glm::vec3(extent.x, -extent.y, -extent.z),
                position + glm::vec3(extent.x, -extent.y, extent.z),
                position + glm::vec3(extent.x, extent.y, -extent.z),
                position + glm::vec3(extent.x, extent.y, extent.z),
            };

            for (const glm::vec3 &corner : corners)
            {
                const glm::vec3 lightSpaceCorner = glm::vec3(view * glm::vec4(corner, 1.0f));
                minBounds = glm::min(minBounds, lightSpaceCorner);
                maxBounds = glm::max(maxBounds, lightSpaceCorner);
            }
        }

        const float zPadding = glm::max(casterBounds.radius, 25.0f);
        minBounds.z -= zPadding;
        maxBounds.z += zPadding;

        const glm::vec2 extents = glm::max(glm::vec2(maxBounds - minBounds), glm::vec2(1.0f));
        const glm::vec2 texelSize = extents / static_cast<float>(kShadowMapResolution);
        glm::vec2 centerXY = (glm::vec2(minBounds) + glm::vec2(maxBounds)) * 0.5f;
        centerXY = glm::floor(centerXY / texelSize) * texelSize;
        const glm::vec2 halfExtents = extents * 0.5f;

        minBounds.x = centerXY.x - halfExtents.x;
        maxBounds.x = centerXY.x + halfExtents.x;
        minBounds.y = centerXY.y - halfExtents.y;
        maxBounds.y = centerXY.y + halfExtents.y;

        const glm::mat4 projection = glm::ortho(minBounds.x, maxBounds.x, minBounds.y, maxBounds.y, -maxBounds.z, -minBounds.z);
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
        glViewport(0, 0, kShadowMapResolution, kShadowMapResolution);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glDisable(GL_POLYGON_OFFSET_FILL);

        m_shadowPassShader->Bind();

        for (auto *light : *ctx.lights)
        {
            if (!light || !light->castsShadows || !light->isDirty)
            {
                continue;
            }

            auto *shadowMap = light->shadowMap.get();
            if (!shadowMap)
            {
                continue;
            }

            if (light->type == scene::LightType::Point)
            {
                glDisable(GL_POLYGON_OFFSET_FILL);
                const float farPlane = glm::max(light->range, 0.1f);
                light->shadowFarPlane = farPlane;
                const auto shadowMatrices = BuildPointShadowMatrices(*light, farPlane);

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

            const glm::mat4 shadowMatrix = light->type == scene::LightType::Directional
                                               ? BuildDirectionalShadowMatrix(*light, *ctx.renderCommands)
                                               : BuildSpotShadowMatrix(*light);
            const auto shadowFrustumPlanes = light->type == scene::LightType::Directional
                                                 ? std::array<FrustumPlane, 6>{}
                                                 : ExtractFrustumPlanes(shadowMatrix);

            light->shadowMatrix = shadowMatrix;
            light->shadowFarPlane = glm::max(light->range, 0.1f);

            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(2.0f, 4.0f);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMap->GetTextureID(), 0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            {
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
        }

        m_shadowPassShader->Unbind();
        glDisable(GL_POLYGON_OFFSET_FILL);
        glCullFace(GL_BACK);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
    }
}