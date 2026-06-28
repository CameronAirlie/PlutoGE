#include "PlutoGE/render/passes/PhysicalSkyPass.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"

#include <algorithm>
#include <glm/glm.hpp>

namespace PlutoGE::render
{
    namespace
    {
        const scene::PhysicalSkyComponent *FindSky(const scene::Entity *entity)
        {
            if (!entity || !entity->IsActive())
                return nullptr;
            for (const auto *sky : entity->GetComponents<scene::PhysicalSkyComponent>())
            {
                if (sky && sky->IsEnabled())
                    return sky;
            }
            for (const auto *child : entity->GetChildren())
            {
                if (const auto *sky = FindSky(child))
                    return sky;
            }
            return nullptr;
        }

        const scene::Light *FindPrimaryDirectionalLight(const RenderContext &ctx)
        {
            const scene::Light *best = nullptr;
            float bestScore = -1.0f;
            if (!ctx.lights)
                return nullptr;
            for (const auto *light : *ctx.lights)
            {
                if (!light || light->type != scene::LightType::Directional)
                    continue;
                const float luminance = glm::dot(glm::max(light->color, glm::vec3(0.0f)), glm::vec3(0.2126f, 0.7152f, 0.0722f));
                const float score = luminance * std::max(light->intensity, 0.0f);
                if (score > bestScore)
                {
                    bestScore = score;
                    best = light;
                }
            }
            return best;
        }

        Shader *CreatePhysicalSkyShader()
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

                uniform sampler2D uSceneDepth;
                uniform mat4 uInverseView;
                uniform mat4 uInverseProjection;
                uniform vec3 uSunDirection;
                uniform vec3 uSunColor;
                uniform vec3 uMoonColor;
                uniform vec3 uGroundColor;
                uniform float uRayleighStrength;
                uniform float uMieStrength;
                uniform float uMieAnisotropy;
                uniform float uOzoneStrength;
                uniform float uSunIntensity;
                uniform float uSunAngularRadius;
                uniform float uExposure;
                uniform float uNightIntensity;
                uniform float uStarIntensity;
                uniform float uMoonIntensity;
                uniform float uMoonAngularRadius;

                const float PI = 3.14159265359;

                float Hash12(vec2 p)
                {
                    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
                    p3 += dot(p3, p3.yzx + 33.33);
                    return fract((p3.x + p3.y) * p3.z);
                }

                vec2 Hash22(vec2 p)
                {
                    float n = Hash12(p);
                    return vec2(n, Hash12(p + n + 19.19));
                }

                vec3 WorldDirection(vec2 uv)
                {
                    vec4 view = uInverseProjection * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
                    return normalize((uInverseView * vec4(normalize(view.xyz / max(view.w, 0.0001)), 0.0)).xyz);
                }

                float RayleighPhase(float cosine)
                {
                    return 3.0 * (1.0 + cosine * cosine) / (16.0 * PI);
                }

                float MiePhase(float cosine)
                {
                    float g = clamp(uMieAnisotropy, -0.95, 0.95);
                    float denominator = max(1.0 + g * g - 2.0 * g * cosine, 0.0001);
                    return (1.0 - g * g) / (4.0 * PI * pow(denominator, 1.5));
                }

                float StarField(vec3 direction)
                {
                    vec2 sphericalUv = vec2(
                        atan(direction.z, direction.x) / (2.0 * PI) + 0.5,
                        asin(clamp(direction.y, -1.0, 1.0)) / PI + 0.5);
                    vec2 grid = sphericalUv * vec2(900.0, 450.0);
                    vec2 cell = floor(grid);
                    vec2 center = Hash22(cell);
                    float distanceToStar = length(fract(grid) - center);
                    float magnitude = smoothstep(0.996, 1.0, Hash12(cell + 71.7));
                    float point = 1.0 - smoothstep(0.015, 0.065, distanceToStar);
                    return point * magnitude * (0.45 + 0.55 * Hash12(cell + 13.1));
                }

