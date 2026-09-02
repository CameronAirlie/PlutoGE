#include "PlutoGE/render/postprocess/VoxelConeTracingEffect.h"

#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/UniformNames.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <glm/gtc/type_ptr.hpp>

#include <glad/glad.h>

namespace PlutoGE::render
{
    namespace
    {
        // Voxelization uses a geometry shader and image atomics, so a single
        // instanced draw can contain far more GPU work than an ordinary scene
        // draw. Keep every submission below a conservative work limit. This is
        // also what makes m_voxelizationCommandBudget a real per-frame budget:
        // previously one "command" could contain an unbounded instance count
        // and prevent the render thread from processing editor commands.
        constexpr std::size_t kMaxVoxelInstancesPerDraw = 32;
        constexpr std::size_t kMaxVoxelTrianglesPerDraw = 32768;
        // Geometry-shader voxelization performs several image atomics per
        // fragment. A command-count budget alone allowed eight individually
        // large draws to land in one frame. Keep the progressive job below a
        // predictable amount of raster/atomic work regardless of how scene
        // commands happen to be batched.
        constexpr std::size_t kMaxVoxelTrianglesPerFrame = 65536;

        bool ParseBool(const std::string &value) { return value == "true" || value == "1"; }

        std::size_t HashBytes(const void *data, std::size_t size, std::size_t seed = 1469598103934665603ull)
        {
            constexpr std::size_t prime = 1099511628211ull;
            auto hash = seed;
            const auto *bytes = static_cast<const std::uint8_t *>(data);
            for (std::size_t index = 0; index < size; ++index)
            {
                hash ^= bytes[index];
                hash *= prime;
            }
            return hash;
        }

        template <typename T>
        std::size_t HashValue(const T &value, std::size_t seed)
        {
            return HashBytes(&value, sizeof(value), seed);
        }

        bool HasStaticVoxelContributors(const std::vector<RenderCommand> &commands)
        {
            return std::any_of(
                commands.begin(), commands.end(),
                [](const RenderCommand &command)
                {
                    return command.isStatic && command.mesh && command.material &&
                           (!command.jointMatrices || command.jointMatrices->empty());
                });
        }

        std::size_t ComputeSceneSignature(const std::vector<RenderCommand> &commands)
        {
            // Render commands are sorted using their camera-selected LOD. Build an
            // order-independent signature which describes world content instead,
            // otherwise moving the camera can invalidate the voxel cache even when
            // no object has changed.
            std::size_t hash = 1469598103934665603ull;
            std::size_t sum = 0;
            std::size_t mixed = 0;
            std::size_t contributorCount = 0;
            const bool hasStaticContributors = HasStaticVoxelContributors(commands);
            for (const auto &command : commands)
            {
                // Prefer a cached representation of static scene lighting.
                // When an imported scene contains no static rigid geometry, use
                // its rigid meshes as a compatibility fallback. Skinned meshes
                // still receive VCTGI but never contribute stale voxel occlusion.
                if ((hasStaticContributors && !command.isStatic) || !command.mesh || !command.material ||
                    (command.jointMatrices && !command.jointMatrices->empty()))
                    continue;

                ++contributorCount;
                std::size_t commandHash = HashValue(command.mesh, 1469598103934665603ull);
                commandHash = HashValue(command.material, commandHash);
                commandHash = HashValue(command.submeshIndex, commandHash);
                commandHash = HashBytes(glm::value_ptr(command.model), sizeof(glm::mat4), commandHash);
                if (command.instanceModels)
                {
                    commandHash = HashValue(command.instanceModels->size(), commandHash);
                    // Instance buffers used by foliage are immutable snapshots.
                    // Hash their identity instead of walking thousands of
                    // matrices every frame; rebuilding the cluster replaces the
                    // shared snapshot and therefore changes this pointer.
                    commandHash = HashValue(command.instanceModels.get(), commandHash);
                }
                if (command.material)
                {
                    const auto &config = command.material->GetConfig();
                    commandHash = HashBytes(glm::value_ptr(config.color), sizeof(glm::vec4), commandHash);
                    commandHash = HashBytes(glm::value_ptr(config.emission), sizeof(glm::vec3), commandHash);
                    commandHash = HashBytes(glm::value_ptr(config.uvScale), sizeof(glm::vec2), commandHash);
                    commandHash = HashValue(config.albedoTexture, commandHash);
                    commandHash = HashValue(config.metallicTexture, commandHash);
                    commandHash = HashValue(config.metallic, commandHash);
                    commandHash = HashValue(config.metallicTextureChannel, commandHash);
                    commandHash = HashValue(config.alphaMode, commandHash);
                    commandHash = HashValue(config.alphaCutoff, commandHash);
                    commandHash = HashValue(config.surfaceType, commandHash);
                }
                sum += commandHash;
                mixed ^= commandHash + 0x9e3779b97f4a7c15ull + (commandHash << 6u) + (commandHash >> 2u);
            }
            hash = HashValue(contributorCount, hash);
            hash = HashValue(sum, hash);
            return HashValue(mixed, hash);
        }

        bool IntersectsVoxelVolume(const RenderCommand &command, const glm::vec3 &origin, float size)
        {
            const glm::vec3 closest = glm::clamp(command.worldBounds.center, origin, origin + glm::vec3(size));
            const glm::vec3 offset = command.worldBounds.center - closest;
            return glm::dot(offset, offset) <= command.worldBounds.radius * command.worldBounds.radius;
        }

        const scene::Light *SelectInjectionLight(const std::vector<scene::Light *> *lights)
        {
            if (!lights)
                return nullptr;

            const scene::Light *selected = nullptr;
            const std::size_t candidateCount = std::min<std::size_t>(lights->size(), 16);
            for (std::size_t index = 0; index < candidateCount; ++index)
            {
                const auto *light = (*lights)[index];
                if (!light || light->type != scene::LightType::Directional)
                    continue;
                // Prefer a shadowed sun, then choose the strongest directional
                // source. Injecting additional directional lights without their
                // visibility maps would add energy through occluders.
                const bool selectedShadowed = selected && selected->castsShadows &&
                                              selected->activeShadowCascadeCount > 0;
                const bool candidateShadowed = light->castsShadows &&
                                               light->activeShadowCascadeCount > 0;
                if (!selected || (candidateShadowed && !selectedShadowed) ||
                    (candidateShadowed == selectedShadowed && light->intensity > selected->intensity))
                {
                    selected = light;
                }
            }
            return selected;
        }

        std::size_t ComputeLightSignature(const std::vector<scene::Light *> *lights, bool includeLocalLights)
        {
            const auto *light = SelectInjectionLight(lights);
            std::size_t hash = 1469598103934665603ull;
            if (light)
            {
                hash = HashValue(light->type, hash);
                hash = HashBytes(glm::value_ptr(light->direction), sizeof(glm::vec3), hash);
                hash = HashBytes(glm::value_ptr(light->color), sizeof(glm::vec3), hash);
                hash = HashValue(light->intensity, hash);
                hash = HashValue(light->castsShadows, hash);
                hash = HashValue(light->activeShadowCascadeCount, hash);
                hash = HashValue(light->directionalShadowSettings.maxDistance, hash);
                hash = HashValue(light->directionalShadowSettings.casterDistance, hash);
                hash = HashValue(light->directionalShadowSettings.splitLambda, hash);
            }
            if (includeLocalLights && lights)
            {
                for (const auto *candidate : *lights)
                {
                    if (!candidate || candidate->type == scene::LightType::Directional)
                        continue;
                    hash = HashValue(candidate->type, hash);
                    hash = HashBytes(glm::value_ptr(candidate->position), sizeof(glm::vec3), hash);
                    hash = HashBytes(glm::value_ptr(candidate->direction), sizeof(glm::vec3), hash);
                    hash = HashBytes(glm::value_ptr(candidate->color), sizeof(glm::vec3), hash);
                    hash = HashValue(candidate->intensity, hash);
                    hash = HashValue(candidate->range, hash);
                }
            }
            return hash;
        }
    }

    VoxelConeTracingEffect::~VoxelConeTracingEffect()
    {
        ReleaseVolume();
        if (m_voxelInstanceBuffer)
            glDeleteBuffers(1, &m_voxelInstanceBuffer);
        m_voxelInstanceBuffer = 0;
        m_voxelInstanceCapacity = 0;
        if (m_indirectTarget)
            m_indirectTarget->Cleanup();
        if (m_debugTarget)
            m_debugTarget->Cleanup();
        for (auto &target : m_historyColorTargets)
            if (target)
                target->Cleanup();
        for (auto &target : m_historyMetadataTargets)
            if (target)
                target->Cleanup();
    }

    std::vector<PostProcessParameter> VoxelConeTracingEffect::GetParameters() const
    {
        return {
            {"Resolution", PostProcessParameterType::Enum, std::to_string(m_resolution == 64 ? 0 : 1), {"64", "128"}},
            {"Cascade Count", PostProcessParameterType::Int, std::to_string(m_requestedCascadeCount)},
            {"Volume Size", PostProcessParameterType::Float, std::to_string(m_volumeSize)},
            {"Intensity", PostProcessParameterType::Float, std::to_string(m_intensity)},
            {"Cone Count", PostProcessParameterType::Int, std::to_string(m_coneCount)},
            {"Trace Quality", PostProcessParameterType::Enum, std::to_string(m_traceResolutionDivisor == 4 ? 0 : 1), {"Balanced", "High"}},
            {"Inject Local Lights", PostProcessParameterType::Bool, m_injectLocalLights ? "true" : "false"},
            {"Voxelization LOD Bias", PostProcessParameterType::Int, std::to_string(m_voxelizationLodBias)},
            {"Voxelization Command Budget", PostProcessParameterType::Int, std::to_string(m_voxelizationCommandBudget)},
            {"Cone Aperture", PostProcessParameterType::Float, std::to_string(m_aperture)},
            {"Max Distance", PostProcessParameterType::Float, std::to_string(m_maxDistance)},
            {"Normal Bias", PostProcessParameterType::Float, std::to_string(m_normalBias)},
            {"Update Interval", PostProcessParameterType::Int, std::to_string(m_updateInterval)},
            {"Temporal Blend", PostProcessParameterType::Float, std::to_string(m_temporalBlend)},
            {"History Depth Threshold", PostProcessParameterType::Float, std::to_string(m_historyDepthThreshold)},
            {"History Normal Threshold", PostProcessParameterType::Float, std::to_string(m_historyNormalThreshold)},
            {"Debug View", PostProcessParameterType::Enum, std::to_string(m_debugView), {"Final Composite", "Raw Cone Trace", "Temporal Result", "History Validation", "Voxel Radiance", "Voxel Opacity", "Upsample Classification", "Voxel Sample Count", "Cascade Coverage"}},
            {"Indirect Only", PostProcessParameterType::Bool, m_indirectOnly ? "true" : "false"},
        };
    }

    VoxelConeTracingEffect::Settings VoxelConeTracingEffect::GetSettings() const noexcept
    {
        return {.resolution = m_resolution,
                .cascadeCount = m_requestedCascadeCount,
                .coneCount = m_coneCount,
                .traceResolutionDivisor = m_traceResolutionDivisor,
                .updateInterval = m_updateInterval,
                .voxelizationCommandBudget = m_voxelizationCommandBudget,
                .voxelizationLodBias = m_voxelizationLodBias,
                .debugView = m_debugView,
                .volumeSize = m_volumeSize,
                .intensity = m_intensity,
                .aperture = m_aperture,
                .maxDistance = m_maxDistance,
                .normalBias = m_normalBias,
                .temporalBlend = m_temporalBlend,
                .historyDepthThreshold = m_historyDepthThreshold,
                .historyNormalThreshold = m_historyNormalThreshold,
                .injectLocalLights = m_injectLocalLights,
                .indirectOnly = m_indirectOnly};
    }

