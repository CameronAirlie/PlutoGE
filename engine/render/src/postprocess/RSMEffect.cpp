#include "PlutoGE/render/postprocess/RSMEffect.h"

#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <algorithm>
#include <array>
#include <limits>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>

namespace PlutoGE::render
{
    namespace
    {
        constexpr int kMaxRsmSamples = 32;
        constexpr float kDirectionalShadowPadding = 2.0f;

        struct FrustumPlane
        {
            glm::vec3 normal{0.0f};
            float distance = 0.0f;
        };

        struct DirectionalRsmSource
        {
            const scene::Light *light = nullptr;
            glm::mat4 lightSpaceMatrix{1.0f};
            int shadowWidth = 0;
            int shadowHeight = 0;
        };

        bool HasUsableDirectionalShadowCapture(const scene::Light &light)
        {
            return light.castsShadows &&
                   light.activeShadowCascadeCount > 0 &&
                   light.shadowCascadeMaps[0] != nullptr;
        }

        MeshBounds GetWorldBounds(const RenderCommand &command)
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

            return MeshBounds{
                .center = worldCenter,
                .radius = bounds.radius * std::max(scaleX, std::max(scaleY, scaleZ)),
            };
        }

        glm::vec3 ResolveUpVector(const glm::vec3 &direction)
        {
            return std::abs(direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
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

        bool IsBoundsVisible(const MeshBounds &bounds, const std::array<FrustumPlane, 6> &planes)
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

        std::array<glm::vec3, 8> BuildCameraFrustumCorners(const CameraData &cameraData)
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

        std::array<glm::vec3, 8> BuildRsmFrustumCorners(const CameraData &cameraData, float nearPlane, float farPlane)
        {
            const auto frustumCorners = BuildCameraFrustumCorners(cameraData);
            std::array<glm::vec3, 8> rsmCorners{};
            const float clipRange = glm::max(cameraData.farPlane - cameraData.nearPlane, 0.0001f);
            const float nearFactor = glm::clamp((nearPlane - cameraData.nearPlane) / clipRange, 0.0f, 1.0f);
            const float farFactor = glm::clamp((farPlane - cameraData.nearPlane) / clipRange, 0.0f, 1.0f);

            for (std::size_t index = 0; index < 4; ++index)
            {
                const glm::vec3 &cameraNearCorner = frustumCorners[index];
                const glm::vec3 &cameraFarCorner = frustumCorners[index + 4];
                rsmCorners[index] = cameraNearCorner + (cameraFarCorner - cameraNearCorner) * nearFactor;
                rsmCorners[index + 4] = cameraNearCorner + (cameraFarCorner - cameraNearCorner) * farFactor;
            }

            return rsmCorners;
        }

        void ExpandDirectionalDepthBounds(const glm::mat4 &lightView,
                                          const std::vector<RenderCommand> &renderCommands,
                                          glm::vec3 &minBounds,
                                          glm::vec3 &maxBounds)
        {
            const glm::vec2 receiverMin(minBounds.x, minBounds.y);
            const glm::vec2 receiverMax(maxBounds.x, maxBounds.y);

            for (const auto &command : renderCommands)
            {
                if (!command.mesh || !command.material)
                {
                    continue;
                }

                const MeshBounds bounds = GetWorldBounds(command);
                const glm::vec3 lightSpaceCenter = glm::vec3(lightView * glm::vec4(bounds.center, 1.0f));
                const float radius = glm::max(bounds.radius, 0.001f);

                if (lightSpaceCenter.x + radius < receiverMin.x ||
                    lightSpaceCenter.x - radius > receiverMax.x ||
                    lightSpaceCenter.y + radius < receiverMin.y ||
                    lightSpaceCenter.y - radius > receiverMax.y)
                {
                    continue;
                }

                minBounds.z = glm::min(minBounds.z, lightSpaceCenter.z - radius - 2.0f);
                maxBounds.z = glm::max(maxBounds.z, lightSpaceCenter.z + radius + 2.0f);
            }
        }

        glm::mat4 BuildRsmLightSpaceMatrix(const scene::Light &light,
                                           const CameraData &cameraData,
                                           const std::vector<RenderCommand> &renderCommands)
        {
            const glm::vec3 lightDirection = glm::normalize(light.direction);
            const float shadowDistance = light.directionalShadowSettings.maxDistance > 0.0f
                                             ? glm::min(cameraData.farPlane, glm::max(light.directionalShadowSettings.maxDistance, cameraData.nearPlane + 0.1f))
                                             : cameraData.farPlane;
            const auto frustumCorners = BuildRsmFrustumCorners(cameraData, cameraData.nearPlane, shadowDistance);
            glm::vec3 frustumCenter(0.0f);
            for (const glm::vec3 &corner : frustumCorners)
            {
                frustumCenter += corner;
            }
            frustumCenter /= static_cast<float>(frustumCorners.size());

            float radius = 0.0f;
            for (const glm::vec3 &corner : frustumCorners)
            {
                radius = glm::max(radius, glm::length(corner - frustumCenter));
            }

            radius = glm::max(radius + kDirectionalShadowPadding, 10.0f);
            glm::vec3 eye = frustumCenter - lightDirection * (radius * 2.0f);
            const glm::vec3 upVector = ResolveUpVector(lightDirection);
            glm::mat4 lightView = glm::lookAt(eye, frustumCenter, upVector);

            glm::vec3 minBounds(std::numeric_limits<float>::max());
            glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
            for (const glm::vec3 &corner : frustumCorners)
            {
                const glm::vec3 lightSpaceCorner = glm::vec3(lightView * glm::vec4(corner, 1.0f));
                minBounds = glm::min(minBounds, lightSpaceCorner);
                maxBounds = glm::max(maxBounds, lightSpaceCorner);
            }

            minBounds -= glm::vec3(kDirectionalShadowPadding);
            maxBounds += glm::vec3(kDirectionalShadowPadding);
            ExpandDirectionalDepthBounds(lightView, renderCommands, minBounds, maxBounds);

            if (maxBounds.z > -kDirectionalShadowPadding)
            {
                const float retreatDistance = maxBounds.z + kDirectionalShadowPadding;
                eye -= lightDirection * retreatDistance;
                lightView = glm::lookAt(eye, frustumCenter, upVector);
                minBounds.z -= retreatDistance;
                maxBounds.z -= retreatDistance;
            }

            const glm::vec2 extents = glm::max(glm::vec2(maxBounds - minBounds), glm::vec2(1.0f));
            const int shadowResolution = std::max(light.directionalShadowSettings.resolution, 256);
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
            const glm::mat4 lightProjection = glm::ortho(minBounds.x, maxBounds.x, minBounds.y, maxBounds.y, nearPlane, farPlane);
            return lightProjection * lightView;
        }

        DirectionalRsmSource FindDirectionalRsmSource(const RenderContext &renderContext)
        {
            DirectionalRsmSource source;
            if (!renderContext.lights || !renderContext.renderCommands || !renderContext.hasCameraData)
            {
                return source;
            }

            float strongestDirectionalWeight = 0.0f;
            for (const auto *light : *renderContext.lights)
            {
                if (!light ||
                    light->type != scene::LightType::Directional ||
                    glm::dot(light->direction, light->direction) <= 0.000001f)
                {
                    continue;
                }

                const float lightWeight = light->intensity * std::max(light->color.x, std::max(light->color.y, light->color.z));
                if (lightWeight <= strongestDirectionalWeight)
                {
                    continue;
                }

                strongestDirectionalWeight = lightWeight;
                source.light = light;
                source.lightSpaceMatrix = BuildRsmLightSpaceMatrix(*light, renderContext.cameraData, *renderContext.renderCommands);
                if (HasUsableDirectionalShadowCapture(*light))
                {
                    source.shadowWidth = light->shadowCascadeMaps[0]->GetWidth();
                    source.shadowHeight = light->shadowCascadeMaps[0]->GetHeight();
                    continue;
                }

                const int fallbackResolution = std::max(light->directionalShadowSettings.resolution, 1024);
                source.shadowWidth = fallbackResolution;
                source.shadowHeight = fallbackResolution;
            }

            return source;
        }

        void DeleteTexture(unsigned int &textureId)
        {
            if (textureId != 0)
            {
                glDeleteTextures(1, &textureId);
                textureId = 0;
            }
        }

        void CreateColorAttachment(unsigned int &textureId, int width, int height, unsigned int attachment)
        {
            glGenTextures(1, &textureId);
            glBindTexture(GL_TEXTURE_2D, textureId);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, GL_TEXTURE_2D, textureId, 0);
        }
    }

