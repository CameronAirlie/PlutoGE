#include "PlutoGE/render/postprocess/VoxelConeTracingEffect.h"

#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"
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

        std::size_t ComputeSceneSignature(const std::vector<RenderCommand> &commands)
        {
            // Render commands are sorted using their camera-selected LOD. Build an
            // order-independent signature which describes world content instead,
            // otherwise moving the camera can invalidate the voxel cache even when
            // no object has changed.
            std::size_t hash = HashValue(commands.size(), 1469598103934665603ull);
            std::size_t sum = 0;
            std::size_t mixed = 0;
            for (const auto &command : commands)
            {
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
                if (command.jointMatrices)
                {
                    // Animated geometry must invalidate the volume when its pose
                    // changes. This is intentionally the only per-command
                    // variable-length data still hashed every frame.
                    commandHash = HashValue(command.jointMatrices->size(), commandHash);
                    for (const auto &joint : *command.jointMatrices)
                        commandHash = HashBytes(glm::value_ptr(joint), sizeof(glm::mat4), commandHash);
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

        std::size_t ComputeLightSignature(const std::vector<scene::Light *> *lights)
        {
            const auto *light = SelectInjectionLight(lights);
            if (!light)
                return 0;
            std::size_t hash = HashValue(light->type, 1469598103934665603ull);
            hash = HashBytes(glm::value_ptr(light->direction), sizeof(glm::vec3), hash);
            hash = HashBytes(glm::value_ptr(light->color), sizeof(glm::vec3), hash);
            hash = HashValue(light->intensity, hash);
            hash = HashValue(light->castsShadows, hash);
            hash = HashValue(light->activeShadowCascadeCount, hash);
            hash = HashValue(light->directionalShadowSettings.maxDistance, hash);
            hash = HashValue(light->directionalShadowSettings.splitLambda, hash);
            return hash;
        }
    }

    VoxelConeTracingEffect::~VoxelConeTracingEffect()
    {
        ReleaseVolume();
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
            {"Volume Size", PostProcessParameterType::Float, std::to_string(m_volumeSize)},
            {"Intensity", PostProcessParameterType::Float, std::to_string(m_intensity)},
            {"Cone Count", PostProcessParameterType::Int, std::to_string(m_coneCount)},
            {"Voxelization Command Budget", PostProcessParameterType::Int, std::to_string(m_voxelizationCommandBudget)},
            {"Cone Aperture", PostProcessParameterType::Float, std::to_string(m_aperture)},
            {"Max Distance", PostProcessParameterType::Float, std::to_string(m_maxDistance)},
            {"Normal Bias", PostProcessParameterType::Float, std::to_string(m_normalBias)},
            {"Update Interval", PostProcessParameterType::Int, std::to_string(m_updateInterval)},
            {"Temporal Blend", PostProcessParameterType::Float, std::to_string(m_temporalBlend)},
            {"History Depth Threshold", PostProcessParameterType::Float, std::to_string(m_historyDepthThreshold)},
            {"History Normal Threshold", PostProcessParameterType::Float, std::to_string(m_historyNormalThreshold)},
            {"Debug View", PostProcessParameterType::Enum, std::to_string(m_debugView),
             {"Final Composite", "Raw Cone Trace", "Temporal Result", "History Validation",
              "Voxel Radiance", "Voxel Opacity", "Upsample Classification", "Voxel Sample Count"}},
            {"Indirect Only", PostProcessParameterType::Bool, m_indirectOnly ? "true" : "false"},
        };
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
            // Legacy scenes may contain "Voxelization LOD Bias". VCT now
            // deliberately ignores it: generated mesh LODs simplify positions
            // without an attribute error metric, so their UVs, normals and
            // alpha-cutout silhouettes are not safe radiance sources.
            else if (p.name == "Voxelization LOD Bias")
                continue;
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
                const int next = std::clamp(std::stoi(p.value), 0, 7);
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
layout(location=14) in ivec4 aJoints;layout(location=15) in vec4 aWeights;
uniform mat4 uModel;uniform vec2 uUVScale;uniform int uUseSkinning;uniform mat4 uJointMatrices[128];
out VS { vec3 p; vec3 n; vec2 uv; } v;
void main(){mat4 skin=mat4(1);if(uUseSkinning!=0){float w=aWeights.x+aWeights.y+aWeights.z+aWeights.w;if(w>.0001)skin=uJointMatrices[clamp(aJoints.x,0,127)]*aWeights.x+uJointMatrices[clamp(aJoints.y,0,127)]*aWeights.y+uJointMatrices[clamp(aJoints.z,0,127)]*aWeights.z+uJointMatrices[clamp(aJoints.w,0,127)]*aWeights.w;}
 vec4 local=skin*vec4(aPos,1),p=uModel*local;mat3 model3=mat3(uModel);vec3 c0=cross(model3[1],model3[2]),c1=cross(model3[2],model3[0]),c2=cross(model3[0],model3[1]);float orientation=dot(model3[0],c0)<0?-1.0:1.0;mat3 normalMatrix=mat3(c0,c1,c2)*orientation;
 v.p=p.xyz;v.n=normalize(normalMatrix*(mat3(skin)*aNormal));v.uv=aUV*uUVScale;gl_Position=p;})";
        voxel.geometrySource = R"(#version 430 core
layout(triangles) in; layout(triangle_strip,max_vertices=3) out;
in VS { vec3 p; vec3 n; vec2 uv; } vin[]; out GS { vec3 p; vec3 n; vec2 uv; } g;
uniform vec3 uVolumeOrigin;uniform float uVolumeSize;
void main(){ vec3 n=abs(cross(vin[1].p-vin[0].p,vin[2].p-vin[0].p)); int axis=n.y>n.x?(n.z>n.y?2:1):(n.z>n.x?2:0);
 for(int i=0;i<3;i++){vec3 q=(vin[i].p-uVolumeOrigin)/uVolumeSize*2.0-1.0;g.p=vin[i].p;g.n=vin[i].n;g.uv=vin[i].uv;gl_Position=axis==0?vec4(q.zy,0,1):axis==1?vec4(q.xz,0,1):vec4(q.xy,0,1);EmitVertex();}EndPrimitive();})";
        voxel.fragmentSource = R"(#version 430 core