    void VoxelConeTracingEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &p : parameters)
        {
            if (p.name == "Resolution")
            {
                const int values[] = {64, 128};
                const int next = values[std::clamp(std::stoi(p.value), 0, 1)];
                if (next != m_resolution)
                {
                    m_resolution = next;
                    ReleaseVolume();
                    ResetHistory();
                }
            }
            else if (p.name == "Cascade Count")
            {
                const int next = std::clamp(std::stoi(p.value), 1, static_cast<int>(kCascadeCount));
                if (next != m_requestedCascadeCount)
                {
                    m_requestedCascadeCount = next;
                    ReleaseVolume();
                    ResetHistory();
                }
            }
            else if (p.name == "Volume Size")
            {
                const float next = std::clamp(std::stof(p.value), 4.0f, 512.0f);
                if (next != m_volumeSize)
                {
                    m_volumeSize = next;
                    for (auto &cascade : m_cascades)
                    {
                        cascade.hasVolume = false;
                        cascade.rebuildInProgress = false;
                        cascade.lastVoxelizedFrame = ~0ull;
                        cascade.jobs.clear();
                    }
                    ResetHistory();
                }
            }
            else if (p.name == "Intensity")
                m_intensity = std::clamp(std::stof(p.value), 0.0f, 8.0f);
            else if (p.name == "Cone Count")
                m_coneCount = std::clamp(std::stoi(p.value), 1, 6);
            else if (p.name == "Trace Quality")
            {
                const int nextDivisor = std::clamp(std::stoi(p.value), 0, 1) == 0 ? 4 : 2;
                if (nextDivisor != m_traceResolutionDivisor)
                {
                    m_traceResolutionDivisor = nextDivisor;
                    ResetHistory();
                }
            }
            else if (p.name == "Inject Local Lights")
            {
                const bool next = ParseBool(p.value);
                if (next != m_injectLocalLights)
                {
                    m_injectLocalLights = next;
                    for (auto &cascade : m_cascades)
                    {
                        cascade.hasVolume = false;
                        cascade.rebuildInProgress = false;
                        cascade.lastVoxelizedFrame = ~0ull;
                        cascade.jobs.clear();
                    }
                    m_lastContentCheckFrame = ~0ull;
                    ResetHistory();
                }
            }
            else if (p.name == "Voxelization LOD Bias")
            {
                const int next = std::clamp(std::stoi(p.value), 0, 4);
                if (next != m_voxelizationLodBias)
                {
                    m_voxelizationLodBias = next;
                    for (auto &cascade : m_cascades)
                    {
                        cascade.hasVolume = false;
                        cascade.rebuildInProgress = false;
                        cascade.lastVoxelizedFrame = ~0ull;
                        cascade.jobs.clear();
                    }
                    ResetHistory();
                }
            }
            else if (p.name == "Voxelization Command Budget")
                m_voxelizationCommandBudget = std::clamp(std::stoi(p.value), 1, 256);
            else if (p.name == "Cone Aperture")
                m_aperture = std::clamp(std::stof(p.value), 0.1f, 1.2f);
            else if (p.name == "Max Distance")
                m_maxDistance = std::clamp(std::stof(p.value), 1.0f, 256.0f);
            else if (p.name == "Normal Bias")
            {
                const float storedBias = std::stof(p.value);
                // Values above one voxel use the old cone-start convention and
                // can skip narrow gaps entirely. Migrate them to the new default.
                m_normalBias = storedBias > 1.0f ? 0.35f : std::clamp(storedBias, 0.0f, 1.0f);
            }
            else if (p.name == "Update Interval")
                m_updateInterval = std::clamp(std::stoi(p.value), 1, 16);
            else if (p.name == "Temporal Blend")
                m_temporalBlend = std::clamp(std::stof(p.value), 0.0f, 0.98f);
            else if (p.name == "History Depth Threshold")
                m_historyDepthThreshold = std::clamp(std::stof(p.value), 0.001f, 10.0f);
            else if (p.name == "History Normal Threshold")
                m_historyNormalThreshold = std::clamp(std::stof(p.value), 0.0f, 0.999f);
            else if (p.name == "Debug View")
            {
                const int next = std::clamp(std::stoi(p.value), 0, 8);
                if (next != m_debugView)
                {
                    m_debugView = next;
                    ResetHistory();
                }
            }
            else if (p.name == "Indirect Only")
                m_indirectOnly = ParseBool(p.value);
        }
    }

    void VoxelConeTracingEffect::Initialize()
    {
        ShaderSource voxel;
        voxel.vertexSource = R"(#version 430 core
layout(location=0) in vec3 aPos;layout(location=1) in vec3 aNormal;layout(location=2) in vec2 aUV;
layout(location=5) in mat4 aInstanceModel;
layout(location=14) in ivec4 aJoints;layout(location=15) in vec4 aWeights;
uniform mat4 uModel;uniform vec2 uUVScale;uniform int uUseSkinning,uUseInstancing;uniform mat4 uJointMatrices[128];
out VS { vec3 p; vec3 n; vec2 uv; } v;
void main(){mat4 skin=mat4(1);if(uUseSkinning!=0){float w=aWeights.x+aWeights.y+aWeights.z+aWeights.w;if(w>.0001)skin=uJointMatrices[clamp(aJoints.x,0,127)]*aWeights.x+uJointMatrices[clamp(aJoints.y,0,127)]*aWeights.y+uJointMatrices[clamp(aJoints.z,0,127)]*aWeights.z+uJointMatrices[clamp(aJoints.w,0,127)]*aWeights.w;}
 mat4 model=uUseInstancing!=0?aInstanceModel:uModel;vec4 local=skin*vec4(aPos,1),p=model*local;mat3 model3=mat3(model);vec3 c0=cross(model3[1],model3[2]),c1=cross(model3[2],model3[0]),c2=cross(model3[0],model3[1]);float orientation=dot(model3[0],c0)<0?-1.0:1.0;mat3 normalMatrix=mat3(c0,c1,c2)*orientation;
 v.p=p.xyz;v.n=normalize(normalMatrix*(mat3(skin)*aNormal));v.uv=aUV*uUVScale;gl_Position=p;})";
        voxel.geometrySource = R"(#version 430 core
layout(triangles) in; layout(triangle_strip,max_vertices=3) out;
in VS { vec3 p; vec3 n; vec2 uv; } vin[]; out GS { vec3 p; vec3 n; vec2 uv; } g;
uniform vec3 uVolumeOrigin,uEmission;uniform float uVolumeSize;uniform int uVoxelResolution;
vec2 projected(vec3 q,int axis){return axis==0?q.zy:axis==1?q.xz:q.xy;}
void main(){ vec3 faceNormal=abs(cross(vin[1].p-vin[0].p,vin[2].p-vin[0].p)); int axis=faceNormal.y>faceNormal.x?(faceNormal.z>faceNormal.y?2:1):(faceNormal.z>faceNormal.x?2:0);
 vec2 projectedPosition[3];for(int i=0;i<3;i++){vec3 q=(vin[i].p-uVolumeOrigin)/uVolumeSize*2.0-1.0;projectedPosition[i]=projected(q,axis);}
 // Ordinary rasterization can completely miss sub-voxel emissive triangles.
 // Expand only their raster footprint by half a voxel. The interpolated world
 // position remains on the original triangle, so this improves coverage without
 // moving the radiance source or thickening non-emissive occluders.
 if(any(greaterThan(uEmission,vec3(0)))&&uVoxelResolution>0){float area=(projectedPosition[1].x-projectedPosition[0].x)*(projectedPosition[2].y-projectedPosition[0].y)-(projectedPosition[1].y-projectedPosition[0].y)*(projectedPosition[2].x-projectedPosition[0].x);float orientation=area>=0?1.0:-1.0;float margin=1.41421356/float(uVoxelResolution);
  for(int i=0;i<3;i++){int previous=(i+2)%3,next=(i+1)%3;vec2 incoming=projectedPosition[i]-projectedPosition[previous],outgoing=projectedPosition[next]-projectedPosition[i];vec2 n0=orientation*vec2(incoming.y,-incoming.x)/max(length(incoming),1e-7);vec2 n1=orientation*vec2(outgoing.y,-outgoing.x)/max(length(outgoing),1e-7);vec2 miter=n0+n1;float miterLength=length(miter);if(miterLength>1e-6){miter/=miterLength;projectedPosition[i]+=miter*(margin/max(dot(miter,n1),.25));}}
 }
 for(int i=0;i<3;i++){g.p=vin[i].p;g.n=vin[i].n;g.uv=vin[i].uv;gl_Position=vec4(projectedPosition[i],0,1);EmitVertex();}EndPrimitive();})";
        voxel.fragmentSource = R"(#version 430 core
layout(r32ui,binding=0) uniform uimage3D uAccumulationR;layout(r32ui,binding=1) uniform uimage3D uAccumulationG;layout(r32ui,binding=2) uniform uimage3D uAccumulationB;layout(r32ui,binding=3) uniform uimage3D uAccumulationCount;layout(r32ui,binding=4) uniform uimage3D uAccumulationOpacity;in GS { vec3 p; vec3 n; vec2 uv; } g;
uniform vec3 uVolumeOrigin,uEmission,uLightDirection,uLightColor;uniform float uVolumeSize,uLightIntensity;uniform int uHasInjectionLight,uInjectionLightHasShadow;
const int MAX_LOCAL_LIGHTS=7;uniform int uLocalLightCount,uLocalLightType[MAX_LOCAL_LIGHTS];uniform vec3 uLocalLightPosition[MAX_LOCAL_LIGHTS],uLocalLightDirection[MAX_LOCAL_LIGHTS],uLocalLightColor[MAX_LOCAL_LIGHTS];uniform float uLocalLightIntensity[MAX_LOCAL_LIGHTS],uLocalLightRange[MAX_LOCAL_LIGHTS];
uniform vec4 uColor;uniform sampler2D uAlbedoTexture,uMetallicTexture;uniform float uHasAlbedoTexture,uHasMetallicTexture,uMetallicFactor,uAlphaCutoff;uniform int uMetallicTextureChannel,uAlphaMode,uSurfaceType;
uniform sampler2D uShadow0,uShadow1,uShadow2,uShadow3;uniform mat4 uShadowMatrix[4],uViewMatrix;uniform vec3 uShadowOrigin[4];uniform float uShadowSplit[4];uniform int uShadowCascadeCount;
float shadowSample(int c,vec2 uv){if(c==0)return texture(uShadow0,uv).r;if(c==1)return texture(uShadow1,uv).r;if(c==2)return texture(uShadow2,uv).r;return texture(uShadow3,uv).r;}
vec2 shadowTexel(int c){if(c==0)return 1.0/vec2(textureSize(uShadow0,0));if(c==1)return 1.0/vec2(textureSize(uShadow1,0));if(c==2)return 1.0/vec2(textureSize(uShadow2,0));return 1.0/vec2(textureSize(uShadow3,0));}
const uint MAX_VOXEL_SAMPLES=1048575u;
bool claimSample(ivec3 coord){uint count=imageLoad(uAccumulationCount,coord).r;for(;;){if(count>=MAX_VOXEL_SAMPLES)return false;uint observed=imageAtomicCompSwap(uAccumulationCount,coord,count,count+1u);if(observed==count)return true;count=observed;}}
bool projectShadow(int c,vec3 p,out vec3 q){vec4 lp=uShadowMatrix[c]*vec4(p-uShadowOrigin[c],1);q=lp.xyz/max(lp.w,.0001);q=q*.5+.5;return all(greaterThanEqual(q,vec3(0)))&&all(lessThanEqual(q,vec3(1)));}
float visibility(vec3 p,vec3 n,vec3 lightDirection){if(uShadowCascadeCount<=0)return 1;float d=max(-(uViewMatrix*vec4(p,1)).z,0.0);if(d>uShadowSplit[uShadowCascadeCount-1])return 1;int selected=uShadowCascadeCount-1;for(int i=0;i<4;i++){if(i>=uShadowCascadeCount)break;if(d<=uShadowSplit[i]){selected=i;break;}}
 vec3 normal=normalize(n),lightDir=normalize(-lightDirection);float ndl=max(dot(normal,lightDir),0.0);
 // Voxel injection previously tested the exact rasterized surface against the
 // shadow map. Quantization then produced alternating self-shadowed/lit bands
 // on large coplanar walls and floors. Match the visible lighting receiver bias
 // and include a small voxel-relative floor so it remains meaningful at coarse
 // VCT resolutions without jumping through nearby geometry.
 float voxelSize=uVolumeSize/float(imageSize(uAccumulationR).x);
 float normalBias=max(.004*(1.0-ndl),.00075)+voxelSize*.015;
 vec3 receiver=p+normal*normalBias,q;int covered=-1;
 if(projectShadow(selected,receiver,q))covered=selected;else for(int i=0;i<4;i++){if(i>=uShadowCascadeCount)break;if(i!=selected&&projectShadow(i,receiver,q)){covered=i;break;}}if(covered<0)return 1;
 float cascadeScale=clamp(uShadowSplit[covered]/max(uShadowSplit[0],.0001),1.0,8.0);
 float bias=max(.00012+(1.0-ndl)*.00035,.00004)*cascadeScale;
 vec2 texel=shadowTexel(covered);float lit=0.0;
 for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){vec2 o=vec2(x,y)*texel;lit+=q.z-bias<=shadowSample(covered,q.xy+o)?1.0:0.0;}
 return lit/9.0;}
