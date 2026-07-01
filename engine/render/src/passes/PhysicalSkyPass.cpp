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
                uniform int uEnvironmentCapture;

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
                    if (uEnvironmentCapture != 0)
                    {
                        float azimuth = (uv.x - 0.5) * 2.0 * PI;
                        float elevation = uv.y * PI;
                        float horizontal = sin(elevation);
                        return normalize(vec3(cos(azimuth) * horizontal, cos(elevation), sin(azimuth) * horizontal));
                    }
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
                    if (uEnvironmentCapture == 0 && texture(uSceneDepth, vUv).r < 0.999999)
                        discard;
                    FragColor = vec4(max(Atmosphere(WorldDirection(vUv)), vec3(0.0)), 1.0);
                }
            )";
            return Shader::Create(source);
        }

        const scene::PhysicalSkyComponent *FindSceneSky(const scene::Scene *scene)
        {
            if (!scene)
                return nullptr;
            for (const auto *root : scene->GetRootEntities())
            {
                if (const auto *sky = FindSky(root))
                    return sky;
            }
            return nullptr;
        }

        glm::vec3 ResolveSunDirection(const RenderContext &ctx)
        {
            const scene::Light *sun = FindPrimaryDirectionalLight(ctx);
            glm::vec3 direction = sun ? -sun->direction : glm::vec3(0.25f, 0.8f, 0.4f);
            return glm::dot(direction, direction) > 0.000001f ? glm::normalize(direction) : glm::vec3(0.0f, 1.0f, 0.0f);
        }

        void SetSkyUniforms(Shader &shader, const scene::PhysicalSkyComponent &sky, const glm::vec3 &sunDirection)
        {
            shader.SetUniform("uSunDirection", sunDirection);
            shader.SetUniform("uSunColor", sky.GetSunColor());
            shader.SetUniform("uMoonColor", sky.GetMoonColor());
            shader.SetUniform("uGroundColor", sky.GetGroundColor());
            shader.SetUniform("uRayleighStrength", sky.GetRayleighStrength());
            shader.SetUniform("uMieStrength", sky.GetMieStrength());
            shader.SetUniform("uMieAnisotropy", sky.GetMieAnisotropy());
            shader.SetUniform("uOzoneStrength", sky.GetOzoneStrength());
            shader.SetUniform("uSunIntensity", sky.GetSunIntensity());
            shader.SetUniform("uSunAngularRadius", sky.GetSunAngularRadius());
            shader.SetUniform("uExposure", sky.GetExposure());
            shader.SetUniform("uNightIntensity", sky.GetNightIntensity());
            shader.SetUniform("uStarIntensity", sky.GetStarIntensity());
            shader.SetUniform("uMoonIntensity", sky.GetMoonIntensity());
            shader.SetUniform("uMoonAngularRadius", sky.GetMoonAngularRadius());
        }
    }

    PhysicalSkyPass::~PhysicalSkyPass()
    {
        if (m_environmentFramebuffer)
            glDeleteFramebuffers(1, &m_environmentFramebuffer);
        if (m_environmentTexture)
            glDeleteTextures(1, &m_environmentTexture);
        if (m_vao)
            glDeleteVertexArrays(1, &m_vao);
    }

    void PhysicalSkyPass::Initialize()
    {
        m_shader = CreatePhysicalSkyShader();
        glGenVertexArrays(1, &m_vao);
    }

    bool PhysicalSkyPass::PrepareEnvironment(const RenderContext &ctx)
    {
        const auto *sky = FindSceneSky(ctx.scene);
        if (!m_shader || !m_vao || !sky)
        {
            m_environmentAvailable = false;
            m_environmentSun = nullptr;
            m_environmentSunVisibility = 1.0f;
            return false;
        }

        if (m_environmentAvailable && m_lastEnvironmentFrame == ctx.frameSequence && m_lastEnvironmentScene == ctx.scene)
            return true;

        if (!m_environmentTexture)
        {
            glGenTextures(1, &m_environmentTexture);
            glBindTexture(GL_TEXTURE_2D, m_environmentTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_environmentWidth, m_environmentHeight, 0, GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glGenerateMipmap(GL_TEXTURE_2D);
        }
        if (!m_environmentFramebuffer)
            glGenFramebuffers(1, &m_environmentFramebuffer);

        GLint previousDrawFramebuffer = 0;
        GLint previousReadFramebuffer = 0;
        GLint previousViewport[4]{};
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
        glGetIntegerv(GL_VIEWPORT, previousViewport);

        glBindFramebuffer(GL_FRAMEBUFFER, m_environmentFramebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_environmentTexture, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
            glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
            m_environmentAvailable = false;
            return false;
        }

        m_environmentSun = FindPrimaryDirectionalLight(ctx);
        const glm::vec3 sunDirection = ResolveSunDirection(ctx);
        m_environmentSunVisibility = glm::smoothstep(-0.02f, 0.03f, sunDirection.y);

        glViewport(0, 0, m_environmentWidth, m_environmentHeight);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        m_shader->Bind();
        m_shader->SetUniform("uEnvironmentCapture", 1);
        SetSkyUniforms(*m_shader, *sky, sunDirection);
        Graphics::DrawFullscreenTriangle();
        m_shader->Unbind();

        glBindTexture(GL_TEXTURE_2D, m_environmentTexture);
        glGenerateMipmap(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
        glViewport(previousViewport[0], previousViewport[1], previousViewport[2], previousViewport[3]);
        glDepthMask(GL_TRUE);

        m_lastEnvironmentFrame = ctx.frameSequence;
        m_lastEnvironmentScene = ctx.scene;
        m_environmentAvailable = true;
        return true;
    }

    float PhysicalSkyPass::GetDirectionalLightVisibility(const scene::Light *light) const
    {
        return m_environmentAvailable && light && light == m_environmentSun ? m_environmentSunVisibility : 1.0f;
    }

    void PhysicalSkyPass::Execute(const RenderContext &ctx)
    {
        if (!m_shader || !m_vao || !ctx.scene || !ctx.temporaryRenderTarget || !ctx.hasCameraData)
            return;

        const scene::PhysicalSkyComponent *sky = FindSceneSky(ctx.scene);
        if (!sky)
            return;

        const glm::vec3 sunDirection = ResolveSunDirection(ctx);

        Graphics::BindRenderTarget(ctx.temporaryRenderTarget);
        glViewport(0, 0, ctx.temporaryRenderTarget->GetWidth(), ctx.temporaryRenderTarget->GetHeight());
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        m_shader->Bind();
        m_shader->SetUniform("uEnvironmentCapture", 0);
        m_shader->SetUniform("uInverseView", glm::inverse(ctx.cameraData.view));
        m_shader->SetUniform("uInverseProjection", glm::inverse(ctx.cameraData.projection));
        SetSkyUniforms(*m_shader, *sky, sunDirection);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.temporaryRenderTarget->GetDepthTextureID());
        m_shader->SetUniform("uSceneDepth", 0);
        Graphics::DrawFullscreenTriangle();
        m_shader->Unbind();
        glDepthMask(GL_TRUE);
        Graphics::UnbindRenderTarget();
    }
}
