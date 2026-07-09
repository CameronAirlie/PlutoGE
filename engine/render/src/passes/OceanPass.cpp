#include "PlutoGE/render/passes/OceanPass.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/OceanComponent.h"

#include <algorithm>
#include <glm/glm.hpp>
#include <vector>

namespace PlutoGE::render
{
    namespace
    {
        constexpr int kMaxOceanAreas = 8;
        constexpr int kMaxOceanAreaPoints = 32;

        struct OceanDraw
        {
            const scene::OceanComponent *component = nullptr;
            glm::mat4 transform{1.0f};
            glm::mat4 inverseTransform{1.0f};
        };

        const scene::Light *FindPrimaryDirectionalLight(const RenderContext &ctx)
        {
            if (!ctx.lights)
            {
                return nullptr;
            }

            const scene::Light *bestLight = nullptr;
            float bestScore = -1.0f;
            for (const auto *light : *ctx.lights)
            {
                if (!light || light->type != scene::LightType::Directional)
                {
                    continue;
                }

                const float luminance = glm::dot(glm::max(light->color, glm::vec3(0.0f)), glm::vec3(0.2126f, 0.7152f, 0.0722f));
                const float score = luminance * std::max(light->intensity, 0.0f);
                if (score > bestScore)
                {
                    bestScore = score;
                    bestLight = light;
                }
            }

            return bestLight;
        }

        void CollectOceans(const scene::Entity *entity, std::vector<OceanDraw> &oceans)
        {
            if (!entity || !entity->IsActive())
            {
                return;
            }

            for (const auto *ocean : entity->GetComponents<scene::OceanComponent>())
            {
                if (!ocean || !ocean->IsEnabled())
                {
                    continue;
                }

                const glm::mat4 transform = entity->GetWorldTransform();
                oceans.push_back(OceanDraw{
                    .component = ocean,
                    .transform = transform,
                    .inverseTransform = glm::inverse(transform),
                });
            }

            for (const auto *child : entity->GetChildren())
            {
                CollectOceans(child, oceans);
            }
        }

        Shader *CreateOceanShader()
        {
            ShaderSource source;
            source.vertexSource = R"(
                #version 330 core
                out vec2 vUv;
                void main()
                {
                    vec2 p[3] = vec2[3](vec2(-1.0,-1.0), vec2(3.0,-1.0), vec2(-1.0,3.0));
                    gl_Position = vec4(p[gl_VertexID], 0.0, 1.0);
                    vUv = gl_Position.xy * 0.5 + 0.5;
                }
            )";
            source.fragmentSource = R"(
                #version 330 core
                in vec2 vUv;
                out vec4 FragColor;

                uniform sampler2D uSceneColor;
                uniform sampler2D uSceneDepth;
                uniform mat4 uInverseView;
                uniform mat4 uInverseProjection;
                uniform vec3 uCameraPosition;
                uniform mat4 uOceanTransform;
                uniform mat4 uInverseOceanTransform;
                uniform vec3 uShallowColor;
                uniform vec3 uDeepColor;
                uniform vec3 uFoamColor;
                uniform float uOpacity;
                uniform float uSmoothness;
                uniform float uMaxVisibilityDepth;
                uniform float uRefractionStrength;
                uniform float uWaveAmplitude;
                uniform float uWaveLength;
                uniform float uWaveSpeed;
                uniform float uWaveChoppiness;
                uniform float uFoamDistance;
                uniform float uFoamIntensity;
                uniform float uSimulationTime;
                uniform int uInvertAreaMask;
                uniform int uAreaCount;
                uniform int uAreaPointCounts[8];
                uniform vec2 uAreaPoints[256];
                uniform vec3 uSunDirection;
                uniform vec3 uSunColor;
                uniform float uSunIntensity;

                const float PI = 3.14159265359;

                vec3 ReconstructWorldPosition(vec2 uv, float depth)
                {
                    vec4 clip = vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
                    vec4 view = uInverseProjection * clip;
                    view /= max(abs(view.w), 0.000001);
                    return (uInverseView * view).xyz;
                }

