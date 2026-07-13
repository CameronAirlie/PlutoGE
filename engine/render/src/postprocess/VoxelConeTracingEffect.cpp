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
                    for (const auto &model : *command.instanceModels)
                        commandHash = HashBytes(glm::value_ptr(model), sizeof(glm::mat4), commandHash);
                }
                if (command.material)
                {
                    const auto &config = command.material->GetConfig();
                    commandHash = HashBytes(glm::value_ptr(config.color), sizeof(glm::vec4), commandHash);
                    commandHash = HashBytes(glm::value_ptr(config.emission), sizeof(glm::vec3), commandHash);
                    commandHash = HashValue(config.albedoTexture, commandHash);
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

        std::size_t ComputeLightSignature(const std::vector<scene::Light *> *lights)
        {
            if (!lights) return 0;
            std::size_t hash = HashValue(lights->size(), 1469598103934665603ull);
            for (const auto *light : *lights)
            {
                hash = HashValue(light, hash);
                if (!light) continue;
                hash = HashValue(light->type, hash);
                hash = HashBytes(glm::value_ptr(light->direction), sizeof(glm::vec3), hash);
                hash = HashBytes(glm::value_ptr(light->color), sizeof(glm::vec3), hash);
                hash = HashValue(light->intensity, hash);
                hash = HashValue(light->castsShadows, hash);
                hash = HashValue(light->activeShadowCascadeCount, hash);
            }
            return hash;
        }
    }

    VoxelConeTracingEffect::~VoxelConeTracingEffect() { ReleaseVolume(); }

    std::vector<PostProcessParameter> VoxelConeTracingEffect::GetParameters() const
    {
        return {
            {"Resolution", PostProcessParameterType::Enum, std::to_string(m_resolution == 64 ? 0 : m_resolution == 128 ? 1
                                                                                                                       : 2),
             {"64", "128", "256"}},
            {"Volume Size", PostProcessParameterType::Float, std::to_string(m_volumeSize)},
            {"Intensity", PostProcessParameterType::Float, std::to_string(m_intensity)},
            {"Cone Count", PostProcessParameterType::Int, std::to_string(m_coneCount)},
            {"Voxelization LOD Bias", PostProcessParameterType::Int, std::to_string(m_voxelizationLodBias)},
            {"Cone Aperture", PostProcessParameterType::Float, std::to_string(m_aperture)},
            {"Max Distance", PostProcessParameterType::Float, std::to_string(m_maxDistance)},
            {"Normal Bias", PostProcessParameterType::Float, std::to_string(m_normalBias)},
            {"Update Interval", PostProcessParameterType::Int, std::to_string(m_updateInterval)},
            {"Temporal Blend", PostProcessParameterType::Float, std::to_string(m_temporalBlend)},
            {"History Depth Threshold", PostProcessParameterType::Float, std::to_string(m_historyDepthThreshold)},
            {"History Normal Threshold", PostProcessParameterType::Float, std::to_string(m_historyNormalThreshold)},
            {"Indirect Only", PostProcessParameterType::Bool, m_indirectOnly ? "true" : "false"},
        };
    }

    void VoxelConeTracingEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        for (const auto &p : parameters)
        {
            if (p.name == "Resolution")
            {
                const int values[] = {64, 128, 256};
                const int next = values[std::clamp(std::stoi(p.value), 0, 2)];
                if (next != m_resolution) { m_resolution = next; ResetHistory(); }
            }
            else if (p.name == "Volume Size")
            {
                const float next = std::clamp(std::stof(p.value), 4.0f, 512.0f);
                if (next != m_volumeSize) { m_volumeSize = next; ResetHistory(); }
            }
            else if (p.name == "Intensity")
                m_intensity = std::clamp(std::stof(p.value), 0.0f, 8.0f);
            else if (p.name == "Cone Count")
                m_coneCount = std::clamp(std::stoi(p.value), 1, 6);
            else if (p.name == "Voxelization LOD Bias")
            {
                const int next = std::clamp(std::stoi(p.value), 0, 8);
                if (next != m_voxelizationLodBias)
                {
                    m_voxelizationLodBias = next;
                    m_hasVoxelVolume = false;
                    ResetHistory();
                }
            }
            else if (p.name == "Cone Aperture")
                m_aperture = std::clamp(std::stof(p.value), 0.1f, 1.2f);
            else if (p.name == "Max Distance")
                m_maxDistance = std::clamp(std::stof(p.value), 1.0f, 256.0f);
            else if (p.name == "Normal Bias")
                m_normalBias = std::clamp(std::stof(p.value), 0.0f, 4.0f);
            else if (p.name == "Update Interval")
                m_updateInterval = std::clamp(std::stoi(p.value), 1, 16);
            else if (p.name == "Temporal Blend")
                m_temporalBlend = std::clamp(std::stof(p.value), 0.0f, 0.98f);
            else if (p.name == "History Depth Threshold")
                m_historyDepthThreshold = std::clamp(std::stof(p.value), 0.001f, 0.25f);
            else if (p.name == "History Normal Threshold")
                m_historyNormalThreshold = std::clamp(std::stof(p.value), 0.0f, 0.999f);
            else if (p.name == "Indirect Only")
                m_indirectOnly = ParseBool(p.value);
        }
    }

    void VoxelConeTracingEffect::Initialize()
    {
        ShaderSource voxel;
        voxel.vertexSource = R"(#version 430 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNormal; layout(location=2) in vec2 aUV;
uniform mat4 uModel; out VS { vec3 p; vec3 n; vec2 uv; } v;
void main(){ vec4 p=uModel*vec4(aPos,1); v.p=p.xyz; v.n=normalize(transpose(inverse(mat3(uModel)))*aNormal); v.uv=aUV; gl_Position=p; })";
        voxel.geometrySource = R"(#version 430 core
