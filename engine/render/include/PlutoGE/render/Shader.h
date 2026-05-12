#pragma once

#include <glad/glad.h>
#include <string>
#include <iostream>
#include <unordered_map>
#include <glm/glm.hpp>

namespace PlutoGE::render
{
    struct ShaderSource
    {
        std::string vertexSource;
        std::string fragmentSource;
    };

    struct ShaderConfig
    {
        // Future configuration options can be added here
        std::string vertexShaderPath;
        std::string fragmentShaderPath;
    };

    class Texture;
    class Shader
    {
    public:
        Shader() = default;
        ~Shader() = default;

        static Shader *Create(const ShaderConfig &config);
        static Shader *Create(const ShaderSource &source);

        //         static Shader *CreateDefault()
        //         {
        //             ShaderSource defaultSource;

        //             defaultSource.vertexSource = R"(
        //                 #version 330 core
        //                 layout(location = 0) in vec3 aPos;
        //                 layout(location = 1) in vec3 aNormal;
        //                 layout(location = 2) in vec2 aUV;
        //                 layout(location = 3) in vec4 aTangent;

        //                 uniform mat4 uModel;
        //                 uniform mat4 uView;
        //                 uniform mat4 uProjection;

        //                 out vec3 FragPos;
        //                 out vec3 Normal;
        //                 out vec2 UV;
        //                 out mat3 TBN;

        //                 void main()
        //                 {
        //                     FragPos = vec3(uModel * vec4(aPos, 1.0));
        //                     Normal = mat3(transpose(inverse(uModel))) * aNormal; // Transform normal to world space
        //                     UV = aUV;
        //                     gl_Position = uProjection * uView * vec4(FragPos, 1.0);
        //                     TBN = mat3(
        //                         normalize(mat3(uModel) * aTangent.xyz), // Tangent
        //                         normalize(cross(Normal, normalize(mat3(uModel) * aTangent.xyz))), // Bitangent
        //                         Normal // Normal
        //                     );
        //                 }
        //             )";

        //             defaultSource.fragmentSource = R"(
        // #version 330 core
        // out vec4 FragColor;

        // in vec3 FragPos;
        // in vec3 Normal;
        // in vec2 UV;
        // in mat3 TBN;

        // uniform vec3 uColor;
        // uniform sampler2D uAlbedoTexture;
        // uniform float uHasAlbedoTexture;

        // uniform sampler2D uNormalTexture;
        // uniform float uHasNormalTexture;

        // uniform float uMetallic;
        // uniform sampler2D uMetallicTexture;
        // uniform float uHasMetallicTexture;

        // uniform sampler2D uRoughnessTexture;
        // uniform float uRoughness;
        // uniform float uHasRoughnessTexture;

        // void main()
        // {
        //     vec3 ambient = 0.1 * uColor;

        //     vec3 lightDir = normalize(vec3(0.5, 1.0, 0.6));
        //     vec3 normal = normalize(Normal);

        //     if (uHasNormalTexture > 0.5)
        //     {
        //         normal = texture(uNormalTexture, UV).rgb;
        //         normal = normalize(normal * 2.0 - 1.0);
        //         normal = normalize(TBN * normal); // Transform to world space
        //     }

        //     float lightIntensity = 1.0;
        //     float lightDirection = max(dot(normal, lightDir), 0.0);

        //     vec3 albedo = uColor;
        //     if (uHasAlbedoTexture > 0.5)
        //     {
        //         vec4 texAlbedo = texture(uAlbedoTexture, UV);
        //         if (texAlbedo.a < 0.1)
        //             discard;
        //         albedo = texAlbedo.rgb;
        //     }

        //     vec3 diffuse = albedo * lightDirection * lightIntensity;

        //     vec4 color = vec4(ambient + diffuse, 1.0);
        //     FragColor = color;
        // }
        //             )";

        //             return CreateShaderFromSource(defaultSource);
        //         }

        static Shader *FullScreenQuad()
        {
            ShaderSource source;

            source.vertexSource = R"(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec2 aUV;

            out vec2 UV;

            void main()
            {
                vec2 vertices[3]=vec2[3](
                    vec2(-1.0, -1.0),
                    vec2(3.0, -1.0),
                    vec2(-1.0, 3.0)
                );
                gl_Position = vec4(vertices[gl_VertexID], 0.0, 1.0);
                UV = 0.5 * gl_Position.xy + vec2(0.5); // Map from [-1, 1] to [0, 1]
            }
        )";