    RSMEffect::~RSMEffect()
    {
        ReleaseCaptureResources();
    }

    std::vector<PostProcessParameter> RSMEffect::GetParameters() const
    {
        return {
            PostProcessParameter{
                .name = "Intensity",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_intensity),
            },
            PostProcessParameter{
                .name = "Capture Scale",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_captureScale),
            },
            PostProcessParameter{
                .name = "Samples",
                .type = PostProcessParameterType::Int,
                .value = std::to_string(m_sampleCount),
            },
            PostProcessParameter{
                .name = "Sample Radius",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_sampleRadius),
            },
            PostProcessParameter{
                .name = "Max Distance",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_maxDistance),
            },
            PostProcessParameter{
                .name = "Normal Bias",
                .type = PostProcessParameterType::Float,
                .value = std::to_string(m_normalBias),
            },
            PostProcessParameter{
                .name = "Debug Output",
                .type = PostProcessParameterType::Enum,
                .value = std::to_string(static_cast<int>(m_debugOutput)),
                .enumOptions = {"Indirect", "Center Flux", "Occupied Samples", "Coverage", "Sample Count"},
            },
        };
    }

    void RSMEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Intensity")
            {
                m_intensity = std::clamp(std::stof(parameter.value), 0.0f, 16.0f);
            }
            else if (parameter.name == "Capture Scale")
            {
                m_captureScale = std::clamp(std::stof(parameter.value), 0.25f, 1.0f);
            }
            else if (parameter.name == "Samples")
            {
                m_sampleCount = std::clamp(std::stoi(parameter.value), 4, kMaxRsmSamples);
            }
            else if (parameter.name == "Sample Radius")
            {
                m_sampleRadius = std::clamp(std::stof(parameter.value), 2.0f, 320.0f);
            }
            else if (parameter.name == "Max Distance")
            {
                m_maxDistance = std::clamp(std::stof(parameter.value), 0.5f, 256.0f);
            }
            else if (parameter.name == "Normal Bias")
            {
                m_normalBias = std::clamp(std::stof(parameter.value), 0.0f, 0.95f);
            }
            else if (parameter.name == "Debug Output")
            {
                const int debugOutput = std::clamp(std::stoi(parameter.value), 0, 4);
                m_debugOutput = static_cast<RsmDebugOutput>(debugOutput);
            }
        }
    }

    void RSMEffect::Initialize()
    {
        ShaderSource captureSource;
        captureSource.vertexSource = R"(
            #version 330 core

            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec3 aNormal;
            layout(location = 2) in vec2 aUV;
            layout(location = 3) in vec4 aTangent;

            uniform mat4 uModel;
            uniform mat4 uLightSpaceMatrix;

            out vec3 FragPos;
            out vec3 FragNormal;
            out vec2 UV;
            out mat3 TBN;

            void main()
            {
                vec4 worldPos = uModel * vec4(aPos, 1.0);
                FragPos = worldPos.xyz;
                mat3 normalMatrix = transpose(inverse(mat3(uModel)));
                vec3 worldNormal = normalize(normalMatrix * aNormal);
                vec3 worldTangent = normalize(normalMatrix * aTangent.xyz);
                worldTangent = normalize(worldTangent - dot(worldTangent, worldNormal) * worldNormal);
                vec3 worldBitangent = cross(worldNormal, worldTangent) * aTangent.w;
                FragNormal = worldNormal;
                UV = aUV;
                TBN = mat3(worldTangent, normalize(worldBitangent), worldNormal);
                gl_Position = uLightSpaceMatrix * worldPos;
            }
        )";
        captureSource.fragmentSource = R"(
            #version 330 core

            layout(location = 0) out vec3 gNormal;
            layout(location = 1) out vec3 gFlux;

            in vec3 FragPos;
            in vec3 FragNormal;
            in vec2 UV;
            in mat3 TBN;

            uniform vec4 uColor;
            uniform sampler2D uAlbedoTexture;
            uniform float uHasAlbedoTexture;
            uniform sampler2D uNormalTexture;
            uniform float uHasNormalTexture;
            uniform float uFlipNormalY;
            uniform vec3 uLightDirection;
            uniform vec3 uLightColor;
            uniform float uLightIntensity;

            void main()
            {
                vec3 albedo = uColor.rgb;
                float alpha = uColor.a;
                if (uHasAlbedoTexture > 0.5)
                {
                    vec4 albedoSample = texture(uAlbedoTexture, UV);
                    albedo *= albedoSample.rgb;
                    alpha *= albedoSample.a;
                }

                if (alpha < 0.1)
                {
                    discard;
                }

                vec3 normal = normalize(FragNormal);
                if (uHasNormalTexture > 0.5)
                {
                    normal = texture(uNormalTexture, UV).rgb;
                    if (uFlipNormalY > 0.5)
                    {
                        normal.g = 1.0 - normal.g;
                    }
                    normal = normalize(normal * 2.0 - 1.0);
                    normal = normalize(TBN * normal);
                }

                float ndotl = max(dot(normal, -normalize(uLightDirection)), 0.0);
                vec3 flux = albedo * uLightColor * (uLightIntensity * ndotl);

                gNormal = normal;
                gFlux = flux;
            }
        )";
        m_captureShader = Shader::Create(captureSource);

        ShaderSource resolveSource;
        resolveSource.vertexSource = R"(
            #version 330 core

            out vec2 UV;

            void main()
            {
                vec2 vertices[3] = vec2[3](
                    vec2(-1.0, -1.0),
                    vec2(3.0, -1.0),
                    vec2(-1.0, 3.0)
                );
                gl_Position = vec4(vertices[gl_VertexID], 0.0, 1.0);
                UV = 0.5 * gl_Position.xy + vec2(0.5);
            }
        )";
        resolveSource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uScenePositionTexture;
            uniform sampler2D uSceneNormalTexture;
            uniform sampler2D uSceneAlbedoTexture;
            uniform sampler2D uRsmNormalTexture;
            uniform sampler2D uRsmFluxTexture;
            uniform sampler2D uRsmDepthTexture;
            uniform mat4 uRsmLightSpaceMatrix;
            uniform mat4 uInverseRsmLightSpaceMatrix;
            uniform float uIntensity;
            uniform float uSampleRadius;
            uniform float uMaxDistance;
            uniform float uNormalBias;
            uniform int uSampleCount;
            uniform float uFrameJitter;
            uniform int uDebugOutput;

            const int MAX_RSM_SAMPLES = 32;
            const float BOUNCE_SCALE = 0.3;
            const float RECEIVER_DEPTH_THRESHOLD = 0.0035;
            const vec2 poissonDisk[MAX_RSM_SAMPLES] = vec2[](
                vec2(-0.613392, 0.617481),
                vec2(0.170019, -0.040254),
                vec2(-0.299417, 0.791925),
                vec2(0.645680, 0.493210),
                vec2(-0.651784, 0.717887),
                vec2(0.421003, 0.027070),
                vec2(-0.817194, -0.271096),
                vec2(-0.705374, -0.668203),
                vec2(0.977050, -0.108615),
                vec2(0.063326, 0.142369),
                vec2(0.203528, 0.214331),
                vec2(-0.667531, 0.326090),
                vec2(-0.098422, -0.295755),
                vec2(-0.885922, 0.215369),
                vec2(0.566637, 0.605213),
                vec2(0.039766, -0.396100),
                vec2(0.751946, 0.453352),
                vec2(0.078707, -0.715323),
                vec2(-0.075838, -0.529344),
                vec2(0.724479, -0.580798),
                vec2(0.222999, -0.215125),
                vec2(-0.467574, -0.405438),
                vec2(-0.248268, -0.814753),
                vec2(0.354411, -0.887570),
                vec2(0.175817, 0.382366),
                vec2(0.487472, -0.063082),
                vec2(-0.084078, 0.898312),
                vec2(0.488876, -0.783441),
                vec2(0.470016, 0.217933),
                vec2(-0.696890, -0.549791),
                vec2(-0.149693, 0.605762),
                vec2(0.034211, 0.979980)
            );

            vec2 RotateOffset(vec2 offset, float angle)
            {
                float s = sin(angle);
                float c = cos(angle);
                return vec2(offset.x * c - offset.y * s, offset.x * s + offset.y * c);
            }

            float StableNoise(vec2 seed)
            {
                return fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
            }

            vec3 ReconstructRsmWorldPosition(vec2 sampleUv, float depth)
            {
                vec4 clipPosition = vec4(sampleUv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
                vec4 worldPosition = uInverseRsmLightSpaceMatrix * clipPosition;
                return worldPosition.xyz / max(worldPosition.w, 0.0001);
            }

            void main()
            {
                ivec2 rsmTextureSize = textureSize(uRsmFluxTexture, 0);
                vec3 worldPos = texture(uScenePositionTexture, UV).rgb;
                vec3 normal = normalize(texture(uSceneNormalTexture, UV).xyz);
                vec4 albedoMetallic = texture(uSceneAlbedoTexture, UV);
                if (dot(normal, normal) < 0.01)
                {
                    FragColor = vec4(0.0);
                    return;
                }

                vec4 lightSpacePosition = uRsmLightSpaceMatrix * vec4(worldPos, 1.0);
                if (abs(lightSpacePosition.w) <= 0.0001)
                {
                    FragColor = vec4(0.0);
                    return;
                }

                vec3 lightSpaceNdc = lightSpacePosition.xyz / lightSpacePosition.w;
                if (lightSpaceNdc.x < -1.0 || lightSpaceNdc.x > 1.0 ||
                    lightSpaceNdc.y < -1.0 || lightSpaceNdc.y > 1.0 ||
                    lightSpaceNdc.z < -1.0 || lightSpaceNdc.z > 1.0)
                {
                    FragColor = vec4(0.0);
                    return;
                }

                vec2 centerUv = lightSpaceNdc.xy * 0.5 + vec2(0.5);
                vec2 texelSize = 1.0 / vec2(rsmTextureSize);
                vec2 stableSeed = floor(UV * vec2(textureSize(uScenePositionTexture, 0)));
                float angle = (StableNoise(stableSeed) + uFrameJitter) * 6.28318530718;
                float maxDistanceSq = uMaxDistance * uMaxDistance;
                float metallic = clamp(albedoMetallic.a, 0.0, 1.0);
                vec3 receiverAlbedo = albedoMetallic.rgb * (1.0 - metallic);
                vec3 centerFlux = texture(uRsmFluxTexture, centerUv).xyz;

                if (uDebugOutput == 1)
                {
                    FragColor = vec4(centerFlux * uIntensity * 0.5, 1.0);
                    return;
                }

                vec3 indirect = vec3(0.0);
                float indirectWeightSum = 0.0;
                int occupiedSamples = 0;
                int validSamples = 0;

                for (int sampleIndex = 0; sampleIndex < MAX_RSM_SAMPLES; ++sampleIndex)
                {
                    if (sampleIndex >= uSampleCount)
                    {
                        break;
                    }

                    vec2 sampleUv = centerUv;
                    float radialScale = 0.0;
                    if (sampleIndex > 0)
                    {
                        float radialJitter = fract(uFrameJitter + float(sampleIndex) * 0.61803398875);
                        radialScale = sqrt((float(sampleIndex) + radialJitter) / float(max(uSampleCount, 1)));
                        vec2 sampleOffset = RotateOffset(poissonDisk[sampleIndex - 1], angle) * radialScale;
                        sampleUv += sampleOffset * uSampleRadius * texelSize;
                    }
                    if (sampleUv.x < 0.0 || sampleUv.x > 1.0 || sampleUv.y < 0.0 || sampleUv.y > 1.0)
                    {
                        continue;
                    }

                    float vplDepth = texture(uRsmDepthTexture, sampleUv).r;
                    if (vplDepth >= 0.99999)
                    {
                        continue;
                    }

                    vec3 vplPos = ReconstructRsmWorldPosition(sampleUv, vplDepth);
                    vec3 vplNormal = normalize(texture(uRsmNormalTexture, sampleUv).xyz);
                    vec3 vplFlux = texture(uRsmFluxTexture, sampleUv).xyz;
                    if (dot(vplNormal, vplNormal) < 0.01 || dot(vplFlux, vplFlux) < 0.000001)
                    {
                        continue;
                    }

                    ++occupiedSamples;

                    vec3 toVpl = vplPos - worldPos;
                    float distSq = dot(toVpl, toVpl);
                    if (distSq > maxDistanceSq)
                    {
                        continue;
                    }

                    vec3 direction = vec3(0.0);
                    if (distSq <= 0.0004)
                    {
                        direction = normalize(normal + vplNormal);
                        if (dot(direction, direction) < 0.0001)
                        {
                            direction = normal;
                        }
                        distSq = 0.25;
                    }
                    else
                    {
                        direction = normalize(toVpl);
                    }

                    float receiverFacing = max(dot(normal, direction), 0.0);
                    float emitterFacing = max(dot(vplNormal, -direction), 0.0);
                    float receiverWeight = mix(0.2, 1.0, sqrt(receiverFacing));
                    float emitterWeight = mix(0.2, 1.0, emitterFacing);
                    emitterWeight *= smoothstep(0.0, 1.0 - uNormalBias * 0.5, emitterFacing);
                    if (receiverWeight <= 0.0 || emitterWeight <= 0.0)
                    {
                        continue;
                    }

                    float attenuation = 1.0 / (1.0 + distSq * 0.05);
                    float radialWeight = sampleIndex == 0 ? 1.0 : max(1.0 - radialScale, 0.25);
                    float sampleWeight = receiverWeight * emitterWeight * radialWeight;
                    indirect += vplFlux * attenuation * sampleWeight;
                    indirectWeightSum += sampleWeight;
                    ++validSamples;
                }

                if (uDebugOutput == 2)
                {
                    float coverage = occupiedSamples > 0 ? 1.0 : 0.0;
                    FragColor = vec4(vec3(coverage), 1.0);
                    return;
                }

                if (uDebugOutput == 3)
                {
                    float coverage = validSamples > 0 ? 1.0 : 0.0;
                    FragColor = vec4(vec3(coverage), 1.0);
                    return;
                }

                if (uDebugOutput == 4)
                {
                    float normalizedCount = float(validSamples) / float(max(uSampleCount, 1));
                    FragColor = vec4(vec3(normalizedCount), 1.0);
                    return;
                }

                if (indirectWeightSum > 0.0)
                {
                    indirect *= uIntensity * BOUNCE_SCALE / indirectWeightSum;
                }

                indirect *= receiverAlbedo;
                FragColor = vec4(max(indirect, vec3(0.0)), 1.0);
            }
        )";
        m_resolveShader = Shader::Create(resolveSource);

        ShaderSource blurSource;
        blurSource.vertexSource = resolveSource.vertexSource;
        blurSource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uSceneTexture;
            uniform sampler2D uScenePositionTexture;
            uniform sampler2D uSceneNormalTexture;
            uniform vec2 uBlurDirection;

            const int BLUR_RADIUS = 7;

            void main()
            {
                vec3 centerPos = texture(uScenePositionTexture, UV).rgb;
                vec3 centerNormal = normalize(texture(uSceneNormalTexture, UV).xyz);
                vec3 centerColor = texture(uSceneTexture, UV).rgb;

                if (dot(centerNormal, centerNormal) < 0.01)
                {
                    FragColor = vec4(centerColor, 1.0);
                    return;
                }

                vec2 texelSize = 1.0 / vec2(textureSize(uSceneTexture, 0));
                vec3 colorSum = vec3(0.0);
                float weightSum = 0.0;

                for (int offsetIndex = -BLUR_RADIUS; offsetIndex <= BLUR_RADIUS; ++offsetIndex)
                {
                    vec2 offset = uBlurDirection * float(offsetIndex);
                    vec2 sampleUv = UV + offset * texelSize;
                    if (sampleUv.x < 0.0 || sampleUv.x > 1.0 || sampleUv.y < 0.0 || sampleUv.y > 1.0)
                    {
                        continue;
                    }

                    vec3 samplePos = texture(uScenePositionTexture, sampleUv).rgb;
                    vec3 sampleNormal = normalize(texture(uSceneNormalTexture, sampleUv).xyz);
                    vec3 sampleColor = texture(uSceneTexture, sampleUv).rgb;
                    if (dot(sampleNormal, sampleNormal) < 0.01)
                    {
                        continue;
                    }

                    float spatialWeight = exp(-dot(offset, offset) * 0.45);
                    float positionWeight = exp(-length(samplePos - centerPos) * 4.5);
                    float normalWeight = pow(max(dot(centerNormal, sampleNormal), 0.0), 18.0);
                    float weight = spatialWeight * positionWeight * max(normalWeight, 0.0001);

                    colorSum += sampleColor * weight;
                    weightSum += weight;
                }

                FragColor = vec4(colorSum / max(weightSum, 0.0001), 1.0);
            }
        )";
        m_blurShader = Shader::Create(blurSource);

        ShaderSource temporalResolveSource;
        temporalResolveSource.vertexSource = resolveSource.vertexSource;
        temporalResolveSource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uScenePositionTexture;
            uniform sampler2D uSceneNormalTexture;
            uniform sampler2D uSceneMotionTexture;
            uniform sampler2D uCurrentIndirectTexture;
            uniform sampler2D uHistoryColorTexture;
            uniform sampler2D uHistoryMetadataTexture;
            uniform mat4 uView;
            uniform mat4 uPreviousView;
            uniform mat4 uPreviousViewProjection;
            uniform int uHasHistory;
            uniform float uTemporalBlend;
            uniform float uHistoryDepthThreshold;
            uniform float uHistoryNormalThreshold;
            uniform float uNearPlane;
            uniform float uFarPlane;

            vec2 EncodeNormal(vec3 normal)
            {
                normal /= (abs(normal.x) + abs(normal.y) + abs(normal.z));
                if (normal.z < 0.0)
                {
                    normal.xy = (1.0 - abs(normal.yx)) * sign(normal.xy);
                }

                return normal.xy * 0.5 + 0.5;
            }

            vec3 DecodeNormal(vec2 encoded)
            {
                vec2 f = encoded * 2.0 - 1.0;
                vec3 normal = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
                float t = clamp(-normal.z, 0.0, 1.0);
                normal.xy += vec2(normal.x >= 0.0 ? -t : t, normal.y >= 0.0 ? -t : t);
                return normalize(normal);
            }

            float NormalizeViewDepth(float viewDepth)
            {
                return clamp((viewDepth - uNearPlane) / max(uFarPlane - uNearPlane, 0.0001), 0.0, 1.0);
            }

            void main()
            {
                vec3 centerPos = texture(uScenePositionTexture, UV).rgb;
                vec3 centerNormal = normalize(texture(uSceneNormalTexture, UV).xyz);
                vec3 currentIndirect = texture(uCurrentIndirectTexture, UV).rgb;

                if (dot(centerNormal, centerNormal) < 0.01)
                {
                    FragColor = vec4(currentIndirect, 1.0);
                    return;
                }

                vec3 resolvedIndirect = currentIndirect;
                if (uHasHistory != 0)
                {
                    vec2 motionVector = texture(uSceneMotionTexture, UV).xy;
                    vec2 historyUv = UV - motionVector;
                    if (historyUv.x >= 0.0 && historyUv.x <= 1.0 && historyUv.y >= 0.0 && historyUv.y <= 1.0)
                    {
                        vec4 previousClip = uPreviousViewProjection * vec4(centerPos, 1.0);
                        if (previousClip.w > 0.0001)
                        {
                            vec4 historyMetadata = texture(uHistoryMetadataTexture, historyUv);
                            float previousViewDepth = -(uPreviousView * vec4(centerPos, 1.0)).z;
                            float previousViewDepthNorm = NormalizeViewDepth(previousViewDepth);
                            vec3 historyNormal = DecodeNormal(historyMetadata.xy);
                            float normalSimilarity = dot(historyNormal, centerNormal);
                            if (historyMetadata.w > 0.5 &&
                                abs(historyMetadata.z - previousViewDepthNorm) <= uHistoryDepthThreshold &&
                                normalSimilarity >= uHistoryNormalThreshold)
                            {
                                vec3 historyIndirect = texture(uHistoryColorTexture, historyUv).rgb;
                                float motionMagnitude = length(motionVector);
                                float motionWeight = clamp(1.0 - motionMagnitude * 32.0, 0.0, 1.0);
                                float blendWeight = uTemporalBlend * motionWeight;
                                resolvedIndirect = mix(currentIndirect, historyIndirect, blendWeight);
                            }
                        }
                    }
                }

                FragColor = vec4(max(resolvedIndirect, vec3(0.0)), 1.0);
            }
        )";
        m_temporalResolveShader = Shader::Create(temporalResolveSource);

        ShaderSource historyMetadataSource;
        historyMetadataSource.vertexSource = resolveSource.vertexSource;
        historyMetadataSource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uScenePositionTexture;
            uniform sampler2D uSceneNormalTexture;
            uniform mat4 uView;
            uniform float uNearPlane;
            uniform float uFarPlane;

            vec2 EncodeNormal(vec3 normal)
            {
                normal /= (abs(normal.x) + abs(normal.y) + abs(normal.z));
                if (normal.z < 0.0)
                {
                    normal.xy = (1.0 - abs(normal.yx)) * sign(normal.xy);
                }

                return normal.xy * 0.5 + 0.5;
            }

            float NormalizeViewDepth(float viewDepth)
            {
                return clamp((viewDepth - uNearPlane) / max(uFarPlane - uNearPlane, 0.0001), 0.0, 1.0);
            }

            void main()
            {
                vec3 centerPos = texture(uScenePositionTexture, UV).rgb;
                vec3 centerNormal = normalize(texture(uSceneNormalTexture, UV).xyz);
                if (dot(centerNormal, centerNormal) < 0.01)
                {
                    FragColor = vec4(0.0);
                    return;
                }

                float currentViewDepth = -(uView * vec4(centerPos, 1.0)).z;
                vec2 encodedNormal = EncodeNormal(centerNormal);
                FragColor = vec4(encodedNormal, NormalizeViewDepth(currentViewDepth), 1.0);
            }
        )";
        m_historyMetadataShader = Shader::Create(historyMetadataSource);
    }

    void RSMEffect::ResetHistory()
    {
        m_hasHistory = false;
        m_historyIndex = 0;
    }

    void RSMEffect::Apply(const PostProcessContext &context)
    {
        if (!context.sourceRenderTarget || !context.destinationRenderTarget)
        {
            return;
        }

        glBindFramebuffer(GL_READ_FRAMEBUFFER, context.sourceRenderTarget->GetFramebufferID());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, context.destinationRenderTarget->GetFramebufferID());
        glBlitFramebuffer(
            0, 0, context.sourceRenderTarget->GetWidth(), context.sourceRenderTarget->GetHeight(),
            0, 0, context.destinationRenderTarget->GetWidth(), context.destinationRenderTarget->GetHeight(),
            GL_COLOR_BUFFER_BIT,
            GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    RenderTarget *RSMEffect::GenerateResolvedIndirectLighting(const PostProcessContext &context, int width, int height)
    {
        if (!m_captureShader ||
            !m_resolveShader ||
            !m_blurShader ||
            !m_temporalResolveShader ||
            !m_historyMetadataShader ||
            !context.renderContext.gBuffer ||
            !context.renderContext.hasCameraData ||
            !context.renderContext.renderCommands ||
            !context.sourceRenderTarget ||
            width <= 0 ||
            height <= 0)
        {
            return nullptr;
        }

        const DirectionalRsmSource rsmSource = FindDirectionalRsmSource(context.renderContext);
        const int baseWidth = std::max(rsmSource.shadowWidth, 256);
        const int baseHeight = std::max(rsmSource.shadowHeight, 256);
        const int captureWidth = std::max(static_cast<int>(static_cast<float>(baseWidth) * m_captureScale), 128);
        const int captureHeight = std::max(static_cast<int>(static_cast<float>(baseHeight) * m_captureScale), 128);
        EnsureResources(captureWidth, captureHeight, width, height);

        if (!rsmSource.light)
        {
            if (m_debugOutput != RsmDebugOutput::Indirect && m_resolvedIndirectRenderTarget && m_resolvedIndirectRenderTarget->IsInitialized())
            {
                Graphics::BindRenderTarget(m_resolvedIndirectRenderTarget.get());
                glViewport(0, 0, width, height);
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_CULL_FACE);
                glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                return m_resolvedIndirectRenderTarget.get();
            }

            return nullptr;
        }

        RenderTarget *previousHistoryColorTarget = m_historyColorRenderTargets[m_historyIndex].get();
        RenderTarget *resolvedHistoryColorTarget = m_historyColorRenderTargets[(m_historyIndex + 1) % m_historyColorRenderTargets.size()].get();
        RenderTarget *previousHistoryMetadataTarget = m_historyMetadataRenderTargets[m_historyIndex].get();
        RenderTarget *resolvedHistoryMetadataTarget = m_historyMetadataRenderTargets[(m_historyIndex + 1) % m_historyMetadataRenderTargets.size()].get();

        if (m_captureFramebuffer == 0 ||
            !m_rawIndirectRenderTarget || !m_rawIndirectRenderTarget->IsInitialized() ||
            !m_blurIntermediateRenderTarget || !m_blurIntermediateRenderTarget->IsInitialized() ||
            !m_resolvedIndirectRenderTarget || !m_resolvedIndirectRenderTarget->IsInitialized() ||
            !previousHistoryColorTarget || !resolvedHistoryColorTarget ||
            !previousHistoryMetadataTarget || !resolvedHistoryMetadataTarget)
        {
            if (m_debugOutput != RsmDebugOutput::Indirect && m_resolvedIndirectRenderTarget && m_resolvedIndirectRenderTarget->IsInitialized())
            {
                Graphics::BindRenderTarget(m_resolvedIndirectRenderTarget.get());
                glViewport(0, 0, width, height);
                glDisable(GL_DEPTH_TEST);
                glDisable(GL_CULL_FACE);
                glClearColor(1.0f, 1.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                return m_resolvedIndirectRenderTarget.get();
            }

            return nullptr;
        }

        GLint previousViewport[4] = {0, 0, 0, 0};
        glGetIntegerv(GL_VIEWPORT, previousViewport);

        glBindFramebuffer(GL_FRAMEBUFFER, m_captureFramebuffer);
        glViewport(0, 0, m_captureWidth, m_captureHeight);
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        m_captureShader->Bind();
        m_captureShader->SetUniform("uLightSpaceMatrix", rsmSource.lightSpaceMatrix);
        m_captureShader->SetUniform("uLightDirection", rsmSource.light->direction);
        m_captureShader->SetUniform("uLightColor", rsmSource.light->color);
        m_captureShader->SetUniform("uLightIntensity", rsmSource.light->intensity);

        const auto rsmCapturePlanes = ExtractFrustumPlanes(rsmSource.lightSpaceMatrix);
        Material *boundMaterial = nullptr;
        for (const auto &command : *context.renderContext.renderCommands)
        {
            if (!command.mesh || !command.material)
            {
                continue;
            }

            if (!IsBoundsVisible(GetWorldBounds(command), rsmCapturePlanes))
            {
                continue;
            }

            if (command.material != boundMaterial)
            {
                command.material->Bind(m_captureShader);
                boundMaterial = command.material;
            }

            m_captureShader->SetUniform("uModel", command.model);
            command.mesh->DrawSubmesh(command.submeshIndex);
        }

        RenderTarget *initialTarget = m_debugOutput == RsmDebugOutput::Indirect ? m_rawIndirectRenderTarget.get() : m_resolvedIndirectRenderTarget.get();
        Graphics::BindRenderTarget(initialTarget);
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        const PostProcessContext internalContext{
            .renderContext = context.renderContext,
            .sourceRenderTarget = context.sourceRenderTarget,
            .destinationRenderTarget = nullptr,
        };
        m_resolveShader->Bind();
        BindCommonInputs(m_resolveShader, internalContext);

        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_2D, m_normalTexture);
        m_resolveShader->SetUniform("uRsmNormalTexture", 5);

        glActiveTexture(GL_TEXTURE6);
        glBindTexture(GL_TEXTURE_2D, m_fluxTexture);
        m_resolveShader->SetUniform("uRsmFluxTexture", 6);

        glActiveTexture(GL_TEXTURE7);
        glBindTexture(GL_TEXTURE_2D, m_depthTexture);
        m_resolveShader->SetUniform("uRsmDepthTexture", 7);

        m_resolveShader->SetUniform("uRsmLightSpaceMatrix", rsmSource.lightSpaceMatrix);
        m_resolveShader->SetUniform("uInverseRsmLightSpaceMatrix", glm::inverse(rsmSource.lightSpaceMatrix));
        m_resolveShader->SetUniform("uIntensity", m_intensity);
        m_resolveShader->SetUniform("uSampleRadius", m_sampleRadius);
        m_resolveShader->SetUniform("uMaxDistance", m_maxDistance);
        m_resolveShader->SetUniform("uNormalBias", m_normalBias);
        m_resolveShader->SetUniform("uSampleCount", m_sampleCount);
        m_resolveShader->SetUniform("uFrameJitter", static_cast<float>(context.renderContext.frameSequence % 64ull) / 64.0f);
        m_resolveShader->SetUniform("uDebugOutput", static_cast<int>(m_debugOutput));
        DrawFullscreenTriangle();

        if (m_debugOutput == RsmDebugOutput::Indirect)
        {
            const PostProcessContext horizontalBlurContext{
                .renderContext = context.renderContext,
                .sourceRenderTarget = m_rawIndirectRenderTarget.get(),
                .destinationRenderTarget = nullptr,
            };

            Graphics::BindRenderTarget(m_blurIntermediateRenderTarget.get());
            glViewport(0, 0, width, height);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            m_blurShader->Bind();
            BindCommonInputs(m_blurShader, horizontalBlurContext);
            m_blurShader->SetUniform("uBlurDirection", glm::vec2(1.0f, 0.0f));
            DrawFullscreenTriangle();

            const PostProcessContext verticalBlurContext{
                .renderContext = context.renderContext,
                .sourceRenderTarget = m_blurIntermediateRenderTarget.get(),
                .destinationRenderTarget = nullptr,
            };

            Graphics::BindRenderTarget(m_resolvedIndirectRenderTarget.get());
            glViewport(0, 0, width, height);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            m_blurShader->Bind();
            BindCommonInputs(m_blurShader, verticalBlurContext);
            m_blurShader->SetUniform("uBlurDirection", glm::vec2(0.0f, 1.0f));
            DrawFullscreenTriangle();

            const glm::mat4 viewProjection = context.renderContext.cameraData.projection * context.renderContext.cameraData.view;

            const PostProcessContext temporalContext{
                .renderContext = context.renderContext,
                .sourceRenderTarget = m_resolvedIndirectRenderTarget.get(),
                .destinationRenderTarget = nullptr,
            };

            Graphics::BindRenderTarget(resolvedHistoryColorTarget);
            glViewport(0, 0, width, height);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            m_temporalResolveShader->Bind();
            BindCommonInputs(m_temporalResolveShader, temporalContext);
            glActiveTexture(GL_TEXTURE5);
            glBindTexture(GL_TEXTURE_2D, m_resolvedIndirectRenderTarget->GetColorTextureID());
            m_temporalResolveShader->SetUniform("uCurrentIndirectTexture", 5);
            glActiveTexture(GL_TEXTURE6);
            glBindTexture(GL_TEXTURE_2D, previousHistoryColorTarget->GetColorTextureID());
            m_temporalResolveShader->SetUniform("uHistoryColorTexture", 6);
            glActiveTexture(GL_TEXTURE7);
            glBindTexture(GL_TEXTURE_2D, previousHistoryMetadataTarget->GetColorTextureID());
            m_temporalResolveShader->SetUniform("uHistoryMetadataTexture", 7);
            glActiveTexture(GL_TEXTURE8);
            glBindTexture(GL_TEXTURE_2D, context.renderContext.gBuffer->GetMotionTextureID());
            m_temporalResolveShader->SetUniform("uSceneMotionTexture", 8);
            m_temporalResolveShader->SetUniform("uView", context.renderContext.cameraData.view);
            m_temporalResolveShader->SetUniform("uPreviousView", m_previousView);
            m_temporalResolveShader->SetUniform("uPreviousViewProjection", m_previousViewProjection);
            m_temporalResolveShader->SetUniform("uHasHistory", m_hasHistory ? 1 : 0);
            m_temporalResolveShader->SetUniform("uTemporalBlend", m_temporalBlend);
            m_temporalResolveShader->SetUniform("uHistoryDepthThreshold", m_historyDepthThreshold);
            m_temporalResolveShader->SetUniform("uHistoryNormalThreshold", m_historyNormalThreshold);
            m_temporalResolveShader->SetUniform("uNearPlane", context.renderContext.cameraData.nearPlane);
            m_temporalResolveShader->SetUniform("uFarPlane", context.renderContext.cameraData.farPlane);
            DrawFullscreenTriangle();

            Graphics::BindRenderTarget(resolvedHistoryMetadataTarget);
            glViewport(0, 0, width, height);
            glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
            glClear(GL_COLOR_BUFFER_BIT);

            m_historyMetadataShader->Bind();
            BindCommonInputs(m_historyMetadataShader, temporalContext);
            m_historyMetadataShader->SetUniform("uView", context.renderContext.cameraData.view);
            m_historyMetadataShader->SetUniform("uNearPlane", context.renderContext.cameraData.nearPlane);
            m_historyMetadataShader->SetUniform("uFarPlane", context.renderContext.cameraData.farPlane);
            DrawFullscreenTriangle();

            m_historyIndex = static_cast<std::uint8_t>((m_historyIndex + 1) % m_historyColorRenderTargets.size());
            m_previousView = context.renderContext.cameraData.view;
            m_previousViewProjection = viewProjection;
            m_hasHistory = true;
        }
        else
        {
            ResetHistory();
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        if (m_debugOutput == RsmDebugOutput::Indirect)
        {
            return m_historyColorRenderTargets[m_historyIndex].get();
        }

        return m_resolvedIndirectRenderTarget.get();
    }

    void RSMEffect::EnsureResources(int captureWidth, int captureHeight, int resolvedWidth, int resolvedHeight)
    {
        if (!m_rawIndirectRenderTarget)
        {
            m_rawIndirectRenderTarget = std::make_unique<RenderTarget>(RenderTargetConfig{
                .width = resolvedWidth,
                .height = resolvedHeight,
                .clearColor = glm::vec4(0.0f),
            });
        }
        else if (m_rawIndirectRenderTarget->GetWidth() != resolvedWidth || m_rawIndirectRenderTarget->GetHeight() != resolvedHeight)
        {
            m_rawIndirectRenderTarget->Resize(resolvedWidth, resolvedHeight);
        }

        if (!m_blurIntermediateRenderTarget)
        {
            m_blurIntermediateRenderTarget = std::make_unique<RenderTarget>(RenderTargetConfig{
                .width = resolvedWidth,
                .height = resolvedHeight,
                .clearColor = glm::vec4(0.0f),
            });
        }
        else if (m_blurIntermediateRenderTarget->GetWidth() != resolvedWidth || m_blurIntermediateRenderTarget->GetHeight() != resolvedHeight)
        {
            m_blurIntermediateRenderTarget->Resize(resolvedWidth, resolvedHeight);
        }

        if (!m_resolvedIndirectRenderTarget)
        {
            m_resolvedIndirectRenderTarget = std::make_unique<RenderTarget>(RenderTargetConfig{
                .width = resolvedWidth,
                .height = resolvedHeight,
                .clearColor = glm::vec4(0.0f),
            });
        }
        else if (m_resolvedIndirectRenderTarget->GetWidth() != resolvedWidth || m_resolvedIndirectRenderTarget->GetHeight() != resolvedHeight)
        {
            m_resolvedIndirectRenderTarget->Resize(resolvedWidth, resolvedHeight);
        }

        for (auto &historyRenderTarget : m_historyColorRenderTargets)
        {
            if (!historyRenderTarget)
            {
                historyRenderTarget = std::make_unique<RenderTarget>(RenderTargetConfig{
                    .width = resolvedWidth,
                    .height = resolvedHeight,
                    .clearColor = glm::vec4(0.0f),
                });
            }
            else if (historyRenderTarget->GetWidth() != resolvedWidth || historyRenderTarget->GetHeight() != resolvedHeight)
            {
                historyRenderTarget->Resize(resolvedWidth, resolvedHeight);
            }
        }

        for (auto &historyMetadataRenderTarget : m_historyMetadataRenderTargets)
        {
            if (!historyMetadataRenderTarget)
            {
                historyMetadataRenderTarget = std::make_unique<RenderTarget>(RenderTargetConfig{
                    .width = resolvedWidth,
                    .height = resolvedHeight,
                    .clearColor = glm::vec4(0.0f),
                });
            }
            else if (historyMetadataRenderTarget->GetWidth() != resolvedWidth || historyMetadataRenderTarget->GetHeight() != resolvedHeight)
            {
                historyMetadataRenderTarget->Resize(resolvedWidth, resolvedHeight);
            }
        }

        if (m_captureFramebuffer != 0 && captureWidth == m_captureWidth && captureHeight == m_captureHeight)
        {
            return;
        }

        ReleaseCaptureResources();

        glGenFramebuffers(1, &m_captureFramebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, m_captureFramebuffer);

        CreateColorAttachment(m_normalTexture, captureWidth, captureHeight, GL_COLOR_ATTACHMENT0);
        CreateColorAttachment(m_fluxTexture, captureWidth, captureHeight, GL_COLOR_ATTACHMENT1);

        glGenTextures(1, &m_depthTexture);
        glBindTexture(GL_TEXTURE_2D, m_depthTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, captureWidth, captureHeight, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTexture, 0);

        constexpr unsigned int drawBuffers[] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
        glDrawBuffers(static_cast<int>(std::size(drawBuffers)), drawBuffers);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            ReleaseCaptureResources();
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        m_captureWidth = captureWidth;
        m_captureHeight = captureHeight;
    }

    void RSMEffect::ReleaseCaptureResources()
    {
        DeleteTexture(m_normalTexture);
        DeleteTexture(m_fluxTexture);
        DeleteTexture(m_depthTexture);
        if (m_captureFramebuffer != 0)
        {
            glDeleteFramebuffers(1, &m_captureFramebuffer);
            m_captureFramebuffer = 0;
        }
        m_captureWidth = 0;
        m_captureHeight = 0;
    }
}