                vec3 Atmosphere(vec3 direction)
                {
                    vec3 sunDirection = normalize(uSunDirection);
                    float sunHeight = sunDirection.y;
                    float viewHeight = max(direction.y, 0.001);
                    float viewAirMass = 1.0 / max(viewHeight + 0.075, 0.075);
                    float sunAirMass = 1.0 / max(sunHeight + 0.075, 0.04);
                    float day = smoothstep(-0.09, 0.035, sunHeight);
                    float night = 1.0 - smoothstep(-0.12, 0.035, sunHeight);
                    float twilight = smoothstep(-0.28, -0.02, sunHeight) * (1.0 - smoothstep(0.02, 0.22, sunHeight));

                    vec3 betaRayleigh = vec3(0.028, 0.067, 0.155) * uRayleighStrength;
                    vec3 betaMie = vec3(0.035) * uMieStrength;
                    vec3 betaOzone = vec3(0.004, 0.012, 0.002) * uOzoneStrength;
                    vec3 betaExtinction = betaRayleigh + betaMie + betaOzone;
                    vec3 viewTransmittance = exp(-betaExtinction * viewAirMass);
                    vec3 sunTransmittance = exp(-betaExtinction * sunAirMass);
                    float cosine = dot(direction, sunDirection);
                    vec3 phaseScattering = betaRayleigh * RayleighPhase(cosine) + betaMie * MiePhase(cosine);
                    vec3 scatteringIntegral = phaseScattering * (vec3(1.0) - viewTransmittance) / max(betaExtinction, vec3(0.0001));
                    vec3 sky = scatteringIntegral * sunTransmittance * uSunColor * uSunIntensity * day;

                    float horizon = pow(1.0 - clamp(direction.y, 0.0, 1.0), 5.0);
                    float sunsetAlignment = pow(max(cosine, 0.0), 12.0);
                    sky += vec3(1.0, 0.12, 0.018) * twilight * horizon * (0.18 + sunsetAlignment * 1.6) * uSunIntensity;

                    vec3 nightGradient = mix(vec3(0.002, 0.003, 0.009), vec3(0.012, 0.022, 0.06), pow(clamp(direction.y, 0.0, 1.0), 0.35));
                    sky += nightGradient * uNightIntensity * 24.0 * night;
                    float stars = StarField(direction) * smoothstep(-0.02, 0.16, direction.y) * night;
                    sky += vec3(0.72, 0.82, 1.0) * stars * uStarIntensity;

                    float sunRadius = radians(max(uSunAngularRadius, 0.01));
                    float sunDisc = smoothstep(cos(sunRadius * 1.35), cos(sunRadius), cosine) * smoothstep(-0.08, 0.02, sunHeight);
                    float sunHalo = pow(max(cosine, 0.0), 512.0) * day;
                    sky += uSunColor * uSunIntensity * (sunDisc + sunHalo * 0.08) * sunTransmittance;

                    vec3 moonDirection = -sunDirection;
                    float moonCosine = dot(direction, moonDirection);
                    float moonHeight = moonDirection.y;
                    float moonRadius = radians(max(uMoonAngularRadius, 0.01));
                    float moonDisc = smoothstep(cos(moonRadius * 1.25), cos(moonRadius), moonCosine);
                    float moonHalo = pow(max(moonCosine, 0.0), 256.0);
                    float moonVisibility = night * smoothstep(-0.04, 0.04, moonHeight);
                    sky += uMoonColor * uMoonIntensity * moonVisibility * (moonDisc + moonHalo * 0.08);

                    float groundBlend = smoothstep(-0.08, 0.015, direction.y);
                    return mix(uGroundColor * mix(0.35, 1.0, day), sky, groundBlend) * uExposure;
                }

                void main()
                {
                    if (texture(uSceneDepth, vUv).r < 0.999999)
                        discard;
                    FragColor = vec4(max(Atmosphere(WorldDirection(vUv)), vec3(0.0)), 1.0);
                }
            )";
            return Shader::Create(source);
        }
    }

    void PhysicalSkyPass::Initialize()
    {
        m_shader = CreatePhysicalSkyShader();
        glGenVertexArrays(1, &m_vao);
    }

    void PhysicalSkyPass::Execute(const RenderContext &ctx)
    {
        if (!m_shader || !m_vao || !ctx.scene || !ctx.temporaryRenderTarget || !ctx.hasCameraData)
            return;

        const scene::PhysicalSkyComponent *sky = nullptr;
        for (const auto *root : ctx.scene->GetRootEntities())
        {
            sky = FindSky(root);
            if (sky) break;
        }
        if (!sky)
            return;

        const scene::Light *sun = FindPrimaryDirectionalLight(ctx);
        glm::vec3 sunDirection = sun ? -sun->direction : glm::vec3(0.25f, 0.8f, 0.4f);
        if (glm::dot(sunDirection, sunDirection) <= 0.000001f)
            sunDirection = glm::vec3(0.0f, 1.0f, 0.0f);
        else
            sunDirection = glm::normalize(sunDirection);

        Graphics::BindRenderTarget(ctx.temporaryRenderTarget);
        glViewport(0, 0, ctx.temporaryRenderTarget->GetWidth(), ctx.temporaryRenderTarget->GetHeight());
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        m_shader->Bind();
        m_shader->SetUniform("uInverseView", glm::inverse(ctx.cameraData.view));
        m_shader->SetUniform("uInverseProjection", glm::inverse(ctx.cameraData.projection));
        m_shader->SetUniform("uSunDirection", sunDirection);
        m_shader->SetUniform("uSunColor", sky->GetSunColor());
        m_shader->SetUniform("uMoonColor", sky->GetMoonColor());
        m_shader->SetUniform("uGroundColor", sky->GetGroundColor());
        m_shader->SetUniform("uRayleighStrength", sky->GetRayleighStrength());
        m_shader->SetUniform("uMieStrength", sky->GetMieStrength());
        m_shader->SetUniform("uMieAnisotropy", sky->GetMieAnisotropy());
        m_shader->SetUniform("uOzoneStrength", sky->GetOzoneStrength());
        m_shader->SetUniform("uSunIntensity", sky->GetSunIntensity());
        m_shader->SetUniform("uSunAngularRadius", sky->GetSunAngularRadius());
        m_shader->SetUniform("uExposure", sky->GetExposure());
        m_shader->SetUniform("uNightIntensity", sky->GetNightIntensity());
        m_shader->SetUniform("uStarIntensity", sky->GetStarIntensity());
        m_shader->SetUniform("uMoonIntensity", sky->GetMoonIntensity());
        m_shader->SetUniform("uMoonAngularRadius", sky->GetMoonAngularRadius());
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.temporaryRenderTarget->GetDepthTextureID());
        m_shader->SetUniform("uSceneDepth", 0);
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        m_shader->Unbind();
        glDepthMask(GL_TRUE);
        Graphics::UnbindRenderTarget();
    }
}