                vec3 WorldDirection(vec2 uv)
                {
                    vec4 view = uInverseProjection * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
                    return normalize((uInverseView * vec4(normalize(view.xyz / max(view.w, 0.0001)), 0.0)).xyz);
                }

                float Hash12(vec2 p)
                {
                    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
                    p3 += dot(p3, p3.yzx + 33.33);
                    return fract((p3.x + p3.y) * p3.z);
                }

                float ValueNoise(vec2 p)
                {
                    vec2 i = floor(p);
                    vec2 f = fract(p);
                    vec2 u = f * f * (3.0 - 2.0 * f);
                    float a = Hash12(i);
                    float b = Hash12(i + vec2(1.0, 0.0));
                    float c = Hash12(i + vec2(0.0, 1.0));
                    float d = Hash12(i + vec2(1.0, 1.0));
                    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y) * 2.0 - 1.0;
                }

                float Fbm(vec2 p)
                {
                    float value = 0.0;
                    float amplitude = 0.5;
                    float frequency = 1.0;
                    for (int octave = 0; octave < 4; ++octave)
                    {
                        value += ValueNoise(p * frequency) * amplitude;
                        frequency *= 2.03;
                        amplitude *= 0.5;
                    }
                    return value;
                }

                float DirectionalWave(vec2 direction, vec2 position, float frequency, float phase, float sharpness)
                {
                    float wave = sin(dot(position, normalize(direction)) * frequency + phase);
                    return sign(wave) * pow(abs(wave), sharpness);
                }

                bool PointInPolygon(vec2 point, int areaIndex)
                {
                    int pointCount = uAreaPointCounts[areaIndex];
                    if (pointCount < 3)
                    {
                        return false;
                    }

                    bool inside = false;
                    int baseIndex = areaIndex * 32;
                    vec2 previous = uAreaPoints[baseIndex + pointCount - 1];
                    for (int pointIndex = 0; pointIndex < pointCount; ++pointIndex)
                    {
                        vec2 current = uAreaPoints[baseIndex + pointIndex];
                        bool intersects = ((current.y > point.y) != (previous.y > point.y)) &&
                                          (point.x < (previous.x - current.x) * (point.y - current.y) / max(previous.y - current.y, 0.000001) + current.x);
                        if (intersects)
                        {
                            inside = !inside;
                        }
                        previous = current;
                    }

                    return inside;
                }

                bool MaskReject(vec2 point)
                {
                    bool insideAny = false;
                    for (int areaIndex = 0; areaIndex < uAreaCount; ++areaIndex)
                    {
                        insideAny = insideAny || PointInPolygon(point, areaIndex);
                    }

                    return uInvertAreaMask != 0 ? !insideAny : insideAny;
                }

                float WaveHeight(vec2 xz)
                {
                    float baseWaveNumber = (2.0 * PI) / max(uWaveLength, 0.01);
                    float phase = uSimulationTime * uWaveSpeed;
                    float sharpness = mix(1.0, 2.6, clamp(uWaveChoppiness * 0.35, 0.0, 1.0));

                    float largeSwell = DirectionalWave(vec2(0.92, 0.38), xz, baseWaveNumber * 0.42, phase * 0.55, sharpness) * 0.48;
                    float crossingSwell = DirectionalWave(vec2(-0.35, 0.94), xz, baseWaveNumber * 0.63, phase * 0.76 + 1.4, sharpness + 0.25) * 0.28;
                    float mediumWave = DirectionalWave(vec2(0.74, -0.67), xz, baseWaveNumber * 1.35, phase * 1.28 - 0.8, sharpness + 0.6) * 0.16;

                    vec2 swellUv = xz / max(uWaveLength, 0.01);
                    vec2 warp = vec2(
                        Fbm(swellUv * 0.85 + vec2(phase * 0.05, -phase * 0.03)),
                        Fbm(swellUv * 1.1 + vec2(-phase * 0.04, phase * 0.06))) * 1.7;
                    float lowNoise = Fbm(swellUv * 1.35 + warp + vec2(phase * 0.07, phase * 0.05)) * 0.14;
                    float detailNoise = Fbm(swellUv * 5.4 + warp * 1.8 + vec2(phase * 0.22, -phase * 0.19)) * 0.06;
                    float rippleNoise = Fbm(swellUv * 12.0 + warp * 2.7 + vec2(-phase * 0.48, phase * 0.43)) * 0.025;

                    float stackedWaves = largeSwell + crossingSwell + mediumWave + lowNoise + detailNoise + rippleNoise;
                    return stackedWaves * uWaveAmplitude;
                }