            source.fragmentSource = R"(
            #version 330 core
            out vec4 FragColor;

            in vec2 UV;

            uniform sampler2D uColorTexture;
            uniform sampler2D uDepthTexture;

            void main()
            {
                FragColor = texture(uColorTexture, UV);
                
            }
        )";

            return CreateShaderFromSource(source);
        }

        static Shader *CreateGeometryPassShader()
        {
            ShaderSource source;

            source.vertexSource = R"(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec3 aNormal;
            layout(location = 2) in vec2 aUV;
            layout(location = 3) in vec4 aTangent;

            uniform mat4 uModel;
            uniform mat4 uPreviousModel;
            uniform mat4 uView;
            uniform mat4 uProjection;
            uniform mat4 uCurrentViewProjection;
            uniform mat4 uPreviousViewProjection;

            out vec3 FragPos;
            out vec3 Normal;
            out vec2 UV;
            out mat3 TBN;
            out vec4 CurrentClipPos;
            out vec4 PreviousClipPos;

            void main()
            {
                vec4 currentWorldPos = uModel * vec4(aPos, 1.0);
                vec4 previousWorldPos = uPreviousModel * vec4(aPos, 1.0);
                FragPos = currentWorldPos.xyz;
                mat3 normalMatrix = transpose(inverse(mat3(uModel)));
                vec3 worldNormal = normalize(normalMatrix * aNormal);
                vec3 worldTangent = normalize(normalMatrix * aTangent.xyz);
                worldTangent = normalize(worldTangent - dot(worldTangent, worldNormal) * worldNormal);
                vec3 worldBitangent = cross(worldNormal, worldTangent) * aTangent.w;

                Normal = worldNormal;
                UV = aUV;
                CurrentClipPos = uCurrentViewProjection * currentWorldPos;
                PreviousClipPos = uPreviousViewProjection * previousWorldPos;
                gl_Position = CurrentClipPos;
                TBN = mat3(
                    worldTangent,
                    normalize(worldBitangent),
                    worldNormal
                );
            }
        )";

            source.fragmentSource = R"(
            #version 330 core
            
            layout (location = 0) out vec3 gPosition;
            layout (location = 1) out vec4 gNormalRoughness;
            layout (location = 2) out vec4 gAlbedoMetallic;
            layout (location = 3) out vec2 gMotionVector;
            
            in vec3 FragPos;
            in vec3 Normal;
            in vec2 UV;
            in mat3 TBN;
            in vec4 CurrentClipPos;
            in vec4 PreviousClipPos;

            uniform sampler2D uAlbedoTexture;
            uniform float uHasAlbedoTexture = 0.0;
            uniform vec4 uColor = vec4(1.0, 1.0, 1.0, 1.0); // Placeholder color
            
            uniform sampler2D uNormalTexture;
            uniform float uHasNormalTexture = 0.0;
            uniform float uFlipNormalY = 0.0;

            uniform sampler2D uMetallicTexture;
            uniform float uHasMetallicTexture = 0.0;
            uniform float uMetallicFactor = 0.0;
            uniform int uMetallicTextureChannel = 0;
            
            uniform sampler2D uRoughnessTexture;
            uniform float uHasRoughnessTexture = 0.0;
            uniform float uRoughnessFactor = 1.0;
            uniform int uRoughnessTextureChannel = 0;

            float ReadTextureChannel(vec4 value, int channel)
            {
                if (channel == 1)
                {
                    return value.g;
                }

                if (channel == 2)
                {
                    return value.b;
                }

                if (channel == 3)
                {
                    return value.a;
                }

                return value.r;
            }
            
            void main()
            {
                gPosition = FragPos;
                vec3 albedo = uColor.rgb;
                float opacity = uColor.a;
                float metallic = clamp(uMetallicFactor, 0.0, 1.0);
                float roughness = clamp(uRoughnessFactor, 0.04, 1.0);
                vec3 normal = normalize(Normal);

                if (uHasAlbedoTexture > 0.5)
                {
                    vec4 texAlbedo = texture(uAlbedoTexture, UV);
                    opacity *= texAlbedo.a;
                    if (opacity < 0.1)
                        discard;
                    albedo *= texAlbedo.rgb;
                }
                
                if (uHasNormalTexture > 0.5)
                {
                    normal = texture(uNormalTexture, UV).rgb;
                    if (uFlipNormalY > 0.5)
                    {
                        normal.g = 1.0 - normal.g;
                    }
                    normal = normalize(normal * 2.0 - 1.0);
                    normal = normalize(TBN * normal); // Transform to world space
                }

                if (uHasMetallicTexture > 0.5)
                {
                    metallic *= ReadTextureChannel(texture(uMetallicTexture, UV), uMetallicTextureChannel);
                }

                if (uHasRoughnessTexture > 0.5)
                {
                    roughness *= ReadTextureChannel(texture(uRoughnessTexture, UV), uRoughnessTextureChannel);
                }

                gNormalRoughness = vec4(normalize(normal), clamp(roughness, 0.04, 1.0));
                gAlbedoMetallic = vec4(albedo, clamp(metallic, 0.0, 1.0));

                if (abs(CurrentClipPos.w) > 0.0001 && abs(PreviousClipPos.w) > 0.0001)
                {
                    vec2 currentUv = (CurrentClipPos.xy / CurrentClipPos.w) * 0.5 + 0.5;
                    vec2 previousUv = (PreviousClipPos.xy / PreviousClipPos.w) * 0.5 + 0.5;
                    gMotionVector = currentUv - previousUv;
                }
                else
                {
                    gMotionVector = vec2(0.0);
                }
            }
        )";

            return CreateShaderFromSource(source);
        }

        static Shader *CreateLightingPassShader()
        {
            ShaderSource source;

            source.vertexSource = R"(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec2 aUV;

            out vec2 UV;

            void main()
            {
                vec2 vertices[3]=vec2[3](
                    vec2(-1.0, -1.0),
                    vec2(3.0, -1.0),
                    vec2(-1.0, 3.0)
                );
                gl_Position = vec4(vertices[gl_VertexID], 0.0, 1.0);
                UV = 0.5 * gl_Position.xy + vec2(0.5); // Map from [-1, 1] to [0, 1]
            }
        )";

            source.fragmentSource = R"(