layout(triangles) in; layout(triangle_strip,max_vertices=3) out;
in VS { vec3 p; vec3 n; vec2 uv; } vin[]; out GS { vec3 p; vec3 n; vec2 uv; } g;
uniform vec3 uVolumeOrigin; uniform float uVolumeSize;
void main(){ vec3 n=abs(cross(vin[1].p-vin[0].p,vin[2].p-vin[0].p)); int axis=n.y>n.x?(n.z>n.y?2:1):(n.z>n.x?2:0);
 for(int i=0;i<3;i++){ vec3 q=(vin[i].p-uVolumeOrigin)/uVolumeSize*2.0-1.0; g.p=vin[i].p;g.n=vin[i].n;g.uv=vin[i].uv;
 gl_Position=axis==0?vec4(q.zy,0,1):axis==1?vec4(q.xz,0,1):vec4(q.xy,0,1);EmitVertex();}EndPrimitive();})";
        voxel.fragmentSource = R"(#version 430 core
layout(rgba16f,binding=0) uniform image3D uVolume; in GS { vec3 p; vec3 n; vec2 uv; } g;
uniform vec3 uVolumeOrigin,uLightDirection,uLightColor,uEmission; uniform float uVolumeSize,uLightIntensity;
uniform vec4 uColor; uniform sampler2D uAlbedoTexture; uniform float uHasAlbedoTexture;
uniform sampler2D uShadow0,uShadow1,uShadow2,uShadow3;uniform mat4 uShadowMatrix[4];uniform vec3 uShadowOrigin[4];uniform float uShadowSplit[4];uniform vec3 uCameraPosition;uniform int uShadowCascadeCount;
float shadowSample(int c,vec2 uv){if(c==0)return texture(uShadow0,uv).r;if(c==1)return texture(uShadow1,uv).r;if(c==2)return texture(uShadow2,uv).r;return texture(uShadow3,uv).r;}
float visibility(vec3 p,vec3 n){if(uShadowCascadeCount<=0)return 1;float d=length(p-uCameraPosition);int c=uShadowCascadeCount-1;for(int i=0;i<4;i++){if(i>=uShadowCascadeCount)break;if(d<=uShadowSplit[i]){c=i;break;}}
 vec4 lp=uShadowMatrix[c]*vec4(p-uShadowOrigin[c],1);vec3 q=lp.xyz/max(lp.w,.0001);q=q*.5+.5;if(any(lessThan(q,vec3(0)))||any(greaterThan(q,vec3(1))))return 1;
 float bias=max(.00015*(1.0-max(dot(normalize(n),-normalize(uLightDirection)),0.0)),.00003);return q.z-bias<=shadowSample(c,q.xy)?1:0;}