                vec3 WaveNormal(vec2 xz)
                {
                    float offset = max(uWaveLength * 0.01, 0.025);
                    float center = WaveHeight(xz);
                    float hx = WaveHeight(xz + vec2(offset, 0.0)) - center;
                    float hz = WaveHeight(xz + vec2(0.0, offset)) - center;
                    return normalize(vec3(-hx / offset, 1.0, -hz / offset));
                }

                void main()
                {
                    vec3 viewDirection = WorldDirection(vUv);
                    vec3 rayOriginLocal = (uInverseOceanTransform * vec4(uCameraPosition, 1.0)).xyz;
                    vec3 rayDirectionLocal = normalize((uInverseOceanTransform * vec4(viewDirection, 0.0)).xyz);
                    if (abs(rayDirectionLocal.y) < 0.00001)
                    {
                        discard;
                    }

                    float t = -rayOriginLocal.y / rayDirectionLocal.y;
                    if (t <= 0.0)
                    {
                        discard;
                    }

                    vec3 localHit = rayOriginLocal + rayDirectionLocal * t;
                    if (MaskReject(localHit.xz))
                    {
                        discard;
                    }

                    float waveOffset = WaveHeight(localHit.xz);
                    localHit.y = waveOffset;
                    vec3 worldHit = (uOceanTransform * vec4(localHit, 1.0)).xyz;
                    float waterDistance = length(worldHit - uCameraPosition);

                    float sceneDepth = texture(uSceneDepth, vUv).r;
                    vec3 sceneColor = texture(uSceneColor, vUv).rgb;
                    float waterDepth = uMaxVisibilityDepth;
                    if (sceneDepth < 0.999999)
                    {
                        vec3 sceneWorld = ReconstructWorldPosition(vUv, sceneDepth);
                        float sceneDistance = length(sceneWorld - uCameraPosition);
                        if (waterDistance >= sceneDistance - 0.001)
                        {
                            discard;
                        }

                        vec3 sceneLocal = (uInverseOceanTransform * vec4(sceneWorld, 1.0)).xyz;
                        if (sceneLocal.y >= localHit.y)
                        {
                            discard;
                        }
                        waterDepth = clamp(localHit.y - sceneLocal.y, 0.0, uMaxVisibilityDepth);
                    }

                    vec3 normalLocal = WaveNormal(localHit.xz);
                    vec3 normal = normalize(mat3(transpose(uInverseOceanTransform)) * normalLocal);
                    vec2 distortion = normal.xz * uRefractionStrength;
                    vec3 refractedColor = texture(uSceneColor, clamp(vUv + distortion, vec2(0.0), vec2(1.0))).rgb;

                    float depthFactor = clamp(waterDepth / max(uMaxVisibilityDepth, 0.0001), 0.0, 1.0);
                    vec3 waterTint = mix(uShallowColor, uDeepColor, depthFactor);
                    vec3 viewVector = normalize(uCameraPosition - worldHit);
                    float fresnel = pow(1.0 - clamp(dot(viewVector, normal), 0.0, 1.0), 5.0);
                    vec3 reflectionColor = mix(sceneColor, vec3(0.42, 0.58, 0.72), 0.55);
                    vec3 halfVector = normalize(viewVector + normalize(uSunDirection));
                    float sunSpecular = pow(max(dot(normal, halfVector), 0.0), mix(16.0, 256.0, clamp(uSmoothness, 0.0, 1.0))) * max(uSunIntensity, 0.0);
                    vec3 sunGlint = uSunColor * sunSpecular * 0.015;
                    float foam = (1.0 - smoothstep(0.0, max(uFoamDistance, 0.0001), waterDepth)) * uFoamIntensity;
                    foam *= 0.65 + 0.35 * Hash12(localHit.xz * 0.3 + uSimulationTime);

                    vec3 composed = mix(refractedColor, waterTint, 0.58);
                    composed = mix(composed, reflectionColor + sunGlint, fresnel * clamp(uSmoothness, 0.0, 1.0));
                    composed = mix(composed, uFoamColor, clamp(foam, 0.0, 1.0));
                    FragColor = vec4(composed, clamp(uOpacity, 0.0, 1.0));
                }
            )";
            return Shader::Create(source);
        }

        void CopySceneColor(RenderTarget &source, RenderTarget &destination)
        {
            destination.Resize(source.GetWidth(), source.GetHeight());
            glBindFramebuffer(GL_READ_FRAMEBUFFER, source.GetFramebufferID());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.GetFramebufferID());
            glBlitFramebuffer(
                0, 0, source.GetWidth(), source.GetHeight(),
                0, 0, destination.GetWidth(), destination.GetHeight(),
                GL_COLOR_BUFFER_BIT,
                GL_LINEAR);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }

    void OceanPass::Initialize()
    {
        m_shader = CreateOceanShader();
    }

    void OceanPass::Execute(const RenderContext &ctx)
    {
        if (!m_shader || !ctx.scene || !ctx.temporaryRenderTarget || !ctx.gBuffer || !ctx.hasCameraData)
        {
            return;
        }

        std::vector<OceanDraw> oceans;
        for (const auto *root : ctx.scene->GetRootEntities())
        {
            CollectOceans(root, oceans);
        }

        if (oceans.empty())
        {
            return;
        }

        if (!m_sceneColorCopy)
        {
            m_sceneColorCopy = std::make_unique<RenderTarget>(RenderTargetConfig{
                .width = ctx.temporaryRenderTarget->GetWidth(),
                .height = ctx.temporaryRenderTarget->GetHeight(),
                .clearColor = glm::vec4(0.0f),
            });
        }
        CopySceneColor(*ctx.temporaryRenderTarget, *m_sceneColorCopy);

        const scene::Light *sun = FindPrimaryDirectionalLight(ctx);
        glm::vec3 sunDirection = sun ? -sun->direction : glm::vec3(0.25f, 0.8f, 0.35f);
        if (glm::dot(sunDirection, sunDirection) > 0.000001f)
        {
            sunDirection = glm::normalize(sunDirection);
        }
        else
        {
            sunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
        }
        const glm::vec3 sunColor = sun ? glm::max(sun->color, glm::vec3(0.0f)) : glm::vec3(1.0f);
        const float sunVisibility = ctx.renderer ? ctx.renderer->GetPhysicalSkyDirectionalLightVisibility(sun) : 1.0f;
        const float sunIntensity = sun ? std::max(sun->intensity, 0.0f) * sunVisibility : 1.0f;
        const glm::mat4 inverseView = glm::inverse(ctx.cameraData.view);
        const glm::vec3 cameraPosition(inverseView[3]);

        Graphics::BindRenderTarget(ctx.temporaryRenderTarget);
        glViewport(0, 0, ctx.temporaryRenderTarget->GetWidth(), ctx.temporaryRenderTarget->GetHeight());
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        m_shader->Bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_sceneColorCopy->GetColorTextureID());
        m_shader->SetUniform("uSceneColor", 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, ctx.gBuffer->GetDepthTextureID());
        m_shader->SetUniform("uSceneDepth", 1);
        m_shader->SetUniform("uInverseView", inverseView);
        m_shader->SetUniform("uInverseProjection", glm::inverse(ctx.cameraData.projection));
        m_shader->SetUniform("uCameraPosition", cameraPosition);
        m_shader->SetUniform("uSunDirection", sunDirection);
        m_shader->SetUniform("uSunColor", sunColor);
        m_shader->SetUniform("uSunIntensity", sunIntensity);

        for (const OceanDraw &ocean : oceans)
        {
            const auto &areas = ocean.component->GetAreas();
            std::vector<glm::vec2> flattenedPoints(kMaxOceanAreas * kMaxOceanAreaPoints, glm::vec2(0.0f));
            std::vector<int> pointCounts(kMaxOceanAreas, 0);
            const int areaCount = std::min(static_cast<int>(areas.size()), kMaxOceanAreas);
            for (int areaIndex = 0; areaIndex < areaCount; ++areaIndex)
            {
                const int pointCount = std::min(static_cast<int>(areas[static_cast<std::size_t>(areaIndex)].points.size()), kMaxOceanAreaPoints);
                pointCounts[static_cast<std::size_t>(areaIndex)] = pointCount;
                for (int pointIndex = 0; pointIndex < pointCount; ++pointIndex)
                {
                    flattenedPoints[static_cast<std::size_t>(areaIndex * kMaxOceanAreaPoints + pointIndex)] =
                        areas[static_cast<std::size_t>(areaIndex)].points[static_cast<std::size_t>(pointIndex)];
                }
            }

            m_shader->SetUniform("uOceanTransform", ocean.transform);
            m_shader->SetUniform("uInverseOceanTransform", ocean.inverseTransform);
            m_shader->SetUniform("uShallowColor", ocean.component->GetShallowColor());
            m_shader->SetUniform("uDeepColor", ocean.component->GetDeepColor());
            m_shader->SetUniform("uFoamColor", ocean.component->GetFoamColor());
            m_shader->SetUniform("uOpacity", ocean.component->GetOpacity());
            m_shader->SetUniform("uSmoothness", ocean.component->GetSmoothness());
            m_shader->SetUniform("uMaxVisibilityDepth", ocean.component->GetMaxVisibilityDepth());
            m_shader->SetUniform("uRefractionStrength", ocean.component->GetRefractionStrength());
            m_shader->SetUniform("uWaveAmplitude", ocean.component->GetWaveAmplitude());
            m_shader->SetUniform("uWaveLength", ocean.component->GetWaveLength());
            m_shader->SetUniform("uWaveSpeed", ocean.component->GetWaveSpeed());
            m_shader->SetUniform("uWaveChoppiness", ocean.component->GetWaveChoppiness());
            m_shader->SetUniform("uFoamDistance", ocean.component->GetFoamDistance());
            m_shader->SetUniform("uFoamIntensity", ocean.component->GetFoamIntensity());
            m_shader->SetUniform("uSimulationTime", ocean.component->GetSimulationTime());
            m_shader->SetUniform("uInvertAreaMask", ocean.component->GetInvertAreaMask() ? 1 : 0);
            m_shader->SetUniform("uAreaCount", areaCount);
            for (int areaIndex = 0; areaIndex < kMaxOceanAreas; ++areaIndex)
            {
                m_shader->SetUniform("uAreaPointCounts[" + std::to_string(areaIndex) + "]", pointCounts[static_cast<std::size_t>(areaIndex)]);
            }
            for (int pointIndex = 0; pointIndex < kMaxOceanAreas * kMaxOceanAreaPoints; ++pointIndex)
            {
                m_shader->SetUniform("uAreaPoints[" + std::to_string(pointIndex) + "]", flattenedPoints[static_cast<std::size_t>(pointIndex)]);
            }

            Graphics::DrawFullscreenTriangle();
        }

        m_shader->Unbind();
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        Graphics::UnbindRenderTarget();
    }
}