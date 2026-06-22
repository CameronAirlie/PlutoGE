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
            layout(location = 4) in vec2 aUV2;
            layout(location = 5) in mat4 aModel;
            layout(location = 9) in mat4 aPreviousModel;
            layout(location = 13) in vec4 aInstanceFlags;
            layout(location = 14) in ivec4 aJoints;
            layout(location = 15) in vec4 aWeights;

            uniform mat4 uView;
            uniform mat4 uProjection;
            uniform mat4 uCurrentViewProjection;
            uniform mat4 uPreviousViewProjection;
            uniform int uUseSkinning = 0;
            uniform mat4 uJointMatrices[48];

            out vec3 FragPos;
            out vec3 Normal;
            out vec2 UV;
            out vec2 UV2;
            out mat3 TBN;
            out vec4 CurrentClipPos;
            out vec4 PreviousClipPos;
            flat out vec4 InstanceFlags;

            void main()
            {
                mat4 skinMatrix = mat4(1.0);
                if (uUseSkinning != 0)
                {
                    float totalWeight = aWeights.x + aWeights.y + aWeights.z + aWeights.w;
                    if (totalWeight > 0.0001)
                    {
                        skinMatrix =
                            uJointMatrices[clamp(aJoints.x, 0, 47)] * aWeights.x +
                            uJointMatrices[clamp(aJoints.y, 0, 47)] * aWeights.y +
                            uJointMatrices[clamp(aJoints.z, 0, 47)] * aWeights.z +
                            uJointMatrices[clamp(aJoints.w, 0, 47)] * aWeights.w;
                    }
                }

                vec4 skinnedPosition = skinMatrix * vec4(aPos, 1.0);
                vec3 skinnedNormal = mat3(skinMatrix) * aNormal;
                vec3 skinnedTangent = mat3(skinMatrix) * aTangent.xyz;

                vec4 currentWorldPos = aModel * skinnedPosition;
                vec4 previousWorldPos = aPreviousModel * skinnedPosition;
                FragPos = currentWorldPos.xyz;
                mat3 normalMatrix = transpose(inverse(mat3(aModel)));
                vec3 worldNormal = normalize(normalMatrix * skinnedNormal);
                vec3 worldTangent = normalize(normalMatrix * skinnedTangent);
                worldTangent = normalize(worldTangent - dot(worldTangent, worldNormal) * worldNormal);
                vec3 worldBitangent = cross(worldNormal, worldTangent) * aTangent.w;

                Normal = worldNormal;
                UV = aUV;
                UV2 = aUV2;
                CurrentClipPos = uCurrentViewProjection * currentWorldPos;
                PreviousClipPos = uPreviousViewProjection * previousWorldPos;
                gl_Position = CurrentClipPos;
                InstanceFlags = aInstanceFlags;
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
            layout (location = 4) out vec4 gBakedLighting;
            layout (location = 5) out float gDebug;
            
            in vec3 FragPos;
            in vec3 Normal;
            in vec2 UV;
            in vec2 UV2;
            in mat3 TBN;
            in vec4 CurrentClipPos;
            in vec4 PreviousClipPos;
            flat in vec4 InstanceFlags;

            uniform sampler2D uAlbedoTexture;
            uniform float uHasAlbedoTexture = 0.0;
            uniform vec4 uColor = vec4(1.0, 1.0, 1.0, 1.0); // Placeholder color
            uniform int uAlphaMode = 0;
            uniform float uAlphaCutoff = 0.5;
            
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

            uniform sampler2D uLightmapTexture;
            uniform float uHasLightmapTexture = 0.0;

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
                    albedo *= texAlbedo.rgb;
                }

                if (uAlphaMode == 1 && opacity < uAlphaCutoff)
                {
                    discard;
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
                gBakedLighting = vec4(0.0);
                gDebug = InstanceFlags.w <= 0.5 ? -1.0 : clamp(InstanceFlags.z / InstanceFlags.w, 0.0, 1.0);

                if (InstanceFlags.x > 0.5 && uHasLightmapTexture > 0.5)
                {
                    vec2 lightmapUv = clamp(mix(UV2, UV, clamp(InstanceFlags.y, 0.0, 1.0)), vec2(0.0), vec2(1.0));
                    gBakedLighting = vec4(max(texture(uLightmapTexture, lightmapUv).rgb, vec3(0.0)), 1.0);
                }

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
uniform sampler2D gDepth;
uniform sampler2D gNormal;
uniform sampler2D gAlbedoSpec;
uniform sampler2D gBakedLighting;
uniform sampler2D gDebug;
uniform sampler2D uEnvironmentMap;

const float PI = 3.14159265359;
const int PASS_MODE_AMBIENT = 0;
const int LIGHT_TYPE_POINT = 0;
const int LIGHT_TYPE_DIRECTIONAL = 1;
const int LIGHT_TYPE_SPOT = 2;
const int MAX_SHADOW_CASCADES = 4;
const int DEBUG_VIEW_SHADOW_CASCADES = 6;
const int DEBUG_VIEW_LOD = 9;

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
    vec3 CascadeWorldOrigins[MAX_SHADOW_CASCADES];
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
uniform mat4 uInverseViewMatrix;
uniform mat4 uInverseProjectionMatrix;
uniform sampler2D uShadowMap2D;
uniform sampler2D uShadowCascadeMap0;
uniform sampler2D uShadowCascadeMap1;
uniform sampler2D uShadowCascadeMap2;
uniform sampler2D uShadowCascadeMap3;
uniform samplerCube uShadowMapCube;
uniform sampler3D uLpvVolume;
uniform sampler3D uPreviousLpvVolume;
uniform sampler3D uBakedProbeVolume;
uniform samplerCube uIblCaptureMaps[4];
uniform vec3 uLpvOrigin;
uniform vec3 uLpvSize;
uniform vec3 uPreviousLpvOrigin;
uniform vec3 uPreviousLpvSize;
uniform vec3 uBakedProbeOrigin;
uniform vec3 uBakedProbeSize;
uniform vec3 uIblCaptureOrigins[4];
uniform vec3 uIblCaptureSizes[4];
uniform int uLpvEnabled;
uniform int uBakedProbeEnabled;
uniform int uEnvironmentEnabled;
uniform int uIblCaptureEnabled[4];
uniform int uIblCaptureCount;
uniform float uLpvTransitionBlend;
uniform float uEnvironmentIntensity;
uniform float uEnvironmentMaxMipLevel;
uniform float uIblCaptureIntensities[4];
uniform float uIblCaptureBlendDistances[4];
uniform float uIblCaptureMaxMipLevels[4];
uniform int uAmbientOutputMode;
uniform int uDebugViewMode;

vec3 GetLodDebugColor(float normalizedLod)
{
    if (normalizedLod < 0.0)
    {
        return vec3(0.28, 0.28, 0.28);
    }

    float lod = clamp(normalizedLod, 0.0, 1.0);
    if (lod < 0.3333)
    {
        return mix(vec3(0.05, 0.28, 1.0), vec3(0.1, 0.85, 0.25), lod / 0.3333);
    }
    if (lod < 0.6667)
    {
        return mix(vec3(0.1, 0.85, 0.25), vec3(1.0, 0.78, 0.15), (lod - 0.3333) / 0.3334);
    }
    return mix(vec3(1.0, 0.78, 0.15), vec3(1.0, 0.1, 0.08), (lod - 0.6667) / 0.3333);
}

const int AMBIENT_OUTPUT_FULL = 0;
const int AMBIENT_OUTPUT_LPV_ONLY = 1;
const int AMBIENT_OUTPUT_NONE = 2;
const int MAX_IBL_CAPTURE_VOLUMES = 4;

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

vec3 FresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness)
{
    return f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec2 DirectionToEquirectangularUv(vec3 direction)
{
    const float invPi = 0.31830988618;
    const float invTwoPi = 0.15915494309;
    vec3 normalizedDirection = normalize(direction);
    vec2 uv = vec2(atan(normalizedDirection.z, normalizedDirection.x) * invTwoPi + 0.5,
                   acos(clamp(normalizedDirection.y, -1.0, 1.0)) * invPi);
    return uv;
}

vec3 SampleEnvironment(vec3 direction, float lod)
{
    if (uEnvironmentEnabled == 0)
    {
        return vec3(0.0);
    }

    vec2 uv = DirectionToEquirectangularUv(direction);
    return max(textureLod(uEnvironmentMap, uv, clamp(lod, 0.0, uEnvironmentMaxMipLevel)).rgb, vec3(0.0)) * uEnvironmentIntensity;
}

vec3 SampleIblCaptureMap(int captureIndex, vec3 direction, float lod)
{
    if (captureIndex == 0)
    {
        return max(textureLod(uIblCaptureMaps[0], direction, clamp(lod, 0.0, uIblCaptureMaxMipLevels[0])).rgb, vec3(0.0)) * uIblCaptureIntensities[0];
    }
    if (captureIndex == 1)
    {
        return max(textureLod(uIblCaptureMaps[1], direction, clamp(lod, 0.0, uIblCaptureMaxMipLevels[1])).rgb, vec3(0.0)) * uIblCaptureIntensities[1];
    }
    if (captureIndex == 2)
    {
        return max(textureLod(uIblCaptureMaps[2], direction, clamp(lod, 0.0, uIblCaptureMaxMipLevels[2])).rgb, vec3(0.0)) * uIblCaptureIntensities[2];
    }

    return max(textureLod(uIblCaptureMaps[3], direction, clamp(lod, 0.0, uIblCaptureMaxMipLevels[3])).rgb, vec3(0.0)) * uIblCaptureIntensities[3];
}

float ComputeIblCaptureWeight(vec3 fragPos, int captureIndex)
{
    if (captureIndex >= uIblCaptureCount || uIblCaptureEnabled[captureIndex] == 0)
    {
        return 0.0;
    }

    vec3 captureSize = max(uIblCaptureSizes[captureIndex], vec3(0.0001));
    vec3 captureUv = (fragPos - uIblCaptureOrigins[captureIndex]) / captureSize;
    if (any(lessThan(captureUv, vec3(0.0))) || any(greaterThan(captureUv, vec3(1.0))))
    {
        return 0.0;
    }

    vec3 halfSize = captureSize * 0.5;
    float minHalfExtent = max(min(min(halfSize.x, halfSize.y), halfSize.z), 0.0001);
    float normalizedBlendDistance = clamp(uIblCaptureBlendDistances[captureIndex] / minHalfExtent, 0.0, 1.0);
    vec3 edgeDistance = min(captureUv, vec3(1.0) - captureUv);
    float normalizedEdgeDistance = min(min(edgeDistance.x, edgeDistance.y), edgeDistance.z) * 2.0;
    return normalizedBlendDistance <= 0.0001 ? 1.0 : smoothstep(0.0, normalizedBlendDistance, normalizedEdgeDistance);
}

vec3 BoxProjectIblDirection(vec3 fragPos, vec3 direction, int captureIndex)
{
    vec3 boxMin = uIblCaptureOrigins[captureIndex];
    vec3 boxMax = uIblCaptureOrigins[captureIndex] + max(uIblCaptureSizes[captureIndex], vec3(0.0001));
    vec3 captureCenter = (boxMin + boxMax) * 0.5;
    vec3 safeDirection = direction;
    safeDirection.x = abs(safeDirection.x) < 0.0001 ? (safeDirection.x < 0.0 ? -0.0001 : 0.0001) : safeDirection.x;
    safeDirection.y = abs(safeDirection.y) < 0.0001 ? (safeDirection.y < 0.0 ? -0.0001 : 0.0001) : safeDirection.y;
    safeDirection.z = abs(safeDirection.z) < 0.0001 ? (safeDirection.z < 0.0 ? -0.0001 : 0.0001) : safeDirection.z;
    vec3 firstPlane = (boxMax - fragPos) / safeDirection;
    vec3 secondPlane = (boxMin - fragPos) / safeDirection;
    vec3 farPlane = max(firstPlane, secondPlane);
    float distanceToBox = min(min(farPlane.x, farPlane.y), farPlane.z);
    vec3 hitPosition = fragPos + direction * max(distanceToBox, 0.0);
    return normalize(hitPosition - captureCenter);
}

vec3 SampleIblEnvironment(vec3 fragPos, vec3 direction, float lod, bool useBoxProjection)
{
    vec3 capturedColor = vec3(0.0);
    float totalWeight = 0.0;
    for (int captureIndex = 0; captureIndex < MAX_IBL_CAPTURE_VOLUMES; ++captureIndex)
    {
        float weight = ComputeIblCaptureWeight(fragPos, captureIndex);
        if (weight <= 0.0)
        {
            continue;
        }

        vec3 sampleDirection = useBoxProjection ? BoxProjectIblDirection(fragPos, direction, captureIndex) : direction;
        capturedColor += SampleIblCaptureMap(captureIndex, sampleDirection, lod) * weight;
        totalWeight += weight;
    }

    float captureCoverage = clamp(totalWeight, 0.0, 1.0);
    vec3 blendedCaptureColor = totalWeight > 0.0001 ? capturedColor / totalWeight : vec3(0.0);
    return blendedCaptureColor * captureCoverage + SampleEnvironment(direction, lod) * (1.0 - captureCoverage);
}

vec3 ComputeSkyColor(vec2 uv)
{
    vec2 clip = uv * 2.0 - 1.0;
    vec4 viewDirection = uInverseProjectionMatrix * vec4(clip, 1.0, 1.0);
    vec3 worldDirection = normalize((uInverseViewMatrix * vec4(normalize(viewDirection.xyz / max(viewDirection.w, 0.0001)), 0.0)).xyz);
    return SampleEnvironment(worldDirection, 0.0);
}

vec3 EnvBRDFApprox(vec3 specularColor, float roughness, float ndotv)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * ndotv)) * r.x + r.y;
    vec2 ab = vec2(-1.04, 1.04) * a004 + r.zw;
    return specularColor * ab.x + ab.y;
}

