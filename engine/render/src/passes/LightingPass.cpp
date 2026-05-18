#include "PlutoGE/render/passes/LightingPass.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/postprocess/RSMEffect.h"
#include "PlutoGE/render/postprocess/SceneCompositeEffect.h"
#include "PlutoGE/render/postprocess/SSGIEffect.h"
#include "PlutoGE/render/passes/LightPropagationVolumePass.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/Scene.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace PlutoGE::render
{
    namespace
    {
        constexpr int kPositionTextureSlot = 0;
        constexpr int kNormalTextureSlot = 1;
        constexpr int kAlbedoTextureSlot = 2;
        constexpr int kBakedLightingTextureSlot = 3;
        constexpr int kDepthTextureSlot = 4;
        constexpr int kDirectionalShadowCascadeTextureStartSlot = 5;
        constexpr int kShadowMap2DTextureSlot = kDirectionalShadowCascadeTextureStartSlot + scene::kMaxDirectionalShadowCascades;
        constexpr int kShadowMapCubeTextureSlot = kShadowMap2DTextureSlot + 1;
        constexpr int kLightPropagationVolumeTextureSlot = kShadowMapCubeTextureSlot + 1;
        constexpr int kPreviousLightPropagationVolumeTextureSlot = kLightPropagationVolumeTextureSlot + 1;
        constexpr int kBakedProbeTextureSlot = kPreviousLightPropagationVolumeTextureSlot + 1;
        constexpr int kEnvironmentTextureSlot = kBakedProbeTextureSlot + 1;
        constexpr int kAmbientPassMode = 0;
        constexpr int kLightPassMode = 1;
        constexpr int kIndirectTextureSlot = 0;
        constexpr int kAmbientOutputFull = 0;
        constexpr int kAmbientOutputLpvOnly = 1;
        constexpr int kAmbientOutputNone = 2;
        constexpr std::size_t kLightingSetupStage = 0;
        constexpr std::size_t kLightingAmbientStage = 1;
        constexpr std::size_t kLightingAccumulationStage = 2;

        struct IndirectLightingSettings
        {
            bool enableSsgi = true;
            IndirectDebugView debugView = IndirectDebugView::None;
        };

        bool IsLpvEffectEnabled(const RenderContext &ctx)
        {
            if (!ctx.postProcessEffects)
            {
                return false;
            }

            for (const auto *effect : *ctx.postProcessEffects)
            {
                if (effect && effect->IsEnabled() && effect->GetTypeName() == "LPV")
                {
                    return true;
                }
            }

            return false;
        }

        SSGIEffect *FindEnabledSsgiEffect(const RenderContext &ctx)
        {
            if (!ctx.postProcessEffects)
            {
                return nullptr;
            }

            for (auto *effect : *ctx.postProcessEffects)
            {
                if (!effect || !effect->IsEnabled() || effect->GetTypeName() != "SSGI")
                {
                    continue;
                }

                return static_cast<SSGIEffect *>(effect);
            }

            return nullptr;
        }

        RSMEffect *FindEnabledRsmEffect(const RenderContext &ctx)
        {
            if (!ctx.postProcessEffects)
            {
                return nullptr;
            }

            for (auto *effect : *ctx.postProcessEffects)
            {
                if (!effect || !effect->IsEnabled() || effect->GetTypeName() != "RSM")
                {
                    continue;
                }

                return static_cast<RSMEffect *>(effect);
            }

            return nullptr;
        }

        IndirectLightingSettings ResolveIndirectLightingSettings(const RenderContext &ctx)
        {
            IndirectLightingSettings settings;
            if (!ctx.postProcessEffects)
            {
                return settings;
            }

            for (auto *effect : *ctx.postProcessEffects)
            {
                if (!effect || !effect->IsEnabled() || effect->GetTypeName() != "SceneComposite")
                {
                    continue;
                }

                auto *sceneCompositeEffect = static_cast<SceneCompositeEffect *>(effect);
                settings.enableSsgi = sceneCompositeEffect->IsSsgiEnabled();
                settings.debugView = sceneCompositeEffect->GetIndirectDebugView();
                break;
            }

            return settings;
        }

        Shader *CreateDirectLightingAccumulationShader()
        {
            ShaderSource source;
            source.vertexSource = R"(
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

            source.fragmentSource = R"(
                #version 330 core

                out vec4 FragColor;
                in vec2 UV;

                uniform sampler2D gPosition;
                uniform sampler2D gNormal;
                uniform sampler2D gAlbedoSpec;
                uniform sampler2D gBakedLighting;

                const float PI = 3.14159265359;
                const int LIGHT_TYPE_POINT = 0;
                const int LIGHT_TYPE_DIRECTIONAL = 1;
                const int LIGHT_TYPE_SPOT = 2;
                const int MAX_SHADOW_CASCADES = 4;
                const int DEBUG_VIEW_SHADOW_CASCADES = 6;

                struct Light {
                    vec3 Position;
                    vec3 Color;
                    float Intensity;
                    float Range;
                    vec3 Direction;
                    int Type;
                    int CastsShadows;
                    mat4 LightSpaceMatrix;
                    float ShadowFarPlane;
                    int IsStatic;
                    mat4 CascadeLightSpaceMatrices[MAX_SHADOW_CASCADES];
                    float CascadeSplits[MAX_SHADOW_CASCADES];
                    int CascadeCount;
                    float ShadowSoftness;
                    float CascadeBlendDistance;
                };

                uniform Light uLight;
                uniform vec3 uViewPos;
                uniform mat4 uViewMatrix;
                uniform sampler2D uShadowMap2D;
                uniform sampler2D uShadowCascadeMap0;
                uniform sampler2D uShadowCascadeMap1;
                uniform sampler2D uShadowCascadeMap2;
                uniform sampler2D uShadowCascadeMap3;
                uniform samplerCube uShadowMapCube;
                uniform int uDebugViewMode;

                float DistributionGGX(vec3 normal, vec3 halfwayDir, float roughness)
                {
                    float alpha = roughness * roughness;
                    float alphaSq = alpha * alpha;
                    float ndoth = max(dot(normal, halfwayDir), 0.0);
                    float ndothSq = ndoth * ndoth;
                    float denominator = ndothSq * (alphaSq - 1.0) + 1.0;
                    return alphaSq / max(PI * denominator * denominator, 0.0001);
                }

                float GeometrySchlickGGX(float ndotv, float roughness)
                {
                    float r = roughness + 1.0;
                    float k = (r * r) / 8.0;
                    return ndotv / max(ndotv * (1.0 - k) + k, 0.0001);
                }

                float GeometrySmith(vec3 normal, vec3 viewDir, vec3 lightDir, float roughness)
                {
                    float ndotv = max(dot(normal, viewDir), 0.0);
                    float ndotl = max(dot(normal, lightDir), 0.0);
                    return GeometrySchlickGGX(ndotv, roughness) * GeometrySchlickGGX(ndotl, roughness);
                }

                vec3 FresnelSchlick(float cosTheta, vec3 f0)
                {
                    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
                }

                float ComputePointAttenuation(vec3 fragPos, Light light)
                {
                    float distanceToLight = length(light.Position - fragPos);
                    float normalizedDistance = light.Range > 0.0001 ? distanceToLight / light.Range : 1.0;
                    float attenuation = clamp(1.0 - normalizedDistance, 0.0, 1.0);
                    return attenuation * attenuation;
                }

                float ComputeSpotAttenuation(vec3 fragPos, vec3 lightDir, Light light)
                {
                    float distanceAttenuation = ComputePointAttenuation(fragPos, light);
                    float spotEffect = dot(-lightDir, normalize(light.Direction));
                    return distanceAttenuation * smoothstep(0.9, 0.975, spotEffect);
                }

                vec3 EvaluatePbrLighting(vec3 normal, vec3 viewDir, vec3 albedo, float metallic, float roughness, vec3 lightDir, vec3 radiance)
                {
                    vec3 halfwayDir = normalize(viewDir + lightDir);
                    float ndotv = max(dot(normal, viewDir), 0.0);
                    float ndotl = max(dot(normal, lightDir), 0.0);

                    if (ndotl <= 0.0 || ndotv <= 0.0)
                    {
                        return vec3(0.0);
                    }

                    vec3 f0 = mix(vec3(0.04), albedo, metallic);
                    vec3 fresnel = FresnelSchlick(max(dot(halfwayDir, viewDir), 0.0), f0);
                    float distribution = DistributionGGX(normal, halfwayDir, roughness);
                    float geometry = GeometrySmith(normal, viewDir, lightDir, roughness);

                    vec3 specular = (distribution * geometry * fresnel) / max(4.0 * ndotv * ndotl, 0.0001);
                    vec3 kS = fresnel;
                    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
                    vec3 diffuse = kD * albedo / PI;

                    return (diffuse + specular) * radiance * ndotl;
                }
            )";

            source.fragmentSource += R"(
                float SampleShadowMapPCF(sampler2D shadowMap, vec3 projectedCoords, float depthBias, float softness)
                {
                    if (softness <= 0.001)
                    {
                        float closestDepth = texture(shadowMap, projectedCoords.xy).r;
                        return projectedCoords.z - depthBias > closestDepth ? 1.0 : 0.0;
                    }

                    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
                    float filterRadius = max(softness, 0.5);
                    vec2 offsets[4] = vec2[](
                        vec2(-0.5, -0.5),
                        vec2(0.5, -0.5),
                        vec2(-0.5, 0.5),
                        vec2(0.5, 0.5)
                    );
                    float shadow = 0.0;
                    for (int sampleIndex = 0; sampleIndex < 4; ++sampleIndex)
                    {
                        vec2 sampleCoords = projectedCoords.xy + offsets[sampleIndex] * texelSize * filterRadius;
                        float closestDepth = texture(shadowMap, sampleCoords).r;
                        shadow += projectedCoords.z - depthBias > closestDepth ? 1.0 : 0.0;
                    }

                    return shadow * 0.25;
                }

                float SampleDirectionalCascadeShadow(int cascadeIndex, vec3 projectedCoords, float depthBias, float softness)
                {
                    if (cascadeIndex == 0)
                    {
                        return SampleShadowMapPCF(uShadowCascadeMap0, projectedCoords, depthBias, softness);
                    }
                    if (cascadeIndex == 1)
                    {
                        return SampleShadowMapPCF(uShadowCascadeMap1, projectedCoords, depthBias, softness);
                    }
                    if (cascadeIndex == 2)
                    {
                        return SampleShadowMapPCF(uShadowCascadeMap2, projectedCoords, depthBias, softness);
                    }
                    return SampleShadowMapPCF(uShadowCascadeMap3, projectedCoords, depthBias, softness);
                }

                float ComputeSingleProjectedShadow(vec3 receiverPosition, sampler2D shadowMap, mat4 lightSpaceMatrix, float depthBias, float softness)
                {
                    vec4 lightSpacePosition = lightSpaceMatrix * vec4(receiverPosition, 1.0);
                    vec3 projectedCoords = lightSpacePosition.xyz / max(lightSpacePosition.w, 0.0001);
                    projectedCoords = projectedCoords * 0.5 + 0.5;

                    if (projectedCoords.z < 0.0 || projectedCoords.z > 1.0 || projectedCoords.x < 0.0 || projectedCoords.x > 1.0 || projectedCoords.y < 0.0 || projectedCoords.y > 1.0)
                    {
                        return 0.0;
                    }

                    return SampleShadowMapPCF(shadowMap, projectedCoords, depthBias, softness);
                }

                float ComputeSpotShadow(vec3 fragPos, vec3 normal, Light light)
                {
                    vec3 surfaceNormal = normalize(normal);
                    vec3 lightVector = normalize(light.Position - fragPos);
                    float ndotl = max(dot(surfaceNormal, lightVector), 0.0);
                    float normalBias = max(0.00075 * (1.0 - ndotl), 0.00005);
                    float depthBias = max(0.00035 * (1.0 - ndotl), 0.00005);
                    vec3 receiverPosition = fragPos + surfaceNormal * normalBias;
                    return ComputeSingleProjectedShadow(receiverPosition, uShadowMap2D, light.LightSpaceMatrix, depthBias, 1.25);
                }

                int SelectDirectionalCascadeIndex(Light light, float viewDepth)
                {
                    for (int cascadeIndex = 0; cascadeIndex < light.CascadeCount; ++cascadeIndex)
                    {
                        if (viewDepth <= light.CascadeSplits[cascadeIndex])
                        {
                            return cascadeIndex;
                        }
                    }

                    return light.CascadeCount - 1;
                }

                vec3 GetDirectionalCascadeDebugColor(int cascadeIndex)
                {
                    if (cascadeIndex == 0)
                    {
                        return vec3(0.20, 0.55, 1.00);
                    }
                    if (cascadeIndex == 1)
                    {
                        return vec3(0.20, 0.90, 0.35);
                    }
                    if (cascadeIndex == 2)
                    {
                        return vec3(1.00, 0.80, 0.20);
                    }
                    return vec3(1.00, 0.35, 0.20);
                }

                bool ProjectDirectionalCascadeCoords(vec3 receiverPosition, Light light, int cascadeIndex, out vec3 projectedCoords)
                {
                    vec4 lightSpacePosition = light.CascadeLightSpaceMatrices[cascadeIndex] * vec4(receiverPosition, 1.0);
                    projectedCoords = lightSpacePosition.xyz / max(lightSpacePosition.w, 0.0001);
                    projectedCoords = projectedCoords * 0.5 + 0.5;

                    return !(projectedCoords.z < 0.0 || projectedCoords.z > 1.0 || projectedCoords.x < 0.0 || projectedCoords.x > 1.0 || projectedCoords.y < 0.0 || projectedCoords.y > 1.0);
                }

                float ComputeDirectionalCascadeShadow(vec3 receiverPosition, Light light, int cascadeIndex, float depthBias, out bool hasCoverage)
                {
                    vec3 projectedCoords;
                    hasCoverage = ProjectDirectionalCascadeCoords(receiverPosition, light, cascadeIndex, projectedCoords);
                    if (!hasCoverage)
                    {
                        return 0.0;
                    }

                    return SampleDirectionalCascadeShadow(cascadeIndex, projectedCoords, depthBias, light.ShadowSoftness);
                }
            )";

            source.fragmentSource += R"(
                float ComputeDirectionalShadowFast(vec3 fragPos, vec3 normal, Light light, out int sampledCascadeIndex, out bool hasAnyCascadeCoverage)
                {
                    sampledCascadeIndex = -1;
                    hasAnyCascadeCoverage = false;

                    if (light.CascadeCount <= 0)
                    {
                        return 0.0;
                    }

                    vec3 surfaceNormal = normalize(normal);
                    vec3 lightVector = normalize(-light.Direction);
                    float ndotl = max(dot(surfaceNormal, lightVector), 0.0);
                    float normalBias = max(0.0015 * (1.0 - ndotl), 0.00015);
                    vec3 receiverPosition = fragPos + surfaceNormal * normalBias;
                    float viewDepth = abs((uViewMatrix * vec4(fragPos, 1.0)).z);

                    if (viewDepth > light.CascadeSplits[light.CascadeCount - 1])
                    {
                        return 0.0;
                    }

                    int cascadeIndex = SelectDirectionalCascadeIndex(light, viewDepth);
                    float depthBias = max(0.0002 + (1.0 - ndotl) * 0.00035, 0.00005);
                    bool hasCascadeCoverage = false;
                    float shadow = ComputeDirectionalCascadeShadow(receiverPosition, light, cascadeIndex, depthBias, hasCascadeCoverage);
                    int resolvedCascadeIndex = cascadeIndex;

                    if (!hasCascadeCoverage && cascadeIndex < light.CascadeCount - 1)
                    {
                        bool hasNextCascadeCoverage = false;
                        float nextShadow = ComputeDirectionalCascadeShadow(receiverPosition, light, cascadeIndex + 1, depthBias, hasNextCascadeCoverage);
                        if (hasNextCascadeCoverage)
                        {
                            shadow = nextShadow;
                            hasCascadeCoverage = true;
                            resolvedCascadeIndex = cascadeIndex + 1;
                        }
                    }

                    if (!hasCascadeCoverage)
                    {
                        return 0.0;
                    }

                    hasAnyCascadeCoverage = true;
                    sampledCascadeIndex = resolvedCascadeIndex;

                    if (resolvedCascadeIndex < light.CascadeCount - 1)
                    {
                        float splitDistance = light.CascadeSplits[resolvedCascadeIndex];
                        float blendStart = max(splitDistance - light.CascadeBlendDistance, 0.0);
                        if (viewDepth > blendStart)
                        {
                            bool hasNextCascadeCoverage = false;
                            float nextShadow = ComputeDirectionalCascadeShadow(receiverPosition, light, resolvedCascadeIndex + 1, depthBias, hasNextCascadeCoverage);
                            if (hasNextCascadeCoverage)
                            {
                                float blendFactor = clamp((viewDepth - blendStart) / max(splitDistance - blendStart, 0.0001), 0.0, 1.0);
                                shadow = mix(shadow, nextShadow, blendFactor);
                            }
                        }
                    }

                    return shadow;
                }

                float ComputePointShadow(vec3 fragPos, vec3 normal, Light light)
                {
                    vec3 surfaceNormal = normalize(normal);
                    vec3 lightDir = normalize(light.Position - fragPos);
                    float slopeBias = 0.05 * (1.0 - max(dot(surfaceNormal, lightDir), 0.0));
                    float bias = max(light.ShadowFarPlane * 0.00075, 0.004 + slopeBias);
                    vec3 receiverPosition = fragPos + surfaceNormal * bias;
                    vec3 fragToLight = receiverPosition - light.Position;
                    float currentDepth = length(fragToLight);
                    vec3 sampleDirection = normalize(fragToLight);
                    vec3 referenceUp = abs(sampleDirection.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
                    vec3 tangent = normalize(cross(referenceUp, sampleDirection));
                    vec3 bitangent = cross(sampleDirection, tangent);
                    float angularRadius = 0.006;
                    vec2 sampleOffsets[4] = vec2[](
                        vec2(0.0, 0.0),
                        vec2(0.8660, 0.5),
                        vec2(-0.8660, 0.5),
                        vec2(0.0, -1.0)
                    );

                    float shadow = 0.0;
                    for (int sampleIndex = 0; sampleIndex < 4; ++sampleIndex)
                    {
                        vec3 blurredDirection = normalize(
                            sampleDirection +
                            tangent * sampleOffsets[sampleIndex].x * angularRadius +
                            bitangent * sampleOffsets[sampleIndex].y * angularRadius);
                        float closestDepth = texture(uShadowMapCube, blurredDirection).r * light.ShadowFarPlane;
                        shadow += currentDepth - bias > closestDepth ? 1.0 : 0.0;
                    }

                    return shadow / 4.0;
                }

                float ComputeShadow(vec3 fragPos, vec3 normal, Light light, out int sampledCascadeIndex, out bool hasAnyCascadeCoverage)
                {
                    sampledCascadeIndex = -1;
                    hasAnyCascadeCoverage = false;

                    if (light.CastsShadows == 0)
                    {
                        return 0.0;
                    }
                    if (light.Type == LIGHT_TYPE_POINT)
                    {
                        return ComputePointShadow(fragPos, normal, light);
                    }
                    if (light.Type == LIGHT_TYPE_DIRECTIONAL)
                    {
                        return ComputeDirectionalShadowFast(fragPos, normal, light, sampledCascadeIndex, hasAnyCascadeCoverage);
                    }
                    return ComputeSpotShadow(fragPos, normal, light);
                }

                vec3 ComputeLightContribution(vec3 fragPos, vec3 normal, vec3 viewDir, vec3 albedo, float metallic, float roughness, Light light)
                {
                    vec3 lightDir;
                    float attenuation = 1.0;

                    if (light.Type == LIGHT_TYPE_POINT)
                    {
                        lightDir = normalize(light.Position - fragPos);
                        attenuation = ComputePointAttenuation(fragPos, light);
                    }
                    else if (light.Type == LIGHT_TYPE_DIRECTIONAL)
                    {
                        lightDir = normalize(-light.Direction);
                    }
                    else
                    {
                        lightDir = normalize(light.Position - fragPos);
                        attenuation = ComputeSpotAttenuation(fragPos, lightDir, light);
                    }

                    if (attenuation <= 0.0001)
                    {
                        return vec3(0.0);
                    }

                    float ndotl = dot(normal, lightDir);
                    if (ndotl <= 0.0001)
                    {
                        return vec3(0.0);
                    }

                    vec3 radiance = light.Color * light.Intensity * attenuation;
                    if (dot(radiance, radiance) <= 0.000001)
                    {
                        return vec3(0.0);
                    }

                    int sampledCascadeIndex = -1;
                    bool hasAnyCascadeCoverage = false;
                    float shadow = ComputeShadow(fragPos, normal, light, sampledCascadeIndex, hasAnyCascadeCoverage);

                    if (uDebugViewMode == DEBUG_VIEW_SHADOW_CASCADES)
                    {
                        if (light.Type != LIGHT_TYPE_DIRECTIONAL || light.CastsShadows == 0)
                        {
                            return vec3(0.0);
                        }
                        if (!hasAnyCascadeCoverage)
                        {
                            return vec3(1.0, 0.0, 0.0);
                        }
                        return GetDirectionalCascadeDebugColor(sampledCascadeIndex);
                    }

                    return EvaluatePbrLighting(normal, viewDir, albedo, metallic, roughness, lightDir, radiance) * (1.0 - shadow);
                }
            )";

            source.fragmentSource += R"(
                void main()
                {
                    vec3 fragPos = texture(gPosition, UV).rgb;
                    vec4 normalRoughness = texture(gNormal, UV);
                    if (dot(normalRoughness.rgb, normalRoughness.rgb) <= 0.000001)
                    {
                        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
                        return;
                    }

                    vec4 albedoMetallic = texture(gAlbedoSpec, UV);
                    vec3 normal = normalize(normalRoughness.rgb);
                    vec3 albedo = albedoMetallic.rgb;
                    float roughness = clamp(normalRoughness.a, 0.04, 1.0);
                    float metallic = clamp(albedoMetallic.a, 0.0, 1.0);
                    if (uLight.IsStatic != 0)
                    {
                        float bakedStaticMask = texture(gBakedLighting, UV).a;
                        if (bakedStaticMask > 0.5)
                        {
                            FragColor = vec4(0.0, 0.0, 0.0, 1.0);
                            return;
                        }
                    }

                    vec3 viewDir = normalize(uViewPos - fragPos);
                    vec3 lighting = ComputeLightContribution(fragPos, normal, viewDir, albedo, metallic, roughness, uLight);
                    FragColor = vec4(lighting, 1.0);
                }
            )";

            return Shader::Create(source);
        }

        void BindLightingInputs(Shader *shader, const RenderContext &ctx)
        {
            auto *gBuffer = ctx.gBuffer;
            glActiveTexture(GL_TEXTURE0 + kPositionTextureSlot);
            glBindTexture(GL_TEXTURE_2D, gBuffer->GetPositionTextureID());
            if (shader->HasUniform("gPosition"))
            {
                shader->SetUniform("gPosition", kPositionTextureSlot);
            }

            glActiveTexture(GL_TEXTURE0 + kNormalTextureSlot);
            glBindTexture(GL_TEXTURE_2D, gBuffer->GetNormalTextureID());
            if (shader->HasUniform("gNormal"))
            {
                shader->SetUniform("gNormal", kNormalTextureSlot);
            }

            glActiveTexture(GL_TEXTURE0 + kAlbedoTextureSlot);
            glBindTexture(GL_TEXTURE_2D, gBuffer->GetAlbedoTextureID());
            if (shader->HasUniform("gAlbedoSpec"))
            {
                shader->SetUniform("gAlbedoSpec", kAlbedoTextureSlot);
            }

            glActiveTexture(GL_TEXTURE0 + kBakedLightingTextureSlot);
            glBindTexture(GL_TEXTURE_2D, gBuffer->GetBakedLightingTextureID());
            if (shader->HasUniform("gBakedLighting"))
            {
                shader->SetUniform("gBakedLighting", kBakedLightingTextureSlot);
            }

            glActiveTexture(GL_TEXTURE0 + kDepthTextureSlot);
            glBindTexture(GL_TEXTURE_2D, gBuffer->GetDepthTextureID());
            if (shader->HasUniform("gDepth"))
            {
                shader->SetUniform("gDepth", kDepthTextureSlot);
            }

            for (int cascadeIndex = 0; cascadeIndex < scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
            {
                const int textureSlot = kDirectionalShadowCascadeTextureStartSlot + cascadeIndex;
                glActiveTexture(GL_TEXTURE0 + textureSlot);
                glBindTexture(GL_TEXTURE_2D, 0);
                const std::string uniformName = "uShadowCascadeMap" + std::to_string(cascadeIndex);
                if (shader->HasUniform(uniformName))
                {
                    shader->SetUniform(uniformName, textureSlot);
                }
            }

            glActiveTexture(GL_TEXTURE0 + kShadowMap2DTextureSlot);
            glBindTexture(GL_TEXTURE_2D, 0);
            if (shader->HasUniform("uShadowMap2D"))
            {
                shader->SetUniform("uShadowMap2D", kShadowMap2DTextureSlot);
            }

            glActiveTexture(GL_TEXTURE0 + kShadowMapCubeTextureSlot);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
            if (shader->HasUniform("uShadowMapCube"))
            {
                shader->SetUniform("uShadowMapCube", kShadowMapCubeTextureSlot);
            }

            auto *lpvPass = ctx.lightPropagationVolumePass;
            auto *lpvTexture = lpvPass ? lpvPass->GetVolumeTexture() : nullptr;
            glActiveTexture(GL_TEXTURE0 + kLightPropagationVolumeTextureSlot);
            glBindTexture(GL_TEXTURE_3D, lpvTexture ? lpvTexture->GetTextureID() : 0);
            if (shader->HasUniform("uLpvVolume"))
            {
                shader->SetUniform("uLpvVolume", kLightPropagationVolumeTextureSlot);
            }

            auto *previousLpvTexture = lpvPass ? lpvPass->GetPreviousVolumeTexture() : nullptr;
            glActiveTexture(GL_TEXTURE0 + kPreviousLightPropagationVolumeTextureSlot);
            glBindTexture(GL_TEXTURE_3D, previousLpvTexture ? previousLpvTexture->GetTextureID() : 0);
            if (shader->HasUniform("uPreviousLpvVolume"))
            {
                shader->SetUniform("uPreviousLpvVolume", kPreviousLightPropagationVolumeTextureSlot);
            }

            auto *bakedProbeTexture = ctx.scene ? ctx.scene->GetBakedProbeTexture() : nullptr;
            glActiveTexture(GL_TEXTURE0 + kBakedProbeTextureSlot);
            glBindTexture(GL_TEXTURE_3D, bakedProbeTexture ? bakedProbeTexture->GetTextureID() : 0);
            if (shader->HasUniform("uBakedProbeVolume"))
            {
                shader->SetUniform("uBakedProbeVolume", kBakedProbeTextureSlot);
            }

            auto *environmentTexture = ctx.scene ? ctx.scene->GetEnvironmentMapTexture() : nullptr;
            glActiveTexture(GL_TEXTURE0 + kEnvironmentTextureSlot);
            glBindTexture(GL_TEXTURE_2D, environmentTexture ? environmentTexture->GetTextureID() : 0);
            if (shader->HasUniform("uEnvironmentMap"))
            {
                shader->SetUniform("uEnvironmentMap", kEnvironmentTextureSlot);
            }
        }

        bool BindShadowMapForLight(const scene::Light &light)
        {
            for (int cascadeIndex = 0; cascadeIndex < scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
            {
                glActiveTexture(GL_TEXTURE0 + kDirectionalShadowCascadeTextureStartSlot + cascadeIndex);
                glBindTexture(GL_TEXTURE_2D, 0);
            }

            glActiveTexture(GL_TEXTURE0 + kShadowMap2DTextureSlot);
            glBindTexture(GL_TEXTURE_2D, 0);

            glActiveTexture(GL_TEXTURE0 + kShadowMapCubeTextureSlot);
            glBindTexture(GL_TEXTURE_CUBE_MAP, 0);

            if (!light.castsShadows)
            {
                return false;
            }

            if (light.type == scene::LightType::Directional)
            {
                if (light.activeShadowCascadeCount <= 0)
                {
                    return false;
                }

                bool boundCascade = false;
                for (int cascadeIndex = 0; cascadeIndex < light.activeShadowCascadeCount; ++cascadeIndex)
                {
                    auto *shadowCascadeMap = light.shadowCascadeMaps[cascadeIndex].get();
                    if (!shadowCascadeMap)
                    {
                        continue;
                    }

                    glActiveTexture(GL_TEXTURE0 + kDirectionalShadowCascadeTextureStartSlot + cascadeIndex);
                    glBindTexture(GL_TEXTURE_2D, shadowCascadeMap->GetTextureID());
                    boundCascade = true;
                }

                return boundCascade;
            }

            if (!light.shadowMap)
            {
                return false;
            }

            auto *shadowMap = light.shadowMap.get();
            if (!shadowMap)
            {
                return false;
            }

            if (light.type == scene::LightType::Point)
            {
                if (shadowMap->GetType() != GL_TEXTURE_CUBE_MAP)
                {
                    return false;
                }

                glActiveTexture(GL_TEXTURE0 + kShadowMapCubeTextureSlot);
                glBindTexture(GL_TEXTURE_CUBE_MAP, shadowMap->GetTextureID());
                return true;
            }

            if (shadowMap->GetType() != GL_TEXTURE_2D)
            {
                return false;
            }

            glActiveTexture(GL_TEXTURE0 + kShadowMap2DTextureSlot);
            glBindTexture(GL_TEXTURE_2D, shadowMap->GetTextureID());
            return true;
        }

        void BindLightUniforms(Shader *shader, const scene::Light &light, bool hasShadowMap)
        {
            shader->SetUniform("uLight.Position", light.position);
            shader->SetUniform("uLight.Color", light.color);
            shader->SetUniform("uLight.Intensity", light.intensity);
            shader->SetUniform("uLight.Range", light.range);
            shader->SetUniform("uLight.Direction", light.direction);
            shader->SetUniform("uLight.Type", static_cast<int>(light.type));
            shader->SetUniform("uLight.IsStatic", light.isStatic ? 1 : 0);
            shader->SetUniform("uLight.CastsShadows", hasShadowMap ? 1 : 0);
            shader->SetUniform("uLight.LightSpaceMatrix", light.shadowMatrix);
            shader->SetUniform("uLight.ShadowFarPlane", light.shadowFarPlane);
            shader->SetUniform("uLight.CascadeCount", light.type == scene::LightType::Directional && hasShadowMap ? light.activeShadowCascadeCount : 0);
            shader->SetUniform("uLight.ShadowSoftness", light.directionalShadowSettings.softness);
            shader->SetUniform("uLight.CascadeBlendDistance", light.directionalShadowSettings.cascadeBlendDistance);

            for (int cascadeIndex = 0; cascadeIndex < scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
            {
                shader->SetUniform("uLight.CascadeLightSpaceMatrices[" + std::to_string(cascadeIndex) + "]", light.shadowCascadeMatrices[cascadeIndex]);
                shader->SetUniform("uLight.CascadeSplits[" + std::to_string(cascadeIndex) + "]", light.shadowCascadeSplits[cascadeIndex]);
            }
        }
    }

    void LightingPass::Initialize()
    {
        m_lightingPassShader = Shader::CreateLightingPassShader();
        m_directLightingPassShader = CreateDirectLightingAccumulationShader();

        ShaderSource indirectCompositeSource;
        indirectCompositeSource.vertexSource = R"(
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
        indirectCompositeSource.fragmentSource = R"(
            #version 330 core

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uIndirectTexture;

            void main()
            {
                FragColor = vec4(texture(uIndirectTexture, UV).rgb, 1.0);
            }
        )";

        m_indirectCompositeShader = Shader::Create(indirectCompositeSource);
    }

    void LightingPass::Execute(const RenderContext &ctx)
    {
        if (!m_lightingPassShader || !m_directLightingPassShader || !ctx.temporaryRenderTarget || !ctx.gBuffer || !ctx.lights)
        {
            return;
        }

        if (ctx.renderer)
        {
            int lightCount = 0;
            int shadowedLightCount = 0;
            for (auto *light : *ctx.lights)
            {
                if (!light)
                {
                    continue;
                }

                ++lightCount;
                if (light->castsShadows && ((light->type == scene::LightType::Directional && light->activeShadowCascadeCount > 0) || light->shadowMap))
                {
                    ++shadowedLightCount;
                }
            }

            ctx.renderer->SetLightingPassCounters(lightCount, shadowedLightCount);
            ctx.renderer->BeginLightingStageTiming(kLightingSetupStage);
        }

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        Graphics::ClearRenderTarget(ctx.temporaryRenderTarget);

        glBindFramebuffer(GL_READ_FRAMEBUFFER, ctx.gBuffer->GetFBO());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, ctx.temporaryRenderTarget->GetFramebufferID());
        glBlitFramebuffer(
            0, 0, ctx.gBuffer->GetWidth(), ctx.gBuffer->GetHeight(),
            0, 0, ctx.temporaryRenderTarget->GetWidth(), ctx.temporaryRenderTarget->GetHeight(),
            GL_DEPTH_BUFFER_BIT,
            GL_NEAREST);

        Graphics::BindRenderTarget(ctx.temporaryRenderTarget);

        if (ctx.renderer)
        {
            ctx.renderer->EndLightingStageTiming(kLightingSetupStage);
            ctx.renderer->BeginLightingStageTiming(kLightingAmbientStage);
        }

        m_lightingPassShader->Bind();
        BindLightingInputs(m_lightingPassShader, ctx);

        const glm::vec3 cameraPos = glm::vec3(glm::inverse(ctx.cameraData.view)[3]);
        auto *lpvPass = ctx.lightPropagationVolumePass;
        const IndirectLightingSettings indirectSettings = ResolveIndirectLightingSettings(ctx);
        const bool enableLpv = IsLpvEffectEnabled(ctx);
        const bool useRsm = FindEnabledRsmEffect(ctx) != nullptr;
        int ambientOutputMode = kAmbientOutputFull;
        bool renderDirectLighting = true;
        bool renderRsm = useRsm;
        bool renderSsgi = indirectSettings.enableSsgi;
        bool compositeRsmOnly = false;
        bool compositeSsgiOnly = false;

        const auto compositeIndirectTarget = [&](RenderTarget *resolvedIndirectTarget, bool indirectOnly)
        {
            if (!resolvedIndirectTarget)
            {
                return;
            }

            Graphics::BindRenderTarget(ctx.temporaryRenderTarget);
            glViewport(0, 0, ctx.temporaryRenderTarget->GetWidth(), ctx.temporaryRenderTarget->GetHeight());
            if (indirectOnly)
            {
                glDisable(GL_BLEND);
            }
            else
            {
                glEnable(GL_BLEND);
                glBlendEquation(GL_FUNC_ADD);
                glBlendFunc(GL_ONE, GL_ONE);
            }

            m_indirectCompositeShader->Bind();
            glActiveTexture(GL_TEXTURE0 + kIndirectTextureSlot);
            glBindTexture(GL_TEXTURE_2D, resolvedIndirectTarget->GetColorTextureID());
            m_indirectCompositeShader->SetUniform("uIndirectTexture", kIndirectTextureSlot);
            glDrawArrays(GL_TRIANGLES, 0, 3);
        };

        switch (indirectSettings.debugView)
        {
        case IndirectDebugView::GiOnly:
            ambientOutputMode = enableLpv ? kAmbientOutputLpvOnly : kAmbientOutputNone;
            renderDirectLighting = false;
            renderRsm = useRsm;
            renderSsgi = false;
            compositeRsmOnly = useRsm;
            break;
        case IndirectDebugView::RsmOnly:
            ambientOutputMode = kAmbientOutputNone;
            renderDirectLighting = false;
            renderRsm = useRsm;
            renderSsgi = false;
            compositeRsmOnly = true;
            break;
        case IndirectDebugView::SsgiOnly:
            ambientOutputMode = kAmbientOutputNone;
            renderDirectLighting = false;
            renderRsm = false;
            renderSsgi = indirectSettings.enableSsgi;
            compositeSsgiOnly = true;
            break;
        case IndirectDebugView::CombinedIndirect:
            ambientOutputMode = enableLpv ? kAmbientOutputLpvOnly : kAmbientOutputNone;
            renderDirectLighting = false;
            renderRsm = useRsm;
            renderSsgi = indirectSettings.enableSsgi;
            break;
        case IndirectDebugView::None:
        default:
            renderRsm = useRsm;
            break;
        }

        m_lightingPassShader->SetUniform("uViewPos", cameraPos);
        m_lightingPassShader->SetUniform("uViewMatrix", ctx.cameraData.view);
        m_lightingPassShader->SetUniform("uInverseViewMatrix", glm::inverse(ctx.cameraData.view));
        m_lightingPassShader->SetUniform("uInverseProjectionMatrix", glm::inverse(ctx.cameraData.projection));
        m_lightingPassShader->SetUniform("uLpvEnabled", enableLpv && lpvPass && lpvPass->GetVolumeTexture() ? 1 : 0);
        m_lightingPassShader->SetUniform("uLpvOrigin", lpvPass ? lpvPass->GetGridOrigin() : glm::vec3(0.0f));
        m_lightingPassShader->SetUniform("uLpvSize", lpvPass ? lpvPass->GetGridSize() : glm::vec3(1.0f));
        m_lightingPassShader->SetUniform("uPreviousLpvOrigin", lpvPass ? lpvPass->GetPreviousGridOrigin() : glm::vec3(0.0f));
        m_lightingPassShader->SetUniform("uPreviousLpvSize", lpvPass ? lpvPass->GetPreviousGridSize() : glm::vec3(1.0f));
        m_lightingPassShader->SetUniform("uLpvTransitionBlend", lpvPass ? lpvPass->GetTransitionBlendFactor() : 1.0f);
        m_lightingPassShader->SetUniform("uAmbientOutputMode", ambientOutputMode);
        m_lightingPassShader->SetUniform("uBakedProbeEnabled", ctx.scene && ctx.scene->HasBakedProbeVolume() ? 1 : 0);
        m_lightingPassShader->SetUniform("uBakedProbeOrigin", ctx.scene ? ctx.scene->GetBakedProbeVolume().origin : glm::vec3(0.0f));
        m_lightingPassShader->SetUniform("uBakedProbeSize", ctx.scene ? ctx.scene->GetBakedProbeVolume().size : glm::vec3(1.0f));
        m_lightingPassShader->SetUniform("uEnvironmentEnabled", ctx.scene && ctx.scene->GetEnvironmentMapTexture() ? 1 : 0);
        m_lightingPassShader->SetUniform("uEnvironmentIntensity", ctx.scene ? ctx.scene->GetEnvironmentIntensity() : 1.0f);
        m_lightingPassShader->SetUniform("uDebugViewMode", static_cast<int>(ctx.postProcessDebugView));
        const auto *environmentTexture = ctx.scene ? ctx.scene->GetEnvironmentMapTexture() : nullptr;
        const float environmentMaxMipLevel = environmentTexture ? static_cast<float>(std::max(0, static_cast<int>(std::floor(std::log2(static_cast<float>(std::max(environmentTexture->GetWidth(), environmentTexture->GetHeight()))))))) : 0.0f;
        m_lightingPassShader->SetUniform("uEnvironmentMaxMipLevel", environmentMaxMipLevel);

        glDisable(GL_BLEND);
        m_lightingPassShader->SetUniform("uPassMode", kAmbientPassMode);
        glBindVertexArray(0);
        glDrawArrays(GL_TRIANGLES, 0, 3);

        if (ctx.renderer)
        {
            ctx.renderer->EndLightingStageTiming(kLightingAmbientStage);
            ctx.renderer->BeginLightingStageTiming(kLightingAccumulationStage);
        }

        if (renderDirectLighting)
        {
            m_directLightingPassShader->Bind();
            BindLightingInputs(m_directLightingPassShader, ctx);
            m_directLightingPassShader->SetUniform("uViewPos", cameraPos);
            m_directLightingPassShader->SetUniform("uViewMatrix", ctx.cameraData.view);
            m_directLightingPassShader->SetUniform("uDebugViewMode", static_cast<int>(ctx.postProcessDebugView));

            glEnable(GL_BLEND);
            glBlendEquation(GL_FUNC_ADD);
            glBlendFunc(GL_ONE, GL_ONE);

            for (auto *light : *ctx.lights)
            {
                if (!light)
                {
                    continue;
                }

                const bool hasShadowMap = BindShadowMapForLight(*light);
                BindLightUniforms(m_directLightingPassShader, *light, hasShadowMap);
                glDrawArrays(GL_TRIANGLES, 0, 3);
            }
        }

        if (m_indirectCompositeShader && renderSsgi)
        {
            if (auto *ssgiEffect = FindEnabledSsgiEffect(ctx))
            {
                RenderTarget *resolvedIndirectTarget = ssgiEffect->GenerateResolvedIndirectLighting(PostProcessContext{
                                                                                                        .renderContext = ctx,
                                                                                                        .sourceRenderTarget = ctx.temporaryRenderTarget,
                                                                                                        .destinationRenderTarget = nullptr,
                                                                                                    },
                                                                                                    ctx.temporaryRenderTarget->GetWidth(), ctx.temporaryRenderTarget->GetHeight());
                compositeIndirectTarget(resolvedIndirectTarget, compositeSsgiOnly || ssgiEffect->OutputsIndirectOnly());
            }
        }

        if (m_indirectCompositeShader && renderRsm)
        {
            if (auto *rsmEffect = FindEnabledRsmEffect(ctx))
            {
                RenderTarget *resolvedIndirectTarget = rsmEffect->GenerateResolvedIndirectLighting(PostProcessContext{
                                                                                                       .renderContext = ctx,
                                                                                                       .sourceRenderTarget = ctx.temporaryRenderTarget,
                                                                                                       .destinationRenderTarget = nullptr,
                                                                                                   },
                                                                                                   ctx.temporaryRenderTarget->GetWidth(), ctx.temporaryRenderTarget->GetHeight());
                compositeIndirectTarget(resolvedIndirectTarget, compositeRsmOnly);
            }
        }

        glDisable(GL_BLEND);
        if (ctx.renderer)
        {
            ctx.renderer->EndLightingStageTiming(kLightingAccumulationStage);
        }
        Graphics::UnbindRenderTarget();
    }
}