void main(){ vec3 tc=(g.p-uVolumeOrigin)/uVolumeSize; if(any(lessThan(tc,vec3(0)))||any(greaterThanEqual(tc,vec3(1))))discard;
 vec4 a=uColor;if(uHasAlbedoTexture>.5)a*=texture(uAlbedoTexture,g.uv);if(uAlphaMode==1&&a.a<uAlphaCutoff)discard;
 float metallic=clamp(uMetallicFactor,0,1);if(uHasMetallicTexture>.5){vec4 packedMetallic=texture(uMetallicTexture,g.uv);metallic*=uMetallicTextureChannel==0?packedMetallic.r:uMetallicTextureChannel==1?packedMetallic.g:uMetallicTextureChannel==2?packedMetallic.b:packedMetallic.a;}
 bool glassSurface=uSurfaceType==1;bool alphaBlend=uAlphaMode==2;float radianceCoverage=(glassSurface||alphaBlend)?clamp(a.a,0,1):1.0;float opacity=glassSurface?0.0:radianceCoverage;vec3 normal=normalize(g.n),directRadiance=vec3(0);if(uHasInjectionLight!=0){vec3 lightDir=normalize(-uLightDirection);float ndl=max(dot(normal,lightDir),0);float shadow=uInjectionLightHasShadow!=0?visibility(g.p,g.n,uLightDirection):1;directRadiance=uLightColor*uLightIntensity*ndl*shadow;}for(int i=0;i<MAX_LOCAL_LIGHTS;i++){if(i>=uLocalLightCount)break;vec3 toLight=uLocalLightPosition[i]-g.p;float distanceToLight=length(toLight),range=max(uLocalLightRange[i],.0001);if(distanceToLight>=range)continue;vec3 lightDir=toLight/max(distanceToLight,.0001);float attenuation=pow(clamp(1.0-distanceToLight/range,0.0,1.0),2.0);if(uLocalLightType[i]==2){float spotEffect=dot(-lightDir,normalize(uLocalLightDirection[i]));attenuation*=smoothstep(.9,.975,spotEffect);}directRadiance+=uLocalLightColor[i]*uLocalLightIntensity[i]*attenuation*max(dot(normal,lightDir),0.0);}vec3 diffuseBounce=uSurfaceType==0?a.rgb*(1-metallic)*directRadiance*(radianceCoverage/3.14159265):vec3(0);vec3 r=diffuseBounce+max(uEmission,vec3(0))*radianceCoverage;
 // Keep invalid or extreme material/light values out of the half-float mip chain.
 // RGB is premultiplied by occupancy so partially occupied mip voxels cannot
 // contribute the radiance of a completely filled voxel.
 if(any(isnan(r))||any(isinf(r)))r=vec3(0);r=clamp(r,vec3(0),vec3(16));uvec3 encoded=uvec3(round(r*(4095.0/16.0)));uint encodedOpacity=uint(round(opacity*4095.0));ivec3 coord=clamp(ivec3(tc*imageSize(uAccumulationR)),ivec3(0),imageSize(uAccumulationR)-ivec3(1));if(claimSample(coord)){imageAtomicAdd(uAccumulationR,coord,encoded.r);imageAtomicAdd(uAccumulationG,coord,encoded.g);imageAtomicAdd(uAccumulationB,coord,encoded.b);imageAtomicAdd(uAccumulationOpacity,coord,encodedOpacity);}})";
        m_voxelizationShader = Shader::Create(voxel);

        ShaderSource resolve;
        resolve.computeSource = R"(#version 430 core
layout(local_size_x=4,local_size_y=4,local_size_z=4)in;
layout(r32ui,binding=0)readonly uniform uimage3D uAccumulationR;
layout(r32ui,binding=1)readonly uniform uimage3D uAccumulationG;
layout(r32ui,binding=2)readonly uniform uimage3D uAccumulationB;
layout(r32ui,binding=3)readonly uniform uimage3D uAccumulationCount;
layout(r32ui,binding=4)readonly uniform uimage3D uAccumulationOpacity;
layout(rgba16f,binding=5)writeonly uniform image3D uResolvedVolume;
uniform int uResolution,uDestinationZOffset;
float opacityAt(ivec3 coord,ivec3 size){if(any(lessThan(coord,ivec3(0)))||any(greaterThanEqual(coord,size)))return 0;uint count=imageLoad(uAccumulationCount,coord).r;if(count==0u)return 0;return clamp(float(imageLoad(uAccumulationOpacity,coord).r)/(4095.0*float(count)),0.0,1.0);}
void main(){ivec3 coord=ivec3(gl_GlobalInvocationID),localSize=ivec3(uResolution);if(any(greaterThanEqual(coord,localSize)))return;uint count=imageLoad(uAccumulationCount,coord).r;vec3 radiance=vec3(0);if(count>0u){vec3 sums=vec3(imageLoad(uAccumulationR,coord).r,imageLoad(uAccumulationG,coord).r,imageLoad(uAccumulationB,coord).r);radiance=sums*(16.0/4095.0)/float(count);}float opacity=opacityAt(coord,localSize);const ivec3 offsets[6]=ivec3[6](ivec3(1,0,0),ivec3(-1,0,0),ivec3(0,1,0),ivec3(0,-1,0),ivec3(0,0,1),ivec3(0,0,-1));for(int i=0;i<6;i++)opacity=max(opacity,opacityAt(coord+offsets[i],localSize)*.35);imageStore(uResolvedVolume,coord+ivec3(0,0,uDestinationZOffset),vec4(radiance,opacity));})";
        m_voxelResolveShader = Shader::Create(resolve);

        ShaderSource directionalMip;
        directionalMip.computeSource = R"(#version 430 core
layout(local_size_x=4,local_size_y=4,local_size_z=4)in;
uniform sampler3D uSource;uniform int uSourceMip,uAxis,uSign,uCascadeIndex,uCascadeMipSize;
layout(rgba16f,binding=0)writeonly uniform image3D uDestination;
vec4 over(vec4 front,vec4 back){return vec4(front.rgb+(1.0-front.a)*back.rgb,front.a+(1.0-front.a)*back.a);}
void main(){ivec3 localDst=ivec3(gl_GlobalInvocationID),localSize=ivec3(uCascadeMipSize);if(any(greaterThanEqual(localDst,localSize)))return;int sourceCascadeSize=uCascadeMipSize*2;ivec3 base=localDst*2+ivec3(0,0,uCascadeIndex*sourceCascadeSize);vec4 total=vec4(0);for(int a=0;a<2;a++)for(int b=0;b<2;b++){ivec3 nearOffset=ivec3(0),farOffset=ivec3(0);int nearCoord=uSign>0?0:1,farCoord=1-nearCoord;if(uAxis==0){nearOffset=ivec3(nearCoord,a,b);farOffset=ivec3(farCoord,a,b);}else if(uAxis==1){nearOffset=ivec3(a,nearCoord,b);farOffset=ivec3(a,farCoord,b);}else{nearOffset=ivec3(a,b,nearCoord);farOffset=ivec3(a,b,farCoord);}vec4 front=texelFetch(uSource,base+nearOffset,uSourceMip);vec4 back=texelFetch(uSource,base+farOffset,uSourceMip);total+=over(front,back);}ivec3 atlasDst=localDst+ivec3(0,0,uCascadeIndex*uCascadeMipSize);imageStore(uDestination,atlasDst,total*0.25);})";
        m_directionalMipShader = Shader::Create(directionalMip);

        ShaderSource trace;
        trace.vertexSource = R"(#version 430 core
out vec2 UV;void main(){vec2 p[3]=vec2[3](vec2(-1),vec2(3,-1),vec2(-1,3));gl_Position=vec4(p[gl_VertexID],0,1);UV=gl_Position.xy*.5+.5;})";
        trace.fragmentSource = R"(#version 430 core
in vec2 UV;out vec4 FragColor;uniform sampler2D uScenePositionTexture,uSceneNormalTexture;
uniform usampler3D uVoxelSampleCount0,uVoxelSampleCount1,uVoxelSampleCount2;
uniform sampler3D uVoxel0,uVoxel1,uVoxel2,uVoxel3,uVoxel4,uVoxel5;
uniform vec3 uCascadeOrigin[3];uniform float uCascadeSize[3],uIntensity,uAperture,uMaxDistance,uNormalBias,uMaxMip;uniform int uCascadeCount,uAtlasCascadeCount,uConeCount,uDebugView;uniform mat4 uView;
bool containsCascade(int c,vec3 p,out vec3 tc){tc=(p-uCascadeOrigin[c])/uCascadeSize[c];return all(greaterThanEqual(tc,vec3(0)))&&all(lessThan(tc,vec3(1)));}
int findCascade(vec3 p,out vec3 tc){for(int c=0;c<3;c++){if(c>=uCascadeCount)break;if(containsCascade(c,p,tc))return c;}tc=vec3(0);return -1;}
vec3 atlasCoord(int c,vec3 tc,float lod){int clampMip=int(clamp(ceil(lod),0.0,uMaxMip));ivec3 atlasSize=textureSize(uVoxel0,clampMip);float localDepth=max(float(atlasSize.z)/float(uAtlasCascadeCount),1.0);vec3 halfTexel=vec3(.5/vec2(atlasSize.xy),.5/localDepth);vec3 local=clamp(tc,halfTexel,vec3(1)-halfTexel);return vec3(local.xy,(float(c)+local.z)/float(uAtlasCascadeCount));}
vec4 sampleVolume(int i,vec3 tc,float lod){if(i==0)return textureLod(uVoxel0,tc,lod);if(i==1)return textureLod(uVoxel1,tc,lod);if(i==2)return textureLod(uVoxel2,tc,lod);if(i==3)return textureLod(uVoxel3,tc,lod);if(i==4)return textureLod(uVoxel4,tc,lod);return textureLod(uVoxel5,tc,lod);}
vec4 sampleCascadeDirectional(int c,vec3 tc,vec3 d,float lod){vec3 atc=atlasCoord(c,tc,lod),w=abs(d);w/=max(w.x+w.y+w.z,.0001);vec4 sx=sampleVolume(d.x>=0?0:1,atc,lod),sy=sampleVolume(d.y>=0?2:3,atc,lod),sz=sampleVolume(d.z>=0?4:5,atc,lod);return sx*w.x+sy*w.y+sz*w.z;}
float voxelSizeAt(vec3 p){vec3 tc;int c=findCascade(p,tc);return c>=0?uCascadeSize[c]/float(textureSize(uVoxel0,0).x):uCascadeSize[max(uCascadeCount-1,0)]/float(textureSize(uVoxel0,0).x);}
vec4 sampleWorld(vec3 p,vec3 d,float diameter,out float voxelSize){vec3 tc;int c=findCascade(p,tc);if(c<0){voxelSize=uCascadeSize[max(uCascadeCount-1,0)]/float(textureSize(uVoxel0,0).x);return vec4(0);}voxelSize=uCascadeSize[c]/float(textureSize(uVoxel0,0).x);float lod=clamp(log2(max(diameter,voxelSize)/voxelSize),0,uMaxMip);vec4 nearSample=sampleCascadeDirectional(c,tc,d,lod);if(c+1<uCascadeCount){vec3 farTc;if(containsCascade(c+1,p,farTc)){float edge=min(min(min(tc.x,1-tc.x),min(tc.y,1-tc.y)),min(tc.z,1-tc.z))*float(textureSize(uVoxel0,0).x);if(edge<4.0){float farVoxel=uCascadeSize[c+1]/float(textureSize(uVoxel0,0).x);float farLod=clamp(log2(max(diameter,farVoxel)/farVoxel),0,uMaxMip);vec4 farSample=sampleCascadeDirectional(c+1,farTc,d,farLod);nearSample=mix(farSample,nearSample,smoothstep(0.0,4.0,edge));}}}return nearSample;}
float rayBoxExit(vec3 o,vec3 d){int outer=max(uCascadeCount-1,0);vec3 boxMin=uCascadeOrigin[outer],boxMax=boxMin+vec3(uCascadeSize[outer]);vec3 safeD=vec3(abs(d.x)<.00001?(d.x<0?-.00001:.00001):d.x,abs(d.y)<.00001?(d.y<0?-.00001:.00001):d.y,abs(d.z)<.00001?(d.z<0?-.00001:.00001):d.z);vec3 t0=(boxMin-o)/safeD,t1=(boxMax-o)/safeD;vec3 farT=max(t0,t1);return max(min(min(farT.x,farT.y),farT.z),0.0);}
uint sampleCount(int c,ivec3 coord){if(c==0)return texelFetch(uVoxelSampleCount0,coord,0).r;if(c==1)return texelFetch(uVoxelSampleCount1,coord,0).r;return texelFetch(uVoxelSampleCount2,coord,0).r;}
const float PI=3.14159265;vec3 cone(vec3 o,vec3 n,vec3 d){float startVoxel=voxelSizeAt(o);o+=n*startVoxel*mix(.35,.75,clamp(uNormalBias,0,1));float firstSample=startVoxel;float traceLimit=min(uMaxDistance,max(rayBoxExit(o,d)-firstSample,0.0));float dist=firstSample;vec4 sum=vec4(0);
 for(int i=0;i<48&&dist<traceLimit&&sum.a<.98;i++){float dia=max(startVoxel,2.0*uAperture*dist),sampleVoxelSize;vec4 s=sampleWorld(o+d*dist,d,dia,sampleVoxelSize);sum.rgb+=(1-sum.a)*s.rgb;sum.a+=(1-sum.a)*s.a;dist+=max(sampleVoxelSize,dia*.5);}return sum.rgb;}