layout(r32ui,binding=0) uniform uimage3D uAccumulationR;layout(r32ui,binding=1) uniform uimage3D uAccumulationG;layout(r32ui,binding=2) uniform uimage3D uAccumulationB;layout(r32ui,binding=3) uniform uimage3D uAccumulationCount;layout(r32ui,binding=4) uniform uimage3D uAccumulationOpacity;in GS { vec3 p; vec3 n; vec2 uv; } g;
uniform vec3 uVolumeOrigin,uEmission,uLightDirection,uLightColor;uniform float uVolumeSize,uLightIntensity;uniform int uHasInjectionLight,uInjectionLightHasShadow;
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
 bool glassSurface=uSurfaceType==1;bool alphaBlend=uAlphaMode==2;float radianceCoverage=(glassSurface||alphaBlend)?clamp(a.a,0,1):1.0;float opacity=glassSurface?0.0:radianceCoverage;vec3 normal=normalize(g.n),directRadiance=vec3(0);if(uHasInjectionLight!=0){vec3 lightDir=normalize(-uLightDirection);float ndl=max(dot(normal,lightDir),0);float shadow=uInjectionLightHasShadow!=0?visibility(g.p,g.n,uLightDirection):1;directRadiance=uLightColor*uLightIntensity*ndl*shadow;}vec3 diffuseBounce=uSurfaceType==0?a.rgb*(1-metallic)*directRadiance*(radianceCoverage/3.14159265):vec3(0);vec3 r=diffuseBounce+max(uEmission,vec3(0))*radianceCoverage;
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
float opacityAt(ivec3 coord,ivec3 size){if(any(lessThan(coord,ivec3(0)))||any(greaterThanEqual(coord,size)))return 0;uint count=imageLoad(uAccumulationCount,coord).r;if(count==0u)return 0;return clamp(float(imageLoad(uAccumulationOpacity,coord).r)/(4095.0*float(count)),0.0,1.0);}
void main(){ivec3 coord=ivec3(gl_GlobalInvocationID);ivec3 size=imageSize(uResolvedVolume);if(any(greaterThanEqual(coord,size)))return;uint count=imageLoad(uAccumulationCount,coord).r;vec3 radiance=vec3(0);if(count>0u){vec3 sums=vec3(imageLoad(uAccumulationR,coord).r,imageLoad(uAccumulationG,coord).r,imageLoad(uAccumulationB,coord).r);radiance=sums*(16.0/4095.0)/float(count);}float opacity=opacityAt(coord,size);const ivec3 offsets[6]=ivec3[6](ivec3(1,0,0),ivec3(-1,0,0),ivec3(0,1,0),ivec3(0,-1,0),ivec3(0,0,1),ivec3(0,0,-1));for(int i=0;i<6;i++)opacity=max(opacity,opacityAt(coord+offsets[i],size)*.35);imageStore(uResolvedVolume,coord,vec4(radiance,opacity));})";
        m_voxelResolveShader = Shader::Create(resolve);

        ShaderSource directionalMip;
        directionalMip.computeSource = R"(#version 430 core