void main(){ vec3 tc=(g.p-uVolumeOrigin)/uVolumeSize; if(any(lessThan(tc,vec3(0)))||any(greaterThanEqual(tc,vec3(1))))discard;
 vec4 a=uColor;if(uHasAlbedoTexture>.5)a*=texture(uAlbedoTexture,g.uv);if(a.a<.1)discard;
 float ndl=max(dot(normalize(g.n),-normalize(uLightDirection)),0); vec3 r=a.rgb*uLightColor*uLightIntensity*ndl*visibility(g.p,g.n)+uEmission;
 imageStore(uVolume,ivec3(tc*imageSize(uVolume)),vec4(r,clamp(a.a,0,1)));})";
        m_voxelizationShader = Shader::Create(voxel);

        ShaderSource trace;
        trace.vertexSource = R"(#version 430 core
out vec2 UV;void main(){vec2 p[3]=vec2[3](vec2(-1),vec2(3,-1),vec2(-1,3));gl_Position=vec4(p[gl_VertexID],0,1);UV=gl_Position.xy*.5+.5;})";
        trace.fragmentSource = R"(#version 430 core
in vec2 UV;out vec4 FragColor;uniform sampler2D uScenePositionTexture,uSceneNormalTexture,uSceneAlbedoTexture;uniform sampler3D uVoxelVolume;
uniform vec3 uVolumeOrigin;uniform float uVolumeSize,uIntensity,uAperture,uMaxDistance,uNormalBias;uniform int uConeCount;uniform float uMaxMip;
const float PI=3.14159265; vec3 cone(vec3 o,vec3 d){float vox=uVolumeSize/float(textureSize(uVoxelVolume,0).x),dist=vox*uNormalBias;vec4 sum=vec4(0);
 for(int i=0;i<48&&dist<uMaxDistance&&sum.a<.98;i++){float dia=max(vox,2.0*uAperture*dist);float lod=clamp(log2(dia/vox),0,uMaxMip);vec3 tc=(o+d*dist-uVolumeOrigin)/uVolumeSize;if(any(lessThan(tc,vec3(0)))||any(greaterThan(tc,vec3(1))))break;
 vec4 s=textureLod(uVoxelVolume,tc,lod);sum.rgb+=(1-sum.a)*s.rgb;sum.a+=(1-sum.a)*s.a;dist+=max(vox,dia*.5);}return sum.rgb;}
void main(){vec3 p=texture(uScenePositionTexture,UV).xyz,n=normalize(texture(uSceneNormalTexture,UV).xyz),alb=texture(uSceneAlbedoTexture,UV).rgb;if(dot(n,n)<.1){FragColor=vec4(0);return;}
 vec3 up=abs(n.y)<.99?vec3(0,1,0):vec3(1,0,0),t=normalize(cross(up,n)),b=cross(n,t);vec3 total=cone(p,n);
 for(int i=1;i<6;i++){if(i>=uConeCount)break;float a=6.2831853*float(i-1)/max(float(uConeCount-1),1);vec3 d=normalize(n*.55+(t*cos(a)+b*sin(a))*.835);total+=cone(p,d);}
 FragColor=vec4(alb*total*(uIntensity/max(float(uConeCount),1.0))/PI,1);})";
        m_coneTraceShader = Shader::Create(trace);

        ShaderSource temporal;
        temporal.vertexSource = trace.vertexSource;
        temporal.fragmentSource = R"(#version 430 core
