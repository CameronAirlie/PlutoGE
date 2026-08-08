#include "PlutoGE/render/postprocess/SurfaceCacheGIEffect.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <algorithm>
#include <functional>

namespace PlutoGE::render
{
    namespace
    {
        bool ParseBool(const std::string &value) { return value == "true" || value == "1"; }
        void HashCombine(std::size_t &seed, std::size_t value)
        {
            seed ^= value + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        }
    }

    SurfaceCacheGIEffect::~SurfaceCacheGIEffect()
    {
    }

    std::vector<PostProcessParameter> SurfaceCacheGIEffect::GetParameters() const
    {
        return {
            {"Atlas Size", PostProcessParameterType::Int, std::to_string(m_atlasSize)},
            {"Texels Per Unit", PostProcessParameterType::Int, std::to_string(m_texelsPerUnit)},
            {"Minimum Card Resolution", PostProcessParameterType::Int, std::to_string(m_minCardResolution)},
            {"Maximum Card Resolution", PostProcessParameterType::Int, std::to_string(m_maxCardResolution)},
            {"Capture Budget", PostProcessParameterType::Int, std::to_string(m_captureBudget)},
            {"Maximum Capture Lights", PostProcessParameterType::Int, std::to_string(m_maxCaptureLights)},
            {"Radiance Intensity", PostProcessParameterType::Float, std::to_string(m_radianceIntensity)},
            {"Environment Intensity", PostProcessParameterType::Float, std::to_string(m_environmentIntensity)},
            {"Radiance Clamp", PostProcessParameterType::Float, std::to_string(m_radianceClamp)},
            {"Directional Shadows", PostProcessParameterType::Bool, m_directionalShadows ? "true" : "false"},
            {"Static Geometry Only", PostProcessParameterType::Bool, m_staticGeometryOnly ? "true" : "false"},
            {"Debug View", PostProcessParameterType::Enum, std::to_string(m_debugView), {"Scene", "Albedo / Metallic", "Normal / Roughness", "Emission", "Card Depth", "Direct Radiance"}},
        };
    }

    void SurfaceCacheGIEffect::SetParameters(const std::vector<PostProcessParameter> &parameters)
    {
        int previousAtlasSize = m_atlasSize;
        const int previousTexelsPerUnit = m_texelsPerUnit;
        const int previousMinimumResolution = m_minCardResolution;
        const int previousMaximumResolution = m_maxCardResolution;
        for (const auto &parameter : parameters)
        {
            if (parameter.name == "Atlas Size") m_atlasSize = std::clamp(std::stoi(parameter.value), 512, 8192);
            else if (parameter.name == "Texels Per Unit") m_texelsPerUnit = std::clamp(std::stoi(parameter.value), 4, 256);
            else if (parameter.name == "Minimum Card Resolution") m_minCardResolution = std::clamp(std::stoi(parameter.value), 8, 128);
            else if (parameter.name == "Maximum Card Resolution") m_maxCardResolution = std::clamp(std::stoi(parameter.value), 32, 1024);
            else if (parameter.name == "Capture Budget") m_captureBudget = std::clamp(std::stoi(parameter.value), 1, 256);
            else if (parameter.name == "Maximum Capture Lights") m_maxCaptureLights = std::clamp(std::stoi(parameter.value), 1, 8);
            else if (parameter.name == "Radiance Intensity") m_radianceIntensity = std::clamp(std::stof(parameter.value), 0.0f, 8.0f);
            else if (parameter.name == "Environment Intensity") m_environmentIntensity = std::clamp(std::stof(parameter.value), 0.0f, 8.0f);
            else if (parameter.name == "Radiance Clamp") m_radianceClamp = std::clamp(std::stof(parameter.value), 1.0f, 256.0f);
            else if (parameter.name == "Directional Shadows") m_directionalShadows = ParseBool(parameter.value);
            else if (parameter.name == "Static Geometry Only") { const bool value = ParseBool(parameter.value); if (value != m_staticGeometryOnly) { m_staticGeometryOnly = value; m_cacheLayoutDirty = true; } }
            else if (parameter.name == "Debug View") m_debugView = std::clamp(std::stoi(parameter.value), 0, 5);
        }
        m_maxCardResolution = std::max(m_maxCardResolution, m_minCardResolution);
        m_cacheLayoutDirty = m_cacheLayoutDirty || previousAtlasSize != m_atlasSize ||
                             previousTexelsPerUnit != m_texelsPerUnit ||
                             previousMinimumResolution != m_minCardResolution ||
                             previousMaximumResolution != m_maxCardResolution;
        if (previousAtlasSize != m_atlasSize)
        {
            m_atlas.reset();
        }
    }