#version 330 core

out vec4 FragColor;
in vec2 UV;

uniform sampler2D gPosition;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoSpec;

const float PI = 3.14159265359;
const int PASS_MODE_AMBIENT = 0;
const int LIGHT_TYPE_POINT = 0;
const int LIGHT_TYPE_DIRECTIONAL = 1;
const int LIGHT_TYPE_SPOT = 2;
const int MAX_SHADOW_CASCADES = 4;

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
    mat4 CascadeLightSpaceMatrices[MAX_SHADOW_CASCADES];
    float CascadeSplits[MAX_SHADOW_CASCADES];
    int CascadeCount;
    float ShadowSoftness;
    float CascadeBlendDistance;
};

uniform int uPassMode;
uniform Light uLight;
uniform vec3 uViewPos;
uniform mat4 uViewMatrix;
uniform sampler2D uShadowMap2D;
uniform sampler2D uShadowCascadeMap0;
uniform sampler2D uShadowCascadeMap1;
uniform sampler2D uShadowCascadeMap2;
uniform sampler2D uShadowCascadeMap3;
uniform samplerCube uShadowMapCube;
uniform sampler2D uAoTexture;
uniform sampler3D uLpvVolume;
uniform sampler3D uPreviousLpvVolume;
uniform vec3 uLpvOrigin;
uniform vec3 uLpvSize;
uniform vec3 uPreviousLpvOrigin;
uniform vec3 uPreviousLpvSize;
uniform int uLpvEnabled;
uniform float uLpvTransitionBlend;
uniform int uAmbientOutputMode;

const int AMBIENT_OUTPUT_FULL = 0;
const int AMBIENT_OUTPUT_LPV_ONLY = 1;
const int AMBIENT_OUTPUT_NONE = 2;

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