vec3 ComputeEnvironmentDiffuse(vec3 fragPos, vec3 normal, vec3 albedo, float metallic, float roughness, vec3 f0, float ndotv)
{
    vec3 diffuseIrradiance = SampleIblEnvironment(fragPos, normal, max(uEnvironmentMaxMipLevel - 1.0, 0.0), false);
    vec3 fresnel = FresnelSchlickRoughness(ndotv, f0, roughness);
    vec3 kD = (vec3(1.0) - fresnel) * (1.0 - metallic);
    return diffuseIrradiance * albedo * kD;
}

vec3 ComputeEnvironmentSpecular(vec3 fragPos, vec3 normal, vec3 viewDir, float roughness, vec3 f0, float ndotv)
{
    vec3 reflectionDir = reflect(-viewDir, normal);
    vec3 prefilteredColor = SampleIblEnvironment(fragPos, reflectionDir, roughness * uEnvironmentMaxMipLevel, true);
    return prefilteredColor * EnvBRDFApprox(f0, roughness, ndotv);
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
    float closestDepth = texture(shadowMap, projectedCoords.xy).r;
    return projectedCoords.z - depthBias > closestDepth ? 1.0 : 0.0;
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
    vec4 lightSpacePosition = light.CascadeLightSpaceMatrices[cascadeIndex] * vec4(receiverPosition - light.CascadeWorldOrigins[cascadeIndex], 1.0);
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

float ComputeDirectionalShadow(vec3 fragPos,
                               vec3 normal,
                               Light light,
                               out int selectedCascadeIndex,
                               out int sampledCascadeIndex,
                               out bool selectedCascadeHasCoverage,
                               out bool usedCascadeFallback,
                               out bool hasAnyCascadeCoverage)
{
    selectedCascadeIndex = -1;
    sampledCascadeIndex = -1;
    selectedCascadeHasCoverage = false;
    usedCascadeFallback = false;
    hasAnyCascadeCoverage = false;

    if (light.CascadeCount <= 0)
    {
        return 0.0;
    }

    vec3 surfaceNormal = normalize(normal);
    vec3 lightVector = normalize(-light.Direction);
    float ndotl = max(dot(surfaceNormal, lightVector), 0.0);
    float normalBias = max(0.004 * (1.0 - ndotl), 0.00075);
    vec3 receiverPosition = fragPos + surfaceNormal * normalBias;
    float viewDepth = abs((uViewMatrix * vec4(fragPos, 1.0)).z);

    if (viewDepth > light.CascadeSplits[light.CascadeCount - 1])
    {
        return 0.0;
    }

    int cascadeIndex = SelectDirectionalCascadeIndex(light, viewDepth);
    selectedCascadeIndex = cascadeIndex;
    float baseDepthBias = max(0.00012 + (1.0 - ndotl) * 0.00035, 0.00004);
    bool hasCascadeCoverage = false;
    float shadow = 0.0;
    int shadowCascadeIndex = cascadeIndex;
    vec3 selectedCascadeCoords;
    selectedCascadeHasCoverage = ProjectDirectionalCascadeCoords(receiverPosition, light, cascadeIndex, selectedCascadeCoords);

    for (int sampleCascadeIndex = cascadeIndex; sampleCascadeIndex < light.CascadeCount; ++sampleCascadeIndex)
    {
        float cascadeBiasScale = clamp(light.CascadeSplits[sampleCascadeIndex] / max(light.CascadeSplits[0], 0.0001), 1.0, 8.0);
        float depthBias = baseDepthBias * cascadeBiasScale;
        shadow = ComputeDirectionalCascadeShadow(receiverPosition, light, sampleCascadeIndex, depthBias, hasCascadeCoverage);
        if (hasCascadeCoverage)
        {
            shadowCascadeIndex = sampleCascadeIndex;
            sampledCascadeIndex = sampleCascadeIndex;
            hasAnyCascadeCoverage = true;
            usedCascadeFallback = sampleCascadeIndex != cascadeIndex;
            break;
        }
    }

    if (!hasCascadeCoverage)
    {
        return 0.0;
    }

    if (shadowCascadeIndex < light.CascadeCount - 1)
    {
        float splitDistance = light.CascadeSplits[shadowCascadeIndex];
        float blendStart = max(splitDistance - light.CascadeBlendDistance, 0.0);
        if (viewDepth > blendStart)
        {
            bool hasNextCascadeCoverage = false;
            float nextCascadeBiasScale = clamp(light.CascadeSplits[shadowCascadeIndex + 1] / max(light.CascadeSplits[0], 0.0001), 1.0, 8.0);
            float nextShadow = ComputeDirectionalCascadeShadow(receiverPosition, light, shadowCascadeIndex + 1, baseDepthBias * nextCascadeBiasScale, hasNextCascadeCoverage);
            if (hasNextCascadeCoverage)
            {
                float blendFactor = clamp((viewDepth - blendStart) / max(splitDistance - blendStart, 0.0001), 0.0, 1.0);
                shadow = mix(shadow, nextShadow, blendFactor);
            }
        }
    }

    return shadow;
}

float ComputeDirectionalShadow(vec3 fragPos, vec3 normal, Light light)
{
    int selectedCascadeIndex;
    int sampledCascadeIndex;
    bool selectedCascadeHasCoverage;
    bool usedCascadeFallback;
    bool hasAnyCascadeCoverage;
    return ComputeDirectionalShadow(
        fragPos,
        normal,
        light,
        selectedCascadeIndex,
        sampledCascadeIndex,
        selectedCascadeHasCoverage,
        usedCascadeFallback,
        hasAnyCascadeCoverage);
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
		)";

            source.fragmentSource += R"(

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

    if (uDebugViewMode == DEBUG_VIEW_SHADOW_CASCADES)
    {
        if (light.Type != LIGHT_TYPE_DIRECTIONAL || light.CastsShadows == 0)
        {
            return vec3(0.0);
        }

        int selectedCascadeIndex;
        int sampledCascadeIndex;
        bool selectedCascadeHasCoverage;
        bool usedCascadeFallback;
        bool hasAnyCascadeCoverage;
        ComputeDirectionalShadow(
            fragPos,
            normal,
            light,
            selectedCascadeIndex,
            sampledCascadeIndex,
            selectedCascadeHasCoverage,
            usedCascadeFallback,
            hasAnyCascadeCoverage);

        if (!hasAnyCascadeCoverage)
        {
            return vec3(1.0, 0.0, 0.0);
        }

        vec3 debugColor = GetDirectionalCascadeDebugColor(sampledCascadeIndex);
        if (!selectedCascadeHasCoverage)
        {
            return mix(debugColor, vec3(1.0, 0.0, 1.0), 0.7);
        }

        if (usedCascadeFallback)
        {
            return mix(debugColor, vec3(1.0), 0.45);
        }

        return debugColor;
    }

    float shadow = ComputeShadow(fragPos, normal, light);
    return EvaluatePbrLighting(normal, viewDir, albedo, metallic, roughness, lightDir, radiance) * (1.0 - shadow);
}