in vec2 UV;out vec4 FragColor;
uniform sampler2D uCurrentIndirectTexture,uHistoryColorTexture,uHistoryMetadataTexture,uSceneMotionTexture,uScenePositionTexture,uSceneNormalTexture;
uniform mat4 uPreviousView;uniform float uNearPlane,uFarPlane,uTemporalBlend,uHistoryDepthThreshold,uHistoryNormalThreshold;uniform int uHasHistory;
vec3 decodeNormal(vec2 e){vec2 f=e*2-1;vec3 n=vec3(f,1-abs(f.x)-abs(f.y));float t=clamp(-n.z,0,1);n.xy+=vec2(n.x>=0?-t:t,n.y>=0?-t:t);return normalize(n);}
float depthNorm(float d){return clamp((d-uNearPlane)/max(uFarPlane-uNearPlane,.0001),0,1);}
void main(){vec3 current=texture(uCurrentIndirectTexture,UV).rgb,resolved=current;vec3 p=texture(uScenePositionTexture,UV).xyz,n=normalize(texture(uSceneNormalTexture,UV).xyz);
if(uHasHistory!=0&&dot(n,n)>.01){vec2 motion=texture(uSceneMotionTexture,UV).xy,huv=UV-motion;if(all(greaterThanEqual(huv,vec2(0)))&&all(lessThanEqual(huv,vec2(1)))){vec4 meta=texture(uHistoryMetadataTexture,huv);float pd=depthNorm(-(uPreviousView*vec4(p,1)).z);if(meta.w>.5&&abs(meta.z-pd)<=uHistoryDepthThreshold&&dot(decodeNormal(meta.xy),n)>=uHistoryNormalThreshold){vec2 texel=1.0/vec2(textureSize(uCurrentIndirectTexture,0));vec3 lo=vec3(1e20),hi=vec3(-1e20);for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){vec3 s=texture(uCurrentIndirectTexture,UV+vec2(x,y)*texel).rgb;lo=min(lo,s);hi=max(hi,s);}vec3 padding=max((hi-lo)*.5,vec3(.03));vec3 history=clamp(texture(uHistoryColorTexture,huv).rgb,lo-padding,hi+padding);resolved=mix(current,history,uTemporalBlend*clamp(1-length(motion)*8,0,1));}}}
FragColor=vec4(max(resolved,vec3(0)),1);})";
        m_temporalResolveShader = Shader::Create(temporal);

        ShaderSource metadata;
        metadata.vertexSource = trace.vertexSource;
        metadata.fragmentSource = R"(#version 430 core