layout(local_size_x=4,local_size_y=4,local_size_z=4)in;
uniform sampler3D uSource;uniform int uSourceMip,uAxis,uSign;
layout(rgba16f,binding=0)writeonly uniform image3D uDestination;
vec4 over(vec4 front,vec4 back){return vec4(front.rgb+(1.0-front.a)*back.rgb,front.a+(1.0-front.a)*back.a);}
void main(){ivec3 dst=ivec3(gl_GlobalInvocationID),dstSize=imageSize(uDestination);if(any(greaterThanEqual(dst,dstSize)))return;ivec3 base=dst*2;vec4 total=vec4(0);for(int a=0;a<2;a++)for(int b=0;b<2;b++){ivec3 nearOffset=ivec3(0),farOffset=ivec3(0);int nearCoord=uSign>0?0:1,farCoord=1-nearCoord;if(uAxis==0){nearOffset=ivec3(nearCoord,a,b);farOffset=ivec3(farCoord,a,b);}else if(uAxis==1){nearOffset=ivec3(a,nearCoord,b);farOffset=ivec3(a,farCoord,b);}else{nearOffset=ivec3(a,b,nearCoord);farOffset=ivec3(a,b,farCoord);}vec4 front=texelFetch(uSource,base+nearOffset,uSourceMip);vec4 back=texelFetch(uSource,base+farOffset,uSourceMip);total+=over(front,back);}imageStore(uDestination,dst,total*0.25);})";
        m_directionalMipShader = Shader::Create(directionalMip);

        ShaderSource trace;
        trace.vertexSource = R"(#version 430 core
out vec2 UV;void main(){vec2 p[3]=vec2[3](vec2(-1),vec2(3,-1),vec2(-1,3));gl_Position=vec4(p[gl_VertexID],0,1);UV=gl_Position.xy*.5+.5;})";
        trace.fragmentSource = R"(#version 430 core
in vec2 UV;out vec4 FragColor;uniform sampler2D uScenePositionTexture,uSceneNormalTexture;uniform usampler3D uVoxelSampleCount;
uniform sampler3D uVoxel0,uVoxel1,uVoxel2,uVoxel3,uVoxel4,uVoxel5;
uniform vec3 uVolumeOrigin;uniform float uVolumeSize,uIntensity,uAperture,uMaxDistance,uNormalBias,uMaxMip;uniform int uConeCount,uDebugView;uniform mat4 uView;
vec4 sampleVolume(int i,vec3 tc,float lod){if(i==0)return textureLod(uVoxel0,tc,lod);if(i==1)return textureLod(uVoxel1,tc,lod);if(i==2)return textureLod(uVoxel2,tc,lod);if(i==3)return textureLod(uVoxel3,tc,lod);if(i==4)return textureLod(uVoxel4,tc,lod);return textureLod(uVoxel5,tc,lod);}
vec4 sampleDirectional(vec3 tc,vec3 d,float lod){vec3 w=abs(d);w/=max(w.x+w.y+w.z,.0001);vec4 sx=sampleVolume(d.x>=0?0:1,tc,lod),sy=sampleVolume(d.y>=0?2:3,tc,lod),sz=sampleVolume(d.z>=0?4:5,tc,lod);return sx*w.x+sy*w.y+sz*w.z;}
float voxelSizeAt(vec3 p){return uVolumeSize/float(textureSize(uVoxel0,0).x);}
vec4 sampleWorld(vec3 p,vec3 d,float diameter,out float voxelSize){vec3 tc=(p-uVolumeOrigin)/uVolumeSize;voxelSize=voxelSizeAt(p);if(any(lessThan(tc,vec3(0)))||any(greaterThanEqual(tc,vec3(1))))return vec4(0);float lod=clamp(log2(max(diameter,voxelSize)/voxelSize),0,uMaxMip);return sampleDirectional(tc,d,lod);}
float rayBoxExit(vec3 o,vec3 d){vec3 boxMin=uVolumeOrigin,boxMax=uVolumeOrigin+vec3(uVolumeSize);vec3 safeD=vec3(abs(d.x)<.00001?(d.x<0?-.00001:.00001):d.x,abs(d.y)<.00001?(d.y<0?-.00001:.00001):d.y,abs(d.z)<.00001?(d.z<0?-.00001:.00001):d.z);vec3 t0=(boxMin-o)/safeD,t1=(boxMax-o)/safeD;vec3 farT=max(t0,t1);return max(min(min(farT.x,farT.y),farT.z),0.0);}
const float PI=3.14159265;vec3 cone(vec3 o,vec3 n,vec3 d){float startVoxel=voxelSizeAt(o);
 // A cell-boundary exit changes discontinuously when the receiver crosses a
 // voxel plane. At close range that becomes a large, camera-dependent jump.
 // Use a fixed normal offset and begin one voxel down the cone instead.
 o+=n*startVoxel*mix(.35,.75,clamp(uNormalBias,0,1));float firstSample=startVoxel;float traceLimit=min(uMaxDistance,max(rayBoxExit(o,d)-firstSample,0.0));float dist=firstSample;vec4 sum=vec4(0);
 for(int i=0;i<48&&dist<traceLimit&&sum.a<.98;i++){float dia=max(startVoxel,2.0*uAperture*dist),sampleVoxelSize;vec4 s=sampleWorld(o+d*dist,d,dia,sampleVoxelSize);sum.rgb+=(1-sum.a)*s.rgb;sum.a+=(1-sum.a)*s.a;dist+=max(sampleVoxelSize,dia*.5);}return sum.rgb;}