float SampleShadowMapPCF(sampler2D shadowMap, vec3 projectedCoords, float depthBias, float softness)
{
    vec2 texelSize = 1.0 / vec2(textureSize(shadowMap, 0));
    float filterRadius = max(softness, 0.5);
    float shadow = 0.0;
    float totalWeight = 0.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize * filterRadius;
            vec2 sampleCoords = clamp(projectedCoords.xy + offset, vec2(0.0), vec2(1.0));
            float closestDepth = texture(shadowMap, sampleCoords).r;
            float weight = (x == 0 && y == 0) ? 4.0 : ((x == 0 || y == 0) ? 2.0 : 1.0);
            shadow += projectedCoords.z - depthBias > closestDepth ? weight : 0.0;
            totalWeight += weight;
        }
    }

    return shadow / max(totalWeight, 0.0001);
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

float ComputeDirectionalCascadeShadow(vec3 receiverPosition, Light light, int cascadeIndex, float depthBias)
{
    vec4 lightSpacePosition = light.CascadeLightSpaceMatrices[cascadeIndex] * vec4(receiverPosition, 1.0);
    vec3 projectedCoords = lightSpacePosition.xyz / max(lightSpacePosition.w, 0.0001);
    projectedCoords = projectedCoords * 0.5 + 0.5;

    if (projectedCoords.z < 0.0 || projectedCoords.z > 1.0 || projectedCoords.x < 0.0 || projectedCoords.x > 1.0 || projectedCoords.y < 0.0 || projectedCoords.y > 1.0)
    {
        return 0.0;
    }

    return SampleDirectionalCascadeShadow(cascadeIndex, projectedCoords, depthBias, light.ShadowSoftness);
}