void main(){vec3 p=texture(uScenePositionTexture,UV).xyz,rawNormal=texture(uSceneNormalTexture,UV).xyz,surfaceTc;float normalLengthSquared=dot(rawNormal,rawNormal);int surfaceCascade=findCascade(p,surfaceTc);if(!(normalLengthSquared>=.1&&normalLengthSquared<=1e6)||surfaceCascade<0){FragColor=vec4(0);return;}vec3 n=rawNormal*inversesqrt(normalLengthSquared);float viewDepth=max(-(uView*vec4(p,1)).z,0.0);
 if(uDebugView==1){vec4 voxel=sampleCascadeDirectional(surfaceCascade,surfaceTc,n,0);FragColor=vec4(voxel.rgb/(vec3(1)+voxel.rgb),viewDepth);return;}
 if(uDebugView==2){float opacity=sampleCascadeDirectional(surfaceCascade,surfaceTc,n,0).a;FragColor=vec4(vec3(opacity),viewDepth);return;}
 if(uDebugView==3){ivec3 countSize=textureSize(uVoxelSampleCount0,0);ivec3 countCoord=clamp(ivec3(surfaceTc*vec3(countSize)),ivec3(0),countSize-ivec3(1));uint count=sampleCount(surfaceCascade,countCoord);float level=clamp(log2(float(count)+1.0)/20.0,0.0,1.0);vec3 countColor=count>=1048575u?vec3(1,0,0):vec3(level);FragColor=vec4(countColor,viewDepth);return;}
 if(uDebugView==4){vec3 cascadeColor=surfaceCascade==0?vec3(0,.8,0):surfaceCascade==1?vec3(0,.35,1):vec3(1,.55,0);FragColor=vec4(cascadeColor,viewDepth);return;}
 vec3 up=abs(n.y)<.99?vec3(0,1,0):vec3(1,0,0),t=normalize(cross(up,n)),b=cross(n,t);vec3 total=cone(p,n,n);
 for(int i=1;i<6;i++){if(i>=uConeCount)break;float a=6.2831853*float(i-1)/max(float(uConeCount-1),1);vec3 d=normalize(n*.55+(t*cos(a)+b*sin(a))*.835);total+=cone(p,n,d);}
 // Cone directions approximate cosine-weighted hemisphere sampling, so their
 // mean already contains the receiver's 1/PI Lambertian normalization.
 FragColor=vec4(total*(uIntensity/max(float(uConeCount),1.0)),viewDepth);})";
        m_coneTraceShader = Shader::Create(trace);

        ShaderSource temporal;
        temporal.vertexSource = trace.vertexSource;
        temporal.fragmentSource = R"(#version 430 core
in vec2 UV;out vec4 FragColor;
uniform sampler2D uCurrentIndirectTexture,uHistoryColorTexture,uHistoryMetadataTexture,uSceneMotionTexture,uScenePositionTexture,uSceneNormalTexture;
uniform mat4 uView,uPreviousView;uniform float uTemporalBlend,uHistoryDepthThreshold,uHistoryNormalThreshold;uniform int uHasHistory,uDebugView;
vec3 decodeNormal(vec2 e){vec2 f=e*2-1;vec3 n=vec3(f,1-abs(f.x)-abs(f.y));float t=clamp(-n.z,0,1);n.xy+=vec2(n.x>=0?-t:t,n.y>=0?-t:t);return normalize(n);}
float luminance(vec3 c){return dot(c,vec3(.2126,.7152,.0722));}
void main(){
 vec3 current=texture(uCurrentIndirectTexture,UV).rgb;
 vec3 p=texture(uScenePositionTexture,UV).xyz,rawNormal=texture(uSceneNormalTexture,UV).xyz;
 float normalLengthSquared=dot(rawNormal,rawNormal);
 bool validNormal=normalLengthSquared>.01&&normalLengthSquared<1e6;
 vec3 n=validNormal?rawNormal*inversesqrt(normalLengthSquared):vec3(0);
 float currentDepth=validNormal?max(-(uView*vec4(p,1)).z,0.0):0.0;
 vec2 texel=1.0/vec2(textureSize(uCurrentIndirectTexture,0));
 vec3 lo=current,hi=current;
 float surroundingLuminance=0.0,compatibleCount=0.0;
 for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){
  if(x==0&&y==0)continue;
  vec2 sampleUv=clamp(UV+vec2(x,y)*texel,vec2(0),vec2(1));
  vec3 sampleNormalRaw=texture(uSceneNormalTexture,sampleUv).xyz;
  float sampleNormalLengthSquared=dot(sampleNormalRaw,sampleNormalRaw);
  if(sampleNormalLengthSquared<=.01||sampleNormalLengthSquared>=1e6)continue;
  vec3 sampleNormal=sampleNormalRaw*inversesqrt(sampleNormalLengthSquared);
  vec3 samplePosition=texture(uScenePositionTexture,sampleUv).xyz;
  float sampleDepth=max(-(uView*vec4(samplePosition,1)).z,0.0);
  float neighborhoodDepthTolerance=max(.03,currentDepth*.01);
  if(dot(n,sampleNormal)<.75||abs(sampleDepth-currentDepth)>neighborhoodDepthTolerance)continue;
  vec3 s=texture(uCurrentIndirectTexture,sampleUv).rgb;
  lo=min(lo,s);hi=max(hi,s);
  surroundingLuminance+=luminance(s);compatibleCount+=1.0;
 }
 // Only compare against neighbours on the same surface. The previous clamp
 // treated clipped/background pixels as dark neighbours and pulsed near walls.
 if(compatibleCount>0.0){
  float fireflyLimit=max((surroundingLuminance/compatibleCount)*4.0,.25);
  float currentLuminance=luminance(current);
  if(currentLuminance>fireflyLimit)current*=fireflyLimit/max(currentLuminance,.0001);
 }
 lo=min(lo,current);hi=max(hi,current);vec3 resolved=current;
 vec3 historyDebug=validNormal?vec3(0,0,.8):vec3(0,1,1);
 if(uHasHistory!=0&&validNormal){
  vec2 motion=texture(uSceneMotionTexture,UV).xy,huv=UV-motion;
  if(all(greaterThanEqual(huv,vec2(0)))&&all(lessThanEqual(huv,vec2(1)))){
   ivec2 historySize=textureSize(uHistoryMetadataTexture,0);
   vec2 historyPixel=huv*vec2(historySize)-.5;
   ivec2 historyBase=ivec2(floor(historyPixel)),bestCoord=ivec2(0);
   float previousDepth=max(-(uPreviousView*vec4(p,1)).z,0.0);
   float allowedDepth=max(.01,min(uHistoryDepthThreshold,previousDepth*.01));
   float bestScore=1e20;bool foundHistory=false,foundMetadata=false,foundDepth=false;
   // Metadata and colour must come from the same exact texel. Linear filtering
   // can validate one surface while returning the colour of its neighbour.
   for(int y=0;y<2;y++)for(int x=0;x<2;x++){
    ivec2 coord=clamp(historyBase+ivec2(x,y),ivec2(0),historySize-ivec2(1));
    vec4 meta=texelFetch(uHistoryMetadataTexture,coord,0);
    if(meta.w<=.5)continue;
    foundMetadata=true;
    float depthError=abs(meta.z-previousDepth);
    float normalAgreement=dot(decodeNormal(meta.xy),n);
    if(depthError>allowedDepth)continue;
    foundDepth=true;
    if(normalAgreement<uHistoryNormalThreshold)continue;
    vec2 pixelOffset=(vec2(coord)+.5)-huv*vec2(historySize);
    float score=depthError/max(allowedDepth,.0001)+(1.0-normalAgreement)*4.0+dot(pixelOffset,pixelOffset)*.02;
    if(score<bestScore){bestScore=score;bestCoord=coord;foundHistory=true;}
   }
   if(foundHistory){
    vec3 padding=max((hi-lo)*.5,vec3(.03));
    vec3 historySample=texelFetch(uHistoryColorTexture,bestCoord,0).rgb;
    vec3 clippedHistory=clamp(historySample,lo-padding,hi+padding);
    // A stationary, depth/normal-validated receiver must retain its accumulated
    // radiance when the voxel lighting changes. Clamping it to the new frame's
    // neighbourhood would turn a global illumination step into an instant jump.
    // Reintroduce clipping as motion increases, where stale screen-space samples
    // are more likely to cause trails.
    float motionRejection=clamp(length(motion)*32.0,0,1);
    vec3 history=mix(historySample,clippedHistory,motionRejection);
    float historyWeight=uTemporalBlend*(1.0-motionRejection);
    resolved=mix(current,history,historyWeight);
    historyDebug=vec3(0,.2+.8*historyWeight,0);
   }
   else historyDebug=!foundMetadata?vec3(0,0,.8):!foundDepth?vec3(1,0,0):vec3(1,0,1);
  }
  else historyDebug=vec3(1,1,0);
 }
 FragColor=vec4(uDebugView!=0?historyDebug:max(resolved,vec3(0)),currentDepth);
})";
        m_temporalResolveShader = Shader::Create(temporal);

        ShaderSource metadata;
        metadata.vertexSource = trace.vertexSource;
        metadata.fragmentSource = R"(#version 430 core
