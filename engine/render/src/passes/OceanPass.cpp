#include "PlutoGE/render/passes/OceanPass.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
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

        bool PointInPolygon(const glm::vec2 &point, const scene::OceanAreaPolygon &area)
        {
            if (area.points.size() < 3)
                return false;

            bool inside = false;
            std::size_t previous = area.points.size() - 1;
            for (std::size_t current = 0; current < area.points.size(); ++current)
            {
                const glm::vec2 &a = area.points[current];
                const glm::vec2 &b = area.points[previous];
                if (((a.y > point.y) != (b.y > point.y)) &&
                    point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)
                    inside = !inside;
                previous = current;
            }
            return inside;
        }

        bool IsCameraInsideOcean(const OceanDraw &ocean, const glm::vec3 &cameraPosition)
        {
            const glm::vec3 localCamera = glm::vec3(ocean.inverseTransform * glm::vec4(cameraPosition, 1.0f));
            if (localCamera.y >= 0.0f)
                return false;

            bool insideArea = false;
            for (const auto &area : ocean.component->GetAreas())
                insideArea = insideArea || PointInPolygon(glm::vec2(localCamera.x, localCamera.z), area);

            if (ocean.component->GetAreas().empty())
                return true;
            return ocean.component->GetInvertAreaMask() ? insideArea : !insideArea;
        }

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
                uniform sampler2D uEnvironmentMap;
                uniform int uEnvironmentEnabled;
                uniform float uEnvironmentIntensity;
                uniform mat4 uInverseView;
                uniform mat4 uInverseProjection;
                uniform mat4 uView;
                uniform mat4 uProjection;
                uniform vec3 uCameraPosition;
                uniform mat4 uOceanTransform;
                uniform mat4 uInverseOceanTransform;
                uniform vec3 uShallowColor;
                uniform vec3 uDeepColor;
                uniform vec3 uFoamColor;
                uniform float uOpacity;
                uniform float uSmoothness;
                uniform float uMaxVisibilityDepth;
                uniform float uUnderwaterFadeStart;
                uniform float uUnderwaterFadeSoftness;
                uniform float uUnderwaterDepthFalloff;
                uniform float uUnderwaterLightFalloff;
                uniform float uUnderwaterTurbidity;
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
                uniform sampler2D uShadowCascadeMap0;
                uniform sampler2D uShadowCascadeMap1;
                uniform sampler2D uShadowCascadeMap2;
                uniform sampler2D uShadowCascadeMap3;
                uniform int uShadowCascadeCount;
                uniform vec3 uShadowCascadeWorldOrigins[4];
                uniform mat4 uShadowCascadeMatrices[4];
                uniform float uShadowCascadeSplits[4];
                uniform float uShadowSoftness;
                uniform int uDepthOnly;
                uniform int uUnderwater;

                const float PI = 3.14159265359;

                mat2 Rotation(float radians)
                {
                    float s = sin(radians);
                    float c = cos(radians);
                    return mat2(c, -s, s, c);
                }

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
                    mat2 rotation = Rotation(0.61);
                    for (int octave = 0; octave < 4; ++octave)
                    {
                        value += ValueNoise(p * frequency) * amplitude;
                        p = rotation * p + vec2(17.13, -9.47);
                        frequency *= 2.03;
                        amplitude *= 0.5;
                    }
                    return value;
                }

                float LayeredNoise(vec2 p, float phase)
                {
                    vec2 warpedP = Rotation(0.41) * p;
                    vec2 warp = vec2(
                        Fbm(warpedP * 0.73 + vec2(phase * 0.05, -phase * 0.03)),
                        Fbm(Rotation(-0.93) * warpedP * 0.91 + vec2(-phase * 0.04, phase * 0.06)));
                    vec2 flow = warpedP + warp * 1.9;
                    float low = Fbm(flow * 1.35 + vec2(phase * 0.07, phase * 0.05));
                    float detail = Fbm(Rotation(1.17) * flow * 4.9 + vec2(phase * 0.22, -phase * 0.19));
                    float ripple = Fbm(Rotation(-1.43) * flow * 11.2 + vec2(-phase * 0.48, phase * 0.43));
                    return low * 0.14 + detail * 0.06 + ripple * 0.025;
                }

                float GerstnerHeight(vec2 direction, vec2 position, float waveNumber, float amplitude, float phaseOffset)
                {
                    direction = normalize(direction);
                    float omega = sqrt(9.81 * waveNumber);
                    float phase = dot(position, direction) * waveNumber - omega * uSimulationTime * uWaveSpeed + phaseOffset;
                    return sin(phase) * amplitude;
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
                        float edgeDeltaY = previous.y - current.y;
                        bool intersects = ((current.y > point.y) != (previous.y > point.y)) &&
                                          (point.x < (previous.x - current.x) * (point.y - current.y) / edgeDeltaY + current.x);
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
                    if (uAreaCount <= 0)
                    {
                        return false;
                    }

                    bool insideAny = false;
                    for (int areaIndex = 0; areaIndex < uAreaCount; ++areaIndex)
                    {
                        insideAny = insideAny || PointInPolygon(point, areaIndex);
                    }

                    return uInvertAreaMask != 0 ? !insideAny : insideAny;
                }

                float WaveHeight(vec2 xz)
                {
                    float k = (2.0 * PI) / max(uWaveLength, 0.01);
                    float h = 0.0;
                    h += GerstnerHeight(vec2(0.94, 0.34), xz, k * 0.55, uWaveAmplitude * 0.48, 0.0);
                    h += GerstnerHeight(vec2(0.78, 0.63), xz, k * 0.82, uWaveAmplitude * 0.25, 1.7);
                    h += GerstnerHeight(vec2(0.99, -0.14), xz, k * 1.35, uWaveAmplitude * 0.14, 3.2);
                    h += GerstnerHeight(vec2(0.55, 0.84), xz, k * 2.10, uWaveAmplitude * 0.08, 0.8);
                    h += GerstnerHeight(vec2(-0.25, 0.97), xz, k * 3.40, uWaveAmplitude * 0.05, 4.1);
                    return h;
                }

                bool SolveWaveIntersection(vec3 rayOriginLocal, vec3 rayDirectionLocal, out vec3 localHit)
                {
                    // Parallel/upward rays cannot hit an infinite water plane.
                    if (rayDirectionLocal.y >= -0.0000001)
                    {
                        return false;
                    }

                    float flatT = -rayOriginLocal.y / rayDirectionLocal.y;
                    if (flatT <= 0.0)
                    {
                        return false;
                    }

                    // At grazing angles Newton corrections divide by a nearly
                    // horizontal ray and can jump behind the camera. Distant
                    // waves are sub-pixel anyway, so use the stable mean plane
                    // for the final strip approaching the horizon.
                    if (abs(rayDirectionLocal.y) < 0.002)
                    {
                        localHit = rayOriginLocal + rayDirectionLocal * flatT;
                        localHit.y = 0.0;
                        return true;
                    }

                    float t = flatT;

                    for (int iteration = 0; iteration < 6; ++iteration)
                    {
                        vec3 samplePoint = rayOriginLocal + rayDirectionLocal * t;
                        float surfaceDelta = samplePoint.y - WaveHeight(samplePoint.xz);
                        float correction = surfaceDelta / rayDirectionLocal.y;
                        correction = clamp(correction, -flatT * 0.25, flatT * 0.25);
                        t -= correction;
                    }

                    if (t <= 0.0)
                    {
                        t = flatT;
                    }

                    localHit = rayOriginLocal + rayDirectionLocal * t;
                    localHit.y = WaveHeight(localHit.xz);
                    return true;
                }

            )";
            source.fragmentSource += R"(

                vec2 RippleSlope(vec2 xz)
                {
                    // Small waves affect reflection/refraction without changing the
                    // ray-intersected silhouette. Analytic derivatives avoid the
                    // lumpy, temporally unstable normals produced by noise gradients.
                    float detailLength = clamp(uWaveLength * 0.10, 0.75, 2.5);
                    float baseK = (2.0 * PI) / detailLength;
                    float time = uSimulationTime * uWaveSpeed;
                    float strength = 0.018 * mix(0.75, 1.5, clamp(uWaveChoppiness * 0.25, 0.0, 1.0));
                    vec2 slope = vec2(0.0);

                    vec2 d0 = normalize(vec2(0.96, 0.28));
                    vec2 d1 = normalize(vec2(0.72, 0.69));
                    vec2 d2 = normalize(vec2(0.99, -0.12));
                    vec2 d3 = normalize(vec2(0.45, 0.89));
                    float p0 = dot(xz, d0) * baseK - time * 2.1;
                    float p1 = dot(xz, d1) * baseK * 1.73 - time * 2.7 + 1.3;
                    float p2 = dot(xz, d2) * baseK * 2.41 - time * 3.4 + 3.1;
                    float p3 = dot(xz, d3) * baseK * 3.17 - time * 4.0 + 0.6;

                    // Fade frequencies that approach pixel size to prevent distant
                    // ripples from sparkling and crawling as the camera moves.
                    float a0 = 1.0 - smoothstep(0.45, 1.25, fwidth(p0));
                    float a1 = 1.0 - smoothstep(0.45, 1.25, fwidth(p1));
                    float a2 = 1.0 - smoothstep(0.45, 1.25, fwidth(p2));
                    float a3 = 1.0 - smoothstep(0.45, 1.25, fwidth(p3));
                    slope += d0 * cos(p0) * strength * baseK * a0;
                    slope += d1 * cos(p1) * strength * 0.52 * baseK * 1.73 * a1;
                    slope += d2 * cos(p2) * strength * 0.27 * baseK * 2.41 * a2;
                    slope += d3 * cos(p3) * strength * 0.14 * baseK * 3.17 * a3;
                    return slope;
                }

                vec3 WaveNormal(vec2 xz)
                {
                    float offset = max(uWaveLength * 0.01, 0.025);
                    float center = WaveHeight(xz);
                    float hx = WaveHeight(xz + vec2(offset, 0.0)) - center;
                    float hz = WaveHeight(xz + vec2(0.0, offset)) - center;
                    vec2 slope = vec2(hx, hz) / offset + RippleSlope(xz);
                    return normalize(vec3(-slope.x, 1.0, -slope.y));
                }

                vec3 SampleEnvironment(vec3 direction)
                {
                    direction = normalize(direction);
                    vec2 uv = vec2(atan(direction.z, direction.x) / (2.0 * PI) + 0.5,
                                   asin(clamp(direction.y, -1.0, 1.0)) / PI + 0.5);
                    return texture(uEnvironmentMap, uv).rgb * uEnvironmentIntensity;
                }

                vec3 SampleEnvironmentDiffuse(vec3 normal)
                {
                    if (uEnvironmentEnabled == 0)
                    {
                        return vec3(0.0);
                    }

                    // A small cosine-weighted hemisphere approximation gives the
                    // water body the colour of its surroundings. Reflections alone
                    // only affect grazing angles and leave most of the surface
                    // looking like an unlit, constant-colour overlay.
                    vec3 up = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
                    vec3 tangent = normalize(cross(up, normal));
                    vec3 bitangent = cross(normal, tangent);
                    vec3 irradiance = SampleEnvironment(normal) * 0.40;
                    irradiance += SampleEnvironment(normalize(normal + tangent * 0.85)) * 0.15;
                    irradiance += SampleEnvironment(normalize(normal - tangent * 0.85)) * 0.15;
                    irradiance += SampleEnvironment(normalize(normal + bitangent * 0.85)) * 0.15;
                    irradiance += SampleEnvironment(normalize(normal - bitangent * 0.85)) * 0.15;
                    return max(irradiance, vec3(0.0));
                }

                bool ProjectWorldPosition(vec3 worldPosition, out vec2 uv)
                {
                    vec4 clip = uProjection * uView * vec4(worldPosition, 1.0);
                    if (clip.w <= 0.0001)
                    {
                        return false;
                    }

                    uv = clip.xy / clip.w * 0.5 + 0.5;
                    return all(greaterThanEqual(uv, vec2(0.0))) && all(lessThanEqual(uv, vec2(1.0)));
                }

                float ViewDepth(vec3 worldPosition)
                {
                    return -(uView * vec4(worldPosition, 1.0)).z;
                }

                float SampleShadowCascade(int cascadeIndex, vec3 projectedCoords, float bias)
                {
                    vec2 texelSize;
                    if (cascadeIndex == 0) texelSize = 1.0 / vec2(textureSize(uShadowCascadeMap0, 0));
                    else if (cascadeIndex == 1) texelSize = 1.0 / vec2(textureSize(uShadowCascadeMap1, 0));
                    else if (cascadeIndex == 2) texelSize = 1.0 / vec2(textureSize(uShadowCascadeMap2, 0));
                    else texelSize = 1.0 / vec2(textureSize(uShadowCascadeMap3, 0));

                    float shadow = 0.0;
                    float radius = clamp(uShadowSoftness, 0.5, 3.0);
                    for (int y = -1; y <= 1; ++y)
                    {
                        for (int x = -1; x <= 1; ++x)
                        {
                            vec2 uv = projectedCoords.xy + vec2(x, y) * texelSize * radius;
                            float closestDepth;
                            if (cascadeIndex == 0) closestDepth = texture(uShadowCascadeMap0, uv).r;
                            else if (cascadeIndex == 1) closestDepth = texture(uShadowCascadeMap1, uv).r;
                            else if (cascadeIndex == 2) closestDepth = texture(uShadowCascadeMap2, uv).r;
                            else closestDepth = texture(uShadowCascadeMap3, uv).r;
                            shadow += projectedCoords.z - bias > closestDepth ? 1.0 : 0.0;
                        }
                    }
                    return shadow / 9.0;
                }

                float DirectionalShadowVisibility(vec3 worldPosition, vec3 normal)
                {
                    if (uShadowCascadeCount <= 0)
                    {
                        return 1.0;
                    }

                    float cameraDepth = ViewDepth(worldPosition);
                    int cascadeIndex = uShadowCascadeCount - 1;
                    for (int index = 0; index < uShadowCascadeCount; ++index)
                    {
                        if (cameraDepth <= uShadowCascadeSplits[index])
                        {
                            cascadeIndex = index;
                            break;
                        }
                    }
                    if (cameraDepth > uShadowCascadeSplits[uShadowCascadeCount - 1])
                    {
                        return 1.0;
                    }

                    float ndotl = max(dot(normal, normalize(uSunDirection)), 0.0);
                    vec3 receiver = worldPosition + normal * max(0.004 * (1.0 - ndotl), 0.00075);
                    vec4 lightPosition = uShadowCascadeMatrices[cascadeIndex] *
                                         vec4(receiver - uShadowCascadeWorldOrigins[cascadeIndex], 1.0);
                    vec3 projected = lightPosition.xyz / max(lightPosition.w, 0.0001) * 0.5 + 0.5;
                    if (any(lessThan(projected, vec3(0.0))) || any(greaterThan(projected, vec3(1.0))))
                    {
                        return 1.0;
                    }
                    float biasScale = clamp(uShadowCascadeSplits[cascadeIndex] /
                                            max(uShadowCascadeSplits[0], 0.0001), 1.0, 8.0);
                    float bias = max(0.00012 + (1.0 - ndotl) * 0.00035, 0.00004) * biasScale;
                    return 1.0 - SampleShadowCascade(cascadeIndex, projected, bias);
                }

                bool TraceScreenSpaceReflection(vec3 worldHit, vec3 normal, vec3 viewVector, out vec2 reflectionUv, out float confidence)
                {
                    const int stepCount = 48;
                    const int binaryStepCount = 5;
                    const float maxDistance = 45.0;
                    const float thickness = 0.4;

                    vec3 reflectionDirection = normalize(reflect(-viewVector, normal));
                    vec3 rayOrigin = worldHit + normal * 0.08;
                    float previousTravel = 0.08;
                    float previousDepthDelta = -1.0;
                    float hitTravel = -1.0;

                    for (int stepIndex = 0; stepIndex < stepCount; ++stepIndex)
                    {
                        float fraction = (float(stepIndex) + 1.0) / float(stepCount);
                        float travel = 0.08 + maxDistance * fraction * fraction;
                        vec3 rayPosition = rayOrigin + reflectionDirection * travel;
                        vec2 rayUv;
                        if (!ProjectWorldPosition(rayPosition, rayUv))
                        {
                            break;
                        }

                        float sceneDepth = texture(uSceneDepth, rayUv).r;
                        if (sceneDepth <= 0.000001)
                        {
                            previousTravel = travel;
                            previousDepthDelta = -1.0;
                            continue;
                        }

                        vec3 sceneWorld = ReconstructWorldPosition(rayUv, sceneDepth);
                        float depthDelta = ViewDepth(rayPosition) - ViewDepth(sceneWorld);
                        float adaptiveThickness = thickness * (1.0 + travel / maxDistance);
                        bool crossedSurface = previousDepthDelta < 0.0 && depthDelta >= 0.0;
                        if (crossedSurface || (depthDelta >= 0.0 && depthDelta <= adaptiveThickness))
                        {
                            float low = previousTravel;
                            float high = travel;
                            for (int binaryIndex = 0; binaryIndex < binaryStepCount; ++binaryIndex)
                            {
                                float middle = (low + high) * 0.5;
                                vec3 middlePosition = rayOrigin + reflectionDirection * middle;
                                vec2 middleUv;
                                if (!ProjectWorldPosition(middlePosition, middleUv))
                                {
                                    high = middle;
                                    continue;
                                }

                                float middleDepth = texture(uSceneDepth, middleUv).r;
                                if (middleDepth <= 0.000001)
                                {
                                    low = middle;
                                    continue;
                                }

                                vec3 middleSceneWorld = ReconstructWorldPosition(middleUv, middleDepth);
                                float middleDelta = ViewDepth(middlePosition) - ViewDepth(middleSceneWorld);
                                if (middleDelta >= 0.0) high = middle;
                                else low = middle;
                            }

                            hitTravel = high;
                            if (!ProjectWorldPosition(rayOrigin + reflectionDirection * high, reflectionUv))
                            {
                                return false;
                            }
                            break;
                        }

                        previousTravel = travel;
                        previousDepthDelta = depthDelta;
                    }

                    if (hitTravel < 0.0)
                    {
                        return false;
                    }

                    float edgeDistance = min(min(reflectionUv.x, 1.0 - reflectionUv.x), min(reflectionUv.y, 1.0 - reflectionUv.y));
                    float edgeConfidence = smoothstep(0.0, 0.12, edgeDistance);
                    float distanceConfidence = 1.0 - smoothstep(maxDistance * 0.25, maxDistance, hitTravel);
                    confidence = edgeConfidence * distanceConfidence;
                    return confidence > 0.001;
                }

            )";
            source.fragmentSource += R"(
                void main()
                {
                    vec3 viewDirection = WorldDirection(vUv);
                    vec3 rayOriginLocal = (uInverseOceanTransform * vec4(uCameraPosition, 1.0)).xyz;
                    vec3 rayDirectionLocal = normalize((uInverseOceanTransform * vec4(viewDirection, 0.0)).xyz);

                    if (uUnderwater != 0 && uDepthOnly == 0)
                    {
                        float sceneDepth = texture(uSceneDepth, vUv).r;
                        float travelDistance = uMaxVisibilityDepth * 4.0;
                        float sceneDistance = 1e20;
                        if (sceneDepth > 0.000001)
                        {
                            vec3 sceneWorld = ReconstructWorldPosition(vUv, sceneDepth);
                            sceneDistance = length(sceneWorld - uCameraPosition);
                            travelDistance = min(sceneDistance, uMaxVisibilityDepth * 4.0);
                        }

                        // Looking upward exits the water at the animated surface;
                        // only the submerged portion of that ray contributes haze.
                        vec3 exitHitLocal;
                        bool exitsSurface = SolveWaveIntersection(rayOriginLocal, rayDirectionLocal, exitHitLocal) && !MaskReject(exitHitLocal.xz);
                        float exitDistance = 1e20;
                        if (exitsSurface)
                        {
                            vec3 exitHitWorld = (uOceanTransform * vec4(exitHitLocal, 1.0)).xyz;
                            exitDistance = length(exitHitWorld - uCameraPosition);
                            travelDistance = min(travelDistance, exitDistance);
                        }

                        vec3 cameraSurfaceLocal = vec3(rayOriginLocal.x, WaveHeight(rayOriginLocal.xz), rayOriginLocal.z);
                        vec3 cameraSurfaceWorld = (uOceanTransform * vec4(cameraSurfaceLocal, 1.0)).xyz;
                        vec3 surfaceNormalWorld = normalize(mat3(transpose(uInverseOceanTransform)) * WaveNormal(rayOriginLocal.xz));
                        float cameraDepth = max(dot(cameraSurfaceWorld - uCameraPosition, surfaceNormalWorld), 0.0);
                        float surfaceVisibility = max(uMaxVisibilityDepth, 0.1);
                        float depthVisibility = exp(-cameraDepth * max(uUnderwaterDepthFalloff, 0.0) / surfaceVisibility);
                        float availableLight = exp(-cameraDepth * max(uUnderwaterLightFalloff, 0.0) / surfaceVisibility);
                        float visibility = surfaceVisibility * mix(0.15, 1.0, depthVisibility);
                        vec2 underwaterUv = vUv;
                        // Refract only when the viewing ray reaches the water/air
                        // boundary before geometry. Otherwise displaced background
                        // colour leaks through foreground objects below the surface.
                        bool surfaceBeforeScene = exitsSurface && exitDistance < sceneDistance - 0.01;
                        if (surfaceBeforeScene)
                        {
                            vec3 exitNormal = WaveNormal(exitHitLocal.xz);
                            vec2 surfaceWarp = exitNormal.xz * uRefractionStrength * (2.2 + min(travelDistance, 8.0) * 0.12);
                            float shimmer = LayeredNoise(exitHitLocal.xz / max(uWaveLength, 0.01) * 5.0,
                                                       uSimulationTime * uWaveSpeed);
                            underwaterUv = clamp(vUv + surfaceWarp + vec2(shimmer, -shimmer) * uRefractionStrength * 0.35,
                                                 vec2(0.002), vec2(0.998));
                        }
                        vec3 sceneColor = texture(uSceneColor, underwaterUv).rgb;

                        // Beer-Lambert extinction over the actual camera-to-surface
                        // distance. Red light is absorbed faster than blue/green.
                        vec3 extinction = vec3(4.5, 2.2, 1.2) * max(uUnderwaterTurbidity, 0.0) / visibility;
                        vec3 transmittance = exp(-extinction * travelDistance);
                        float fogAmount = 1.0 - exp(-2.0 * travelDistance / visibility);
                        float visibilityRatio = travelDistance / visibility;
                        float fadeStart = clamp(uUnderwaterFadeStart, 0.0, 1.0);
                        float fadeEnd = fadeStart + max(uUnderwaterFadeSoftness, 0.01);
                        float distanceCutoff = smoothstep(fadeStart, fadeEnd, visibilityRatio);
                        transmittance *= 1.0 - distanceCutoff;
                        float depthColorFactor = 1.0 - availableLight;
                        vec3 waterColor = mix(uShallowColor, uDeepColor, clamp(max(fogAmount, depthColorFactor), 0.0, 1.0));
                        waterColor *= mix(0.08, 1.0, availableLight);
                        vec3 inScattering = waterColor * (vec3(1.0) - transmittance);
                        vec3 composed = sceneColor * transmittance * mix(0.12, 1.0, availableLight) + inScattering;

                        // Even nearby objects inherit a slight water cast, avoiding
                        // the appearance of an unchanged scene behind a screen filter.
                        composed = mix(composed, composed * (uShallowColor * 1.5 + vec3(0.25)), 0.12);
                        // Beyond Max Visibility Depth no scene detail survives;
                        // only the deep-water scattering colour remains.
                        vec3 visibilityLimitColor = uDeepColor * mix(0.03, 0.8, availableLight);
                        composed = mix(composed, visibilityLimitColor, distanceCutoff);
                        FragColor = vec4(composed, 1.0);
                        return;
                    }

                    vec3 localHit;
                    if (!SolveWaveIntersection(rayOriginLocal, rayDirectionLocal, localHit))
                    {
                        discard;
                    }

                    if (MaskReject(localHit.xz))
                    {
                        discard;
                    }

                    vec3 worldHit = (uOceanTransform * vec4(localHit, 1.0)).xyz;
                    float waterDistance = length(worldHit - uCameraPosition);

                    float sceneDepth = texture(uSceneDepth, vUv).r;
                    vec3 sceneColor = texture(uSceneColor, vUv).rgb;
                    float waterDepth = uMaxVisibilityDepth;
                    if (sceneDepth > 0.000001)
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
                        waterDepth = clamp(length(sceneWorld - worldHit), 0.0, uMaxVisibilityDepth);
                    }

                    vec4 waterClip = uProjection * uView * vec4(worldHit, 1.0);
                    // The analytic ocean is not geometry-clipped by the camera
                    // frustum. Intersections beyond the far plane project to a
                    // depth of 1, which fails the depth-only pass's GL_LESS test
                    // and makes later fog/cloud passes classify the ocean as sky.
                    // Keep a valid distant surface just inside the far plane.
                    gl_FragDepth = clamp(waterClip.z / waterClip.w * 0.5 + 0.5,
                                         0.000001, 1.0);
                    if (uDepthOnly != 0)
                    {
                        FragColor = vec4(0.0);
                        return;
                    }

                    vec3 normalLocal = WaveNormal(localHit.xz);
                    vec3 normal = normalize(mat3(transpose(uInverseOceanTransform)) * normalLocal);
                    vec2 distortion = normal.xz * uRefractionStrength;
                    vec3 refractedColor = texture(uSceneColor, clamp(vUv + distortion, vec2(0.0), vec2(1.0))).rgb;

                    float depthFactor = clamp(waterDepth / max(uMaxVisibilityDepth, 0.0001), 0.0, 1.0);
                    vec3 waterTint = mix(uShallowColor, uDeepColor, depthFactor);
                    vec3 environmentLight = SampleEnvironmentDiffuse(normal);
                    float shadowVisibility = DirectionalShadowVisibility(worldHit, normal);
                    // Most incident sunlight is reflected or absorbed rather than
                    // returning as diffuse light from the water body.
                    float sunDiffuse = max(dot(normal, normalize(uSunDirection)), 0.0) *
                                       max(uSunIntensity, 0.0) * shadowVisibility * 0.12;
                    vec3 incidentLight = environmentLight + uSunColor * sunDiffuse;
                    vec3 litWaterTint = waterTint * incidentLight;
                    // Attenuate the seabed through the water column. Without this,
                    // bright sand remains visible through arbitrarily deep water.
                    vec3 waterTransmission = exp(-vec3(6.0, 4.0, 2.8) * depthFactor);
                    refractedColor = refractedColor * waterTransmission + litWaterTint * (vec3(1.0) - waterTransmission);
                    vec3 viewVector = normalize(uCameraPosition - worldHit);
                    float ndotv = clamp(dot(viewVector, normal), 0.0, 1.0);
                    const float waterF0 = 0.0204;
                    float fresnel = waterF0 + (1.0 - waterF0) * pow(1.0 - ndotv, 5.0);
                    // The fallback must not contain the undisplaced scene color,
                    // otherwise submerged geometry appears both directly and refracted.
                    vec3 reflectionDirection = normalize(reflect(-viewVector, normal));
                    vec3 reflectionColor = uEnvironmentEnabled != 0
                        ? SampleEnvironment(reflectionDirection)
                        : vec3(0.0);
                    vec2 reflectionUv;
                    float reflectionConfidence;
                    if (TraceScreenSpaceReflection(worldHit, normal, viewVector, reflectionUv, reflectionConfidence))
                    {
                        vec3 screenSpaceReflection = texture(uSceneColor, reflectionUv).rgb;
                        reflectionColor = mix(reflectionColor, screenSpaceReflection, reflectionConfidence);
                    }
                    vec3 halfVector = normalize(viewVector + normalize(uSunDirection));
                    float sunSpecular = pow(max(dot(normal, halfVector), 0.0), mix(16.0, 256.0, clamp(uSmoothness, 0.0, 1.0))) * max(uSunIntensity, 0.0);
                    vec3 sunGlint = uSunColor * sunSpecular * shadowVisibility * 0.015;
                    float shoreFoam = 1.0 - smoothstep(0.0, max(uFoamDistance, 0.0001), waterDepth);
                    float crest = smoothstep(0.32, 0.72, 1.0 - normalLocal.y) *
                                  smoothstep(0.05, max(uWaveAmplitude * 0.45, 0.051), localHit.y);
                    float foamNoise = Fbm(localHit.xz * 0.18 + vec2(uSimulationTime * 0.05, -uSimulationTime * 0.035));
                    float foam = max(shoreFoam, crest * (0.65 + 0.35 * foamNoise)) * uFoamIntensity;

                    vec3 composed = mix(refractedColor, litWaterTint, 0.58);
                    float reflectionStrength = mix(0.12, 1.0, fresnel) * clamp(uSmoothness, 0.0, 1.0);
                    composed = mix(composed, reflectionColor + sunGlint, reflectionStrength);
                    vec3 litFoamColor = uFoamColor * incidentLight;
                    composed = mix(composed, litFoamColor, clamp(foam, 0.0, 1.0));

                    // Refraction has already sampled and displaced the scene behind
                    // the water. Alpha blending this over that same scene would draw
                    // submerged geometry twice (original plus refracted).
                    float waterOpacity = clamp(uOpacity, 0.0, 1.0);
                    composed = mix(refractedColor, composed, waterOpacity);
                    FragColor = vec4(composed, 1.0);

                }
            )";
            return Shader::Create(source);
        }

        void CopySceneColor(RenderTarget &source, RenderTarget &destination)
        {
            if (!destination.IsInitialized() || source.GetWidth() != destination.GetWidth() || source.GetHeight() != destination.GetHeight())
            {
                return;
            }
            glBindFramebuffer(GL_READ_FRAMEBUFFER, source.GetFramebufferID());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.GetFramebufferID());
            glBlitFramebuffer(
                0, 0, source.GetWidth(), source.GetHeight(),
                0, 0, destination.GetWidth(), destination.GetHeight(),
                GL_COLOR_BUFFER_BIT,
                GL_LINEAR);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        void CopyGeometryDepth(const GBuffer &source, RenderTarget &destination)
        {
            if (!destination.IsInitialized() || source.GetWidth() != destination.GetWidth() || source.GetHeight() != destination.GetHeight())
            {
                return;
            }
            glBindFramebuffer(GL_READ_FRAMEBUFFER, source.GetFBO());
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination.GetFramebufferID());
            glBlitFramebuffer(
                0, 0, source.GetWidth(), source.GetHeight(),
                0, 0, destination.GetWidth(), destination.GetHeight(),
                GL_DEPTH_BUFFER_BIT,
                GL_NEAREST);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        void SetCameraUnderwaterMarker(RenderTarget &target, bool underwater)
        {
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.GetFramebufferID());
            const GLfloat marker[4] = {underwater ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f};
            glClearBufferfv(GL_COLOR, 0, marker);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
    }

    void OceanPass::Initialize()
    {
        m_shader = CreateOceanShader();
    }

    void OceanPass::Execute(const RenderContext &ctx)
    {
        if (!m_shader || !ctx.scene || !ctx.temporaryRenderTarget || !ctx.oceanSurfaceDepthRenderTarget ||
            !ctx.oceanSceneColorCopyRenderTarget || !ctx.gBuffer || !ctx.hasCameraData)
        {
            return;
        }

        // Keep this per-frame target valid even when the scene has no oceans;
        // fog then sees the same geometry depth it normally would.
        CopyGeometryDepth(*ctx.gBuffer, *ctx.oceanSurfaceDepthRenderTarget);
        SetCameraUnderwaterMarker(*ctx.oceanSurfaceDepthRenderTarget, false);

        std::vector<OceanDraw> oceans;
        for (const auto *root : ctx.scene->GetRootEntities())
        {
            CollectOceans(root, oceans);
        }

        if (oceans.empty())
        {
            return;
        }

        CopySceneColor(*ctx.temporaryRenderTarget, *ctx.oceanSceneColorCopyRenderTarget);

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
        const glm::vec3 sunColor = sun ? glm::max(sun->color, glm::vec3(0.0f)) : glm::vec3(0.0f);
        const float sunVisibility = ctx.renderer ? ctx.renderer->GetPhysicalSkyDirectionalLightVisibility(sun) : 1.0f;
        const float sunIntensity = sun ? std::max(sun->intensity, 0.0f) * sunVisibility : 0.0f;
        int shadowCascadeCount = 0;
        if (sun && sun->castsShadows)
        {
            shadowCascadeCount = std::clamp(sun->activeShadowCascadeCount, 0, scene::kMaxDirectionalShadowCascades);
            for (int cascadeIndex = 0; cascadeIndex < shadowCascadeCount; ++cascadeIndex)
            {
                if (!sun->shadowCascadeMaps[static_cast<std::size_t>(cascadeIndex)])
                {
                    shadowCascadeCount = cascadeIndex;
                    break;
                }
            }
        }
        const glm::mat4 inverseView = glm::inverse(ctx.cameraData.view);
        const glm::vec3 cameraPosition(inverseView[3]);
        const bool cameraUnderwater = std::any_of(oceans.begin(), oceans.end(), [&](const OceanDraw &ocean)
                                                  { return IsCameraInsideOcean(ocean, cameraPosition); });
        SetCameraUnderwaterMarker(*ctx.oceanSurfaceDepthRenderTarget, cameraUnderwater);

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
        glBindTexture(GL_TEXTURE_2D, ctx.oceanSceneColorCopyRenderTarget->GetColorTextureID());
        m_shader->SetUniform("uSceneColor", 0);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, ctx.gBuffer->GetDepthTextureID());
        m_shader->SetUniform("uSceneDepth", 1);
        const auto *environmentTexture = ctx.scene->GetEnvironmentMapTexture();
        const GLuint physicalSkyTexture = ctx.renderer ? ctx.renderer->GetPhysicalSkyEnvironmentTextureID() : 0;
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, physicalSkyTexture ? physicalSkyTexture : (environmentTexture ? environmentTexture->GetTextureID() : 0));
        m_shader->SetUniform("uEnvironmentMap", 2);
        m_shader->SetUniform("uEnvironmentEnabled", physicalSkyTexture || environmentTexture ? 1 : 0);
        m_shader->SetUniform("uEnvironmentIntensity", ctx.scene->GetEnvironmentIntensity());
        for (int cascadeIndex = 0; cascadeIndex < scene::kMaxDirectionalShadowCascades; ++cascadeIndex)
        {
            glActiveTexture(GL_TEXTURE3 + cascadeIndex);
            const GLuint shadowTexture = cascadeIndex < shadowCascadeCount
                ? sun->shadowCascadeMaps[static_cast<std::size_t>(cascadeIndex)]->GetTextureID()
                : 0;
            glBindTexture(GL_TEXTURE_2D, shadowTexture);
            m_shader->SetUniform("uShadowCascadeMap" + std::to_string(cascadeIndex), 3 + cascadeIndex);
            m_shader->SetUniform("uShadowCascadeWorldOrigins[" + std::to_string(cascadeIndex) + "]",
                                 sun ? sun->shadowCascadeWorldOrigins[static_cast<std::size_t>(cascadeIndex)] : glm::vec3(0.0f));
            m_shader->SetUniform("uShadowCascadeMatrices[" + std::to_string(cascadeIndex) + "]",
                                 sun ? sun->shadowCascadeMatrices[static_cast<std::size_t>(cascadeIndex)] : glm::mat4(1.0f));
            m_shader->SetUniform("uShadowCascadeSplits[" + std::to_string(cascadeIndex) + "]",
                                 sun ? sun->shadowCascadeSplits[static_cast<std::size_t>(cascadeIndex)] : 0.0f);
        }
        m_shader->SetUniform("uShadowCascadeCount", shadowCascadeCount);
        m_shader->SetUniform("uShadowSoftness", sun ? sun->directionalShadowSettings.softness : 0.0f);
        m_shader->SetUniform("uInverseView", inverseView);
        m_shader->SetUniform("uInverseProjection", glm::inverse(ctx.cameraData.projection));
        m_shader->SetUniform("uView", ctx.cameraData.view);
        m_shader->SetUniform("uProjection", ctx.cameraData.projection);
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
            m_shader->SetUniform("uUnderwaterFadeStart", ocean.component->GetUnderwaterFadeStart());
            m_shader->SetUniform("uUnderwaterFadeSoftness", ocean.component->GetUnderwaterFadeSoftness());
            m_shader->SetUniform("uUnderwaterDepthFalloff", ocean.component->GetUnderwaterDepthFalloff());
            m_shader->SetUniform("uUnderwaterLightFalloff", ocean.component->GetUnderwaterLightFalloff());
            m_shader->SetUniform("uUnderwaterTurbidity", ocean.component->GetUnderwaterTurbidity());
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
            m_shader->SetUniform("uUnderwater", IsCameraInsideOcean(ocean, cameraPosition) ? 1 : 0);
            for (int areaIndex = 0; areaIndex < kMaxOceanAreas; ++areaIndex)
            {
                m_shader->SetUniform("uAreaPointCounts[" + std::to_string(areaIndex) + "]", pointCounts[static_cast<std::size_t>(areaIndex)]);
            }
            for (int pointIndex = 0; pointIndex < kMaxOceanAreas * kMaxOceanAreaPoints; ++pointIndex)
            {
                m_shader->SetUniform("uAreaPoints[" + std::to_string(pointIndex) + "]", flattenedPoints[static_cast<std::size_t>(pointIndex)]);
            }

            m_shader->SetUniform("uDepthOnly", 0);
            Graphics::BindRenderTarget(ctx.temporaryRenderTarget);
            glViewport(0, 0, ctx.temporaryRenderTarget->GetWidth(), ctx.temporaryRenderTarget->GetHeight());
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(GL_FALSE);
            Graphics::DrawFullscreenTriangle();

            m_shader->SetUniform("uDepthOnly", 1);
            Graphics::BindRenderTarget(ctx.oceanSurfaceDepthRenderTarget);
            glViewport(0, 0, ctx.oceanSurfaceDepthRenderTarget->GetWidth(), ctx.oceanSurfaceDepthRenderTarget->GetHeight());
            glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
            glEnable(GL_DEPTH_TEST);
            glDepthFunc(GL_GREATER);
            glDepthMask(GL_TRUE);
            Graphics::DrawFullscreenTriangle();
            glDisable(GL_DEPTH_TEST);
        }

        m_shader->Unbind();
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        Graphics::UnbindRenderTarget();
    }
}