vec3 SampleLPVIndirect(vec3 fragPos, vec3 albedo, float metallic)
{
    if (uLpvEnabled == 0)
    {
        return vec3(0.0);
    }

    float currentSampleWeight = 0.0;
    vec3 currentRadiance = vec3(0.0);
    vec3 currentVolumeSize = max(uLpvSize, vec3(0.0001));
    vec3 currentVolumeUv = (fragPos - uLpvOrigin) / currentVolumeSize;
    if (!any(lessThan(currentVolumeUv, vec3(0.0))) && !any(greaterThan(currentVolumeUv, vec3(1.0))))
    {
        float currentEdgeDistance = min(
            min(min(currentVolumeUv.x, currentVolumeUv.y), currentVolumeUv.z),
            min(min(1.0 - currentVolumeUv.x, 1.0 - currentVolumeUv.y), 1.0 - currentVolumeUv.z));
        currentSampleWeight = smoothstep(0.0, 0.12, currentEdgeDistance);
        currentRadiance = texture(uLpvVolume, currentVolumeUv).rgb * currentSampleWeight;
    }

    float previousSampleWeight = 0.0;
    vec3 previousRadiance = vec3(0.0);
    vec3 previousVolumeSize = max(uPreviousLpvSize, vec3(0.0001));
    vec3 previousVolumeUv = (fragPos - uPreviousLpvOrigin) / previousVolumeSize;
    if (!any(lessThan(previousVolumeUv, vec3(0.0))) && !any(greaterThan(previousVolumeUv, vec3(1.0))))
    {
        float previousEdgeDistance = min(
            min(min(previousVolumeUv.x, previousVolumeUv.y), previousVolumeUv.z),
            min(min(1.0 - previousVolumeUv.x, 1.0 - previousVolumeUv.y), 1.0 - previousVolumeUv.z));
        previousSampleWeight = smoothstep(0.0, 0.12, previousEdgeDistance);
        previousRadiance = texture(uPreviousLpvVolume, previousVolumeUv).rgb * previousSampleWeight;
    }

    vec3 indirectRadiance = currentRadiance;
    if (previousSampleWeight > 0.0)
    {
        float transitionBlend = clamp(uLpvTransitionBlend, 0.0, 1.0);
        if (currentSampleWeight > 0.0)
        {
            indirectRadiance = mix(previousRadiance, currentRadiance, transitionBlend);
        }
        else
        {
            indirectRadiance = previousRadiance * (1.0 - transitionBlend);
        }
    }

    return indirectRadiance * albedo * (1.0 - metallic);
}