in vec2 UV;out vec4 FragColor;uniform sampler2D uScenePositionTexture,uSceneNormalTexture;uniform mat4 uView;uniform float uNearPlane,uFarPlane;
vec2 encodeNormal(vec3 n){n/=abs(n.x)+abs(n.y)+abs(n.z);if(n.z<0)n.xy=(1-abs(n.yx))*sign(n.xy);return n.xy*.5+.5;}
void main(){vec3 p=texture(uScenePositionTexture,UV).xyz,n=normalize(texture(uSceneNormalTexture,UV).xyz);if(dot(n,n)<.01){FragColor=vec4(0);return;}float d=-(uView*vec4(p,1)).z;FragColor=vec4(encodeNormal(n),clamp((d-uNearPlane)/max(uFarPlane-uNearPlane,.0001),0,1),1);})";
        m_historyMetadataShader = Shader::Create(metadata);
    }

    void VoxelConeTracingEffect::ReleaseVolume()
    {
        if (m_voxelFramebuffer)
            glDeleteFramebuffers(1, &m_voxelFramebuffer);
        m_voxelFramebuffer = 0;
        if (m_radianceVolume)
            glDeleteTextures(1, &m_radianceVolume);
        m_radianceVolume = 0;
        m_allocatedResolution = 0;
    }

    void VoxelConeTracingEffect::EnsureResources(int width, int height)
    {
        if (m_allocatedResolution != m_resolution)
        {
            ReleaseVolume();
            glGenTextures(1, &m_radianceVolume);
            glBindTexture(GL_TEXTURE_3D, m_radianceVolume);
            glTexStorage3D(GL_TEXTURE_3D, 1 + static_cast<int>(std::floor(std::log2(m_resolution))), GL_RGBA16F, m_resolution, m_resolution, m_resolution);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
            glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_BORDER);
            glGenFramebuffers(1, &m_voxelFramebuffer);
            glBindFramebuffer(GL_FRAMEBUFFER, m_voxelFramebuffer);
            glFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, m_radianceVolume, 0);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            m_allocatedResolution = m_resolution;
            m_lastVoxelizedFrame = ~0ull;
            m_hasVoxelVolume = false;
        }
        const bool resized = m_indirectTarget && (m_indirectTarget->GetWidth() != width || m_indirectTarget->GetHeight() != height);
        if (!m_indirectTarget)
            m_indirectTarget = std::make_unique<RenderTarget>(RenderTargetConfig{.width = width, .height = height, .clearColor = glm::vec4(0)});
        else if (resized)
            m_indirectTarget->Resize(width, height);
        for (auto &target : m_historyColorTargets)
        {
            if (!target) target = std::make_unique<RenderTarget>(RenderTargetConfig{.width=width,.height=height,.clearColor=glm::vec4(0)});
            else if (target->GetWidth()!=width || target->GetHeight()!=height) target->Resize(width,height);
        }
        for (auto &target : m_historyMetadataTargets)
        {
            if (!target) target = std::make_unique<RenderTarget>(RenderTargetConfig{.width=width,.height=height,.clearColor=glm::vec4(0)});
            else if (target->GetWidth()!=width || target->GetHeight()!=height) target->Resize(width,height);
        }
        if (resized) ResetHistory();
    }

    void VoxelConeTracingEffect::Voxelize(const PostProcessContext &context, const glm::vec3 &volumeOrigin)
    {
        const auto &rc = context.renderContext;
        if (!m_voxelizationShader)
            return;
        const auto *commands = rc.renderer ? &rc.renderer->GetSceneRenderCommands() : rc.renderCommands;
        if (!commands)
            return;
        const glm::vec3 camera = glm::vec3(glm::inverse(rc.cameraData.view)[3]);
        m_volumeOrigin = volumeOrigin;
        const float zero[4] = {0, 0, 0, 0};
        glBindFramebuffer(GL_FRAMEBUFFER, m_voxelFramebuffer);
        glClearBufferfv(GL_COLOR, 0, zero);
        glm::vec3 ld(0, -1, 0), lc(0);
        float li = 0;
        const scene::Light *directionalLight = nullptr;
        if (rc.lights)
        {
            for (auto *l : *rc.lights)
            {
                if (l && l->type == scene::LightType::Directional && l->intensity > li)
                {
                    ld = l->direction;
                    lc = l->color;
                    li = l->intensity;
                    directionalLight = l;
                }
            }
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, m_resolution, m_resolution);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glBindImageTexture(0, m_radianceVolume, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);
        m_voxelizationShader->Bind();
        m_voxelizationShader->SetUniform("uVolumeOrigin", m_volumeOrigin);
        m_voxelizationShader->SetUniform("uVolumeSize", m_volumeSize);
        m_voxelizationShader->SetUniform("uLightDirection", ld);
        m_voxelizationShader->SetUniform("uLightColor", lc);
        m_voxelizationShader->SetUniform("uLightIntensity", li);
        m_voxelizationShader->SetUniform("uCameraPosition", camera);
        const int cascadeCount = directionalLight && directionalLight->castsShadows
                                     ? std::clamp(directionalLight->activeShadowCascadeCount, 0, scene::kMaxDirectionalShadowCascades)
                                     : 0;
        m_voxelizationShader->SetUniform("uShadowCascadeCount", cascadeCount);
        for (int cascade = 0; cascade < scene::kMaxDirectionalShadowCascades; ++cascade)
        {
            const int slot = 6 + cascade;
            glActiveTexture(GL_TEXTURE0 + slot);
            const auto *map = directionalLight ? directionalLight->shadowCascadeMaps[cascade].get() : nullptr;
            glBindTexture(GL_TEXTURE_2D, map ? map->GetTextureID() : 0);
            m_voxelizationShader->SetUniform("uShadow" + std::to_string(cascade), slot);
            m_voxelizationShader->SetUniform("uShadowMatrix[" + std::to_string(cascade) + "]", directionalLight ? directionalLight->shadowCascadeMatrices[cascade] : glm::mat4(1.0f));
            m_voxelizationShader->SetUniform("uShadowOrigin[" + std::to_string(cascade) + "]", directionalLight ? directionalLight->shadowCascadeWorldOrigins[cascade] : glm::vec3(0.0f));
            m_voxelizationShader->SetUniform("uShadowSplit[" + std::to_string(cascade) + "]", directionalLight ? directionalLight->shadowCascadeSplits[cascade] : 0.0f);
        }
        for (const auto &c : *commands)
        {
            if (!c.mesh || !c.material || !IntersectsVoxelVolume(c, m_volumeOrigin, m_volumeSize))
                continue;
            const std::size_t lodCount = c.mesh->GetSubmeshLodCount(c.submeshIndex);
            const std::size_t voxelLod = lodCount > 0
                                             ? std::min<std::size_t>(c.lodIndex + static_cast<std::size_t>(m_voxelizationLodBias), lodCount - 1)
                                             : 0;
            auto draw = [&](const glm::mat4 &model)
            {m_voxelizationShader->SetUniform("uModel",model);m_voxelizationShader->SetUniform("uEmission",c.material->GetConfig().emission);c.material->Bind(m_voxelizationShader);c.mesh->DrawSubmesh(c.submeshIndex,voxelLod); };
            if (c.instanceModels)
                for (const auto &m : *c.instanceModels)
                    draw(m);
            else
                draw(c.model);
        }
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        glBindTexture(GL_TEXTURE_3D, m_radianceVolume);
        glGenerateMipmap(GL_TEXTURE_3D);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }

    RenderTarget *VoxelConeTracingEffect::GenerateResolvedIndirectLighting(const PostProcessContext &context, int width, int height)
    {
        if (!m_coneTraceShader || !m_temporalResolveShader || !m_historyMetadataShader || !context.renderContext.gBuffer || !context.renderContext.hasCameraData)
            return nullptr;
        EnsureResources(width, height);
        if (!m_radianceVolume || !m_indirectTarget)
            return nullptr;
        const auto &renderContext = context.renderContext;
        const float voxelSize = m_volumeSize / static_cast<float>(m_resolution);
        const glm::vec3 cameraPosition = glm::vec3(glm::inverse(renderContext.cameraData.view)[3]);
        glm::vec3 snappedOrigin = m_volumeOrigin;
        if (!m_hasVoxelVolume)
        {
            snappedOrigin = glm::floor((cameraPosition - glm::vec3(m_volumeSize * 0.5f)) / voxelSize) * voxelSize;
        }
        else
        {
            // Keep the voxel-to-world mapping fixed while the camera remains in
            // the central tracking region. Rebuilding for every one-voxel camera
            // step produces visible noise because voxel image writes are unordered.
            const glm::vec3 trackingMin = m_volumeOrigin + glm::vec3(m_volumeSize * 0.30f);
            const glm::vec3 trackingMax = m_volumeOrigin + glm::vec3(m_volumeSize * 0.70f);
            if (glm::any(glm::lessThan(cameraPosition, trackingMin)) ||
                glm::any(glm::greaterThan(cameraPosition, trackingMax)))
            {
                const float scrollStep = voxelSize * 8.0f;
                snappedOrigin = glm::floor((cameraPosition - glm::vec3(m_volumeSize * 0.5f)) / scrollStep) * scrollStep;
            }
        }
        const auto *sceneCommands = renderContext.renderer ? &renderContext.renderer->GetSceneRenderCommands() : renderContext.renderCommands;
        const std::size_t sceneSignature = sceneCommands ? ComputeSceneSignature(*sceneCommands) : 0;
        const std::size_t lightSignature = ComputeLightSignature(renderContext.lights);
        const bool originChanged = !m_hasVoxelVolume || glm::any(glm::notEqual(snappedOrigin, m_volumeOrigin));
        const bool contentChanged = !m_hasVoxelVolume || sceneSignature != m_lastSceneSignature || lightSignature != m_lastLightSignature;
        const bool updateDue = m_lastVoxelizedFrame == ~0ull || renderContext.frameSequence - m_lastVoxelizedFrame >= static_cast<unsigned>(m_updateInterval);
        if ((originChanged || contentChanged) && updateDue)
        {
            Voxelize(context, snappedOrigin);
            m_lastVoxelizedFrame = renderContext.frameSequence;
            m_lastSceneSignature = sceneSignature;
            m_lastLightSignature = lightSignature;
            m_hasVoxelVolume = true;
        }
        Graphics::BindRenderTarget(m_indirectTarget.get());
        glViewport(0, 0, width, height);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        m_coneTraceShader->Bind();
        BindCommonInputs(m_coneTraceShader, context);
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_3D, m_radianceVolume);
        m_coneTraceShader->SetUniform("uVoxelVolume", 5);
        m_coneTraceShader->SetUniform("uVolumeOrigin", m_volumeOrigin);
        m_coneTraceShader->SetUniform("uVolumeSize", m_volumeSize);
        m_coneTraceShader->SetUniform("uIntensity", m_intensity);
        m_coneTraceShader->SetUniform("uAperture", m_aperture);
        m_coneTraceShader->SetUniform("uMaxDistance", std::min(m_maxDistance, m_volumeSize * .5f));
        m_coneTraceShader->SetUniform("uNormalBias", m_normalBias);
        m_coneTraceShader->SetUniform("uConeCount", m_coneCount);
        m_coneTraceShader->SetUniform("uMaxMip", std::floor(std::log2(float(m_resolution))));
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
        m_temporalResolveShader->SetUniform("uPreviousView", m_previousView);
        m_temporalResolveShader->SetUniform("uNearPlane", context.renderContext.cameraData.nearPlane);
        m_temporalResolveShader->SetUniform("uFarPlane", context.renderContext.cameraData.farPlane);
        m_temporalResolveShader->SetUniform("uTemporalBlend", m_temporalBlend);
        m_temporalResolveShader->SetUniform("uHistoryDepthThreshold", m_historyDepthThreshold);
        m_temporalResolveShader->SetUniform("uHistoryNormalThreshold", m_historyNormalThreshold);
        m_temporalResolveShader->SetUniform("uHasHistory", m_hasHistory ? 1 : 0);
        DrawFullscreenTriangle();

        Graphics::BindRenderTarget(m_historyMetadataTargets[next].get());
        glViewport(0, 0, width, height);
        glClear(GL_COLOR_BUFFER_BIT);
        m_historyMetadataShader->Bind();
        BindCommonInputs(m_historyMetadataShader, context);
        m_historyMetadataShader->SetUniform("uView", context.renderContext.cameraData.view);
        m_historyMetadataShader->SetUniform("uNearPlane", context.renderContext.cameraData.nearPlane);
        m_historyMetadataShader->SetUniform("uFarPlane", context.renderContext.cameraData.farPlane);
        DrawFullscreenTriangle();
        m_historyIndex = next;
        m_previousView = context.renderContext.cameraData.view;
        m_hasHistory = true;
        return resolvedColor;
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