in vec2 UV;out vec4 FragColor;uniform sampler2D uScenePositionTexture,uSceneNormalTexture;uniform mat4 uView;
vec2 signNotZero(vec2 v){return vec2(v.x>=0?1:-1,v.y>=0?1:-1);}
vec2 encodeNormal(vec3 n){n/=max(abs(n.x)+abs(n.y)+abs(n.z),.0001);if(n.z<0)n.xy=(1-abs(n.yx))*signNotZero(n.xy);return n.xy*.5+.5;}
void main(){vec3 p=texture(uScenePositionTexture,UV).xyz,rawNormal=texture(uSceneNormalTexture,UV).xyz;float normalLengthSquared=dot(rawNormal,rawNormal);if(!(normalLengthSquared>=.01&&normalLengthSquared<=1e6)){FragColor=vec4(0);return;}vec3 n=rawNormal*inversesqrt(normalLengthSquared);float d=max(-(uView*vec4(p,1)).z,0.0);FragColor=vec4(encodeNormal(n),d,1);})";
        m_historyMetadataShader = Shader::Create(metadata);
    }

    void VoxelConeTracingEffect::ReleaseVolume()
    {
        const auto deleteTextures = [](auto &textures)
        {
            std::vector<unsigned int> liveTextures;
            liveTextures.reserve(std::size(textures));
            for (const unsigned int texture : textures)
                if (texture != 0)
                    liveTextures.push_back(texture);
            if (!liveTextures.empty())
                Graphics::DeleteTextures(static_cast<GLsizei>(liveTextures.size()), liveTextures.data());
        };

        for (auto &cascade : m_cascades)
        {
            if (cascade.framebuffer)
                Graphics::DeleteFramebuffers(1, &cascade.framebuffer);
            cascade.framebuffer = 0;
            const unsigned int transientTextures[] = {
                cascade.accumulationR, cascade.accumulationG, cascade.accumulationB,
                cascade.accumulationCount, cascade.accumulationOpacity};
            deleteTextures(transientTextures);
            // pendingShadowMaps are persistent staging textures reused by
            // progressive voxel rebuilds.
            deleteTextures(cascade.pendingShadowMaps);
            cascade.pendingShadowMaps.fill(0);
            cascade.pendingShadowSourceMaps.fill(0);
            cascade.accumulationR = 0;
            cascade.accumulationG = 0;
            cascade.accumulationB = 0;
            cascade.accumulationCount = 0;
            cascade.accumulationOpacity = 0;
            cascade.jobs.clear();
            cascade.jobIndex = 0;
            cascade.lastVoxelizedFrame = ~0ull;
            cascade.lastSceneSignature = 0;
            cascade.lastLightSignature = 0;
            cascade.pendingSceneSignature = 0;
            cascade.pendingLightSignature = 0;
            cascade.hasVolume = false;
            cascade.rebuildInProgress = false;
        }
        deleteTextures(m_radianceAtlases);
        m_radianceAtlases.fill(0);
        m_allocatedResolution = 0;
        m_allocatedCascadeCount = 0;
        m_cachedSceneSignature = 0;
        m_cachedLightSignature = 0;
        m_lastContentCheckFrame = ~0ull;
    }

    void VoxelConeTracingEffect::EnsureResources(int width, int height)
    {
        const std::size_t desiredCascadeCount =
            m_resolution >= 128
                ? std::min<std::size_t>(static_cast<std::size_t>(m_requestedCascadeCount), 2u)
                : static_cast<std::size_t>(m_requestedCascadeCount);
        if (m_allocatedResolution != m_resolution || m_allocatedCascadeCount != desiredCascadeCount)
        {
            ReleaseVolume();
            m_activeCascadeCount = desiredCascadeCount;
            const int mipCount = 1 + static_cast<int>(std::floor(std::log2(m_resolution)));
            glGenTextures(static_cast<GLsizei>(m_radianceAtlases.size()), m_radianceAtlases.data());
            for (const unsigned int atlas : m_radianceAtlases)
            {
                Graphics::BindTexture(GL_TEXTURE_3D, atlas);
                glTexStorage3D(
                    GL_TEXTURE_3D, mipCount, GL_RGBA16F,
                    m_resolution, m_resolution,
                    m_resolution * static_cast<int>(m_activeCascadeCount));
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAX_LEVEL, mipCount - 1);
            }
            for (std::size_t cascadeIndex = 0; cascadeIndex < m_activeCascadeCount; ++cascadeIndex)
            {
                auto &cascade = m_cascades[cascadeIndex];
                cascade.size = m_volumeSize * std::pow(3.0f, static_cast<float>(cascadeIndex));
                unsigned int accumulationVolumes[5]{};
                glGenTextures(5, accumulationVolumes);
                cascade.accumulationR = accumulationVolumes[0];
                cascade.accumulationG = accumulationVolumes[1];
                cascade.accumulationB = accumulationVolumes[2];
                cascade.accumulationCount = accumulationVolumes[3];
                cascade.accumulationOpacity = accumulationVolumes[4];
                for (const unsigned int volume : accumulationVolumes)
                {
                    Graphics::BindTexture(GL_TEXTURE_3D, volume);
                    glTexStorage3D(GL_TEXTURE_3D, 1, GL_R32UI, m_resolution, m_resolution, m_resolution);
                    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                }
                glGenFramebuffers(1, &cascade.framebuffer);
            }
            Graphics::BindFramebuffer(GL_FRAMEBUFFER, 0);
            m_allocatedResolution = m_resolution;
            m_allocatedCascadeCount = m_activeCascadeCount;
            m_nextCascadeToUpdate = 0;
        }
        const bool resized = m_indirectTarget && (m_indirectTarget->GetWidth() != width || m_indirectTarget->GetHeight() != height);
        if (!m_indirectTarget)
            m_indirectTarget = std::make_unique<RenderTarget>(RenderTargetConfig{.width = width, .height = height, .clearColor = glm::vec4(0)});
        else if (resized)
            m_indirectTarget->Resize(width, height);
        if (m_debugView != 0)
        {
            if (!m_debugTarget)
                m_debugTarget = std::make_unique<RenderTarget>(RenderTargetConfig{.width = width, .height = height, .clearColor = glm::vec4(0)});
            else if (m_debugTarget->GetWidth() != width || m_debugTarget->GetHeight() != height)
                m_debugTarget->Resize(width, height);
        }
        for (auto &target : m_historyColorTargets)
        {
            bool changed = false;
            if (!target)
            {
                target = std::make_unique<RenderTarget>(RenderTargetConfig{.width = width, .height = height, .clearColor = glm::vec4(0)});
                changed = true;
            }
            else if (target->GetWidth() != width || target->GetHeight() != height)
            {
                target->Resize(width, height);
                changed = true;
            }
            if (changed)
            {
                Graphics::BindTexture(GL_TEXTURE_2D, target->GetColorTextureID());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }
        }
        for (auto &target : m_historyMetadataTargets)
        {
            bool changed = false;
            if (!target)
            {
                target = std::make_unique<RenderTarget>(RenderTargetConfig{.width = width, .height = height, .clearColor = glm::vec4(0)});
                changed = true;
            }
            else if (target->GetWidth() != width || target->GetHeight() != height)
            {
                target->Resize(width, height);
                changed = true;
            }
            if (changed)
            {
                Graphics::BindTexture(GL_TEXTURE_2D, target->GetColorTextureID());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            }
        }
        Graphics::BindTexture(GL_TEXTURE_2D, 0);
        if (resized)
            ResetHistory();
    }

    void VoxelConeTracingEffect::BeginVoxelization(std::size_t cascadeIndex, const glm::vec3 &volumeOrigin,
                                                   std::size_t sceneSignature, std::size_t lightSignature,
                                                   const std::vector<RenderCommand> &commands,
                                                   const RenderContext &renderContext)
    {
        auto &cascade = m_cascades[cascadeIndex];
        cascade.pendingOrigin = volumeOrigin;
        cascade.pendingSceneSignature = sceneSignature;
        cascade.pendingLightSignature = lightSignature;
        cascade.pendingView = renderContext.cameraData.view;
        const scene::Light *directionalLight = SelectInjectionLight(renderContext.lights);
        cascade.pendingHasInjectionLight = directionalLight != nullptr;
        cascade.pendingLightDirection = directionalLight ? directionalLight->direction : glm::vec3(0.0f, -1.0f, 0.0f);
        cascade.pendingLightColor = directionalLight ? directionalLight->color : glm::vec3(0.0f);
        cascade.pendingLightIntensity = directionalLight ? directionalLight->intensity : 0.0f;
        cascade.pendingLocalLightCount = 0;
        if (m_injectLocalLights && renderContext.lights)
        {
            std::vector<const scene::Light *> localLights;
            localLights.reserve(renderContext.lights->size());
            const glm::vec3 volumeMax = volumeOrigin + glm::vec3(cascade.size);
            for (const auto *light : *renderContext.lights)
            {
                if (!light || light->type == scene::LightType::Directional ||
                    light->intensity <= 0.0f || light->range <= 0.0f)
                    continue;
                const glm::vec3 closest = glm::clamp(light->position, volumeOrigin, volumeMax);
                const glm::vec3 offset = light->position - closest;
                if (glm::dot(offset, offset) <= light->range * light->range)
                    localLights.push_back(light);
            }
            std::sort(
                localLights.begin(), localLights.end(),
                [](const scene::Light *a, const scene::Light *b)
                {
                    return a->intensity * a->range * a->range >
                           b->intensity * b->range * b->range;
                });
            cascade.pendingLocalLightCount = std::min<int>(
                static_cast<int>(localLights.size()),
                static_cast<int>(kMaxLocalInjectionLights));
            for (int lightIndex = 0; lightIndex < cascade.pendingLocalLightCount; ++lightIndex)
            {
                const auto *light = localLights[lightIndex];
                cascade.pendingLocalLightTypes[lightIndex] = static_cast<int>(light->type);
                cascade.pendingLocalLightPositions[lightIndex] = light->position;
                cascade.pendingLocalLightDirections[lightIndex] = light->direction;
                cascade.pendingLocalLightColors[lightIndex] = light->color;
                cascade.pendingLocalLightIntensities[lightIndex] = light->intensity;
                cascade.pendingLocalLightRanges[lightIndex] = light->range;
            }
        }
        cascade.pendingShadowCascadeCount = directionalLight && directionalLight->castsShadows
                                                ? std::clamp(directionalLight->activeShadowCascadeCount, 0, scene::kMaxDirectionalShadowCascades)
                                                : 0;
        cascade.pendingShadowSourceMaps.fill(0);
        cascade.pendingShadowCopyIndex = 0;
        for (int shadowCascade = 0; shadowCascade < scene::kMaxDirectionalShadowCascades; ++shadowCascade)
        {
            cascade.pendingShadowMatrices[shadowCascade] =
                directionalLight ? directionalLight->shadowCascadeMatrices[shadowCascade] : glm::mat4(1.0f);
            cascade.pendingShadowOrigins[shadowCascade] =
                directionalLight ? directionalLight->shadowCascadeWorldOrigins[shadowCascade] : glm::vec3(0.0f);
            cascade.pendingShadowSplits[shadowCascade] =
                directionalLight ? directionalLight->shadowCascadeSplits[shadowCascade] : 0.0f;
            if (shadowCascade >= cascade.pendingShadowCascadeCount ||
                !directionalLight->shadowCascadeMaps[shadowCascade])
            {
                cascade.pendingShadowCascadeCount = std::min(cascade.pendingShadowCascadeCount, shadowCascade);
                continue;
            }

            const auto *sourceMap = directionalLight->shadowCascadeMaps[shadowCascade].get();
            const int shadowWidth = sourceMap->GetWidth();
            const int shadowHeight = sourceMap->GetHeight();
            if (shadowWidth <= 0 || shadowHeight <= 0)
            {
                cascade.pendingShadowCascadeCount = shadowCascade;
                continue;
            }
            if (!cascade.pendingShadowMaps[shadowCascade] ||
                cascade.pendingShadowWidths[shadowCascade] != shadowWidth ||
                cascade.pendingShadowHeights[shadowCascade] != shadowHeight)
            {
                if (cascade.pendingShadowMaps[shadowCascade])
                    Graphics::DeleteTextures(1, &cascade.pendingShadowMaps[shadowCascade]);
                glGenTextures(1, &cascade.pendingShadowMaps[shadowCascade]);
                Graphics::BindTexture(GL_TEXTURE_2D, cascade.pendingShadowMaps[shadowCascade]);
                glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT24, shadowWidth, shadowHeight);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
                const float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
                glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
                cascade.pendingShadowWidths[shadowCascade] = shadowWidth;
                cascade.pendingShadowHeights[shadowCascade] = shadowHeight;
            }
            cascade.pendingShadowSourceMaps[shadowCascade] = sourceMap->GetTextureID();
        }
        cascade.jobIndex = 0;
        cascade.jobs.clear();
        cascade.jobs.reserve(commands.size());
        const bool hasStaticContributors = HasStaticVoxelContributors(commands);
        for (const auto &command : commands)
        {
            // Prefer the stable cached representation whenever the scene marks
            // voxel contributors static. Imported scenes sometimes mark every
            // rigid submesh dynamic, however; an empty volume is never useful,
            // so fall back to those rigid meshes only when no static contributor
            // exists anywhere in the submitted scene.
            if (!command.mesh || !command.material ||
                (hasStaticContributors && !command.isStatic) ||
                (command.jointMatrices && !command.jointMatrices->empty()) ||
                !IntersectsVoxelVolume(command, volumeOrigin, cascade.size))
                continue;

            // Construct directly in reserved storage. Moving a temporary job
            // through push_back corrupted ownership-bearing members in MSVC
            // debug builds during scene-load voxelization.
            cascade.jobs.emplace_back();
            auto &job = cascade.jobs.back();
            job.command = command;
            job.command.material = nullptr;
            job.command.jointMatrices = nullptr;
            const auto &materialConfig = command.material->GetConfig();
            std::size_t voxelLod = 0;
            // Index-only generated LODs are safe for untextured opaque meshes.
            // Textured and alpha-cutout geometry stays at source LOD so UV seams
            // and foliage silhouettes cannot inject radiance into empty space.
            if (materialConfig.alphaMode == AlphaMode::Opaque &&
                materialConfig.surfaceType == MaterialSurfaceType::Standard &&
                !materialConfig.albedoTexture)
            {
                const std::size_t lodCount = command.mesh->GetSubmeshLodCount(command.submeshIndex);
                if (lodCount > 0)
                {
                    voxelLod = std::min<std::size_t>(
                        static_cast<std::size_t>(m_voxelizationLodBias) + cascadeIndex,
                        lodCount - 1);
                }
            }
            job.material = VoxelMaterialSnapshot{
                .color = materialConfig.color,
                .uvScale = materialConfig.uvScale,
                .emission = materialConfig.emission,
                .albedoTexture = materialConfig.albedoTexture,
                .metallicTexture = materialConfig.metallicTexture,
                .surfaceType = materialConfig.surfaceType,
                .alphaMode = materialConfig.alphaMode,
                .metallicTextureChannel = materialConfig.metallicTextureChannel,
                .alphaCutoff = materialConfig.alphaCutoff,
                .metallic = materialConfig.metallic,
            };
            job.voxelLod = voxelLod;
        }
        cascade.rebuildInProgress = true;

        const unsigned int zero[4] = {0, 0, 0, 0};
        Graphics::BindFramebuffer(GL_FRAMEBUFFER, cascade.framebuffer);
        const unsigned int accumulationVolumes[] = {
            cascade.accumulationR, cascade.accumulationG, cascade.accumulationB,
            cascade.accumulationCount, cascade.accumulationOpacity};
        for (const unsigned int volume : accumulationVolumes)
        {
            glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, volume, 0);
            glClearBufferuiv(GL_COLOR, 0, zero);
        }
        Graphics::BindFramebuffer(GL_FRAMEBUFFER, 0);
        // The accumulation textures switch from framebuffer clears to shader
        // image atomics. Make every cleared layer visible before the first chunk.
        glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }

    void VoxelConeTracingEffect::GenerateDirectionalMips(VoxelCascade &cascade)
    {
        if (!m_directionalMipShader)
            return;
        const int cascadeIndex = static_cast<int>(&cascade - m_cascades.data());
        const int mipCount = 1 + static_cast<int>(std::floor(std::log2(m_resolution)));
        for (std::size_t direction = 1; direction < kDirectionCount; ++direction)
        {
            glCopyImageSubData(
                m_radianceAtlases[0], GL_TEXTURE_3D, 0, 0, 0, cascadeIndex * m_resolution,
                m_radianceAtlases[direction], GL_TEXTURE_3D, 0, 0, 0, cascadeIndex * m_resolution,
                m_resolution, m_resolution, m_resolution);
        }
        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        for (std::size_t direction = 0; direction < kDirectionCount; ++direction)
        {
            m_directionalMipShader->Bind();
            Graphics::ActiveTexture(GL_TEXTURE0);
            Graphics::BindTexture(GL_TEXTURE_3D, m_radianceAtlases[direction]);
            m_directionalMipShader->SetUniform("uSource", 0);
            const int axis = static_cast<int>(direction / 2);
            const int sign = direction % 2 == 0 ? 1 : -1;
            m_directionalMipShader->SetUniform("uAxis", axis);
            m_directionalMipShader->SetUniform("uSign", sign);
            m_directionalMipShader->SetUniform("uCascadeIndex", cascadeIndex);
            for (int mip = 1; mip < mipCount; ++mip)
            {
                const int mipSize = std::max(1, m_resolution >> mip);
                m_directionalMipShader->SetUniform("uSourceMip", mip - 1);
                m_directionalMipShader->SetUniform("uCascadeMipSize", mipSize);
                glBindImageTexture(0, m_radianceAtlases[direction], mip, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
                const GLuint groups = static_cast<GLuint>((mipSize + 3) / 4);
                glDispatchCompute(groups, groups, groups);
                glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
            }
        }
    }

    bool VoxelConeTracingEffect::VoxelizeChunk(std::size_t cascadeIndex, const PostProcessContext &context)
    {
        auto &cascade = m_cascades[cascadeIndex];
        if (!m_voxelizationShader || !m_voxelResolveShader)
            return false;
        // Shadow cascades are several million pixels in this project. Copy at
        // most one per frame so beginning a rebuild cannot create a large,
        // unbudgeted GPU transfer spike before voxel draw budgeting starts.
        while (cascade.pendingShadowCopyIndex < cascade.pendingShadowCascadeCount &&
               !cascade.pendingShadowSourceMaps[cascade.pendingShadowCopyIndex])
            ++cascade.pendingShadowCopyIndex;
        if (cascade.pendingShadowCopyIndex < cascade.pendingShadowCascadeCount)
        {
            const int shadowCascade = cascade.pendingShadowCopyIndex++;
            glCopyImageSubData(
                cascade.pendingShadowSourceMaps[shadowCascade], GL_TEXTURE_2D, 0, 0, 0, 0,
                cascade.pendingShadowMaps[shadowCascade], GL_TEXTURE_2D, 0, 0, 0, 0,
                cascade.pendingShadowWidths[shadowCascade],
                cascade.pendingShadowHeights[shadowCascade], 1);
            glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
            return false;
        }
        // Progressive rebuilds continue on a later frame. Shader-image writes
        // from the previous chunk must be visible before issuing more atomics.
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        Graphics::SetViewport(0, 0, m_resolution, m_resolution);
        Graphics::Disable(GL_DEPTH_TEST);
        Graphics::Disable(GL_CULL_FACE);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glBindImageTexture(0, cascade.accumulationR, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
        glBindImageTexture(1, cascade.accumulationG, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
        glBindImageTexture(2, cascade.accumulationB, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
        glBindImageTexture(3, cascade.accumulationCount, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
        glBindImageTexture(4, cascade.accumulationOpacity, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
        m_voxelizationShader->Bind();
        m_voxelizationShader->SetUniform("uVolumeOrigin", cascade.pendingOrigin);
        m_voxelizationShader->SetUniform("uVolumeSize", cascade.size);
        m_voxelizationShader->SetUniform("uVoxelResolution", m_resolution);
        m_voxelizationShader->SetUniform("uHasInjectionLight", cascade.pendingHasInjectionLight ? 1 : 0);
        m_voxelizationShader->SetUniform("uLightDirection", cascade.pendingLightDirection);
        m_voxelizationShader->SetUniform("uLightColor", cascade.pendingLightColor);
        m_voxelizationShader->SetUniform("uLightIntensity", cascade.pendingLightIntensity);
        m_voxelizationShader->SetUniform("uLocalLightCount", cascade.pendingLocalLightCount);
        static const auto localLightTypeNames = MakeArrayUniformNames<kMaxLocalInjectionLights>("uLocalLightType");
        static const auto localLightPositionNames = MakeArrayUniformNames<kMaxLocalInjectionLights>("uLocalLightPosition");
        static const auto localLightDirectionNames = MakeArrayUniformNames<kMaxLocalInjectionLights>("uLocalLightDirection");
        static const auto localLightColorNames = MakeArrayUniformNames<kMaxLocalInjectionLights>("uLocalLightColor");
        static const auto localLightIntensityNames = MakeArrayUniformNames<kMaxLocalInjectionLights>("uLocalLightIntensity");
        static const auto localLightRangeNames = MakeArrayUniformNames<kMaxLocalInjectionLights>("uLocalLightRange");
        for (int lightIndex = 0; lightIndex < cascade.pendingLocalLightCount; ++lightIndex)
        {
            m_voxelizationShader->SetUniform(localLightTypeNames[lightIndex], cascade.pendingLocalLightTypes[lightIndex]);
            m_voxelizationShader->SetUniform(localLightPositionNames[lightIndex], cascade.pendingLocalLightPositions[lightIndex]);
            m_voxelizationShader->SetUniform(localLightDirectionNames[lightIndex], cascade.pendingLocalLightDirections[lightIndex]);
            m_voxelizationShader->SetUniform(localLightColorNames[lightIndex], cascade.pendingLocalLightColors[lightIndex]);
            m_voxelizationShader->SetUniform(localLightIntensityNames[lightIndex], cascade.pendingLocalLightIntensities[lightIndex]);
            m_voxelizationShader->SetUniform(localLightRangeNames[lightIndex], cascade.pendingLocalLightRanges[lightIndex]);
        }
        m_voxelizationShader->SetUniform("uViewMatrix", cascade.pendingView);
        m_voxelizationShader->SetUniform("uInjectionLightHasShadow", cascade.pendingShadowCascadeCount > 0 ? 1 : 0);
        m_voxelizationShader->SetUniform("uShadowCascadeCount", cascade.pendingShadowCascadeCount);
        static const auto shadowNames = MakeNumberedUniformNames<scene::kMaxDirectionalShadowCascades>("uShadow");
        static const auto shadowMatrixNames = MakeArrayUniformNames<scene::kMaxDirectionalShadowCascades>("uShadowMatrix");
        static const auto shadowOriginNames = MakeArrayUniformNames<scene::kMaxDirectionalShadowCascades>("uShadowOrigin");
        static const auto shadowSplitNames = MakeArrayUniformNames<scene::kMaxDirectionalShadowCascades>("uShadowSplit");
        for (int shadowCascade = 0; shadowCascade < scene::kMaxDirectionalShadowCascades; ++shadowCascade)
        {
            const int slot = 6 + shadowCascade;
            Graphics::ActiveTexture(GL_TEXTURE0 + slot);
            Graphics::BindTexture(GL_TEXTURE_2D, cascade.pendingShadowMaps[shadowCascade]);
            m_voxelizationShader->SetUniform(shadowNames[shadowCascade], slot);
            m_voxelizationShader->SetUniform(shadowMatrixNames[shadowCascade], cascade.pendingShadowMatrices[shadowCascade]);
            m_voxelizationShader->SetUniform(shadowOriginNames[shadowCascade], cascade.pendingShadowOrigins[shadowCascade]);
            m_voxelizationShader->SetUniform(shadowSplitNames[shadowCascade], cascade.pendingShadowSplits[shadowCascade]);
        }
        int submittedDraws = 0;
        std::size_t submittedTriangles = 0;
        while (cascade.jobIndex < cascade.jobs.size() &&
               submittedDraws < m_voxelizationCommandBudget &&
               submittedTriangles < kMaxVoxelTrianglesPerFrame)
        {
            auto &job = cascade.jobs[cascade.jobIndex];
            const auto &c = job.command;
            if (!c.mesh)
            {
                ++cascade.jobIndex;
                continue;
            }

            m_voxelizationShader->SetUniform("uColor", job.material.color);
            m_voxelizationShader->SetUniform("uUVScale", job.material.uvScale);
            m_voxelizationShader->SetUniform("uEmission", glm::max(job.material.emission, glm::vec3(0.0f)));
            m_voxelizationShader->SetUniform("uSurfaceType", static_cast<int>(job.material.surfaceType));
            m_voxelizationShader->SetUniform("uAlphaMode", static_cast<int>(job.material.alphaMode));
            m_voxelizationShader->SetUniform("uAlphaCutoff", job.material.alphaCutoff);
            m_voxelizationShader->SetUniform("uMetallicFactor", job.material.metallic);
            m_voxelizationShader->SetUniform(
                "uMetallicTextureChannel",
                static_cast<int>(job.material.metallicTextureChannel));
            m_voxelizationShader->TrySetUniform(
                "uHasAlbedoTexture",
                job.material.albedoTexture ? 1.0f : 0.0f);
            if (job.material.albedoTexture)
            {
                m_voxelizationShader->TrySetUniform(
                    "uAlbedoTexture", job.material.albedoTexture, 0);
            }
            m_voxelizationShader->TrySetUniform(
                "uHasMetallicTexture",
                job.material.metallicTexture ? 1.0f : 0.0f);
            if (job.material.metallicTexture)
            {
                m_voxelizationShader->TrySetUniform(
                    "uMetallicTexture", job.material.metallicTexture, 2);
            }
            const bool skinned = job.jointMatrices && !job.jointMatrices->empty();
            m_voxelizationShader->SetUniform("uUseSkinning", skinned ? 1 : 0);
            if (skinned)
            {
                const std::size_t jointCount = std::min<std::size_t>(job.jointMatrices->size(), 128);
                m_voxelizationShader->SetUniformMatrixArray(
                    "uJointMatrices[0]", job.jointMatrices->data(), jointCount);
            }

            if (c.instanceModels && !c.instanceModels->empty())
            {
                const std::size_t instanceCount = c.instanceModels->size();
                const std::size_t indexCount = c.mesh->GetSubmeshLodIndexCount(c.submeshIndex, job.voxelLod);
                const std::size_t trianglesPerInstance = std::max<std::size_t>(indexCount / 3, 1);
                const std::size_t instancesForTriangleBudget =
                    std::max<std::size_t>(kMaxVoxelTrianglesPerDraw / trianglesPerInstance, 1);
                const std::size_t remainingTriangleBudget =
                    kMaxVoxelTrianglesPerFrame - submittedTriangles;
                const std::size_t instancesForFrameBudget =
                    std::max<std::size_t>(remainingTriangleBudget / trianglesPerInstance, 1);
                const std::size_t remainingInstances = instanceCount - job.nextInstance;
                const std::size_t batchInstanceCount = std::min(
                    remainingInstances,
                    std::min(kMaxVoxelInstancesPerDraw,
                             std::min(instancesForTriangleBudget, instancesForFrameBudget)));
                if (!m_voxelInstanceBuffer)
                    glGenBuffers(1, &m_voxelInstanceBuffer);
                glBindBuffer(GL_ARRAY_BUFFER, m_voxelInstanceBuffer);
                if (m_voxelInstanceCapacity < batchInstanceCount)
                {
                    m_voxelInstanceCapacity = std::max(
                        batchInstanceCount,
                        m_voxelInstanceCapacity == 0 ? batchInstanceCount : m_voxelInstanceCapacity * 2);
                }
                glBufferData(
                    GL_ARRAY_BUFFER,
                    static_cast<GLsizeiptr>(m_voxelInstanceCapacity * sizeof(glm::mat4)),
                    nullptr,
                    GL_STREAM_DRAW);
                glBufferSubData(
                    GL_ARRAY_BUFFER, 0,
                    static_cast<GLsizeiptr>(batchInstanceCount * sizeof(glm::mat4)),
                    c.instanceModels->data() + job.nextInstance);
                glBindVertexArray(c.mesh->GetVAO());
                for (unsigned int column = 0; column < 4; ++column)
                {
                    const unsigned int location = 5 + column;
                    glEnableVertexAttribArray(location);
                    glVertexAttribPointer(
                        location, 4, GL_FLOAT, GL_FALSE, sizeof(glm::mat4),
                        reinterpret_cast<const void *>(sizeof(glm::vec4) * column));
                    glVertexAttribDivisor(location, 1);
                }
                glBindBuffer(GL_ARRAY_BUFFER, 0);
                m_voxelizationShader->SetUniform("uUseInstancing", 1);
                c.mesh->DrawSubmeshInstancedBound(c.submeshIndex, batchInstanceCount, job.voxelLod);
                job.nextInstance += batchInstanceCount;
                submittedTriangles += trianglesPerInstance * batchInstanceCount;
                ++submittedDraws;
                if (job.nextInstance >= instanceCount)
                    ++cascade.jobIndex;
            }
            else
            {
                m_voxelizationShader->SetUniform("uUseInstancing", 0);
                m_voxelizationShader->SetUniform("uModel", c.model);
                c.mesh->DrawSubmesh(c.submeshIndex, job.voxelLod);
                submittedTriangles +=
                    std::max<std::size_t>(
                        c.mesh->GetSubmeshLodIndexCount(c.submeshIndex, job.voxelLod) / 3,
                        1);
                job.nextInstance = 1;
                ++submittedDraws;
                ++cascade.jobIndex;
            }
        }
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        if (cascade.jobIndex < cascade.jobs.size())
        {
            // Publish this chunk's atomic sums/counts to the next frame.
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
            return false;
        }

        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        m_voxelResolveShader->Bind();
        glBindImageTexture(0, cascade.accumulationR, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
        glBindImageTexture(1, cascade.accumulationG, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
        glBindImageTexture(2, cascade.accumulationB, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
        glBindImageTexture(3, cascade.accumulationCount, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
        glBindImageTexture(4, cascade.accumulationOpacity, 0, GL_TRUE, 0, GL_READ_ONLY, GL_R32UI);
        glBindImageTexture(5, m_radianceAtlases[0], 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        const int atlasCascadeIndex = static_cast<int>(&cascade - m_cascades.data());
        m_voxelResolveShader->SetUniform("uResolution", m_resolution);
        m_voxelResolveShader->SetUniform("uDestinationZOffset", atlasCascadeIndex * m_resolution);
        const GLuint groupCount = static_cast<GLuint>((m_resolution + 3) / 4);
        glDispatchCompute(groupCount, groupCount, groupCount);
        // The resolved image is sampled for the first directional mip chain and
        // copied into the remaining five base levels.
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                        GL_TEXTURE_FETCH_BARRIER_BIT |
                        GL_TEXTURE_UPDATE_BARRIER_BIT);
        GenerateDirectionalMips(cascade);
        cascade.origin = cascade.pendingOrigin;
        cascade.lastSceneSignature = cascade.pendingSceneSignature;
        cascade.lastLightSignature = cascade.pendingLightSignature;
        cascade.rebuildInProgress = false;
        cascade.hasVolume = true;
        cascade.jobs.clear();
        cascade.pendingShadowSourceMaps.fill(0);
        return true;
    }

    RenderTarget *VoxelConeTracingEffect::GenerateResolvedIndirectLighting(const PostProcessContext &context, int width, int height)
    {
        if (!m_coneTraceShader || !m_temporalResolveShader || !m_historyMetadataShader || !context.renderContext.gBuffer || !context.renderContext.hasCameraData)
            return nullptr;
        EnsureResources(width, height);
        if (!m_indirectTarget)
            return nullptr;
        const auto &renderContext = context.renderContext;
        if (m_jobScene != renderContext.scene)
        {
            // Scene restoration after Play destroys every mesh and material in
            // the previous scene. Progressive voxel jobs contain non-owning
            // pointers to those resources, so never carry them across this
            // boundary even when their content signature happens to match.
            m_jobScene = renderContext.scene;
            for (auto &cascade : m_cascades)
            {
                cascade.jobs.clear();
                cascade.jobIndex = 0;
                cascade.rebuildInProgress = false;
                cascade.hasVolume = false;
                cascade.pendingShadowSourceMaps.fill(0);
                cascade.lastSceneSignature = 0;
                cascade.lastLightSignature = 0;
                cascade.lastVoxelizedFrame = ~0ull;
            }
            m_cachedSceneSignature = 0;
            m_cachedLightSignature = 0;
            m_lastContentCheckFrame = ~0ull;
            m_nextCascadeToUpdate = 0;
            ResetHistory();
        }
        const float voxelSize = m_volumeSize / static_cast<float>(m_resolution);
        const glm::vec3 cameraPosition = glm::vec3(glm::inverse(renderContext.cameraData.view)[3]);
        const auto *sceneCommands = renderContext.renderer ? &renderContext.renderer->GetSceneRenderCommands() : renderContext.renderCommands;
        if (!sceneCommands)
            return nullptr;

        std::array<glm::vec3, kCascadeCount> desiredOrigins{};
        for (std::size_t cascadeIndex = 0; cascadeIndex < m_activeCascadeCount; ++cascadeIndex)
        {
            auto &cascade = m_cascades[cascadeIndex];
            cascade.size = m_volumeSize * std::pow(3.0f, static_cast<float>(cascadeIndex));
            const float cascadeVoxelSize = cascade.size / static_cast<float>(m_resolution);
            const float scrollStep = cascadeVoxelSize * 8.0f;
            const glm::vec3 centeredOrigin =
                glm::floor((cameraPosition - glm::vec3(cascade.size * 0.5f)) / scrollStep) * scrollStep;
            desiredOrigins[cascadeIndex] = cascade.hasVolume ? cascade.origin : centeredOrigin;
            if (cascade.hasVolume)
            {
                const glm::vec3 volumeCenter = cascade.origin + glm::vec3(cascade.size * 0.5f);
                const float safeTraceDistance = std::max(
                    cascadeVoxelSize,
                    cascade.size * 0.5f - scrollStep * 0.5f - cascadeVoxelSize * 2.0f);
                const float allowedCenterOffset = std::max(
                    cascadeVoxelSize,
                    cascade.size * 0.5f - safeTraceDistance - cascadeVoxelSize);
                if (glm::any(glm::greaterThan(glm::abs(cameraPosition - volumeCenter),
                                              glm::vec3(allowedCenterOffset))))
                    desiredOrigins[cascadeIndex] = centeredOrigin;
            }
        }

        std::size_t activeRebuild = m_activeCascadeCount;
        for (std::size_t cascadeIndex = 0; cascadeIndex < m_activeCascadeCount; ++cascadeIndex)
        {
            auto &cascade = m_cascades[cascadeIndex];
            if (!cascade.rebuildInProgress)
                continue;
            activeRebuild = cascadeIndex;
            // Finish the immutable snapshot already in flight. Restarting it for
            // every moving transform prevents large scenes from ever producing a
            // complete volume. Any newer signature/origin is scheduled normally
            // after this snapshot becomes active.
            break;
        }

        if (activeRebuild == m_activeCascadeCount)
        {
            // Content hashing walks every render command and animated joint.
            // It used to run every frame forever once a volume became old enough
            // to update. Origin tracking remains immediate, while content/light
            // checks are amortized and skipped entirely during progressive jobs.
            const unsigned long long contentCheckInterval =
                static_cast<unsigned long long>(std::max(m_updateInterval, 4));
            bool originRequiresRebuild = false;
            for (std::size_t cascadeIndex = 0; cascadeIndex < m_activeCascadeCount; ++cascadeIndex)
            {
                originRequiresRebuild =
                    originRequiresRebuild ||
                    !m_cascades[cascadeIndex].hasVolume ||
                    glm::any(glm::notEqual(desiredOrigins[cascadeIndex], m_cascades[cascadeIndex].origin));
            }
            const bool checkContent =
                originRequiresRebuild ||
                m_lastContentCheckFrame == ~0ull ||
                renderContext.frameSequence - m_lastContentCheckFrame >= contentCheckInterval;
            if (checkContent)
            {
                m_cachedSceneSignature = ComputeSceneSignature(*sceneCommands);
                m_cachedLightSignature = ComputeLightSignature(renderContext.lights, m_injectLocalLights);
                m_lastContentCheckFrame = renderContext.frameSequence;
            }
            for (std::size_t offset = 0; offset < m_activeCascadeCount; ++offset)
            {
                const std::size_t cascadeIndex = (m_nextCascadeToUpdate + offset) % m_activeCascadeCount;
                auto &cascade = m_cascades[cascadeIndex];
                const bool originChanged = !cascade.hasVolume ||
                                           glm::any(glm::notEqual(desiredOrigins[cascadeIndex], cascade.origin));
                const bool contentChanged = !cascade.hasVolume ||
                                            (checkContent &&
                                             (m_cachedSceneSignature != cascade.lastSceneSignature ||
                                              m_cachedLightSignature != cascade.lastLightSignature));
                const bool updateDue = cascade.lastVoxelizedFrame == ~0ull ||
                                       renderContext.frameSequence - cascade.lastVoxelizedFrame >= static_cast<unsigned>(m_updateInterval);
                if ((originChanged || contentChanged) && updateDue)
                {
                    BeginVoxelization(
                        cascadeIndex, desiredOrigins[cascadeIndex],
                        m_cachedSceneSignature, m_cachedLightSignature,
                        *sceneCommands, renderContext);
                    activeRebuild = cascadeIndex;
                    break;
                }
            }
        }
        const bool volumeRelocated = activeRebuild < m_activeCascadeCount &&
                                     m_cascades[activeRebuild].hasVolume &&
                                     glm::any(glm::notEqual(
                                         m_cascades[activeRebuild].pendingOrigin,
                                         m_cascades[activeRebuild].origin));
        if (activeRebuild < m_activeCascadeCount && VoxelizeChunk(activeRebuild, context))
        {
            m_cascades[activeRebuild].lastVoxelizedFrame = renderContext.frameSequence;
            m_nextCascadeToUpdate = (activeRebuild + 1) % m_activeCascadeCount;
            // Preserve screen-space history for same-origin radiance updates so
            // light and emissive changes use the configured temporal response.
            // A relocated field has different world-space coverage and remains
            // a hard discontinuity.
            if (volumeRelocated)
                ResetHistory();
        }

        int availableCascadeCount = 0;
        while (availableCascadeCount < static_cast<int>(m_activeCascadeCount) && m_cascades[availableCascadeCount].hasVolume)
            ++availableCascadeCount;
        if (availableCascadeCount == 0)
            return nullptr;
        // Motion vectors and depth/normal rejection handle normal movement, but a
        // multi-voxel displacement can reproject unrelated surfaces onto one
        // another. Treat it as a GI camera cut so bright history cannot trail
        // through the scene during fast traversal.
        if (m_hasPreviousCameraPosition && glm::length(cameraPosition - m_previousCameraPosition) > voxelSize * 2.0f)
            ResetHistory();
        const auto bindTraceVolumes = [&](Shader *shader)
        {
            static const auto voxelNames = MakeNumberedUniformNames<kDirectionCount>("uVoxel");
            static const auto sampleCountNames = MakeNumberedUniformNames<kCascadeCount>("uVoxelSampleCount");
            static const auto cascadeOriginNames = MakeArrayUniformNames<kCascadeCount>("uCascadeOrigin");
            static const auto cascadeSizeNames = MakeArrayUniformNames<kCascadeCount>("uCascadeSize");
            for (std::size_t direction = 0; direction < kDirectionCount; ++direction)
            {
                const int slot = 5 + static_cast<int>(direction);
                Graphics::ActiveTexture(GL_TEXTURE0 + slot);
                Graphics::BindTexture(GL_TEXTURE_3D, m_radianceAtlases[direction]);
                shader->SetUniform(voxelNames[direction], slot);
            }
            for (std::size_t cascadeIndex = 0; cascadeIndex < kCascadeCount; ++cascadeIndex)
            {
                const std::size_t sourceCascade =
                    std::min<std::size_t>(cascadeIndex, static_cast<std::size_t>(availableCascadeCount - 1));
                const int slot = 11 + static_cast<int>(cascadeIndex);
                Graphics::ActiveTexture(GL_TEXTURE0 + slot);
                Graphics::BindTexture(GL_TEXTURE_3D, m_cascades[sourceCascade].accumulationCount);
                shader->SetUniform(sampleCountNames[cascadeIndex], slot);
                shader->SetUniform(cascadeOriginNames[cascadeIndex], m_cascades[sourceCascade].origin);
                shader->SetUniform(cascadeSizeNames[cascadeIndex], m_cascades[sourceCascade].size);
            }
            shader->SetUniform("uCascadeCount", availableCascadeCount);
            shader->SetUniform("uAtlasCascadeCount", static_cast<int>(m_activeCascadeCount));
        };
        Graphics::BindRenderTarget(m_indirectTarget.get());
        Graphics::SetViewport(0, 0, width, height);
        Graphics::Disable(GL_DEPTH_TEST);
        Graphics::Disable(GL_CULL_FACE);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        m_coneTraceShader->Bind();
        BindCommonInputs(m_coneTraceShader, context);
        bindTraceVolumes(m_coneTraceShader);
        m_coneTraceShader->SetUniform("uIntensity", m_intensity);
        m_coneTraceShader->SetUniform("uAperture", m_aperture);
        const auto &activeCascade = m_cascades[availableCascadeCount - 1];
        const float activeVoxelSize = activeCascade.size / static_cast<float>(m_resolution);
        const float scrollStep = activeVoxelSize * 8.0f;
        const float safeMaxDistance = std::max(
            activeVoxelSize,
            activeCascade.size * 0.5f - scrollStep * 0.5f - activeVoxelSize * 2.0f);
        m_coneTraceShader->SetUniform("uMaxDistance", std::min(m_maxDistance, safeMaxDistance));
        m_coneTraceShader->SetUniform("uNormalBias", m_normalBias);
        m_coneTraceShader->SetUniform("uConeCount", m_coneCount);
        m_coneTraceShader->SetUniform("uMaxMip", std::floor(std::log2(float(m_resolution))));
        m_coneTraceShader->SetUniform("uView", context.renderContext.cameraData.view);
        m_coneTraceShader->SetUniform("uDebugView", 0);
        DrawFullscreenTriangle();

        const std::uint8_t next = static_cast<std::uint8_t>((m_historyIndex + 1) % 2);
        auto *resolvedColor = m_historyColorTargets[next].get();
        Graphics::BindRenderTarget(resolvedColor);
        Graphics::SetViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT);
        m_temporalResolveShader->Bind();
        BindCommonInputs(m_temporalResolveShader, context);
        Graphics::ActiveTexture(GL_TEXTURE5);
        Graphics::BindTexture(GL_TEXTURE_2D, m_indirectTarget->GetColorTextureID());
        m_temporalResolveShader->SetUniform("uCurrentIndirectTexture", 5);
        Graphics::ActiveTexture(GL_TEXTURE6);
        Graphics::BindTexture(GL_TEXTURE_2D, m_historyColorTargets[m_historyIndex]->GetColorTextureID());
        m_temporalResolveShader->SetUniform("uHistoryColorTexture", 6);
        Graphics::ActiveTexture(GL_TEXTURE7);
        Graphics::BindTexture(GL_TEXTURE_2D, m_historyMetadataTargets[m_historyIndex]->GetColorTextureID());
        m_temporalResolveShader->SetUniform("uHistoryMetadataTexture", 7);
        Graphics::ActiveTexture(GL_TEXTURE8);
        Graphics::BindTexture(GL_TEXTURE_2D, context.renderContext.gBuffer->GetMotionTextureID());
        m_temporalResolveShader->SetUniform("uSceneMotionTexture", 8);
        m_temporalResolveShader->SetUniform("uView", context.renderContext.cameraData.view);
        m_temporalResolveShader->SetUniform("uPreviousView", m_previousView);
        m_temporalResolveShader->SetUniform("uTemporalBlend", m_temporalBlend);
        m_temporalResolveShader->SetUniform("uHistoryDepthThreshold", m_historyDepthThreshold);
        m_temporalResolveShader->SetUniform("uHistoryNormalThreshold", m_historyNormalThreshold);
        m_temporalResolveShader->SetUniform("uHasHistory", m_hasHistory ? 1 : 0);
        m_temporalResolveShader->SetUniform("uDebugView", 0);
        DrawFullscreenTriangle();

        Graphics::BindRenderTarget(m_historyMetadataTargets[next].get());
        Graphics::SetViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT);
        m_historyMetadataShader->Bind();
        BindCommonInputs(m_historyMetadataShader, context);
        m_historyMetadataShader->SetUniform("uView", context.renderContext.cameraData.view);
        DrawFullscreenTriangle();

        RenderTarget *outputTarget = resolvedColor;
        if (m_debugView == 1)
        {
            outputTarget = m_indirectTarget.get();
        }
        else if (m_debugView == 3)
        {
            // Re-run only the cheap temporal resolve into a disposable target so
            // diagnostic colours never enter the accumulated history.
            Graphics::BindRenderTarget(m_debugTarget.get());
            Graphics::SetViewport(0, 0, width, height);
            glClear(GL_COLOR_BUFFER_BIT);
            m_temporalResolveShader->Bind();
            BindCommonInputs(m_temporalResolveShader, context);
            Graphics::ActiveTexture(GL_TEXTURE5);
            Graphics::BindTexture(GL_TEXTURE_2D, m_indirectTarget->GetColorTextureID());
            m_temporalResolveShader->SetUniform("uCurrentIndirectTexture", 5);
            Graphics::ActiveTexture(GL_TEXTURE6);
            Graphics::BindTexture(GL_TEXTURE_2D, m_historyColorTargets[m_historyIndex]->GetColorTextureID());
            m_temporalResolveShader->SetUniform("uHistoryColorTexture", 6);
            Graphics::ActiveTexture(GL_TEXTURE7);
            Graphics::BindTexture(GL_TEXTURE_2D, m_historyMetadataTargets[m_historyIndex]->GetColorTextureID());
            m_temporalResolveShader->SetUniform("uHistoryMetadataTexture", 7);
            Graphics::ActiveTexture(GL_TEXTURE8);
            Graphics::BindTexture(GL_TEXTURE_2D, context.renderContext.gBuffer->GetMotionTextureID());
            m_temporalResolveShader->SetUniform("uSceneMotionTexture", 8);
            m_temporalResolveShader->SetUniform("uView", context.renderContext.cameraData.view);
            m_temporalResolveShader->SetUniform("uPreviousView", m_previousView);
            m_temporalResolveShader->SetUniform("uTemporalBlend", m_temporalBlend);
            m_temporalResolveShader->SetUniform("uHistoryDepthThreshold", m_historyDepthThreshold);
            m_temporalResolveShader->SetUniform("uHistoryNormalThreshold", m_historyNormalThreshold);
            m_temporalResolveShader->SetUniform("uHasHistory", m_hasHistory ? 1 : 0);
            m_temporalResolveShader->SetUniform("uDebugView", 1);
            DrawFullscreenTriangle();
            outputTarget = m_debugTarget.get();
        }
        else if (m_debugView == 4 || m_debugView == 5 || m_debugView == 7 || m_debugView == 8)
        {
            // Sample the published voxel field at each visible receiver. This
            // separates corrupt volume data from cone/history/upsample errors.
            Graphics::BindRenderTarget(m_debugTarget.get());
            Graphics::SetViewport(0, 0, width, height);
            glClear(GL_COLOR_BUFFER_BIT);
            m_coneTraceShader->Bind();
            BindCommonInputs(m_coneTraceShader, context);
            bindTraceVolumes(m_coneTraceShader);
            m_coneTraceShader->SetUniform("uIntensity", m_intensity);
            m_coneTraceShader->SetUniform("uAperture", m_aperture);
            m_coneTraceShader->SetUniform("uMaxDistance", std::min(m_maxDistance, safeMaxDistance));
            m_coneTraceShader->SetUniform("uNormalBias", m_normalBias);
            m_coneTraceShader->SetUniform("uConeCount", m_coneCount);
            m_coneTraceShader->SetUniform("uMaxMip", std::floor(std::log2(float(m_resolution))));
            m_coneTraceShader->SetUniform("uView", context.renderContext.cameraData.view);
            m_coneTraceShader->SetUniform(
                "uDebugView",
                m_debugView == 4 ? 1 : m_debugView == 5 ? 2
                                   : m_debugView == 7   ? 3
                                                        : 4);
            DrawFullscreenTriangle();
            outputTarget = m_debugTarget.get();
        }

        m_historyIndex = next;
        m_previousView = context.renderContext.cameraData.view;
        m_previousCameraPosition = cameraPosition;
        m_hasPreviousCameraPosition = true;
        m_hasHistory = true;
        return outputTarget;
    }

    void VoxelConeTracingEffect::Apply(const PostProcessContext &context)
    {
        if (!context.sourceRenderTarget || !context.destinationRenderTarget)
            return;
        Graphics::BindFramebuffer(GL_READ_FRAMEBUFFER, context.sourceRenderTarget->GetFramebufferID());
        Graphics::BindFramebuffer(GL_DRAW_FRAMEBUFFER, context.destinationRenderTarget->GetFramebufferID());
        glBlitFramebuffer(0, 0, context.sourceRenderTarget->GetWidth(), context.sourceRenderTarget->GetHeight(), 0, 0, context.destinationRenderTarget->GetWidth(), context.destinationRenderTarget->GetHeight(), GL_COLOR_BUFFER_BIT, GL_NEAREST);
        Graphics::BindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void VoxelConeTracingEffect::ResetHistory()
    {
        m_hasHistory = false;
        m_historyIndex = 0;
    }
}