float ComputeDirectionalShadow(vec3 fragPos, vec3 normal, Light light)
{
    if (light.CascadeCount <= 0)
    {
        return 0.0;
    }

    vec3 surfaceNormal = normalize(normal);
    vec3 lightVector = normalize(-light.Direction);
    float ndotl = max(dot(surfaceNormal, lightVector), 0.0);
    float normalBias = max(0.0015 * (1.0 - ndotl), 0.00015);
    vec3 receiverPosition = fragPos + surfaceNormal * normalBias;
    float viewDepth = abs((uViewMatrix * vec4(receiverPosition, 1.0)).z);

    if (viewDepth > light.CascadeSplits[light.CascadeCount - 1])
    {
        return 0.0;
    }

    int cascadeIndex = SelectDirectionalCascadeIndex(light, viewDepth);
    float depthBias = max(0.0002 + (1.0 - ndotl) * 0.00035, 0.00005);
    float shadow = ComputeDirectionalCascadeShadow(receiverPosition, light, cascadeIndex, depthBias);

    if (cascadeIndex < light.CascadeCount - 1)
    {
        float splitDistance = light.CascadeSplits[cascadeIndex];
        float blendStart = max(splitDistance - light.CascadeBlendDistance, 0.0);
        if (viewDepth > blendStart)
        {
            float nextDepthBias = max(0.0002 + (1.0 - ndotl) * 0.00035, 0.00005);
            float nextShadow = ComputeDirectionalCascadeShadow(receiverPosition, light, cascadeIndex + 1, nextDepthBias);
            float blendFactor = clamp((viewDepth - blendStart) / max(splitDistance - blendStart, 0.0001), 0.0, 1.0);
            shadow = mix(shadow, nextShadow, blendFactor);
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

float ComputeShadow(vec3 fragPos, vec3 normal, Light light)
{
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
        return ComputeDirectionalShadow(fragPos, normal, light);
    }

    return ComputeSpotShadow(fragPos, normal, light);
}

vec3 ComputeLightContribution(vec3 fragPos, vec3 normal, vec3 viewDir, vec3 albedo, float metallic, float roughness, Light light)
{
    vec3 lightDir;
    float attenuation = 1.0;

    if (light.Type == 0)
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

    float shadow = ComputeShadow(fragPos, normal, light);
    return EvaluatePbrLighting(normal, viewDir, albedo, metallic, roughness, lightDir, radiance) * (1.0 - shadow);
}

vec3 SampleLPVIndirect(vec3 fragPos, vec3 albedo, float metallic, float ao)
{
    if (uLpvEnabled == 0)
    {
        return vec3(0.0);
    }

    vec3 volumeSize = max(uLpvSize, vec3(0.0001));
    vec3 volumeUv = (fragPos - uLpvOrigin) / volumeSize;
    if (any(lessThan(volumeUv, vec3(0.0))) || any(greaterThan(volumeUv, vec3(1.0))))
    {
        return vec3(0.0);
    }

    float edgeDistance = min(
        min(min(volumeUv.x, volumeUv.y), volumeUv.z),
        min(min(1.0 - volumeUv.x, 1.0 - volumeUv.y), 1.0 - volumeUv.z));
    float edgeFade = smoothstep(0.0, 0.12, edgeDistance);
    vec3 indirectRadiance = texture(uLpvVolume, volumeUv).rgb;

    return indirectRadiance * albedo * (1.0 - metallic) * ao;
}

void main()
{
    vec3 fragPos = texture(gPosition, UV).rgb;
    vec4 normalRoughness = texture(gNormal, UV);
    vec4 albedoMetallic = texture(gAlbedoSpec, UV);

    if (dot(normalRoughness.rgb, normalRoughness.rgb) <= 0.000001)
    {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec3 normal = normalize(normalRoughness.rgb);
    vec3 albedo = albedoMetallic.rgb;
    float roughness = clamp(normalRoughness.a, 0.04, 1.0);
    float metallic = clamp(albedoMetallic.a, 0.0, 1.0);
    vec3 viewDir = normalize(uViewPos - fragPos);
    float ao = clamp(texture(uAoTexture, UV).r, 0.0, 1.0);

    if (uPassMode == PASS_MODE_AMBIENT)
    {
        vec3 lpvIndirect = SampleLPVIndirect(fragPos, albedo, metallic, ao);
        if (uAmbientOutputMode == AMBIENT_OUTPUT_NONE)
        {
            FragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }

        if (uAmbientOutputMode == AMBIENT_OUTPUT_LPV_ONLY)
        {
            FragColor = vec4(lpvIndirect, 1.0);
            return;
        }

        vec3 ambient = vec3(0.03) * albedo * (1.0 - metallic) * ao;
        ambient += lpvIndirect;
        FragColor = vec4(ambient, 1.0);
        return;
    }

    vec3 lighting = ComputeLightContribution(fragPos, normal, viewDir, albedo, metallic, roughness, uLight);
    FragColor = vec4(lighting, 1.0);
}
        )";

            return CreateShaderFromSource(source);
        }

        static Shader *CreatePostProcessShader()
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

            in vec2 UV;
            out vec4 FragColor;

            uniform sampler2D uSceneTexture;
            uniform sampler2D uSceneDepthTexture;
            uniform sampler2D uScenePositionTexture;
            uniform sampler2D uSceneNormalTexture;
            uniform sampler2D uSceneAlbedoTexture;
            uniform int uDebugViewMode;
            
            float LinearizeDepth(float depth)
            {
                float near = 0.1; // Match with camera near plane
                float far = 100.0; // Match with camera far plane
                float z = depth * 2.0 - 1.0; // Convert from [0, 1] to [-1, 1]
                return (2.0 * near * far) / (far + near - z * (far - near));
            }

            void main()
            {
                vec3 sceneColor = texture(uSceneTexture, UV).rgb;

                if (uDebugViewMode == 0)
                {
                    FragColor = vec4(sceneColor, 1.0);
                    return;
                }

                vec3 position = texture(uScenePositionTexture, UV).rgb;
                vec4 normalRoughness = texture(uSceneNormalTexture, UV);
                vec4 albedoMetallic = texture(uSceneAlbedoTexture, UV);
                float depth = texture(uSceneDepthTexture, UV).r;

                vec3 positionColor = abs(position) / (abs(position) + vec3(1.0));
                vec3 normalColor = normalize(normalRoughness.rgb) * 0.5 + 0.5;
                vec3 albedoColor = albedoMetallic.rgb;
                float depthColor = 1.0 - clamp(LinearizeDepth(depth) / 100.0, 0.0, 1.0);

                if (uDebugViewMode == 1)
                {
                    vec2 quadrantUV = fract(UV * 2.0);
                    vec2 borderDistance = min(quadrantUV, 1.0 - quadrantUV);
                    if (min(borderDistance.x, borderDistance.y) < 0.01)
                    {
                        FragColor = vec4(vec3(0.02), 1.0);
                        return;
                    }

                    position = texture(uScenePositionTexture, quadrantUV).rgb;
                    normalRoughness = texture(uSceneNormalTexture, quadrantUV);
                    albedoMetallic = texture(uSceneAlbedoTexture, quadrantUV);
                    depth = texture(uSceneDepthTexture, quadrantUV).r;

                    positionColor = abs(position) / (abs(position) + vec3(1.0));
                    normalColor = normalize(normalRoughness.rgb) * 0.5 + 0.5;
                    albedoColor = albedoMetallic.rgb;
                    depthColor = 1.0 - clamp(LinearizeDepth(depth) / 100.0, 0.0, 1.0);

                    vec3 quadrantColor;
                    if (UV.x < 0.5 && UV.y >= 0.5)
                    {
                        quadrantColor = positionColor;
                    }
                    else if (UV.x >= 0.5 && UV.y >= 0.5)
                    {
                        quadrantColor = normalColor;
                    }
                    else if (UV.x < 0.5 && UV.y < 0.5)
                    {
                        quadrantColor = albedoColor;
                    }
                    else
                    {
                        quadrantColor = vec3(depthColor);
                    }

                    FragColor = vec4(quadrantColor, 1.0);
                    return;
                }

                vec3 outputColor = sceneColor;
                if (uDebugViewMode == 2)
                {
                    outputColor = positionColor;
                }
                else if (uDebugViewMode == 3)
                {
                    outputColor = normalColor;
                }
                else if (uDebugViewMode == 4)
                {
                    outputColor = albedoColor;
                }
                else if (uDebugViewMode == 5)
                {
                    outputColor = vec3(depthColor);
                }

                FragColor = vec4(outputColor, 1.0);
            }
        )";

            return CreateShaderFromSource(source);
        }

        static Shader *CreateShadowPassShader()
        {
            ShaderSource source;

            source.vertexSource = R"(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec3 aNormal;
            layout(location = 2) in vec2 aUV;
            layout(location = 3) in vec4 aTangent;

            uniform mat4 uModel;
            uniform mat4 uLightSpaceMatrix;

            out vec3 FragPos;
            out vec2 UV;

            void main()
            {
                vec4 worldPosition = uModel * vec4(aPos, 1.0);
                FragPos = worldPosition.xyz;
                UV = aUV;
                gl_Position = uLightSpaceMatrix * worldPosition;
            }
        )";

            source.fragmentSource = R"(
            #version 330 core

            in vec3 FragPos;
            in vec2 UV;

            uniform sampler2D uAlbedoTexture;
            uniform float uHasAlbedoTexture = 0.0;
            uniform int uShadowPassMode = 0;
            uniform vec3 uLightPosition = vec3(0.0);
            uniform float uFarPlane = 1.0;

            void main()
            {
                if (uHasAlbedoTexture > 0.5)
                {
                    vec4 albedo = texture(uAlbedoTexture, UV);
                    if (albedo.a < 0.1)
                    {
                        discard;
                    }
                }

                if (uShadowPassMode == 1)
                {
                    float lightDistance = length(FragPos - uLightPosition);
                    gl_FragDepth = lightDistance / max(uFarPlane, 0.0001);
                    return;
                }

                gl_FragDepth = gl_FragCoord.z;
            }
        )";

            return CreateShaderFromSource(source);
        }

        void Bind() const
        {
            if (m_programID == 0)
            {
                std::cerr << "Error: Attempting to bind an uninitialized shader!" << std::endl;
                return;
            }
            glUseProgram(m_programID);
        }

        void Unbind() const
        {
            glUseProgram(0);
        }

        bool HasUniform(const std::string &name) const;
        void SetUniform(const std::string &name, const glm::mat4 &value) const;
        void SetUniform(const std::string &name, const glm::vec4 &value) const;
        void SetUniform(const std::string &name, const glm::vec3 &value) const;
        void SetUniform(const std::string &name, float value) const;
        void SetUniform(const std::string &name, int value) const;
        void SetUniform(const std::string &name, const Texture *texture, int slot) const;

    protected:
        friend class Graphics;
        GLuint GetProgramID() const { return m_programID; }

    private:
        static Shader *CreateShaderFromSource(const ShaderSource &source);
        GLint ResolveUniformLocation(const std::string &name, bool warnIfMissing) const;

        ShaderConfig m_config;
        GLuint m_programID = 0; // OpenGL shader program ID
        mutable std::unordered_map<std::string, GLint> m_uniformLocationCache;
        GLuint GetUniformLocation(const std::string &name) const;
    };
}