void main(){vec3 p=texture(uScenePositionTexture,UV).xyz,rawNormal=texture(uSceneNormalTexture,UV).xyz;float normalLengthSquared=dot(rawNormal,rawNormal);vec3 surfaceTc=(p-uVolumeOrigin)/uVolumeSize;if(!(normalLengthSquared>=.1&&normalLengthSquared<=1e6)||any(lessThan(surfaceTc,vec3(0)))||any(greaterThanEqual(surfaceTc,vec3(1)))){FragColor=vec4(0);return;}vec3 n=rawNormal*inversesqrt(normalLengthSquared);float viewDepth=max(-(uView*vec4(p,1)).z,0.0);
 if(uDebugView==1){vec4 voxel=sampleDirectional(surfaceTc,n,0);FragColor=vec4(voxel.rgb/(vec3(1)+voxel.rgb),viewDepth);return;}
 if(uDebugView==2){float opacity=sampleDirectional(surfaceTc,n,0).a;FragColor=vec4(vec3(opacity),viewDepth);return;}
 if(uDebugView==3){ivec3 countSize=textureSize(uVoxelSampleCount,0);ivec3 countCoord=clamp(ivec3(surfaceTc*vec3(countSize)),ivec3(0),countSize-ivec3(1));uint count=texelFetch(uVoxelSampleCount,countCoord,0).r;float level=clamp(log2(float(count)+1.0)/20.0,0.0,1.0);vec3 countColor=count>=1048575u?vec3(1,0,0):vec3(level);FragColor=vec4(countColor,viewDepth);return;}
 vec3 up=abs(n.y)<.99?vec3(0,1,0):vec3(1,0,0),t=normalize(cross(up,n)),b=cross(n,t);vec3 total=cone(p,n,n);
 for(int i=1;i<6;i++){if(i>=uConeCount)break;float a=6.2831853*float(i-1)/max(float(uConeCount-1),1);vec3 d=normalize(n*.55+(t*cos(a)+b*sin(a))*.835);total+=cone(p,n,d);}
 // Keep the low-resolution result receiver-independent. Applying albedo here
 // lets a low-resolution foliage/material sample paint its colour onto another
 // surface during upsampling. The full-resolution composite applies the actual
 // receiver's diffuse albedo and metallic attenuation instead.
 FragColor=vec4(total*(uIntensity/max(float(uConeCount),1.0))/PI,viewDepth);})";
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
    vec3 history=clamp(texelFetch(uHistoryColorTexture,bestCoord,0).rgb,lo-padding,hi+padding);
    float historyWeight=uTemporalBlend*clamp(1-length(motion)*32.0,0,1);
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
        for (auto &cascade : m_cascades)
        {
            if (cascade.framebuffer)
                glDeleteFramebuffers(1, &cascade.framebuffer);
            cascade.framebuffer = 0;
            glDeleteTextures(static_cast<GLsizei>(cascade.radianceVolumes.size()), cascade.radianceVolumes.data());
            cascade.radianceVolumes.fill(0);
            const unsigned int transientTextures[] = {
                cascade.accumulationR, cascade.accumulationG, cascade.accumulationB,
                cascade.accumulationCount, cascade.accumulationOpacity};
            glDeleteTextures(static_cast<GLsizei>(std::size(transientTextures)), transientTextures);
            glDeleteTextures(static_cast<GLsizei>(cascade.pendingShadowMaps.size()), cascade.pendingShadowMaps.data());
            cascade.pendingShadowMaps.fill(0);
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
        m_allocatedResolution = 0;
    }

    void VoxelConeTracingEffect::EnsureResources(int width, int height)
    {
        if (m_allocatedResolution != m_resolution)
        {
            ReleaseVolume();
            // Use one camera-following volume until cross-volume radiance
            // normalization and blending are available.
            m_activeCascadeCount = 1u;
            const int mipCount = 1 + static_cast<int>(std::floor(std::log2(m_resolution)));
            for (std::size_t cascadeIndex = 0; cascadeIndex < m_activeCascadeCount; ++cascadeIndex)
            {
                auto &cascade = m_cascades[cascadeIndex];
                cascade.size = m_volumeSize * std::pow(3.0f, static_cast<float>(cascadeIndex));
                glGenTextures(static_cast<GLsizei>(cascade.radianceVolumes.size()), cascade.radianceVolumes.data());
                for (const unsigned int volume : cascade.radianceVolumes)
                {
                    glBindTexture(GL_TEXTURE_3D, volume);
                    glTexStorage3D(GL_TEXTURE_3D, mipCount, GL_RGBA16F, m_resolution, m_resolution, m_resolution);
                    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
                    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
                    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
                }
                unsigned int accumulationVolumes[5]{};
                glGenTextures(5, accumulationVolumes);
                cascade.accumulationR = accumulationVolumes[0];
                cascade.accumulationG = accumulationVolumes[1];
                cascade.accumulationB = accumulationVolumes[2];
                cascade.accumulationCount = accumulationVolumes[3];
                cascade.accumulationOpacity = accumulationVolumes[4];
                for (const unsigned int volume : accumulationVolumes)
                {
                    glBindTexture(GL_TEXTURE_3D, volume);
                    glTexStorage3D(GL_TEXTURE_3D, 1, GL_R32UI, m_resolution, m_resolution, m_resolution);
                    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                }
                glGenFramebuffers(1, &cascade.framebuffer);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            m_allocatedResolution = m_resolution;
            m_nextCascadeToUpdate = 0;
        }
        const bool resized = m_indirectTarget && (m_indirectTarget->GetWidth() != width || m_indirectTarget->GetHeight() != height);
        if (!m_indirectTarget)
            m_indirectTarget = std::make_unique<RenderTarget>(RenderTargetConfig{.width = width, .height = height, .clearColor = glm::vec4(0)});
        else if (resized)
            m_indirectTarget->Resize(width, height);
        if (!m_debugTarget)
            m_debugTarget = std::make_unique<RenderTarget>(RenderTargetConfig{.width = width, .height = height, .clearColor = glm::vec4(0)});
        else if (m_debugTarget->GetWidth() != width || m_debugTarget->GetHeight() != height)
            m_debugTarget->Resize(width, height);
        for (auto &target : m_historyColorTargets)
        {
            if (!target) target = std::make_unique<RenderTarget>(RenderTargetConfig{.width=width,.height=height,.clearColor=glm::vec4(0)});
            else if (target->GetWidth()!=width || target->GetHeight()!=height) target->Resize(width,height);
            glBindTexture(GL_TEXTURE_2D, target->GetColorTextureID());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        for (auto &target : m_historyMetadataTargets)
        {
            if (!target) target = std::make_unique<RenderTarget>(RenderTargetConfig{.width=width,.height=height,.clearColor=glm::vec4(0)});
            else if (target->GetWidth()!=width || target->GetHeight()!=height) target->Resize(width,height);
            glBindTexture(GL_TEXTURE_2D, target->GetColorTextureID());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        glBindTexture(GL_TEXTURE_2D, 0);
        if (resized) ResetHistory();
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
        cascade.pendingShadowCascadeCount = directionalLight && directionalLight->castsShadows
                                                ? std::clamp(directionalLight->activeShadowCascadeCount, 0, scene::kMaxDirectionalShadowCascades)
                                                : 0;
        glDeleteTextures(static_cast<GLsizei>(cascade.pendingShadowMaps.size()), cascade.pendingShadowMaps.data());
        cascade.pendingShadowMaps.fill(0);
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
            glGenTextures(1, &cascade.pendingShadowMaps[shadowCascade]);
            glBindTexture(GL_TEXTURE_2D, cascade.pendingShadowMaps[shadowCascade]);
            glTexStorage2D(GL_TEXTURE_2D, 1, GL_DEPTH_COMPONENT24, shadowWidth, shadowHeight);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            const float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
            glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
            glCopyImageSubData(
                sourceMap->GetTextureID(), GL_TEXTURE_2D, 0, 0, 0, 0,
                cascade.pendingShadowMaps[shadowCascade], GL_TEXTURE_2D, 0, 0, 0, 0,
                shadowWidth, shadowHeight, 1);
        }
        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        cascade.jobIndex = 0;
        cascade.jobs.clear();
        cascade.jobs.reserve(commands.size());
        for (const auto &command : commands)
        {
            if (!command.mesh || !command.material || !IntersectsVoxelVolume(command, volumeOrigin, cascade.size))
                continue;
            std::shared_ptr<const std::vector<glm::mat4>> jointSnapshot;
            if (command.jointMatrices)
                jointSnapshot = std::make_shared<const std::vector<glm::mat4>>(*command.jointMatrices);
            auto materialSnapshot = std::make_shared<Material>(command.material->GetConfig());
            auto snapshot = command;
            snapshot.material = materialSnapshot.get();
            snapshot.jointMatrices = jointSnapshot ? jointSnapshot.get() : nullptr;
            cascade.jobs.push_back(VoxelizationJob{
                .command = std::move(snapshot),
                .material = std::move(materialSnapshot),
                .jointMatrices = std::move(jointSnapshot),
                .nextInstance = 0,
            });
        }
        cascade.rebuildInProgress = true;

        const unsigned int zero[4] = {0, 0, 0, 0};
        glBindFramebuffer(GL_FRAMEBUFFER, cascade.framebuffer);
        const unsigned int accumulationVolumes[] = {
            cascade.accumulationR, cascade.accumulationG, cascade.accumulationB,
            cascade.accumulationCount, cascade.accumulationOpacity};
        for (const unsigned int volume : accumulationVolumes)
        {
            glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, volume, 0);
            glClearBufferuiv(GL_COLOR, 0, zero);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        // The accumulation textures switch from framebuffer clears to shader
        // image atomics. Make every cleared layer visible before the first chunk.
        glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }

    void VoxelConeTracingEffect::GenerateDirectionalMips(VoxelCascade &cascade)
    {
        if (!m_directionalMipShader)
            return;
        const int mipCount = 1 + static_cast<int>(std::floor(std::log2(m_resolution)));
        for (std::size_t direction = 0; direction < kDirectionCount; ++direction)
        {
            if (direction != 0)
            {
                glCopyImageSubData(cascade.radianceVolumes[0], GL_TEXTURE_3D, 0, 0, 0, 0,
                                   cascade.radianceVolumes[direction], GL_TEXTURE_3D, 0, 0, 0, 0,
                                   m_resolution, m_resolution, m_resolution);
                glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
            }
            m_directionalMipShader->Bind();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_3D, cascade.radianceVolumes[direction]);
            m_directionalMipShader->SetUniform("uSource", 0);
            const int axis = static_cast<int>(direction / 2);
            const int sign = direction % 2 == 0 ? 1 : -1;
            m_directionalMipShader->SetUniform("uAxis", axis);
            m_directionalMipShader->SetUniform("uSign", sign);
            for (int mip = 1; mip < mipCount; ++mip)
            {
                const int mipSize = std::max(1, m_resolution >> mip);
                m_directionalMipShader->SetUniform("uSourceMip", mip - 1);
                glBindImageTexture(0, cascade.radianceVolumes[direction], mip, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
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
        // Progressive rebuilds continue on a later frame. Shader-image writes
        // from the previous chunk must be visible before issuing more atomics.
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        glViewport(0, 0, m_resolution, m_resolution);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glBindImageTexture(0, cascade.accumulationR, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
        glBindImageTexture(1, cascade.accumulationG, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
        glBindImageTexture(2, cascade.accumulationB, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
        glBindImageTexture(3, cascade.accumulationCount, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
        glBindImageTexture(4, cascade.accumulationOpacity, 0, GL_TRUE, 0, GL_READ_WRITE, GL_R32UI);
        m_voxelizationShader->Bind();
        m_voxelizationShader->SetUniform("uVolumeOrigin", cascade.pendingOrigin);
        m_voxelizationShader->SetUniform("uVolumeSize", cascade.size);
        m_voxelizationShader->SetUniform("uHasInjectionLight", cascade.pendingHasInjectionLight ? 1 : 0);
        m_voxelizationShader->SetUniform("uLightDirection", cascade.pendingLightDirection);
        m_voxelizationShader->SetUniform("uLightColor", cascade.pendingLightColor);
        m_voxelizationShader->SetUniform("uLightIntensity", cascade.pendingLightIntensity);
        m_voxelizationShader->SetUniform("uViewMatrix", cascade.pendingView);
        m_voxelizationShader->SetUniform("uInjectionLightHasShadow", cascade.pendingShadowCascadeCount > 0 ? 1 : 0);
        m_voxelizationShader->SetUniform("uShadowCascadeCount", cascade.pendingShadowCascadeCount);
        for (int shadowCascade = 0; shadowCascade < scene::kMaxDirectionalShadowCascades; ++shadowCascade)
        {
            const int slot = 6 + shadowCascade;
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(GL_TEXTURE_2D, cascade.pendingShadowMaps[shadowCascade]);
            m_voxelizationShader->SetUniform("uShadow" + std::to_string(shadowCascade), slot);
            m_voxelizationShader->SetUniform("uShadowMatrix[" + std::to_string(shadowCascade) + "]", cascade.pendingShadowMatrices[shadowCascade]);
            m_voxelizationShader->SetUniform("uShadowOrigin[" + std::to_string(shadowCascade) + "]", cascade.pendingShadowOrigins[shadowCascade]);
            m_voxelizationShader->SetUniform("uShadowSplit[" + std::to_string(shadowCascade) + "]", cascade.pendingShadowSplits[shadowCascade]);
        }
        int submittedDraws = 0;
        while (cascade.jobIndex < cascade.jobs.size() && submittedDraws < m_voxelizationCommandBudget)
        {
            auto &job = cascade.jobs[cascade.jobIndex];
            const auto &c = job.command;
            if (!c.mesh || !c.material)
            {
                ++cascade.jobIndex;
                continue;
            }

            m_voxelizationShader->SetUniform("uEmission", c.material->GetConfig().emission);
            c.material->Bind(m_voxelizationShader);
            const bool skinned = job.jointMatrices && !job.jointMatrices->empty();
            m_voxelizationShader->SetUniform("uUseSkinning", skinned ? 1 : 0);
            if (skinned)
            {
                const std::size_t jointCount = std::min<std::size_t>(job.jointMatrices->size(), 128);
                for (std::size_t jointIndex = 0; jointIndex < jointCount; ++jointIndex)
                    m_voxelizationShader->SetUniform(
                        "uJointMatrices[" + std::to_string(jointIndex) + "]",
                        (*job.jointMatrices)[jointIndex]);
            }

            const std::size_t instanceCount = c.instanceModels && !c.instanceModels->empty()
                                                  ? c.instanceModels->size()
                                                  : 1;
            while (job.nextInstance < instanceCount && submittedDraws < m_voxelizationCommandBudget)
            {
                const glm::mat4 &model = c.instanceModels && !c.instanceModels->empty()
                                             ? (*c.instanceModels)[job.nextInstance]
                                             : c.model;
                m_voxelizationShader->SetUniform("uModel", model);
                // Voxel radiance must use the source mesh. Generated LODs are
                // position-only simplifications; on textured architecture and
                // foliage they can collapse UV seams/cutout cards into long
                // triangles that inject object-coloured light into empty space.
                c.mesh->DrawSubmesh(c.submeshIndex, 0);
                ++job.nextInstance;
                ++submittedDraws;
            }
            if (job.nextInstance >= instanceCount)
                ++cascade.jobIndex;
            else
                break;
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
        glBindImageTexture(5, cascade.radianceVolumes[0], 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
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
        glDeleteTextures(static_cast<GLsizei>(cascade.pendingShadowMaps.size()), cascade.pendingShadowMaps.data());
        cascade.pendingShadowMaps.fill(0);
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
        m_volumeChangedThisFrame = false;
        const float voxelSize = m_volumeSize / static_cast<float>(m_resolution);
        const glm::vec3 cameraPosition = glm::vec3(glm::inverse(renderContext.cameraData.view)[3]);
        const auto *sceneCommands = renderContext.renderer ? &renderContext.renderer->GetSceneRenderCommands() : renderContext.renderCommands;
        if (!sceneCommands)
            return nullptr;
        const std::size_t sceneSignature = sceneCommands ? ComputeSceneSignature(*sceneCommands) : 0;
        const std::size_t lightSignature = ComputeLightSignature(renderContext.lights);

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
            for (std::size_t offset = 0; offset < m_activeCascadeCount; ++offset)
            {
                const std::size_t cascadeIndex = (m_nextCascadeToUpdate + offset) % m_activeCascadeCount;
                auto &cascade = m_cascades[cascadeIndex];
                const bool originChanged = !cascade.hasVolume ||
                    glm::any(glm::notEqual(desiredOrigins[cascadeIndex], cascade.origin));
                const bool contentChanged = !cascade.hasVolume || sceneSignature != cascade.lastSceneSignature ||
                                            lightSignature != cascade.lastLightSignature;
                const bool updateDue = cascade.lastVoxelizedFrame == ~0ull ||
                    renderContext.frameSequence - cascade.lastVoxelizedFrame >= static_cast<unsigned>(m_updateInterval);
                if ((originChanged || contentChanged) && updateDue)
                {
                    BeginVoxelization(
                        cascadeIndex, desiredOrigins[cascadeIndex], sceneSignature, lightSignature,
                        *sceneCommands, renderContext);
                    activeRebuild = cascadeIndex;
                    break;
                }
            }
        }
        if (activeRebuild < m_activeCascadeCount && VoxelizeChunk(activeRebuild, context))
        {
            m_cascades[activeRebuild].lastVoxelizedFrame = renderContext.frameSequence;
            m_nextCascadeToUpdate = (activeRebuild + 1) % m_activeCascadeCount;
            m_volumeChangedThisFrame = true;
            // The radiance field and often its world-space origin changed
            // discontinuously. Reprojecting history from the old volume feeds
            // stale material-coloured samples back into the new result.
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
        Graphics::BindRenderTarget(m_indirectTarget.get());
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        m_coneTraceShader->Bind();
        BindCommonInputs(m_coneTraceShader, context);
        const auto &traceCascade = m_cascades[0];
        m_coneTraceShader->SetUniform("uVolumeOrigin", traceCascade.origin);
        m_coneTraceShader->SetUniform("uVolumeSize", traceCascade.size);
        for (std::size_t direction = 0; direction < kDirectionCount; ++direction)
        {
            const int slot = 5 + static_cast<int>(direction);
            glActiveTexture(GL_TEXTURE0 + slot);
            glBindTexture(GL_TEXTURE_3D, traceCascade.radianceVolumes[direction]);
            m_coneTraceShader->SetUniform("uVoxel" + std::to_string(direction), slot);
        }
        glActiveTexture(GL_TEXTURE11);
        glBindTexture(GL_TEXTURE_3D, traceCascade.accumulationCount);
        m_coneTraceShader->SetUniform("uVoxelSampleCount", 11);
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
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT);
        m_temporalResolveShader->Bind();
        BindCommonInputs(m_temporalResolveShader, context);
        glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, m_indirectTarget->GetColorTextureID()); m_temporalResolveShader->SetUniform("uCurrentIndirectTexture", 5);
        glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, m_historyColorTargets[m_historyIndex]->GetColorTextureID()); m_temporalResolveShader->SetUniform("uHistoryColorTexture", 6);
        glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D, m_historyMetadataTargets[m_historyIndex]->GetColorTextureID()); m_temporalResolveShader->SetUniform("uHistoryMetadataTexture", 7);
        glActiveTexture(GL_TEXTURE8); glBindTexture(GL_TEXTURE_2D, context.renderContext.gBuffer->GetMotionTextureID()); m_temporalResolveShader->SetUniform("uSceneMotionTexture", 8);
        m_temporalResolveShader->SetUniform("uView", context.renderContext.cameraData.view);
        m_temporalResolveShader->SetUniform("uPreviousView", m_previousView);
        m_temporalResolveShader->SetUniform(
            "uTemporalBlend",
            m_volumeChangedThisFrame ? std::min(m_temporalBlend, 0.35f) : m_temporalBlend);
        m_temporalResolveShader->SetUniform("uHistoryDepthThreshold", m_historyDepthThreshold);
        m_temporalResolveShader->SetUniform("uHistoryNormalThreshold", m_historyNormalThreshold);
        m_temporalResolveShader->SetUniform("uHasHistory", m_hasHistory ? 1 : 0);
        m_temporalResolveShader->SetUniform("uDebugView", 0);
        DrawFullscreenTriangle();

        Graphics::BindRenderTarget(m_historyMetadataTargets[next].get());
        glViewport(0, 0, width, height);
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
            glViewport(0, 0, width, height);
            glClear(GL_COLOR_BUFFER_BIT);
            m_temporalResolveShader->Bind();
            BindCommonInputs(m_temporalResolveShader, context);
            glActiveTexture(GL_TEXTURE5); glBindTexture(GL_TEXTURE_2D, m_indirectTarget->GetColorTextureID()); m_temporalResolveShader->SetUniform("uCurrentIndirectTexture", 5);
            glActiveTexture(GL_TEXTURE6); glBindTexture(GL_TEXTURE_2D, m_historyColorTargets[m_historyIndex]->GetColorTextureID()); m_temporalResolveShader->SetUniform("uHistoryColorTexture", 6);
            glActiveTexture(GL_TEXTURE7); glBindTexture(GL_TEXTURE_2D, m_historyMetadataTargets[m_historyIndex]->GetColorTextureID()); m_temporalResolveShader->SetUniform("uHistoryMetadataTexture", 7);
            glActiveTexture(GL_TEXTURE8); glBindTexture(GL_TEXTURE_2D, context.renderContext.gBuffer->GetMotionTextureID()); m_temporalResolveShader->SetUniform("uSceneMotionTexture", 8);
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
        else if (m_debugView == 4 || m_debugView == 5 || m_debugView == 7)
        {
            // Sample the published voxel field at each visible receiver. This
            // separates corrupt volume data from cone/history/upsample errors.
            Graphics::BindRenderTarget(m_debugTarget.get());
            glViewport(0, 0, width, height);
            glClear(GL_COLOR_BUFFER_BIT);
            m_coneTraceShader->Bind();
            BindCommonInputs(m_coneTraceShader, context);
            m_coneTraceShader->SetUniform("uVolumeOrigin", traceCascade.origin);
            m_coneTraceShader->SetUniform("uVolumeSize", traceCascade.size);
            for (std::size_t direction = 0; direction < kDirectionCount; ++direction)
            {
                const int slot = 5 + static_cast<int>(direction);
                glActiveTexture(GL_TEXTURE0 + slot);
                glBindTexture(GL_TEXTURE_3D, traceCascade.radianceVolumes[direction]);
                m_coneTraceShader->SetUniform("uVoxel" + std::to_string(direction), slot);
            }
            glActiveTexture(GL_TEXTURE11);
            glBindTexture(GL_TEXTURE_3D, traceCascade.accumulationCount);
            m_coneTraceShader->SetUniform("uVoxelSampleCount", 11);
            m_coneTraceShader->SetUniform("uIntensity", m_intensity);
            m_coneTraceShader->SetUniform("uAperture", m_aperture);
            m_coneTraceShader->SetUniform("uMaxDistance", std::min(m_maxDistance, safeMaxDistance));
            m_coneTraceShader->SetUniform("uNormalBias", m_normalBias);
            m_coneTraceShader->SetUniform("uConeCount", m_coneCount);
            m_coneTraceShader->SetUniform("uMaxMip", std::floor(std::log2(float(m_resolution))));
            m_coneTraceShader->SetUniform("uView", context.renderContext.cameraData.view);
            m_coneTraceShader->SetUniform("uDebugView", m_debugView == 4 ? 1 : (m_debugView == 5 ? 2 : 3));
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
        glBindFramebuffer(GL_READ_FRAMEBUFFER, context.sourceRenderTarget->GetFramebufferID());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, context.destinationRenderTarget->GetFramebufferID());
        glBlitFramebuffer(0, 0, context.sourceRenderTarget->GetWidth(), context.sourceRenderTarget->GetHeight(), 0, 0, context.destinationRenderTarget->GetWidth(), context.destinationRenderTarget->GetHeight(), GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void VoxelConeTracingEffect::ResetHistory()
    {
        m_hasHistory = false;
        m_historyIndex = 0;
    }
}