    void SurfaceCacheGIEffect::Initialize()
    {
        ShaderSource capture;
        capture.vertexSource = R"(
            #version 330 core
            layout(location=0) in vec3 aPos;
            layout(location=1) in vec3 aNormal;
            layout(location=2) in vec2 aUV;
            layout(location=3) in vec4 aTangent;
            uniform mat4 uCardViewProjection;
            out vec2 UV; out vec3 Normal; out vec3 LocalPosition; out mat3 TBN;
            void main() {
                UV = aUV;
                LocalPosition = aPos;
                Normal = normalize(aNormal);
                vec3 tangent = normalize(aTangent.xyz);
                TBN = mat3(tangent, normalize(cross(Normal, tangent) * aTangent.w), Normal);
                gl_Position = uCardViewProjection * vec4(aPos, 1.0);
            })";
        capture.fragmentSource = R"(
            #version 330 core
            layout(location=0) out vec4 OutAlbedoMetallic;
            layout(location=1) out vec4 OutNormalRoughness;
            layout(location=2) out vec3 OutEmission;
            layout(location=3) out float OutDepth;
            layout(location=4) out vec3 OutDirectRadiance;
            in vec2 UV; in vec3 Normal; in vec3 LocalPosition; in mat3 TBN;
            uniform mat4 uModel;
            uniform vec4 uColor; uniform vec2 uUVScale;
            uniform sampler2D uAlbedoTexture; uniform float uHasAlbedoTexture;
            uniform sampler2D uNormalTexture; uniform float uHasNormalTexture; uniform float uFlipNormalY;
            uniform sampler2D uMetallicTexture; uniform float uHasMetallicTexture; uniform int uMetallicTextureChannel; uniform float uMetallicFactor;
            uniform sampler2D uRoughnessTexture; uniform float uHasRoughnessTexture; uniform int uRoughnessTextureChannel; uniform float uRoughnessFactor;
            uniform vec3 uEmission;
            const int MAX_LIGHTS=8; const int DIRECTIONAL=1; const int SPOT=2;
            uniform int uLightCount; uniform int uLightTypes[MAX_LIGHTS];
            uniform vec3 uLightPositions[MAX_LIGHTS]; uniform vec3 uLightDirections[MAX_LIGHTS];
            uniform vec3 uLightColors[MAX_LIGHTS]; uniform float uLightIntensities[MAX_LIGHTS]; uniform float uLightRanges[MAX_LIGHTS];
            uniform int uShadowCascadeCount; uniform mat4 uShadowMatrices[4]; uniform vec3 uShadowOrigins[4]; uniform float uShadowSplits[4];
            uniform vec3 uViewPosition; uniform vec3 uCameraForward;
            uniform sampler2D uShadowMap0; uniform sampler2D uShadowMap1; uniform sampler2D uShadowMap2; uniform sampler2D uShadowMap3;
            uniform int uEnvironmentEnabled; uniform sampler2D uEnvironmentMap; uniform float uEnvironmentIntensity;
            uniform float uRadianceIntensity; uniform float uRadianceClamp;
            float channel(vec4 value, int index) { return index==0?value.r:index==1?value.g:index==2?value.b:value.a; }
            vec2 directionUv(vec3 d){ d=normalize(d); return vec2(atan(d.z,d.x)*0.159154943+0.5, asin(clamp(d.y,-1.0,1.0))*0.318309886+0.5); }
            float sampleShadow(int i, vec3 worldPos, vec3 normal, vec3 lightDir){
                vec4 clip=uShadowMatrices[i]*vec4(worldPos-uShadowOrigins[i],1); vec3 p=clip.xyz/max(clip.w,0.0001)*0.5+0.5;
                if(p.z<=0||p.z>=1||any(lessThan(p.xy,vec2(0)))||any(greaterThan(p.xy,vec2(1)))) return 1.0;
                float bias=max(0.00035*(1.0-dot(normal,lightDir)),0.00005); float depth=1.0;
                if(i==0)depth=texture(uShadowMap0,p.xy).r; else if(i==1)depth=texture(uShadowMap1,p.xy).r; else if(i==2)depth=texture(uShadowMap2,p.xy).r; else depth=texture(uShadowMap3,p.xy).r;
                return p.z-bias<=depth?1.0:0.0;
            }
            void main() {
                vec2 uv = UV * uUVScale;
                vec4 albedo = uColor * (uHasAlbedoTexture > 0.5 ? texture(uAlbedoTexture, uv) : vec4(1.0));
                vec3 normal = normalize(Normal);
                if (uHasNormalTexture > 0.5) { vec3 n=texture(uNormalTexture,uv).xyz*2.0-1.0; n.y=uFlipNormalY>0.5?-n.y:n.y; normal=normalize(TBN*n); }
                float metallic = uMetallicFactor * (uHasMetallicTexture > 0.5 ? channel(texture(uMetallicTexture,uv),uMetallicTextureChannel) : 1.0);
                float roughness = uRoughnessFactor * (uHasRoughnessTexture > 0.5 ? channel(texture(uRoughnessTexture,uv),uRoughnessTextureChannel) : 1.0);
                vec3 worldPos=vec3(uModel*vec4(LocalPosition,1.0));
                vec3 worldNormal=normalize(transpose(inverse(mat3(uModel)))*normal); vec3 irradiance=vec3(0);
                OutAlbedoMetallic=vec4(albedo.rgb,clamp(metallic,0.0,1.0));
                OutNormalRoughness=vec4(worldNormal,clamp(roughness,0.0,1.0));
                OutEmission=max(uEmission,vec3(0.0)); OutDepth=gl_FragCoord.z;
                for(int i=0;i<MAX_LIGHTS;i++){ if(i>=uLightCount)break; vec3 L; float attenuation=1.0;
                    if(uLightTypes[i]==DIRECTIONAL)L=normalize(-uLightDirections[i]); else { vec3 delta=uLightPositions[i]-worldPos; float distance=length(delta); L=delta/max(distance,0.0001); float range=max(uLightRanges[i],0.001); attenuation=pow(clamp(1.0-distance/range,0.0,1.0),2.0); }
                    if(uLightTypes[i]==SPOT){ float cone=dot(normalize(uLightDirections[i]),-L); attenuation*=smoothstep(0.75,0.9,cone); }
                    float visibility=1.0; if(i==0&&uLightTypes[i]==DIRECTIONAL&&uShadowCascadeCount>0){ float distance=max(dot(worldPos-uViewPosition,uCameraForward),0.0); int cascade=uShadowCascadeCount-1; for(int c=0;c<4;c++){if(c<uShadowCascadeCount&&distance<=uShadowSplits[c]){cascade=c;break;}} visibility=sampleShadow(cascade,worldPos,worldNormal,L); }
                    irradiance+=uLightColors[i]*uLightIntensities[i]*attenuation*max(dot(worldNormal,L),0.0)*visibility;
                }
                if(uEnvironmentEnabled!=0) irradiance+=texture(uEnvironmentMap,directionUv(worldNormal)).rgb*uEnvironmentIntensity;
                OutDirectRadiance=min(max(uEmission+albedo.rgb*irradiance*0.318309886*uRadianceIntensity,vec3(0)),vec3(uRadianceClamp));
            })";
        m_captureShader = Shader::Create(capture);

        ShaderSource debug;
        debug.vertexSource = R"(#version 330 core
            out vec2 UV; void main(){ vec2 v[3]=vec2[3](vec2(-1,-1),vec2(3,-1),vec2(-1,3)); gl_Position=vec4(v[gl_VertexID],0,1); UV=gl_Position.xy*.5+.5; })";
        debug.fragmentSource = R"(#version 330 core
            in vec2 UV; out vec4 FragColor; uniform sampler2D uSceneTexture; uniform sampler2D uAtlasTexture;
            uniform int uDebugView; uniform int uResidentCardCount; uniform vec2 uAtlasExtent; uniform float uViewportAspect;
            vec3 background(vec2 uv){ vec2 tile=floor(uv*24.0); float checker=mod(tile.x+tile.y,2.0); return mix(vec3(0.015),vec3(0.035),checker); }
            void main(){
                if(uDebugView==0){FragColor=texture(uSceneTexture,UV);return;}
                if(uResidentCardCount==0){
                    vec2 tile=floor(UV*16.0); float checker=mod(tile.x+tile.y,2.0);
                    FragColor=vec4(mix(vec3(0.08,0.0,0.0),vec3(0.22,0.01,0.04),checker),1.0); return;
                }
                vec2 extent=clamp(uAtlasExtent,vec2(0.0001),vec2(1.0));
                float contentAspect=extent.x/extent.y; vec2 displaySize=vec2(1.0);
                if(uViewportAspect>contentAspect) displaySize.x=contentAspect/uViewportAspect;
                else displaySize.y=uViewportAspect/contentAspect;
                vec2 displayMin=(vec2(1.0)-displaySize)*0.5;
                if(any(lessThan(UV,displayMin))||any(greaterThan(UV,displayMin+displaySize))){FragColor=vec4(background(UV),1.0);return;}
                vec2 atlasUv=((UV-displayMin)/displaySize)*extent;
                vec4 value=texture(uAtlasTexture,atlasUv);
                if(uDebugView==2)value=vec4(value.xyz*.5+.5,1); else if(uDebugView==4)value=vec4(vec3(value.r),1); else value.a=1;
                FragColor=value;
            })";
        m_debugShader = Shader::Create(debug);
    }

    std::size_t SurfaceCacheGIEffect::ComputeSceneSignature(const PostProcessContext &context) const
    {
        std::size_t signature = 0;
        if (!context.renderContext.renderCommands) return signature;
        for (const auto &command : *context.renderContext.renderCommands)
        {
            if ((m_staticGeometryOnly && !command.isStatic) || !command.mesh || !command.material || command.jointMatrices ||
                command.material->GetConfig().alphaMode != AlphaMode::Opaque || command.material->GetConfig().surfaceType != MaterialSurfaceType::Standard)
                continue;
            HashCombine(signature, reinterpret_cast<std::size_t>(command.mesh));
            HashCombine(signature, reinterpret_cast<std::size_t>(command.material));
            HashCombine(signature, command.submeshIndex);
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    HashCombine(signature, std::hash<float>{}(command.model[column][row]));
            const auto &material = command.material->GetConfig();
            HashCombine(signature, std::hash<float>{}(material.color.r));
            HashCombine(signature, std::hash<float>{}(material.color.g));
            HashCombine(signature, std::hash<float>{}(material.color.b));
            HashCombine(signature, std::hash<float>{}(material.metallic));
            HashCombine(signature, std::hash<float>{}(material.roughness));
        }
        HashCombine(signature, static_cast<std::size_t>(m_atlasSize));
        HashCombine(signature, static_cast<std::size_t>(m_texelsPerUnit));
        HashCombine(signature, static_cast<std::size_t>(m_staticGeometryOnly));
        return signature;
    }

    std::size_t SurfaceCacheGIEffect::ComputeLightingSignature(const PostProcessContext &context) const
    {
        std::size_t signature = 0;
        const auto hashFloat = [&](float value) { HashCombine(signature, std::hash<float>{}(value)); };
        if (context.renderContext.lights)
            for (const auto *light : *context.renderContext.lights)
            {
                if (!light) continue;
                HashCombine(signature, static_cast<std::size_t>(light->type));
                hashFloat(light->position.x); hashFloat(light->position.y); hashFloat(light->position.z);
                hashFloat(light->direction.x); hashFloat(light->direction.y); hashFloat(light->direction.z);
                hashFloat(light->color.x); hashFloat(light->color.y); hashFloat(light->color.z);
                hashFloat(light->intensity); hashFloat(light->range);
                HashCombine(signature, static_cast<std::size_t>(light->activeShadowCascadeCount));
                for (int index = 0; index < light->activeShadowCascadeCount; ++index)
                {
                    HashCombine(signature, reinterpret_cast<std::size_t>(light->shadowCascadeMaps[index].get()));
                    hashFloat(light->shadowCascadeSplits[index]);
                    for (int column = 0; column < 4; ++column)
                        for (int row = 0; row < 4; ++row)
                            hashFloat(light->shadowCascadeMatrices[index][column][row]);
                }
            }
        if (context.renderContext.scene)
        {
            HashCombine(signature, reinterpret_cast<std::size_t>(context.renderContext.scene->GetEnvironmentMapTexture()));
            hashFloat(context.renderContext.scene->GetEnvironmentIntensity());
        }
        hashFloat(m_radianceIntensity); hashFloat(m_environmentIntensity); hashFloat(m_radianceClamp);
        HashCombine(signature, static_cast<std::size_t>(m_directionalShadows));
        HashCombine(signature, static_cast<std::size_t>(m_maxCaptureLights));
        return signature;
    }

    void SurfaceCacheGIEffect::RebuildCards(const PostProcessContext &context)
    {
        m_cards.clear();
        m_nextCapture = 0;
        SurfaceCacheAtlasAllocator allocator(m_atlasSize, m_atlasSize, 2);
        SurfaceCardId nextId = 1;
        int occupiedWidth = 1;
        int occupiedHeight = 1;
        if (context.renderContext.renderCommands)
        {
            for (const auto &command : *context.renderContext.renderCommands)
            {
                if ((m_staticGeometryOnly && !command.isStatic) || !command.mesh || !command.material || command.jointMatrices ||
                    command.material->GetConfig().alphaMode != AlphaMode::Opaque || command.material->GetConfig().surfaceType != MaterialSurfaceType::Standard)
                    continue;
                auto cards = SurfaceCardGenerator::GenerateAxisCards(*command.mesh, command.submeshIndex, m_texelsPerUnit,
                                                                     m_minCardResolution, m_maxCardResolution);
                for (auto &card : cards)
                {
                    auto allocation = allocator.Allocate(card.allocation.width, card.allocation.height);
                    if (!allocation) continue;
                    card.id = nextId++;
                    card.allocation = *allocation;
                    occupiedWidth = std::max(occupiedWidth, card.allocation.x + card.allocation.width + allocator.GetPadding());
                    occupiedHeight = std::max(occupiedHeight, card.allocation.y + card.allocation.height + allocator.GetPadding());
                    m_cards.push_back({card, command.mesh, command.material, command.model});
                }
            }
        }
        m_stats.cardCount = static_cast<int>(m_cards.size());
        m_stats.residentCardCount = static_cast<int>(m_cards.size());
        m_stats.capturedCardCount = 0;
        m_stats.atlasUsedPixels = allocator.GetUsedPixels();
        m_stats.atlasTotalPixels = m_atlasSize * m_atlasSize;
        m_debugAtlasExtent = glm::vec2(static_cast<float>(occupiedWidth), static_cast<float>(occupiedHeight)) /
                             static_cast<float>(m_atlasSize);
        if (m_atlas) m_atlas->Clear();
    }

    void SurfaceCacheGIEffect::CapturePendingCards(const PostProcessContext &context)
    {
        if (!m_atlas || !m_captureShader || m_cards.empty() || m_nextCapture >= m_cards.size()) return;
        m_atlas->BindForCapture();
        glEnable(GL_DEPTH_TEST); glEnable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
        m_captureShader->Bind();
        std::vector<const scene::Light *> lights;
        if (context.renderContext.lights)
        {
            const auto shadowed = std::find_if(context.renderContext.lights->begin(), context.renderContext.lights->end(), [](const scene::Light *light) {
                return light && light->type == scene::LightType::Directional && light->castsShadows && light->activeShadowCascadeCount > 0;
            });
            if (shadowed != context.renderContext.lights->end()) lights.push_back(*shadowed);
            for (const auto *light : *context.renderContext.lights)
                if (light && (lights.empty() || light != lights.front()) && lights.size() < static_cast<std::size_t>(m_maxCaptureLights)) lights.push_back(light);
        }
        m_captureShader->SetUniform("uLightCount", static_cast<int>(lights.size()));
        for (int index = 0; index < 8; ++index)
        {
            const auto *light = index < static_cast<int>(lights.size()) ? lights[index] : nullptr;
            const std::string suffix = "[" + std::to_string(index) + "]";
            m_captureShader->SetUniform("uLightTypes" + suffix, light ? static_cast<int>(light->type) : 0);
            m_captureShader->SetUniform("uLightPositions" + suffix, light ? light->position : glm::vec3(0));
            m_captureShader->SetUniform("uLightDirections" + suffix, light ? light->direction : glm::vec3(0,-1,0));
            m_captureShader->SetUniform("uLightColors" + suffix, light ? light->color : glm::vec3(0));
            m_captureShader->SetUniform("uLightIntensities" + suffix, light ? light->intensity : 0.0f);
            m_captureShader->SetUniform("uLightRanges" + suffix, light ? light->range : 1.0f);
        }
        const scene::Light *shadowLight = !lights.empty() && lights.front()->type == scene::LightType::Directional ? lights.front() : nullptr;
        const int cascadeCount = shadowLight && m_directionalShadows ? std::clamp(shadowLight->activeShadowCascadeCount, 0, 4) : 0;
        m_captureShader->SetUniform("uShadowCascadeCount", cascadeCount);
        const glm::mat4 inverseView = glm::inverse(context.renderContext.cameraData.view);
        m_captureShader->SetUniform("uViewPosition", glm::vec3(inverseView[3]));
        m_captureShader->SetUniform("uCameraForward", -glm::normalize(glm::vec3(context.renderContext.cameraData.view[0][2], context.renderContext.cameraData.view[1][2], context.renderContext.cameraData.view[2][2])));
        for (int index = 0; index < 4; ++index)
        {
            const bool valid = shadowLight && index < cascadeCount && shadowLight->shadowCascadeMaps[index];
            glActiveTexture(GL_TEXTURE8 + index); glBindTexture(GL_TEXTURE_2D, valid ? shadowLight->shadowCascadeMaps[index]->GetTextureID() : 0);
            m_captureShader->SetUniform("uShadowMap" + std::to_string(index), 8 + index);
            const std::string suffix = "[" + std::to_string(index) + "]";
            m_captureShader->SetUniform("uShadowMatrices" + suffix, valid ? shadowLight->shadowCascadeMatrices[index] : glm::mat4(1));
            m_captureShader->SetUniform("uShadowOrigins" + suffix, valid ? shadowLight->shadowCascadeWorldOrigins[index] : glm::vec3(0));
            m_captureShader->SetUniform("uShadowSplits" + suffix, valid ? shadowLight->shadowCascadeSplits[index] : 0.0f);
        }
        GLuint environment = context.renderContext.renderer ? context.renderContext.renderer->GetPhysicalSkyEnvironmentTextureID() : 0;
        if (!environment && context.renderContext.scene && context.renderContext.scene->GetEnvironmentMapTexture()) environment = context.renderContext.scene->GetEnvironmentMapTexture()->GetTextureID();
        glActiveTexture(GL_TEXTURE12); glBindTexture(GL_TEXTURE_2D, environment);
        m_captureShader->SetUniform("uEnvironmentMap", 12); m_captureShader->SetUniform("uEnvironmentEnabled", environment ? 1 : 0);
        m_captureShader->SetUniform("uEnvironmentIntensity", (context.renderContext.scene ? context.renderContext.scene->GetEnvironmentIntensity() : 1.0f) * m_environmentIntensity);
        m_captureShader->SetUniform("uRadianceIntensity", m_radianceIntensity); m_captureShader->SetUniform("uRadianceClamp", m_radianceClamp);
        const std::size_t end = std::min(m_cards.size(), m_nextCapture + static_cast<std::size_t>(m_captureBudget));
        for (; m_nextCapture < end; ++m_nextCapture)
        {
            const auto &resident = m_cards[m_nextCapture];
            const auto &rect = resident.card.allocation;
            glViewport(rect.x, rect.y, rect.width, rect.height);
            glScissor(rect.x, rect.y, rect.width, rect.height);
            constexpr GLfloat zero[4]{0,0,0,0}; constexpr GLfloat farDepth[4]{1,0,0,0};
            glClearBufferfv(GL_COLOR,0,zero); glClearBufferfv(GL_COLOR,1,zero); glClearBufferfv(GL_COLOR,2,zero); glClearBufferfv(GL_COLOR,3,farDepth); glClearBufferfv(GL_COLOR,4,zero); glClear(GL_DEPTH_BUFFER_BIT);
            m_captureShader->SetUniform("uCardViewProjection", resident.card.localViewProjection);
            m_captureShader->SetUniform("uModel", resident.model);
            resident.material->Bind(m_captureShader);
            resident.mesh->DrawSubmesh(resident.card.submeshIndex, 0);
        }
        m_stats.capturedCardCount = static_cast<int>(m_nextCapture);
        glDisable(GL_SCISSOR_TEST); glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void SurfaceCacheGIEffect::Apply(const PostProcessContext &context)
    {
        if (!context.sourceRenderTarget || !context.destinationRenderTarget || !m_captureShader || !m_debugShader) return;
        if (!m_atlas) m_atlas = std::make_unique<SurfaceCacheAtlas>();
        if (!m_atlas->Initialize(m_atlasSize)) return;
        const std::size_t signature = ComputeSceneSignature(context);
        if (m_cacheLayoutDirty || signature != m_sceneSignature)
        {
            m_sceneSignature = signature;
            m_cacheLayoutDirty = false;
            RebuildCards(context);
        }
        const std::size_t lightingSignature = ComputeLightingSignature(context);
        if (lightingSignature != m_lightingSignature)
        {
            m_lightingSignature = lightingSignature;
            m_nextCapture = 0;
            m_stats.capturedCardCount = 0;
        }
        CapturePendingCards(context);
        if (context.renderContext.renderer)
            context.renderContext.renderer->RecordSurfaceCacheStats(
                m_stats.cardCount, m_stats.residentCardCount, m_stats.capturedCardCount,
                m_stats.atlasUsedPixels, m_stats.atlasTotalPixels);

        BeginApply(context);
        m_debugShader->Bind();
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, context.sourceRenderTarget->GetColorTextureID());
        m_debugShader->SetUniform("uSceneTexture", 0);
        SurfaceCacheAtlas::Layer layer = SurfaceCacheAtlas::Layer::AlbedoMetallic;
        if (m_debugView == 2) layer = SurfaceCacheAtlas::Layer::NormalRoughness;
        else if (m_debugView == 3) layer = SurfaceCacheAtlas::Layer::Emission;
        else if (m_debugView == 4) layer = SurfaceCacheAtlas::Layer::Depth;
        else if (m_debugView == 5) layer = SurfaceCacheAtlas::Layer::DirectRadiance;
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m_atlas->GetTexture(layer));
        m_debugShader->SetUniform("uAtlasTexture", 1); m_debugShader->SetUniform("uDebugView", m_debugView);
        m_debugShader->SetUniform("uResidentCardCount", m_stats.residentCardCount);
        m_debugShader->SetUniform("uAtlasExtent", m_debugAtlasExtent);
        m_debugShader->SetUniform("uViewportAspect", static_cast<float>(context.destinationRenderTarget->GetWidth()) /
                                                       static_cast<float>(std::max(context.destinationRenderTarget->GetHeight(), 1)));
        DrawFullscreenTriangle(); EndApply();
    }
}