vec3 SampleBakedProbeIrradiance(vec3 fragPos)
{
    if (uBakedProbeEnabled == 0)
    {
        return vec3(0.0);
    }

    vec3 probeSize = max(uBakedProbeSize, vec3(0.0001));
    vec3 probeUv = (fragPos - uBakedProbeOrigin) / probeSize;
    if (any(lessThan(probeUv, vec3(0.0))) || any(greaterThan(probeUv, vec3(1.0))))
    {
        return vec3(0.0);
    }

    return max(texture(uBakedProbeVolume, probeUv).rgb, vec3(0.0));
}
		)";

            source.fragmentSource += R"(

void main()
{
    vec3 fragPos = texture(gPosition, UV).rgb;
    float depth = texture(gDepth, UV).r;
    vec4 normalRoughness = texture(gNormal, UV);
    vec4 albedoMetallic = texture(gAlbedoSpec, UV);

    if (depth >= 0.999999 || dot(normalRoughness.rgb, normalRoughness.rgb) <= 0.000001)
    {
        if (uPassMode == PASS_MODE_AMBIENT)
        {
            FragColor = vec4(ComputeSkyColor(UV), 1.0);
        }
        else
        {
            FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        }
        return;
    }

    vec3 normal = normalize(normalRoughness.rgb);
    vec3 albedo = albedoMetallic.rgb;
    float roughness = clamp(normalRoughness.a, 0.04, 1.0);
    float metallic = clamp(albedoMetallic.a, 0.0, 1.0);
    vec4 bakedLightingMask = texture(gBakedLighting, UV);
    vec3 bakedIrradiance = bakedLightingMask.rgb;
    float bakedStaticMask = bakedLightingMask.a;
    if (uDebugViewMode == DEBUG_VIEW_LOD)
    {
        FragColor = vec4(GetLodDebugColor(texture(gDebug, UV).r), 1.0);
        return;
    }
    vec3 viewDir = normalize(uViewPos - fragPos);
    float ndotv = max(dot(normal, viewDir), 0.0);
    vec3 f0 = mix(vec3(0.04), albedo, metallic);

    if (uPassMode == PASS_MODE_AMBIENT)
    {
        if (uDebugViewMode == DEBUG_VIEW_SHADOW_CASCADES)
        {
            FragColor = vec4(0.0, 0.0, 0.0, 1.0);
            return;
        }

        vec3 lpvIndirect = SampleLPVIndirect(fragPos, albedo, metallic);
        vec3 bakedProbeIndirect = SampleBakedProbeIrradiance(fragPos) * albedo * (1.0 - metallic);
        vec3 environmentDiffuse = ComputeEnvironmentDiffuse(fragPos, normal, albedo, metallic, roughness, f0, ndotv);
        vec3 environmentSpecular = ComputeEnvironmentSpecular(fragPos, normal, viewDir, roughness, f0, ndotv);
        if (uAmbientOutputMode == AMBIENT_OUTPUT_NONE)
        {
            FragColor = vec4(environmentSpecular, 1.0);
            return;
        }

        if (uAmbientOutputMode == AMBIENT_OUTPUT_LPV_ONLY)
        {
            FragColor = vec4(lpvIndirect + bakedProbeIndirect + environmentSpecular, 1.0);
            return;
        }

        vec3 realtimeAmbient = vec3(0.03) * albedo * (1.0 - metallic);
        realtimeAmbient += lpvIndirect;
        realtimeAmbient += bakedProbeIndirect;
        realtimeAmbient += environmentDiffuse;
        vec3 ambient = mix(realtimeAmbient, bakedIrradiance * albedo * (1.0 - metallic), bakedStaticMask);
        ambient += environmentSpecular;
        FragColor = vec4(ambient, 1.0);
        return;
    }

    if (uLight.IsStatic != 0 && bakedStaticMask > 0.5)
    {
        FragColor = vec4(0.0, 0.0, 0.0, 1.0);
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
            layout(location = 5) in mat4 aModel;
            layout(location = 14) in ivec4 aJoints;
            layout(location = 15) in vec4 aWeights;

            uniform mat4 uLightSpaceMatrix;
            uniform vec3 uShadowWorldOrigin = vec3(0.0);
            uniform int uUseSkinning = 0;
            uniform mat4 uJointMatrices[48];

            out vec3 FragPos;
            out vec2 UV;

            void main()
            {
                mat4 skinMatrix = mat4(1.0);
                if (uUseSkinning != 0)
                {
                    float totalWeight = aWeights.x + aWeights.y + aWeights.z + aWeights.w;
                    if (totalWeight > 0.0001)
                    {
                        skinMatrix =
                            uJointMatrices[clamp(aJoints.x, 0, 47)] * aWeights.x +
                            uJointMatrices[clamp(aJoints.y, 0, 47)] * aWeights.y +
                            uJointMatrices[clamp(aJoints.z, 0, 47)] * aWeights.z +
                            uJointMatrices[clamp(aJoints.w, 0, 47)] * aWeights.w;
                    }
                }

                vec4 worldPosition = aModel * (skinMatrix * vec4(aPos, 1.0));
                FragPos = worldPosition.xyz;
                UV = aUV;
                gl_Position = uLightSpaceMatrix * vec4(worldPosition.xyz - uShadowWorldOrigin, 1.0);
            }
        )";

            source.fragmentSource = R"(
            #version 330 core

            in vec3 FragPos;
            in vec2 UV;

            uniform sampler2D uAlbedoTexture;
            uniform float uHasAlbedoTexture = 0.0;
            uniform float uAlphaCutoff = 0.5;
            uniform int uShadowPassMode = 0;
            uniform vec3 uLightPosition = vec3(0.0);
            uniform float uFarPlane = 1.0;

            void main()
            {
                if (uHasAlbedoTexture > 0.5)
                {
                    vec4 albedo = texture(uAlbedoTexture, UV);
                    if (albedo.a < uAlphaCutoff)
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

        static Shader *CreateTransparentPassShader()
        {
            ShaderSource source;

            source.vertexSource = R"(
            #version 330 core
            layout(location = 0) in vec3 aPos;
            layout(location = 1) in vec3 aNormal;
            layout(location = 2) in vec2 aUV;
            layout(location = 3) in vec4 aTangent;
            layout(location = 5) in mat4 aModel;
            layout(location = 14) in ivec4 aJoints;
            layout(location = 15) in vec4 aWeights;

            uniform mat4 uView;
            uniform mat4 uProjection;
            uniform int uUseSkinning = 0;
            uniform mat4 uJointMatrices[48];

            out vec3 FragPos;
            out vec3 Normal;
            out vec2 UV;
            out mat3 TBN;
            out vec4 ClipPos;

            void main()
            {
                mat4 skinMatrix = mat4(1.0);
                if (uUseSkinning != 0)
                {
                    float totalWeight = aWeights.x + aWeights.y + aWeights.z + aWeights.w;
                    if (totalWeight > 0.0001)
                    {
                        skinMatrix =
                            uJointMatrices[clamp(aJoints.x, 0, 47)] * aWeights.x +
                            uJointMatrices[clamp(aJoints.y, 0, 47)] * aWeights.y +
                            uJointMatrices[clamp(aJoints.z, 0, 47)] * aWeights.z +
                            uJointMatrices[clamp(aJoints.w, 0, 47)] * aWeights.w;
                    }
                }

                vec4 skinnedPosition = skinMatrix * vec4(aPos, 1.0);
                vec3 skinnedNormal = mat3(skinMatrix) * aNormal;
                vec3 skinnedTangent = mat3(skinMatrix) * aTangent.xyz;
                vec4 worldPosition = aModel * skinnedPosition;
                mat3 normalMatrix = transpose(inverse(mat3(aModel)));
                vec3 worldNormal = normalize(normalMatrix * skinnedNormal);
                vec3 worldTangent = normalize(normalMatrix * skinnedTangent);
                worldTangent = normalize(worldTangent - dot(worldTangent, worldNormal) * worldNormal);
                vec3 worldBitangent = cross(worldNormal, worldTangent) * aTangent.w;

                FragPos = worldPosition.xyz;
                Normal = worldNormal;
                UV = aUV;
                TBN = mat3(worldTangent, normalize(worldBitangent), worldNormal);
                ClipPos = uProjection * uView * worldPosition;
                gl_Position = ClipPos;
            }
        )";

            source.fragmentSource = R"(
            #version 330 core
            out vec4 FragColor;

            in vec3 FragPos;
            in vec3 Normal;
            in vec2 UV;
            in mat3 TBN;
            in vec4 ClipPos;

            uniform sampler2D uAlbedoTexture;
            uniform float uHasAlbedoTexture = 0.0;
            uniform vec4 uColor = vec4(1.0);
            uniform int uSurfaceType = 0;
            uniform float uAlphaCutoff = 0.01;
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
            uniform float uTransmissionFactor = 0.0;
            uniform float uIor = 1.45;
            uniform float uThickness = 0.01;
            uniform vec3 uAttenuationColor = vec3(1.0);
            uniform float uAttenuationDistance = 1.0;
            uniform vec3 uViewPos;
            uniform mat4 uView;
            uniform sampler2D uSceneColorTexture;
            uniform int uSceneColorEnabled = 0;
            uniform vec2 uSceneColorTextureSize = vec2(1.0);
            uniform float uSceneColorMaxMipLevel = 0.0;
            uniform sampler2D uEnvironmentMap;
            uniform samplerCube uIblCaptureMaps[4];
            uniform vec3 uIblCaptureOrigins[4];
            uniform vec3 uIblCaptureSizes[4];
            uniform int uEnvironmentEnabled = 0;
            uniform int uIblCaptureEnabled[4];
            uniform int uIblCaptureCount = 0;
            uniform float uEnvironmentIntensity = 1.0;
            uniform float uEnvironmentMaxMipLevel = 0.0;
            uniform float uIblCaptureIntensities[4];
            uniform float uIblCaptureBlendDistances[4];
            uniform float uIblCaptureMaxMipLevels[4];
            struct TransparentLight
            {
                vec3 Position;
                vec3 Color;
                float Intensity;
                float Range;
                vec3 Direction;
                int Type;
            };
            uniform int uLightCount = 0;
            uniform TransparentLight uLights[16];

            const float PI = 3.14159265359;
            const int SURFACE_STANDARD = 0;
            const int SURFACE_GLASS = 1;
            const int LIGHT_TYPE_POINT = 0;
            const int LIGHT_TYPE_DIRECTIONAL = 1;
            const int LIGHT_TYPE_SPOT = 2;

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

            vec3 FresnelSchlickRoughness(float cosTheta, vec3 f0, float roughness)
            {
                return f0 + (max(vec3(1.0 - roughness), f0) - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
            }

            vec3 FresnelSchlick(float cosTheta, vec3 f0)
            {
                return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
            }

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
                return GeometrySchlickGGX(max(dot(normal, viewDir), 0.0), roughness) *
                       GeometrySchlickGGX(max(dot(normal, lightDir), 0.0), roughness);
            }

            float ComputePointAttenuation(vec3 fragPos, TransparentLight light)
            {
                float distanceToLight = length(light.Position - fragPos);
                float normalizedDistance = light.Range > 0.0001 ? distanceToLight / light.Range : 1.0;
                float attenuation = clamp(1.0 - normalizedDistance, 0.0, 1.0);
                return attenuation * attenuation;
            }

            float ComputeSpotAttenuation(vec3 fragPos, vec3 lightDir, TransparentLight light)
            {
                float pointAttenuation = ComputePointAttenuation(fragPos, light);
                float spotCos = dot(-lightDir, normalize(light.Direction));
                float spotFactor = smoothstep(0.9, 0.975, spotCos);
                return pointAttenuation * spotFactor;
            }

            vec3 ComputeTransparentLightSpecular(vec3 fragPos, vec3 normal, vec3 viewDir, float roughness, vec3 f0)
            {
                vec3 specularLighting = vec3(0.0);
                for (int lightIndex = 0; lightIndex < 16; ++lightIndex)
                {
                    if (lightIndex >= uLightCount)
                    {
                        break;
                    }

                    TransparentLight light = uLights[lightIndex];
                    vec3 lightDir;
                    float attenuation = 1.0;
                    if (light.Type == LIGHT_TYPE_DIRECTIONAL)
                    {
                        lightDir = normalize(-light.Direction);
                    }
                    else
                    {
                        lightDir = normalize(light.Position - fragPos);
                        attenuation = light.Type == LIGHT_TYPE_SPOT
                                          ? ComputeSpotAttenuation(fragPos, lightDir, light)
                                          : ComputePointAttenuation(fragPos, light);
                    }

                    float ndotl = max(dot(normal, lightDir), 0.0);
                    if (ndotl <= 0.0001 || attenuation <= 0.0001)
                    {
                        continue;
                    }

                    vec3 halfwayDir = normalize(viewDir + lightDir);
                    vec3 fresnel = FresnelSchlick(max(dot(halfwayDir, viewDir), 0.0), f0);
                    float distribution = DistributionGGX(normal, halfwayDir, roughness);
                    float geometry = GeometrySmith(normal, viewDir, lightDir, roughness);
                    vec3 specular = distribution * geometry * fresnel / max(4.0 * max(dot(normal, viewDir), 0.0) * ndotl, 0.0001);
                    vec3 radiance = light.Color * light.Intensity * attenuation;
                    specularLighting += specular * radiance * ndotl;
                }

                return specularLighting;
            }

            vec2 DirectionToEquirectangularUv(vec3 direction)
            {
                const float invPi = 0.31830988618;
                const float invTwoPi = 0.15915494309;
                vec3 normalizedDirection = normalize(direction);
                return vec2(atan(normalizedDirection.z, normalizedDirection.x) * invTwoPi + 0.5,
                            acos(clamp(normalizedDirection.y, -1.0, 1.0)) * invPi);
            }

            vec3 SampleEnvironment(vec3 direction, float lod)
            {
                if (uEnvironmentEnabled == 0)
                {
                    return vec3(0.0);
                }

                return max(textureLod(uEnvironmentMap, DirectionToEquirectangularUv(direction), clamp(lod, 0.0, uEnvironmentMaxMipLevel)).rgb, vec3(0.0)) * uEnvironmentIntensity;
            }

            vec3 SampleIblCaptureMap(int captureIndex, vec3 direction, float lod)
            {
                if (captureIndex == 0)
                {
                    return max(textureLod(uIblCaptureMaps[0], direction, clamp(lod, 0.0, uIblCaptureMaxMipLevels[0])).rgb, vec3(0.0)) * uIblCaptureIntensities[0];
                }
                if (captureIndex == 1)
                {
                    return max(textureLod(uIblCaptureMaps[1], direction, clamp(lod, 0.0, uIblCaptureMaxMipLevels[1])).rgb, vec3(0.0)) * uIblCaptureIntensities[1];
                }
                if (captureIndex == 2)
                {
                    return max(textureLod(uIblCaptureMaps[2], direction, clamp(lod, 0.0, uIblCaptureMaxMipLevels[2])).rgb, vec3(0.0)) * uIblCaptureIntensities[2];
                }

                return max(textureLod(uIblCaptureMaps[3], direction, clamp(lod, 0.0, uIblCaptureMaxMipLevels[3])).rgb, vec3(0.0)) * uIblCaptureIntensities[3];
            }

            float ComputeIblCaptureWeight(vec3 fragPos, int captureIndex)
            {
                if (captureIndex >= uIblCaptureCount || uIblCaptureEnabled[captureIndex] == 0)
                {
                    return 0.0;
                }

                vec3 captureSize = max(uIblCaptureSizes[captureIndex], vec3(0.0001));
                vec3 captureUv = (fragPos - uIblCaptureOrigins[captureIndex]) / captureSize;
                if (any(lessThan(captureUv, vec3(0.0))) || any(greaterThan(captureUv, vec3(1.0))))
                {
                    return 0.0;
                }

                vec3 edgeDistance = min(captureUv, vec3(1.0) - captureUv);
                float minHalfExtent = max(min(min(captureSize.x, captureSize.y), captureSize.z) * 0.5, 0.0001);
                float normalizedEdgeDistance = min(min(edgeDistance.x * captureSize.x, edgeDistance.y * captureSize.y), edgeDistance.z * captureSize.z) / minHalfExtent;
                float normalizedBlendDistance = clamp(uIblCaptureBlendDistances[captureIndex] / minHalfExtent, 0.0, 1.0);
                return normalizedBlendDistance <= 0.0001 ? 1.0 : smoothstep(0.0, normalizedBlendDistance, normalizedEdgeDistance);
            }

            vec3 BoxProjectIblDirection(vec3 fragPos, vec3 direction, int captureIndex)
            {
                vec3 boxMin = uIblCaptureOrigins[captureIndex];
                vec3 boxMax = uIblCaptureOrigins[captureIndex] + max(uIblCaptureSizes[captureIndex], vec3(0.0001));
                vec3 safeDirection = mix(vec3(0.0001), direction, greaterThan(abs(direction), vec3(0.0001)));
                vec3 firstPlane = (boxMax - fragPos) / safeDirection;
                vec3 secondPlane = (boxMin - fragPos) / safeDirection;
                vec3 furthestPlane = max(firstPlane, secondPlane);
                float distanceToBox = min(min(furthestPlane.x, furthestPlane.y), furthestPlane.z);
                vec3 boxHit = fragPos + direction * max(distanceToBox, 0.0);
                vec3 boxCenter = (boxMin + boxMax) * 0.5;
                return normalize(boxHit - boxCenter);
            }

            vec3 SampleIblEnvironment(vec3 fragPos, vec3 direction, float lod)
            {
                vec3 capturedColor = vec3(0.0);
                float captureWeight = 0.0;
                for (int captureIndex = 0; captureIndex < 4; ++captureIndex)
                {
                    float weight = ComputeIblCaptureWeight(fragPos, captureIndex);
                    if (weight <= 0.0)
                    {
                        continue;
                    }

                    capturedColor += SampleIblCaptureMap(captureIndex, BoxProjectIblDirection(fragPos, direction, captureIndex), lod) * weight;
                    captureWeight += weight;
                }

                float captureCoverage = clamp(captureWeight, 0.0, 1.0);
                vec3 blendedCaptureColor = captureWeight > 0.0001 ? capturedColor / captureWeight : vec3(0.0);
                return blendedCaptureColor * captureCoverage + SampleEnvironment(direction, lod) * (1.0 - captureCoverage);
            }

            vec2 GetScreenUv()
            {
                if (abs(ClipPos.w) <= 0.0001)
                {
                    return vec2(0.5);
                }

                return ClipPos.xy / ClipPos.w * 0.5 + 0.5;
            }

            vec3 SampleSceneColor(vec2 uv, float lod)
            {
                if (uSceneColorEnabled == 0)
                {
                    return vec3(0.0);
                }

                vec2 clampedUv = clamp(uv, vec2(0.001), vec2(0.999));
                return max(textureLod(uSceneColorTexture, clampedUv, clamp(lod, 0.0, uSceneColorMaxMipLevel)).rgb, vec3(0.0));
            }

            void main()
            {
                vec4 color = uColor;
                if (uHasAlbedoTexture > 0.5)
                {
                    color *= texture(uAlbedoTexture, UV);
                }

                if (color.a <= uAlphaCutoff)
                {
                    discard;
                }

                vec3 normal = normalize(Normal);
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

                float metallic = clamp(uMetallicFactor, 0.0, 1.0);
                float roughness = clamp(uRoughnessFactor, 0.04, 1.0);
                if (uHasMetallicTexture > 0.5)
                {
                    metallic *= ReadTextureChannel(texture(uMetallicTexture, UV), uMetallicTextureChannel);
                }
                if (uHasRoughnessTexture > 0.5)
                {
                    roughness *= ReadTextureChannel(texture(uRoughnessTexture, UV), uRoughnessTextureChannel);
                }
                roughness = clamp(roughness, 0.04, 1.0);

                vec3 viewDir = normalize(uViewPos - FragPos);
                if (dot(normal, viewDir) < 0.0)
                {
                    normal = -normal;
                }
                float ndotv = max(dot(normal, viewDir), 0.0);
                vec3 f0 = mix(vec3(0.04), color.rgb, metallic);
                if (uSurfaceType == SURFACE_GLASS)
                {
                    float ior = clamp(uIor, 1.0, 2.5);
                    float reflectance = pow((ior - 1.0) / (ior + 1.0), 2.0);
                    f0 = vec3(clamp(reflectance, 0.0, 1.0));
                    metallic = 0.0;
                }
                vec3 fresnel = FresnelSchlickRoughness(ndotv, f0, roughness);
                vec3 reflectionDir = reflect(-viewDir, normal);
                vec3 environmentSpecular = SampleIblEnvironment(FragPos, reflectionDir, roughness * uEnvironmentMaxMipLevel) * fresnel;
                vec3 directSpecular = ComputeTransparentLightSpecular(FragPos, normal, viewDir, roughness, f0);

                float outputAlpha = color.a;
                vec3 baseColor = color.rgb;
                if (uSurfaceType == SURFACE_GLASS)
                {
                    float ior = clamp(uIor, 1.01, 2.5);
                    float transmission = clamp(uTransmissionFactor, 0.0, 1.0);
                    roughness = clamp(roughness, 0.02, 1.0);

                    float attenuationDistance = max(uAttenuationDistance, 0.0001);
                    float attenuationAmount = clamp(max(uThickness, 0.0) / attenuationDistance, 0.0, 1.0);
                    vec3 attenuationTint = mix(vec3(1.0), clamp(uAttenuationColor, vec3(0.0), vec3(1.0)), attenuationAmount);
                    vec3 glassTint = mix(vec3(1.0), color.rgb * attenuationTint, clamp(0.15 + attenuationAmount, 0.0, 1.0));

                    vec2 sceneUv = GetScreenUv();
                    vec3 viewNormal = normalize(mat3(uView) * normal);
                    float eta = 1.0 / ior;
                    float refractionStrength = (1.0 - eta) * transmission * mix(0.075, 0.025, roughness);
                    vec2 texelSize = 1.0 / max(uSceneColorTextureSize, vec2(1.0));
                    vec2 refractedUv = sceneUv + viewNormal.xy * refractionStrength;
                    refractedUv += texelSize * viewNormal.xy * 2.0;

                    float transmissionLod = roughness * uSceneColorMaxMipLevel * 0.65;
                    vec3 sceneTransmission = SampleSceneColor(refractedUv, transmissionLod);
                    if (uSceneColorEnabled == 0)
                    {
                        vec3 refractionDir = refract(-viewDir, normal, eta);
                        sceneTransmission = SampleIblEnvironment(FragPos, length(refractionDir) > 0.001 ? refractionDir : -viewDir, transmissionLod);
                    }
                    sceneTransmission *= glassTint;

                    vec3 reflectance = FresnelSchlick(ndotv, f0);
                    float fresnelAlpha = max(max(reflectance.r, reflectance.g), reflectance.b);
                    vec3 reflectedGlass = SampleIblEnvironment(FragPos, reflectionDir, roughness * uEnvironmentMaxMipLevel) * reflectance;
                    vec3 glassDirectSpecular = ComputeTransparentLightSpecular(FragPos, normal, viewDir, roughness, f0);
                    float reflectionWeight = clamp(fresnelAlpha + roughness * 0.08, 0.02, 0.95);
                    baseColor = mix(color.rgb * glassTint, sceneTransmission, transmission);
                    baseColor = mix(baseColor, reflectedGlass, reflectionWeight);
                    baseColor += glassDirectSpecular;
                    environmentSpecular = vec3(0.0);
                    directSpecular = vec3(0.0);
                    outputAlpha = uSceneColorEnabled != 0 ? 1.0 : clamp(max(color.a * (1.0 - transmission * 0.65), fresnelAlpha * 0.55), 0.04, color.a);
                }

                FragColor = vec4(baseColor + environmentSpecular + directSpecular, outputAlpha);
            }
        )";

            return CreateShaderFromSource(source);
        }

        void Bind() const;

        void Unbind() const;

        static void ResetStateCache();

        bool HasUniform(const std::string &name) const;
        void SetUniform(const std::string &name, const glm::mat4 &value) const;
        void SetUniform(const std::string &name, const glm::vec4 &value) const;
        void SetUniform(const std::string &name, const glm::vec3 &value) const;
        void SetUniform(const std::string &name, const glm::vec2 &value) const;
        void SetUniform(const std::string &name, float value) const;
        void SetUniform(const std::string &name, int value) const;
        void SetUniform(const std::string &name, const Texture *texture, int slot) const;
        bool TrySetUniform(const std::string &name, const glm::vec4 &value) const;
        bool TrySetUniform(const std::string &name, float value) const;
        bool TrySetUniform(const std::string &name, int value) const;
        bool TrySetUniform(const std::string &name, const Texture *texture, int slot) const;

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
