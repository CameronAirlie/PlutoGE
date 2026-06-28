#include "PlutoGE/render/passes/VolumetricCloudPass.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/postprocess/IPostProcessEffect.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/VolumetricCloudComponent.h"

#include <algorithm>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace PlutoGE::render
{
    namespace
    {
        struct CloudDraw
        {
            const scene::VolumetricCloudComponent *component = nullptr;
            float distanceSquared = 0.0f;
        };

        void CollectClouds(const scene::Entity *entity, const glm::vec3 &cameraPosition, std::vector<CloudDraw> &clouds)
        {
            if (!entity || !entity->IsActive())
                return;

            for (const auto *cloud : entity->GetComponents<scene::VolumetricCloudComponent>())
            {
                if (cloud && cloud->IsEnabled() && cloud->GetDensity() > 0.0f && cloud->GetCoverage() > 0.0f)
                {
                    const glm::vec3 offset = entity->GetWorldPosition() - cameraPosition;
                    clouds.push_back({cloud, glm::dot(offset, offset)});
                }
            }
            for (const auto *child : entity->GetChildren())
                CollectClouds(child, cameraPosition, clouds);
        }

        const scene::Light *FindSun(const RenderContext &ctx)
        {
            const scene::Light *sun = nullptr;
            float best = -1.0f;
            if (!ctx.lights)
                return nullptr;
            for (const auto *light : *ctx.lights)
            {
                if (!light || light->type != scene::LightType::Directional)
                    continue;
                const float luminance = glm::dot(glm::max(light->color, glm::vec3(0.0f)), glm::vec3(0.2126f, 0.7152f, 0.0722f));
                const float score = luminance * std::max(light->intensity, 0.0f);
                if (score > best)
                {
                    best = score;
                    sun = light;
                }
            }
            return sun;
        }

        Shader *CreateCloudShader()
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
                uniform mat4 uInverseVolumeTransform;
                uniform vec3 uCameraPosition;
                uniform vec3 uWindOffset;
                uniform vec3 uCloudColor;
                uniform vec3 uLightDirection;
                uniform vec3 uLightColor;
                uniform float uLightIntensity;
                uniform float uCoverage;
                uniform float uDensity;
                uniform float uExtinction;
                uniform float uScatteringAlbedo;
                uniform float uAnisotropy;
                uniform float uAmbientLight;
                uniform float uBaseNoiseScale;
                uniform float uDetailNoiseScale;
                uniform float uDetailErosion;
                uniform float uFarPlane;
                uniform int uPrimarySteps;
                uniform int uLightSteps;
                uniform float uFrameIndex;

                float Hash(vec3 p)
                {
                    p = fract(p * 0.1031);
                    p += dot(p, p.yzx + 33.33);
                    return fract((p.x + p.y) * p.z);
                }

                // Interleaved gradient noise avoids the long diagonal correlations
                // produced by feeding integer pixel coordinates into the 3D hash.
                float PixelJitter(vec2 pixel, float frame)
                {
                    float noise = fract(52.9829189 * fract(dot(pixel, vec2(0.06711056, 0.00583715))));
                    return fract(noise + frame * 0.61803398875);
                }

                float ValueNoise(vec3 p)
                {
                    vec3 i = floor(p), f = fract(p);
                    f = f * f * (3.0 - 2.0 * f);
                    float n000 = Hash(i + vec3(0,0,0));
                    float n100 = Hash(i + vec3(1,0,0));
                    float n010 = Hash(i + vec3(0,1,0));
                    float n110 = Hash(i + vec3(1,1,0));
                    float n001 = Hash(i + vec3(0,0,1));
                    float n101 = Hash(i + vec3(1,0,1));
                    float n011 = Hash(i + vec3(0,1,1));
                    float n111 = Hash(i + vec3(1,1,1));
                    return mix(mix(mix(n000,n100,f.x), mix(n010,n110,f.x), f.y),
                               mix(mix(n001,n101,f.x), mix(n011,n111,f.x), f.y), f.z);
                }

                float BaseFbm(vec3 p)
                {
                    float sum = 0.0, amplitude = 0.55;
                    for (int octave = 0; octave < 3; ++octave)
                    {
                        sum += ValueNoise(p) * amplitude;
                        p = p * 2.03 + vec3(17.1, 9.2, 13.7);
                        amplitude *= 0.5;
                    }
                    return sum;
                }

                float CheapFbm(vec3 p)
                {
                    return ValueNoise(p) * 0.67 + ValueNoise(p * 2.03 + vec3(17.1, 9.2, 13.7)) * 0.33;
                }

                float DetailFbm(vec3 p)
                {
                    return ValueNoise(p) * 0.67 + ValueNoise(p * 2.07 + vec3(8.3, 21.7, 5.9)) * 0.33;
                }

                float DensityShape(vec3 local)
                {
                    float height = local.y + 0.5;
                    float vertical = smoothstep(0.0, 0.14, height) * (1.0 - smoothstep(0.62, 1.0, height));
                    vec2 edgeDistance = vec2(0.5) - abs(local.xz);
                    return vertical * smoothstep(0.0, 0.07, min(edgeDistance.x, edgeDistance.y));
                }

                float SampleDensity(vec3 worldPosition)
                {
                    vec3 local = (uInverseVolumeTransform * vec4(worldPosition, 1.0)).xyz;
                    if (any(greaterThan(abs(local), vec3(0.5))))
                        return 0.0;

                    vec3 advected = worldPosition + uWindOffset;
                    float base = BaseFbm(advected * uBaseNoiseScale);
                    float threshold = 1.0 - uCoverage;
                    float shaped = clamp((base - threshold) / max(uCoverage, 0.001), 0.0, 1.0);
                    if (shaped > 0.001 && uDetailErosion > 0.001)
                    {
                        float detail = DetailFbm(advected * uDetailNoiseScale + vec3(31.4, 7.8, 19.2));
                        shaped = clamp(shaped - (1.0 - detail) * uDetailErosion * (1.0 - shaped), 0.0, 1.0);
                    }
                    return shaped * DensityShape(local) * uDensity;
                }

                float SampleDensityCheap(vec3 worldPosition)
                {
                    vec3 local = (uInverseVolumeTransform * vec4(worldPosition, 1.0)).xyz;
                    if (any(greaterThan(abs(local), vec3(0.5))))
                        return 0.0;
                    float base = CheapFbm((worldPosition + uWindOffset) * uBaseNoiseScale);
                    float shaped = clamp((base - (1.0 - uCoverage)) / max(uCoverage, 0.001), 0.0, 1.0);
                    return shaped * DensityShape(local) * uDensity;
                }

                vec2 IntersectVolume(vec3 rayOrigin, vec3 rayDirection)
                {
                    vec3 o = (uInverseVolumeTransform * vec4(rayOrigin, 1.0)).xyz;
                    vec3 d = (uInverseVolumeTransform * vec4(rayDirection, 0.0)).xyz;
                    vec3 safeD = vec3(
                        d.x < 0.0 ? min(d.x, -0.000001) : max(d.x, 0.000001),
                        d.y < 0.0 ? min(d.y, -0.000001) : max(d.y, 0.000001),
                        d.z < 0.0 ? min(d.z, -0.000001) : max(d.z, 0.000001));
                    vec3 t0 = (-vec3(0.5) - o) / safeD;
                    vec3 t1 = ( vec3(0.5) - o) / safeD;
                    vec3 lo = min(t0, t1), hi = max(t0, t1);
                    return vec2(max(max(lo.x, lo.y), lo.z), min(min(hi.x, hi.y), hi.z));
                }

                float Phase(float cosine)
                {
                    float g = clamp(uAnisotropy, -0.9, 0.9);
                    float denominator = max(1.0 + g*g - 2.0*g*cosine, 0.0001);
                    return (1.0 - g*g) / pow(denominator, 1.5);
                }

                float LightTransmittance(vec3 position, float jitter)
                {
                    vec2 lightHit = IntersectVolume(position + uLightDirection * 0.01, uLightDirection);
                    float stepLength = max(lightHit.y, 0.0) / float(max(uLightSteps, 1));
                    float opticalDepth = 0.0;
                    position += uLightDirection * stepLength * mix(0.25, 0.75, jitter);
                    for (int i = 0; i < 16; ++i)
                    {
                        if (i >= uLightSteps) break;
                        opticalDepth += SampleDensityCheap(position) * uExtinction * stepLength;
                        position += uLightDirection * stepLength;
                    }
                    return exp(-opticalDepth);
                }

                vec3 WorldRay(vec2 uv)
                {
                    vec4 view = uInverseProjection * vec4(uv * 2.0 - 1.0, 1.0, 1.0);
                    return normalize((uInverseView * vec4(normalize(view.xyz / max(view.w, 0.0001)), 0.0)).xyz);
                }

                float OpaqueDistance(vec2 uv, vec3 rayDirection)
                {
                    float depth = texture(uSceneDepth, uv).r;
                    if (depth >= 0.999999) return uFarPlane;
                    vec4 view = uInverseProjection * vec4(uv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
                    view /= max(view.w, 0.0001);
                    vec3 world = (uInverseView * view).xyz;
                    return max(dot(world - uCameraPosition, rayDirection), 0.0);
                }

                void main()
                {
                    vec3 rayDirection = WorldRay(vUv);
                    vec2 hit = IntersectVolume(uCameraPosition, rayDirection);
                    float start = max(hit.x, 0.0);
                    float end = min(hit.y, OpaqueDistance(vUv, rayDirection));
                    if (end <= start) discard;

                    float stepLength = (end - start) / float(max(uPrimarySteps, 1));
                    float jitter = PixelJitter(gl_FragCoord.xy, uFrameIndex);
                    float distanceAlongRay = start + jitter * stepLength;
                    float transmittance = 1.0;
                    vec3 radiance = vec3(0.0);
                    float phase = Phase(dot(rayDirection, uLightDirection));

                    for (int i = 0; i < 128; ++i)
                    {
                        if (i >= uPrimarySteps || transmittance < 0.01) break;
                        vec3 position = uCameraPosition + rayDirection * distanceAlongRay;
                        float density = SampleDensity(position);
                        if (density > 0.001)
                        {
                            float sigmaT = density * uExtinction;
                            float segmentT = exp(-sigmaT * stepLength);
                            float sunlight = uLightIntensity > 0.0001
                                ? LightTransmittance(position, fract(jitter + float(i) * 0.61803398875))
                                : 1.0;
                            vec3 incident = vec3(uAmbientLight) + uLightColor * uLightIntensity * phase * sunlight;
                            vec3 source = uCloudColor * incident * uScatteringAlbedo;
                            radiance += transmittance * source * (1.0 - segmentT);
                            transmittance *= segmentT;
                        }
                        distanceAlongRay += stepLength;
                    }

                    float alpha = 1.0 - transmittance;
                    if (alpha < 0.001) discard;
                    FragColor = vec4(radiance, alpha);
                }
            )";
            return Shader::Create(source);
        }

        Shader *CreateCompositeShader()
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
                uniform sampler2D uCloudTexture;
                void main()
                {
                    FragColor = texture(uCloudTexture, vUv);
                }
            )";
            return Shader::Create(source);
        }
    }

    VolumetricCloudPass::~VolumetricCloudPass() = default;

    void VolumetricCloudPass::Initialize()
    {
        m_shader = CreateCloudShader();
        m_compositeShader = CreateCompositeShader();
        glGenVertexArrays(1, &m_vao);
    }

    bool VolumetricCloudPass::EnsureCloudTarget(int width, int height)
    {
        if (width <= 0 || height <= 0)
            return false;
        if (!m_cloudTarget)
        {
            m_cloudTarget = std::make_unique<RenderTarget>(RenderTargetConfig{
                .width = width,
                .height = height,
                .clearColor = glm::vec4(0.0f),
            });
        }
        else if (m_cloudTarget->GetWidth() != width || m_cloudTarget->GetHeight() != height)
        {
            m_cloudTarget->Resize(width, height);
        }
        return m_cloudTarget && m_cloudTarget->IsInitialized();
    }

    void VolumetricCloudPass::Execute(const RenderContext &ctx)
    {
        if (!m_shader || !m_compositeShader || !m_vao || !ctx.scene || !ctx.temporaryRenderTarget || !ctx.hasCameraData)
            return;

        const glm::mat4 inverseView = glm::inverse(ctx.cameraData.view);
        const glm::vec3 cameraPosition(inverseView[3]);
        std::vector<CloudDraw> clouds;
        for (const auto *root : ctx.scene->GetRootEntities())
            CollectClouds(root, cameraPosition, clouds);
        if (clouds.empty())
            return;

        float renderScale = 0.25f;
        for (const CloudDraw &draw : clouds)
            renderScale = std::max(renderScale, draw.component->GetRenderScale());
        const int cloudWidth = std::max(1, static_cast<int>(ctx.temporaryRenderTarget->GetWidth() * renderScale));
        const int cloudHeight = std::max(1, static_cast<int>(ctx.temporaryRenderTarget->GetHeight() * renderScale));
        if (!EnsureCloudTarget(cloudWidth, cloudHeight))
            return;

        std::sort(clouds.begin(), clouds.end(), [](const CloudDraw &a, const CloudDraw &b) { return a.distanceSquared > b.distanceSquared; });
        const scene::Light *sun = FindSun(ctx);
        const glm::vec3 lightDirection = sun ? glm::normalize(-sun->direction) : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 lightColor = sun ? glm::max(sun->color, glm::vec3(0.0f)) : glm::vec3(1.0f);
        const float lightIntensity = sun ? std::max(sun->intensity, 0.0f) : 0.0f;
        bool hasTemporalAA = false;
        if (ctx.postProcessEffects)
        {
            for (const auto *effect : *ctx.postProcessEffects)
            {
                if (effect && effect->IsEnabled() && effect->GetTypeName() == "TAA")
                {
                    hasTemporalAA = true;
                    break;
                }
            }
        }

        Graphics::ClearRenderTarget(m_cloudTarget.get());
        Graphics::BindRenderTarget(m_cloudTarget.get());
        glViewport(0, 0, cloudWidth, cloudHeight);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        m_shader->Bind();
        m_shader->SetUniform("uInverseView", inverseView);
        m_shader->SetUniform("uInverseProjection", glm::inverse(ctx.cameraData.projection));
        m_shader->SetUniform("uCameraPosition", cameraPosition);
        m_shader->SetUniform("uLightDirection", lightDirection);
        m_shader->SetUniform("uLightColor", lightColor);
        m_shader->SetUniform("uLightIntensity", lightIntensity);
        m_shader->SetUniform("uFarPlane", std::max(ctx.cameraData.farPlane, 1.0f));
        m_shader->SetUniform("uFrameIndex", hasTemporalAA ? static_cast<float>(ctx.frameSequence % 4096) : 0.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, ctx.temporaryRenderTarget->GetDepthTextureID());
        m_shader->SetUniform("uSceneDepth", 0);
        glBindVertexArray(m_vao);

        for (const CloudDraw &draw : clouds)
        {
            const auto &cloud = *draw.component;
            const auto *owner = cloud.GetOwner();
            if (!owner) continue;
            const glm::mat4 volumeTransform = owner->GetWorldTransform() * glm::scale(glm::mat4(1.0f), cloud.GetSize());
            glm::vec3 windDirection = cloud.GetWindDirection();
            if (glm::dot(windDirection, windDirection) > 0.000001f)
                windDirection = glm::normalize(windDirection);
            m_shader->SetUniform("uInverseVolumeTransform", glm::inverse(volumeTransform));
            m_shader->SetUniform("uWindOffset", windDirection * cloud.GetWindSpeed() * cloud.GetSimulationTime());
            m_shader->SetUniform("uCloudColor", cloud.GetCloudColor());
            m_shader->SetUniform("uCoverage", cloud.GetCoverage());
            m_shader->SetUniform("uDensity", cloud.GetDensity());
            m_shader->SetUniform("uExtinction", cloud.GetExtinction());
            m_shader->SetUniform("uScatteringAlbedo", cloud.GetScatteringAlbedo());
            m_shader->SetUniform("uAnisotropy", cloud.GetAnisotropy());
            m_shader->SetUniform("uAmbientLight", cloud.GetAmbientLight());
            m_shader->SetUniform("uBaseNoiseScale", cloud.GetBaseNoiseScale());
            m_shader->SetUniform("uDetailNoiseScale", cloud.GetDetailNoiseScale());
            m_shader->SetUniform("uDetailErosion", cloud.GetDetailErosion());
            m_shader->SetUniform("uPrimarySteps", cloud.GetPrimaryStepCount());
            m_shader->SetUniform("uLightSteps", cloud.GetLightStepCount());
            glDrawArrays(GL_TRIANGLES, 0, 3);
        }

        glBindVertexArray(0);
        m_shader->Unbind();

        // Upsample the premultiplied cloud radiance once, instead of ray marching
        // every full-resolution pixel.
        Graphics::BindRenderTarget(ctx.temporaryRenderTarget);
        glViewport(0, 0, ctx.temporaryRenderTarget->GetWidth(), ctx.temporaryRenderTarget->GetHeight());
        m_compositeShader->Bind();
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_cloudTarget->GetColorTextureID());
        m_compositeShader->SetUniform("uCloudTexture", 0);
        glBindVertexArray(m_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
        m_compositeShader->Unbind();
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
        Graphics::UnbindRenderTarget();
    }
}
