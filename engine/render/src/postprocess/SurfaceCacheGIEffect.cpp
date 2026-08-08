#include "PlutoGE/render/postprocess/SurfaceCacheGIEffect.h"

#include "PlutoGE/render/Graphics.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/RenderTarget.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/Shader.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/GBuffer.h"
#include "PlutoGE/render/visibility/IWorldVisibilityProvider.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/LightComponent.h"

#include <algorithm>
#include <functional>
#include <limits>

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

    SurfaceCacheGIEffect::SurfaceCacheGIEffect() = default;

    SurfaceCacheGIEffect::~SurfaceCacheGIEffect()
    {
        if (m_gatherTarget) m_gatherTarget->Cleanup();
        if (m_indirectGatherTarget) m_indirectGatherTarget->Cleanup();
        for (auto &target : m_gatherHistoryTargets) if (target) target->Cleanup();
        for (auto &target : m_gatherHistoryMetadataTargets) if (target) target->Cleanup();
        if (m_filteredGatherTarget) m_filteredGatherTarget->Cleanup();
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
            {"Visibility Cascade Count", PostProcessParameterType::Int, std::to_string(m_visibilityCascadeCount)},
            {"Screen Ray Count", PostProcessParameterType::Int, std::to_string(m_screenRayCount)},
            {"Screen Trace Steps", PostProcessParameterType::Int, std::to_string(m_screenTraceSteps)},
            {"Screen Trace Distance", PostProcessParameterType::Float, std::to_string(m_screenTraceDistance)},
            {"Gather Temporal Blend", PostProcessParameterType::Float, std::to_string(m_gatherTemporalBlend)},
            {"Indirect Intensity", PostProcessParameterType::Float, std::to_string(m_indirectIntensity)},
            {"Compose Indirect", PostProcessParameterType::Bool, m_composeIndirect ? "true" : "false"},
            {"Radiance Intensity", PostProcessParameterType::Float, std::to_string(m_radianceIntensity)},
            {"Environment Intensity", PostProcessParameterType::Float, std::to_string(m_environmentIntensity)},
            {"Radiance Clamp", PostProcessParameterType::Float, std::to_string(m_radianceClamp)},
            {"Radiance History Blend", PostProcessParameterType::Float, std::to_string(m_radianceHistoryBlend)},
            {"Directional Shadows", PostProcessParameterType::Bool, m_directionalShadows ? "true" : "false"},
            {"Static Geometry Only", PostProcessParameterType::Bool, m_staticGeometryOnly ? "true" : "false"},
            {"Debug View", PostProcessParameterType::Enum, std::to_string(m_debugView), {"Scene", "Albedo / Metallic", "Normal / Roughness", "Emission", "Card Depth", "Direct Radiance", "Accumulated Radiance", "Visibility Cascades", "Card Candidates", "Selected Card", "Atlas UV", "Gather Inputs", "Screen Trace", "Hybrid Trace", "Ray Hit Radiance", "Temporal Radiance", "Filtered Radiance"}},
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
            else if (parameter.name == "Visibility Cascade Count")
            {
                const int value = std::clamp(std::stoi(parameter.value), 1, 3);
                m_visibilityCascadeCount = value;
            }
            else if (parameter.name == "Screen Ray Count") m_screenRayCount = std::clamp(std::stoi(parameter.value), 1, 8);
            else if (parameter.name == "Screen Trace Steps") m_screenTraceSteps = std::clamp(std::stoi(parameter.value), 4, 32);
            else if (parameter.name == "Screen Trace Distance") m_screenTraceDistance = std::clamp(std::stof(parameter.value), 0.25f, 32.0f);
            else if (parameter.name == "Gather Temporal Blend") m_gatherTemporalBlend = std::clamp(std::stof(parameter.value), 0.0f, 0.98f);
            else if (parameter.name == "Indirect Intensity") m_indirectIntensity = std::clamp(std::stof(parameter.value), 0.0f, 8.0f);
            else if (parameter.name == "Compose Indirect") m_composeIndirect = ParseBool(parameter.value);
            else if (parameter.name == "Radiance Intensity") m_radianceIntensity = std::clamp(std::stof(parameter.value), 0.0f, 8.0f);
            else if (parameter.name == "Environment Intensity") m_environmentIntensity = std::clamp(std::stof(parameter.value), 0.0f, 8.0f);
            else if (parameter.name == "Radiance Clamp") m_radianceClamp = std::clamp(std::stof(parameter.value), 1.0f, 256.0f);
            else if (parameter.name == "Radiance History Blend") m_radianceHistoryBlend = std::clamp(std::stof(parameter.value), 0.0f, 0.98f);
            else if (parameter.name == "Directional Shadows") m_directionalShadows = ParseBool(parameter.value);
            else if (parameter.name == "Static Geometry Only") { const bool value = ParseBool(parameter.value); if (value != m_staticGeometryOnly) { m_staticGeometryOnly = value; m_cacheLayoutDirty = true; } }
            else if (parameter.name == "Debug View") m_debugView = std::clamp(std::stoi(parameter.value), 0, 16);
        }
        m_maxCardResolution = std::max(m_maxCardResolution, m_minCardResolution);
        m_cacheLayoutDirty = m_cacheLayoutDirty || previousAtlasSize != m_atlasSize ||
                             previousTexelsPerUnit != m_texelsPerUnit ||
                             previousMinimumResolution != m_minCardResolution ||
                             previousMaximumResolution != m_maxCardResolution;
        if (previousAtlasSize != m_atlasSize)
        {
            m_atlas.reset();
            ResetGatherHistory();
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
            in vec2 UV; out vec4 FragColor; uniform sampler2D uSceneTexture; uniform sampler2D uAtlasTexture; uniform sampler2D uGatherTexture; uniform sampler2D uIndirectGatherTexture;uniform sampler2D uFilteredGatherTexture;
            uniform int uDebugView; uniform int uResidentCardCount; uniform vec2 uAtlasExtent; uniform float uViewportAspect;
            uniform sampler2D uScenePositionTexture; uniform int uVisibilityCascadeCount;
            uniform int uVisibilityStatus;
            uniform vec3 uVisibilityOrigins[3]; uniform float uVisibilitySizes[3]; uniform int uVisibilityValid[3];
            vec3 background(vec2 uv){ vec2 tile=floor(uv*24.0); float checker=mod(tile.x+tile.y,2.0); return mix(vec3(0.015),vec3(0.035),checker); }
            void main(){
                if(uDebugView==0){FragColor=texture(uSceneTexture,UV);return;}
                if(uDebugView==11){vec2 f=texture(uGatherTexture,UV).rg*2.0-1.0;vec3 n=vec3(f,1.0-abs(f.x)-abs(f.y));float t=clamp(-n.z,0.0,1.0);n.xy+=vec2(n.x>=0.0?-t:t,n.y>=0.0?-t:t);FragColor=vec4(normalize(n)*0.5+0.5,1.0);return;}
                if(uDebugView==12){float confidence=texture(uGatherTexture,UV).b;FragColor=vec4(mix(vec3(0.015,0.03,0.12),vec3(1.0,0.28,0.02),confidence),1.0);return;}
                if(uDebugView==13){vec2 confidence=texture(uGatherTexture,UV).ba;float unresolved=max(1.0-confidence.x-confidence.y,0.0);FragColor=vec4(confidence.x,confidence.y,unresolved,1.0);return;}
                if(uDebugView==14){vec4 indirect=texture(uIndirectGatherTexture,UV);vec3 mapped=indirect.rgb/(vec3(1.0)+indirect.rgb);FragColor=vec4(indirect.a>0.001?mapped:vec3(0.0),1.0);return;}
                if(uDebugView==15){vec4 indirect=texture(uIndirectGatherTexture,UV);vec3 mapped=indirect.rgb/(vec3(1.0)+indirect.rgb);FragColor=vec4(indirect.a>0.001?mapped:vec3(0.0),1.0);return;}
                if(uDebugView==16){vec4 indirect=texture(uFilteredGatherTexture,UV);FragColor=vec4(indirect.rgb/(vec3(1.0)+indirect.rgb),1.0);return;}
                if(uDebugView==7){
                    if(uVisibilityStatus==0){vec2 t=floor(UV*16.0);float c=mod(t.x+t.y,2.0);FragColor=vec4(mix(vec3(0.12,0.0,0.18),vec3(0.55,0.0,0.7),c),1);return;}
                    if(uVisibilityStatus==1){vec2 t=floor(UV*16.0);float c=mod(t.x+t.y,2.0);FragColor=vec4(mix(vec3(0.18,0.035,0.0),vec3(0.75,0.24,0.0),c),1);return;}
                    vec3 p=texture(uScenePositionTexture,UV).xyz; int selected=-1;
                    for(int i=0;i<3;i++){if(i>=uVisibilityCascadeCount)break; vec3 tc=(p-uVisibilityOrigins[i])/max(uVisibilitySizes[i],0.0001); if(uVisibilityValid[i]!=0&&all(greaterThanEqual(tc,vec3(0)))&&all(lessThanEqual(tc,vec3(1)))){selected=i;break;}}
                    if(selected==0)FragColor=vec4(0.12,0.55,1.0,1); else if(selected==1)FragColor=vec4(0.2,0.9,0.35,1); else if(selected==2)FragColor=vec4(1.0,0.72,0.15,1); else FragColor=vec4(0.02,0.0,0.04,1); return;
                }
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

        ShaderSource resolve;
        resolve.vertexSource = debug.vertexSource;
        resolve.fragmentSource = R"(#version 330 core
            in vec2 UV; out vec3 FragColor;
            uniform sampler2D uDirectRadiance; uniform sampler2D uPreviousRadiance;
            uniform float uHistoryBlend; uniform int uHasHistory;
            void main(){
                vec3 direct=max(texture(uDirectRadiance,UV).rgb,vec3(0.0));
                vec3 previous=max(texture(uPreviousRadiance,UV).rgb,vec3(0.0));
                FragColor=uHasHistory!=0?mix(direct,previous,uHistoryBlend):direct;
            })";
        m_radianceResolveShader = Shader::Create(resolve);

        ShaderSource lookupDebug;
        lookupDebug.vertexSource = debug.vertexSource;
        lookupDebug.fragmentSource = R"(#version 430 core
            in vec2 UV; out vec4 FragColor; uniform sampler2D uScenePositionTexture; uniform sampler2D uSceneNormalTexture; uniform sampler2D uCardDepthAtlas;
            uniform float uCellSize; uniform int uCellCount; uniform int uCardCount; uniform int uLookupDebugMode;
            struct Cell { ivec4 coordinateAndOffset; uvec4 countAndPadding; };
            struct CardBounds { vec4 minimumAndId; vec4 maximumBounds; vec4 cardNormal; mat4 worldToCardClip; vec4 atlasScaleBias; };
            layout(std430,binding=0) readonly buffer Cells { Cell cells[]; };
            layout(std430,binding=1) readonly buffer Candidates { uint candidates[]; };
            layout(std430,binding=2) readonly buffer Cards { CardBounds cards[]; };
            int compareCell(ivec3 a,ivec3 b){if(a.x!=b.x)return a.x<b.x?-1:1;if(a.y!=b.y)return a.y<b.y?-1:1;if(a.z!=b.z)return a.z<b.z?-1:1;return 0;}
            int findCell(ivec3 key){int low=0,high=uCellCount-1;while(low<=high){int middle=(low+high)/2;int order=compareCell(cells[middle].coordinateAndOffset.xyz,key);if(order==0)return middle;if(order<0)low=middle+1;else high=middle-1;}return -1;}
            vec3 idColor(uint id){return 0.35+0.65*fract(vec3(float(id))*vec3(0.1031,0.11369,0.13787));}
            void main(){
                vec3 worldPosition=texture(uScenePositionTexture,UV).xyz;vec3 rawNormal=texture(uSceneNormalTexture,UV).xyz;float normalLength=length(rawNormal);vec3 surfaceNormal=normalLength>0.01?rawNormal/normalLength:vec3(0.0);ivec3 key=ivec3(floor(worldPosition/max(uCellSize,0.001)));int cellIndex=findCell(key);int count=0;uint selectedId=0u;vec2 selectedUv=vec2(0.0);float bestScore=1e10;uint fallbackId=0u;vec2 fallbackUv=vec2(0.0);float bestFallbackScore=1e10;
                if(cellIndex>=0&&normalLength>0.01){int offset=cells[cellIndex].coordinateAndOffset.w;uint candidateCount=cells[cellIndex].countAndPadding.x;ivec2 atlasSize=textureSize(uCardDepthAtlas,0);for(uint i=0u;i<candidateCount;i++){uint id=candidates[offset+int(i)];if(id==0u||id>uint(uCardCount))continue;CardBounds card=cards[id-1u];bool inside=all(greaterThanEqual(worldPosition,card.minimumAndId.xyz))&&all(lessThanEqual(worldPosition,card.maximumBounds.xyz));float facing=dot(surfaceNormal,normalize(card.cardNormal.xyz));if(!inside||facing<=0.35)continue;count++;vec4 clip=card.worldToCardClip*vec4(worldPosition,1.0);if(abs(clip.w)<=0.00001)continue;vec3 ndc=clip.xyz/clip.w;vec2 projectedUv=ndc.xy*0.5+0.5;float projectedDepth=ndc.z*0.5+0.5;const float projectionTolerance=0.025;bool inCard=all(greaterThanEqual(projectedUv,vec2(-projectionTolerance)))&&all(lessThanEqual(projectedUv,vec2(1.0+projectionTolerance)))&&projectedDepth>=-projectionTolerance&&projectedDepth<=1.0+projectionTolerance;if(!inCard)continue;vec2 cardUv=clamp(projectedUv,vec2(0.0),vec2(1.0));projectedDepth=clamp(projectedDepth,0.0,1.0);float fallbackScore=1.0-facing;if(fallbackScore<bestFallbackScore){bestFallbackScore=fallbackScore;fallbackId=id;fallbackUv=cardUv;}vec2 atlasUv=cardUv*card.atlasScaleBias.xy+card.atlasScaleBias.zw;ivec2 cardMinimum=ivec2(card.atlasScaleBias.zw*vec2(atlasSize));ivec2 cardMaximum=cardMinimum+max(ivec2(card.atlasScaleBias.xy*vec2(atlasSize))-1,ivec2(0));ivec2 center=clamp(ivec2(atlasUv*vec2(atlasSize)),cardMinimum,cardMaximum);float depthError=1e10;bool hasDepth=false;for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){float cachedDepth=texelFetch(uCardDepthAtlas,clamp(center+ivec2(x,y),cardMinimum,cardMaximum),0).r;if(cachedDepth<0.9999){hasDepth=true;depthError=min(depthError,abs(cachedDepth-projectedDepth));}}if(!hasDepth||depthError>0.035)continue;float score=depthError+(1.0-facing)*0.01;if(score<bestScore){bestScore=score;selectedId=id;selectedUv=cardUv;}}}
                if(selectedId==0u){selectedId=fallbackId;selectedUv=fallbackUv;}
                if(uLookupDebugMode==0){float level=clamp(float(count)/6.0,0.0,1.0);vec3 color=count==0?vec3(0.025,0.0,0.04):mix(vec3(0.0,0.25,0.9),vec3(1.0,0.15,0.02),level);FragColor=vec4(color,1.0);return;}
                if(selectedId==0u){FragColor=vec4(0.12,0.0,0.16,1.0);return;}
                if(uLookupDebugMode==1){FragColor=vec4(idColor(selectedId),1.0);return;}
                vec2 grid=step(vec2(0.96),fract(selectedUv*8.0));float line=max(grid.x,grid.y);FragColor=vec4(mix(vec3(selectedUv,0.15),vec3(1.0),line),1.0);
            })";
        m_cardLookupDebugShader = Shader::Create(lookupDebug);

        ShaderSource gather;
        gather.vertexSource = debug.vertexSource;
        gather.fragmentSource = R"(#version 330 core
            in vec2 UV; out vec4 FragColor;
            uniform sampler2D uScenePositionTexture; uniform sampler2D uSceneNormalTexture;
            uniform mat4 uViewProjection; uniform int uRayCount; uniform int uTraceSteps; uniform int uFrameIndex; uniform float uTraceDistance;
            uniform sampler3D uVoxel0;uniform sampler3D uVoxel1;uniform sampler3D uVoxel2;uniform sampler3D uVoxel3;uniform sampler3D uVoxel4;uniform sampler3D uVoxel5;
            uniform vec3 uCascadeOrigins[3];uniform float uCascadeSizes[3];uniform int uCascadeCount;uniform int uAtlasCascadeCount;uniform int uCascadeResolution;
            const float PI=3.14159265359;
            float hash12(vec2 p){vec3 p3=fract(vec3(p.xyx)*0.1031);p3+=dot(p3,p3.yzx+33.33);return fract((p3.x+p3.y)*p3.z);}
            vec2 encodeNormal(vec3 n){n/=max(abs(n.x)+abs(n.y)+abs(n.z),0.0001);if(n.z<0.0)n.xy=(1.0-abs(n.yx))*vec2(n.x>=0.0?1.0:-1.0,n.y>=0.0?1.0:-1.0);return n.xy*0.5+0.5;}
            vec3 hemisphereDirection(vec3 normal,int index,float rotation){
                float count=float(max(uRayCount,1));float u=(float(index)+0.5)/count;float phi=2.0*PI*(fract(float(index)*0.61803398875+rotation));
                float radius=sqrt(u);vec3 local=vec3(radius*cos(phi),radius*sin(phi),sqrt(max(1.0-u,0.0)));
                vec3 helper=abs(normal.z)<0.999?vec3(0,0,1):vec3(1,0,0);vec3 tangent=normalize(cross(helper,normal));vec3 bitangent=cross(normal,tangent);
                return normalize(tangent*local.x+bitangent*local.y+normal*local.z);
            }
            bool screenCandidate(vec3 origin,vec3 direction,float travel,float stepLength,out vec3 hitPosition,out vec3 hitNormal,out float error){
                vec4 clip=uViewProjection*vec4(origin+direction*travel,1.0);if(clip.w<=0.0001)return false;
                vec2 sampleUv=clip.xy/clip.w*0.5+0.5;if(any(lessThan(sampleUv,vec2(0.0)))||any(greaterThan(sampleUv,vec2(1.0))))return false;
                vec3 scenePoint=texture(uScenePositionTexture,sampleUv).xyz,raw=texture(uSceneNormalTexture,sampleUv).xyz;if(length(raw)<.01)return false;
                vec3 delta=scenePoint-origin;float alongRay=dot(delta,direction);error=length(delta-direction*alongRay);
                float thickness=max(.055,max(stepLength*.4,travel*.018));hitNormal=normalize(raw);hitPosition=scenePoint;
                return alongRay>.06&&abs(alongRay-travel)<=stepLength*.8&&error<thickness&&dot(hitNormal,-direction)>.025;
            }
            bool traceScreen(vec3 origin,vec3 direction){
                float stepLength=uTraceDistance/float(max(uTraceSteps,1));
                for(int stepIndex=1;stepIndex<=32;stepIndex++){if(stepIndex>uTraceSteps)break;float travel=stepLength*float(stepIndex),error;vec3 hitPosition,hitNormal;if(!screenCandidate(origin,direction,travel,stepLength,hitPosition,hitNormal,error))continue;float bestError=error;for(int refinement=0;refinement<3;refinement++){float refinedTravel=travel-stepLength*(.375-.375*float(refinement));vec3 refinedPosition,refinedNormal;float refinedError;if(screenCandidate(origin,direction,refinedTravel,stepLength,refinedPosition,refinedNormal,refinedError)&&refinedError<bestError)bestError=refinedError;}return true;}return false;
            }
            int findCascade(vec3 p,out vec3 tc){for(int cascade=0;cascade<3;cascade++){if(cascade>=uCascadeCount)break;tc=(p-uCascadeOrigins[cascade])/max(uCascadeSizes[cascade],0.0001);if(all(greaterThanEqual(tc,vec3(0.0)))&&all(lessThan(tc,vec3(1.0))))return cascade;}tc=vec3(0.0);return -1;}
            vec4 sampleVolume(int index,vec3 tc){if(index==0)return textureLod(uVoxel0,tc,0.0);if(index==1)return textureLod(uVoxel1,tc,0.0);if(index==2)return textureLod(uVoxel2,tc,0.0);if(index==3)return textureLod(uVoxel3,tc,0.0);if(index==4)return textureLod(uVoxel4,tc,0.0);return textureLod(uVoxel5,tc,0.0);}
            float sampleVoxelOpacity(int cascade,vec3 tc,vec3 direction){vec3 atlasTc=vec3(clamp(tc.xy,vec2(0.0),vec2(1.0)),(float(cascade)+clamp(tc.z,0.0,1.0))/float(max(uAtlasCascadeCount,1)));vec3 weight=abs(direction);weight/=max(weight.x+weight.y+weight.z,0.0001);return sampleVolume(direction.x>=0.0?0:1,atlasTc).a*weight.x+sampleVolume(direction.y>=0.0?2:3,atlasTc).a*weight.y+sampleVolume(direction.z>=0.0?4:5,atlasTc).a*weight.z;}
            bool traceVoxel(vec3 origin,vec3 direction){if(uCascadeCount<=0||uCascadeResolution<=0)return false;float distanceAlong=0.08;float accumulatedOpacity=0.0;for(int stepIndex=0;stepIndex<32;stepIndex++){if(distanceAlong>uTraceDistance)break;vec3 tc;int cascade=findCascade(origin+direction*distanceAlong,tc);if(cascade<0){distanceAlong+=0.2;continue;}float voxelSize=uCascadeSizes[cascade]/float(uCascadeResolution);accumulatedOpacity+=(1.0-accumulatedOpacity)*sampleVoxelOpacity(cascade,tc,direction);if(accumulatedOpacity>0.15)return true;distanceAlong+=max(voxelSize,0.04);}return false;}
            void main(){
                vec3 position=texture(uScenePositionTexture,UV).xyz;
                vec3 rawNormal=texture(uSceneNormalTexture,UV).xyz;
                float normalLength=length(rawNormal);
                if(normalLength<0.01){FragColor=vec4(0.0);return;}
                vec3 normal=rawNormal/normalLength;
                float positionValidity=all(lessThan(abs(position),vec3(1e19)))?1.0:0.0;
                float screenHits=0.0;float voxelHits=0.0;float rotation=fract(hash12(floor(gl_FragCoord.xy))+float(uFrameIndex)*0.61803398875);vec3 origin=position+normal*0.04;
                for(int rayIndex=0;rayIndex<8;rayIndex++){if(rayIndex>=uRayCount)break;vec3 direction=hemisphereDirection(normal,rayIndex,rotation);if(traceScreen(origin,direction))screenHits+=1.0;else if(traceVoxel(origin,direction))voxelHits+=1.0;}
                float inverseRayCount=1.0/float(max(uRayCount,1));
                FragColor=vec4(encodeNormal(normal),screenHits*inverseRayCount,voxelHits*inverseRayCount)*positionValidity;
            })";
        m_gatherShader = Shader::Create(gather);

        ShaderSource indirectGather;
        indirectGather.vertexSource = debug.vertexSource;
        indirectGather.fragmentSource = R"(#version 430 core
            in vec2 UV;out vec4 FragColor;
            uniform sampler2D uScenePositionTexture,uSceneNormalTexture,uAccumulatedRadianceAtlas,uCardDepthAtlas;
            uniform mat4 uViewProjection;uniform float uCellSize,uTraceDistance;uniform int uTraceSteps,uRayCount,uFrameIndex,uCellCount,uCardCount;
            uniform sampler3D uVoxel0,uVoxel1,uVoxel2,uVoxel3,uVoxel4,uVoxel5;uniform vec3 uCascadeOrigins[3];uniform float uCascadeSizes[3];uniform int uCascadeCount,uAtlasCascadeCount,uCascadeResolution;
            struct Cell{ivec4 coordinateAndOffset;uvec4 countAndPadding;};
            struct CardBounds{vec4 minimumAndId;vec4 maximumBounds;vec4 cardNormal;mat4 worldToCardClip;vec4 atlasScaleBias;};
            layout(std430,binding=0)readonly buffer Cells{Cell cells[];};layout(std430,binding=1)readonly buffer Candidates{uint candidates[];};layout(std430,binding=2)readonly buffer Cards{CardBounds cards[];};
            int compareCell(ivec3 a,ivec3 b){if(a.x!=b.x)return a.x<b.x?-1:1;if(a.y!=b.y)return a.y<b.y?-1:1;if(a.z!=b.z)return a.z<b.z?-1:1;return 0;}
            int findCell(ivec3 key){int low=0,high=uCellCount-1;while(low<=high){int middle=(low+high)/2;int order=compareCell(cells[middle].coordinateAndOffset.xyz,key);if(order==0)return middle;if(order<0)low=middle+1;else high=middle-1;}return -1;}
            float hash12(vec2 p){vec3 p3=fract(vec3(p.xyx)*.1031);p3+=dot(p3,p3.yzx+33.33);return fract((p3.x+p3.y)*p3.z);}
            vec3 rayDirection(vec3 n,int index){float u=fract((float(index)+.5)/float(max(uRayCount,1))+float(uFrameIndex)*.754877666);u=max(u,.02);float phi=6.2831853*fract(float(index)*.61803398875+hash12(floor(gl_FragCoord.xy))+float(uFrameIndex)*.569840296);float radius=sqrt(u);vec3 local=vec3(radius*cos(phi),radius*sin(phi),sqrt(max(1.0-u,0.0)));vec3 helper=abs(n.z)<.999?vec3(0,0,1):vec3(1,0,0);vec3 t=normalize(cross(helper,n)),b=cross(n,t);return normalize(t*local.x+b*local.y+n*local.z);}
            bool screenCandidate(vec3 origin,vec3 direction,float travel,float stepLength,out vec3 hitPosition,out vec3 hitNormal,out float error){vec4 clip=uViewProjection*vec4(origin+direction*travel,1);if(clip.w<=.0001)return false;vec2 suv=clip.xy/clip.w*.5+.5;if(any(lessThan(suv,vec2(0)))||any(greaterThan(suv,vec2(1))))return false;vec3 raw=texture(uSceneNormalTexture,suv).xyz;if(length(raw)<.01)return false;hitPosition=texture(uScenePositionTexture,suv).xyz;hitNormal=normalize(raw);vec3 delta=hitPosition-origin;float alongRay=dot(delta,direction);error=length(delta-direction*alongRay);float thickness=max(.055,max(stepLength*.4,travel*.018));return alongRay>.06&&abs(alongRay-travel)<=stepLength*.8&&error<thickness&&dot(hitNormal,-direction)>.025;}
            bool traceScreen(vec3 origin,vec3 direction,out vec3 hitPosition,out vec3 hitNormal){float stepLength=uTraceDistance/float(max(uTraceSteps,1));for(int i=1;i<=32;i++){if(i>uTraceSteps)break;float travel=stepLength*float(i),error;if(!screenCandidate(origin,direction,travel,stepLength,hitPosition,hitNormal,error))continue;float bestError=error;vec3 bestPosition=hitPosition,bestNormal=hitNormal;for(int refinement=0;refinement<3;refinement++){float refinedTravel=travel-stepLength*(.375-.375*float(refinement));vec3 refinedPosition,refinedNormal;float refinedError;if(screenCandidate(origin,direction,refinedTravel,stepLength,refinedPosition,refinedNormal,refinedError)&&refinedError<bestError){bestError=refinedError;bestPosition=refinedPosition;bestNormal=refinedNormal;}}hitPosition=bestPosition;hitNormal=bestNormal;return true;}return false;}
            int findCascade(vec3 p,out vec3 tc){for(int c=0;c<3;c++){if(c>=uCascadeCount)break;tc=(p-uCascadeOrigins[c])/max(uCascadeSizes[c],.0001);if(all(greaterThanEqual(tc,vec3(0)))&&all(lessThan(tc,vec3(1))))return c;}tc=vec3(0);return -1;}
            vec4 sampleVolume(int i,vec3 tc){if(i==0)return textureLod(uVoxel0,tc,0);if(i==1)return textureLod(uVoxel1,tc,0);if(i==2)return textureLod(uVoxel2,tc,0);if(i==3)return textureLod(uVoxel3,tc,0);if(i==4)return textureLod(uVoxel4,tc,0);return textureLod(uVoxel5,tc,0);}
            vec4 sampleVoxelDirectional(int c,vec3 tc,vec3 d){vec3 atc=vec3(clamp(tc.xy,vec2(0),vec2(1)),(float(c)+clamp(tc.z,0.0,1.0))/float(max(uAtlasCascadeCount,1)));vec3 w=abs(d);w/=max(w.x+w.y+w.z,.0001);return sampleVolume(d.x>=0?0:1,atc)*w.x+sampleVolume(d.y>=0?2:3,atc)*w.y+sampleVolume(d.z>=0?4:5,atc)*w.z;}
            float voxelOpacity(int c,vec3 tc,vec3 d){return sampleVoxelDirectional(c,tc,d).a;}
            bool traceVoxel(vec3 origin,vec3 direction,out vec3 hitPosition,out vec3 hitNormal,out vec3 hitRadiance){if(uCascadeCount<=0||uCascadeResolution<=0)return false;float distanceAlong=.08,previousDistance=distanceAlong;for(int i=0;i<32;i++){if(distanceAlong>uTraceDistance)break;vec3 p=origin+direction*distanceAlong,tc;int c=findCascade(p,tc);if(c<0){previousDistance=distanceAlong;distanceAlong+=.2;continue;}float voxelSize=uCascadeSizes[c]/float(uCascadeResolution);float localOpacity=voxelOpacity(c,tc,direction);if(localOpacity>.08){float low=previousDistance,high=distanceAlong;for(int refinement=0;refinement<4;refinement++){float middle=(low+high)*.5;vec3 middleTc;int middleCascade=findCascade(origin+direction*middle,middleTc);float middleOpacity=middleCascade>=0?voxelOpacity(middleCascade,middleTc,direction):0.0;if(middleOpacity>.08)high=middle;else low=middle;}hitPosition=origin+direction*high;hitNormal=-direction;vec3 refinedTc;int refinedCascade=findCascade(hitPosition,refinedTc);hitRadiance=refinedCascade>=0?max(sampleVoxelDirectional(refinedCascade,refinedTc,direction).rgb,vec3(0)):vec3(0);return true;}previousDistance=distanceAlong;distanceAlong+=max(voxelSize,.04);}return false;}
            bool sampleCard(vec3 p,vec3 n,bool allowFallback,out vec3 radiance){int cellIndex=findCell(ivec3(floor(p/max(uCellSize,.001))));if(cellIndex<0)return false;int offset=cells[cellIndex].coordinateAndOffset.w;uint count=cells[cellIndex].countAndPadding.x;ivec2 atlasSize=textureSize(uCardDepthAtlas,0);float best=1e10,fallbackBest=1e10;ivec2 bestTexel=ivec2(0),fallbackTexel=ivec2(0);for(uint i=0u;i<count;i++){uint id=candidates[offset+int(i)];if(id==0u||id>uint(uCardCount))continue;CardBounds card=cards[id-1u];if(any(lessThan(p,card.minimumAndId.xyz))||any(greaterThan(p,card.maximumBounds.xyz)))continue;float facing=dot(n,normalize(card.cardNormal.xyz));if(facing<=.2)continue;vec4 clip=card.worldToCardClip*vec4(p,1);if(abs(clip.w)<.00001)continue;vec3 ndc=clip.xyz/clip.w;vec2 cardUv=ndc.xy*.5+.5;float projectedDepth=ndc.z*.5+.5;if(any(lessThan(cardUv,vec2(-.025)))||any(greaterThan(cardUv,vec2(1.025)))||projectedDepth<-.025||projectedDepth>1.025)continue;cardUv=clamp(cardUv,vec2(0),vec2(1));projectedDepth=clamp(projectedDepth,0.0,1.0);ivec2 cardMin=ivec2(card.atlasScaleBias.zw*vec2(atlasSize));ivec2 cardSize=max(ivec2(card.atlasScaleBias.xy*vec2(atlasSize)),ivec2(1));ivec2 cardMax=cardMin+cardSize-1;vec2 atlasUv=cardUv*card.atlasScaleBias.xy+card.atlasScaleBias.zw;ivec2 center=clamp(ivec2(atlasUv*vec2(atlasSize)),cardMin,cardMax);float facingScore=1.0-facing;if(facingScore<fallbackBest){fallbackBest=facingScore;fallbackTexel=center;}float depthError=1e10;bool hasDepth=false;ivec2 depthTexel=center;for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){ivec2 texel=clamp(center+ivec2(x,y),cardMin,cardMax);float cachedDepth=texelFetch(uCardDepthAtlas,texel,0).r;if(cachedDepth<.9999){float error=abs(cachedDepth-projectedDepth);if(error<depthError){depthError=error;depthTexel=texel;}hasDepth=true;}}if(!hasDepth||depthError>.05)continue;float score=depthError+facingScore*.01;if(score<best){best=score;bestTexel=depthTexel;}}ivec2 selectedTexel=best<1e9?bestTexel:fallbackTexel;if(best>=1e9&&(!allowFallback||fallbackBest>=1e9))return false;radiance=max(texelFetch(uAccumulatedRadianceAtlas,selectedTexel,0).rgb,vec3(0));if(any(isnan(radiance))||any(isinf(radiance)))return false;float luminance=dot(radiance,vec3(.2126,.7152,.0722));if(luminance>8.0)return false;radiance=min(radiance,vec3(8.0));return true;}
            void main(){vec3 p=texture(uScenePositionTexture,UV).xyz,raw=texture(uSceneNormalTexture,UV).xyz;if(length(raw)<.01){FragColor=vec4(0);return;}vec3 n=normalize(raw),origin=p+n*.04,total=vec3(0);float resolved=0;for(int rayIndex=0;rayIndex<8;rayIndex++){if(rayIndex>=uRayCount)break;vec3 direction=rayDirection(n,rayIndex),hitP,hitN,radiance,voxelRadiance=vec3(0);bool screenHit=traceScreen(origin,direction,hitP,hitN);bool hit=screenHit;if(!hit)hit=traceVoxel(origin,direction,hitP,hitN,voxelRadiance);if(!hit)continue;bool cardResolved=sampleCard(hitP,hitN,screenHit,radiance);if(cardResolved){total+=radiance;resolved+=1.0;}else if(!screenHit&&dot(voxelRadiance,vec3(.2126,.7152,.0722))>.0001){const float voxelConfidence=.65;total+=min(voxelRadiance,vec3(8.0))*voxelConfidence;resolved+=voxelConfidence;}}float rayCount=float(max(uRayCount,1));vec3 estimate=resolved>0.0?total/max(resolved,.65):vec3(0);float luminance=dot(estimate,vec3(.2126,.7152,.0722));estimate*=min(1.0,4.0/max(luminance,.0001));FragColor=vec4(estimate,clamp(resolved/rayCount,0.0,1.0));}
        )";
        m_indirectGatherShader = Shader::Create(indirectGather);

        ShaderSource temporal;
        temporal.vertexSource = debug.vertexSource;
        temporal.fragmentSource = R"(#version 330 core
            in vec2 UV;out vec4 FragColor;uniform sampler2D uCurrent,uHistory,uHistoryMetadata,uMotion,uPosition,uNormal;
            uniform mat4 uView,uPreviousView;uniform int uHasHistory;uniform float uTemporalBlend;
            vec3 decodeNormal(vec2 e){vec2 f=e*2.0-1.0;vec3 n=vec3(f,1.0-abs(f.x)-abs(f.y));float t=clamp(-n.z,0.0,1.0);n.xy+=vec2(n.x>=0.0?-t:t,n.y>=0.0?-t:t);return normalize(n);}
            void main(){
                vec4 current=texture(uCurrent,UV);
                vec3 p=texture(uPosition,UV).xyz,raw=texture(uNormal,UV).xyz;
                if(length(raw)<.01){FragColor=vec4(0);return;}
                vec3 n=normalize(raw);
                float currentDepth=max(-(uView*vec4(p,1)).z,0.0);
                vec3 neighborhoodMin=current.a>0.0?current.rgb:vec3(1e6);
                vec3 neighborhoodMax=current.a>0.0?current.rgb:vec3(0);
                float compatibleSamples=current.a>0.0?1.0:0.0;
                vec2 texel=1.0/vec2(textureSize(uCurrent,0));
                for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){
                    if(x==0&&y==0)continue;
                    vec2 sampleUv=clamp(UV+vec2(x,y)*texel,vec2(0),vec2(1));
                    vec3 sampleRaw=texture(uNormal,sampleUv).xyz;
                    if(length(sampleRaw)<.01||dot(normalize(sampleRaw),n)<.72)continue;
                    vec3 sampleP=texture(uPosition,sampleUv).xyz;
                    float sampleDepth=max(-(uView*vec4(sampleP,1)).z,0.0);
                    if(abs(sampleDepth-currentDepth)>max(.06,currentDepth*.02))continue;
                    vec4 sampleValue=texture(uCurrent,sampleUv);
                    if(sampleValue.a<=0.0)continue;
                    neighborhoodMin=min(neighborhoodMin,sampleValue.rgb);
                    neighborhoodMax=max(neighborhoodMax,sampleValue.rgb);
                    compatibleSamples+=1.0;
                }
                vec3 resolved=current.rgb;
                float confidence=current.a;
                if(uHasHistory!=0){
                    vec2 motion=texture(uMotion,UV).xy,historyUv=UV-motion;
                    if(all(greaterThanEqual(historyUv,vec2(0)))&&all(lessThanEqual(historyUv,vec2(1)))){
                        ivec2 size=textureSize(uHistoryMetadata,0);
                        vec2 historyPixel=historyUv*vec2(size)-.5;
                        ivec2 historyBase=ivec2(floor(historyPixel)),bestCoord=ivec2(0);
                        float previousDepth=max(-(uPreviousView*vec4(p,1)).z,0.0);
                        float depthTolerance=max(.04,previousDepth*.015),bestScore=1e20;
                        bool foundHistory=false;
                        for(int y=0;y<2;y++)for(int x=0;x<2;x++){
                            ivec2 coord=clamp(historyBase+ivec2(x,y),ivec2(0),size-ivec2(1));
                            vec4 metadata=texelFetch(uHistoryMetadata,coord,0);
                            if(metadata.w<=.5)continue;
                            float depthError=abs(metadata.z-previousDepth);
                            float normalAgreement=dot(decodeNormal(metadata.xy),n);
                            if(depthError>depthTolerance||normalAgreement<=.8)continue;
                            vec2 pixelOffset=(vec2(coord)+.5)-historyUv*vec2(size);
                            float score=depthError/max(depthTolerance,.0001)+(1.0-normalAgreement)*4.0+dot(pixelOffset,pixelOffset)*.02;
                            if(score<bestScore){bestScore=score;bestCoord=coord;foundHistory=true;}
                        }
                        if(foundHistory){
                            vec4 history=texelFetch(uHistory,bestCoord,0);
                            vec3 padding=max((neighborhoodMax-neighborhoodMin)*.5,vec3(.05));
                            vec3 clampedHistory=compatibleSamples>=3.0?clamp(history.rgb,neighborhoodMin-padding,neighborhoodMax+padding):history.rgb;
                            float motionRejection=smoothstep(.25,.75,length(motion));
                            float historyWeight=uTemporalBlend*(1.0-clamp(current.a,0.0,1.0)*.2)*(1.0-motionRejection);
                            resolved=current.a>0.0?mix(current.rgb,clampedHistory,historyWeight):clampedHistory;
                            confidence=max(current.a,history.a*.985);
                        }
                    }
                }
                FragColor=vec4(max(resolved,vec3(0)),confidence);
            }
        )";
        m_gatherTemporalShader = Shader::Create(temporal);

        ShaderSource metadata;
        metadata.vertexSource = debug.vertexSource;
        metadata.fragmentSource = R"(#version 330 core
            in vec2 UV;out vec4 FragColor;uniform sampler2D uPosition,uNormal;uniform mat4 uView;
            vec2 encodeNormal(vec3 n){n/=max(abs(n.x)+abs(n.y)+abs(n.z),.0001);if(n.z<0.0)n.xy=(1.0-abs(n.yx))*vec2(n.x>=0.0?1.0:-1.0,n.y>=0.0?1.0:-1.0);return n.xy*.5+.5;}
            void main(){vec3 p=texture(uPosition,UV).xyz,raw=texture(uNormal,UV).xyz;if(length(raw)<.01){FragColor=vec4(0);return;}FragColor=vec4(encodeNormal(normalize(raw)),max(-(uView*vec4(p,1)).z,0.0),1.0);}
        )";
        m_gatherMetadataShader = Shader::Create(metadata);

        ShaderSource filter;
        filter.vertexSource=debug.vertexSource;
        filter.fragmentSource=R"(#version 330 core
            in vec2 UV;out vec4 FragColor;uniform sampler2D uIndirect,uPosition,uNormal;
            void main(){vec3 centerP=texture(uPosition,UV).xyz,centerRaw=texture(uNormal,UV).xyz;if(length(centerRaw)<.01){FragColor=vec4(0);return;}vec3 centerN=normalize(centerRaw),sum=vec3(0);float weightSum=0,confidenceSum=0;vec2 texel=1.0/vec2(textureSize(uIndirect,0));for(int y=-2;y<=2;y++)for(int x=-2;x<=2;x++){vec2 suv=clamp(UV+vec2(x,y)*texel,vec2(0),vec2(1));vec4 value=texture(uIndirect,suv);if(value.a<=.001)continue;vec3 sampleRaw=texture(uNormal,suv).xyz;if(length(sampleRaw)<.01)continue;vec3 sampleP=texture(uPosition,suv).xyz;float normalWeight=pow(max(dot(centerN,normalize(sampleRaw)),0.0),16.0);float distanceWeight=exp(-length(sampleP-centerP)*3.0);float spatialWeight=exp(-float(x*x+y*y)*.22);float weight=normalWeight*distanceWeight*spatialWeight*mix(.25,1.0,value.a);sum+=value.rgb*weight;weightSum+=weight;confidenceSum+=value.a*weight;}FragColor=weightSum>0.0?vec4(sum/weightSum,clamp(confidenceSum/weightSum,0.0,1.0)):vec4(0);}
        )";m_gatherFilterShader=Shader::Create(filter);

        ShaderSource composite;
        composite.vertexSource=debug.vertexSource;
        composite.fragmentSource=R"(#version 330 core
            in vec2 UV;out vec4 FragColor;uniform sampler2D uScene,uIndirect,uPosition,uNormal,uAlbedo;uniform float uIntensity;
            void main(){vec3 scene=texture(uScene,UV).rgb,centerP=texture(uPosition,UV).xyz,raw=texture(uNormal,UV).xyz;if(length(raw)<.01){FragColor=vec4(scene,1);return;}vec3 n=normalize(raw),sum=vec3(0);float weightSum=0;vec2 texel=1.0/vec2(textureSize(uIndirect,0));for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){vec2 suv=clamp(UV+vec2(x,y)*texel,vec2(0),vec2(1));vec4 indirect=texture(uIndirect,suv);if(indirect.a<=.001)continue;vec3 sampleRaw=texture(uNormal,suv).xyz,sampleP=texture(uPosition,suv).xyz;if(length(sampleRaw)<.01)continue;float weight=pow(max(dot(n,normalize(sampleRaw)),0.0),24.0)*exp(-length(sampleP-centerP)*4.0)*exp(-float(x*x+y*y)*.5)*mix(.2,1.0,indirect.a);sum+=indirect.rgb*weight;weightSum+=weight;}vec3 incoming=weightSum>0.0?sum/weightSum:vec3(0);vec4 albedoMetallic=texture(uAlbedo,UV);vec3 diffuseAlbedo=albedoMetallic.rgb*(1.0-clamp(albedoMetallic.a,0.0,1.0));vec3 indirectDiffuse=diffuseAlbedo*incoming*(uIntensity/3.14159265);FragColor=vec4(max(scene+indirectDiffuse,vec3(0)),1);}
        )";m_compositeShader=Shader::Create(composite);
    }

    void SurfaceCacheGIEffect::EnsureGatherTarget(int width, int height)
    {
        const int gatherWidth = std::max(width / 2, 1);
        const int gatherHeight = std::max(height / 2, 1);
        const bool resolutionChanged = !m_gatherTarget || m_gatherTarget->GetWidth() != gatherWidth || m_gatherTarget->GetHeight() != gatherHeight;
        if (!m_gatherTarget)
            m_gatherTarget = std::make_unique<RenderTarget>(RenderTargetConfig{
                .width = gatherWidth, .height = gatherHeight, .clearColor = glm::vec4(0.0f)});
        else if (m_gatherTarget->GetWidth() != gatherWidth || m_gatherTarget->GetHeight() != gatherHeight)
            m_gatherTarget->Resize(gatherWidth, gatherHeight);
        if (!m_indirectGatherTarget)
            m_indirectGatherTarget = std::make_unique<RenderTarget>(RenderTargetConfig{
                .width = gatherWidth, .height = gatherHeight, .clearColor = glm::vec4(0.0f)});
        else if (m_indirectGatherTarget->GetWidth() != gatherWidth || m_indirectGatherTarget->GetHeight() != gatherHeight)
            m_indirectGatherTarget->Resize(gatherWidth, gatherHeight);
        for (auto &target : m_gatherHistoryTargets)
        {
            if (!target) target = std::make_unique<RenderTarget>(RenderTargetConfig{
                .width = gatherWidth, .height = gatherHeight, .clearColor = glm::vec4(0.0f)});
            else if (target->GetWidth() != gatherWidth || target->GetHeight() != gatherHeight) target->Resize(gatherWidth, gatherHeight);
        }
        for (auto &target : m_gatherHistoryMetadataTargets)
        {
            if (!target) target = std::make_unique<RenderTarget>(RenderTargetConfig{
                .width = gatherWidth, .height = gatherHeight, .clearColor = glm::vec4(0.0f)});
            else if (target->GetWidth() != gatherWidth || target->GetHeight() != gatherHeight) target->Resize(gatherWidth, gatherHeight);
        }
        if(!m_filteredGatherTarget)m_filteredGatherTarget=std::make_unique<RenderTarget>(RenderTargetConfig{.width=gatherWidth,.height=gatherHeight,.clearColor=glm::vec4(0.0f)});else if(m_filteredGatherTarget->GetWidth()!=gatherWidth||m_filteredGatherTarget->GetHeight()!=gatherHeight)m_filteredGatherTarget->Resize(gatherWidth,gatherHeight);
        if (resolutionChanged) ResetGatherHistory();
    }

    void SurfaceCacheGIEffect::FilterGather(const PostProcessContext &context)
    {
        if(!m_filteredGatherTarget||!m_gatherFilterShader||!context.renderContext.gBuffer||!m_hasGatherHistory)return;Graphics::BindRenderTarget(m_filteredGatherTarget.get());glViewport(0,0,m_filteredGatherTarget->GetWidth(),m_filteredGatherTarget->GetHeight());glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);m_gatherFilterShader->Bind();const GLuint textures[]={m_gatherHistoryTargets[m_gatherHistoryIndex]->GetColorTextureID(),context.renderContext.gBuffer->GetPositionTextureID(),context.renderContext.gBuffer->GetNormalTextureID()};const char* names[]={"uIndirect","uPosition","uNormal"};for(int i=0;i<3;i++){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,textures[i]);m_gatherFilterShader->SetUniform(names[i],i);}DrawFullscreenTriangle();glBindFramebuffer(GL_FRAMEBUFFER,0);
    }

    void SurfaceCacheGIEffect::ResetGatherHistory()
    {
        m_gatherHistoryIndex = 0;
        m_hasGatherHistory = false;
        m_hasPreviousGatherCamera = false;
    }

    void SurfaceCacheGIEffect::ResolveGatherHistory(const PostProcessContext &context)
    {
        if (!m_gatherTemporalShader || !m_gatherMetadataShader || !context.renderContext.gBuffer) return;
        const glm::mat4 inverseView=glm::inverse(context.renderContext.cameraData.view);const glm::vec3 cameraPosition=glm::vec3(inverseView[3]);const glm::vec3 cameraForward=-glm::normalize(glm::vec3(inverseView[2]));if(m_hasPreviousGatherCamera){const glm::mat4 previousInverse=glm::inverse(m_previousGatherView);const glm::vec3 previousForward=-glm::normalize(glm::vec3(previousInverse[2]));if(glm::length(cameraPosition-m_previousGatherCameraPosition)>std::max(m_screenTraceDistance*.5f,1.0f)||glm::dot(cameraForward,previousForward)<.75f)ResetGatherHistory();}
        const std::uint8_t next=static_cast<std::uint8_t>((m_gatherHistoryIndex+1u)%2u);auto *output=m_gatherHistoryTargets[next].get();
        Graphics::BindRenderTarget(output);glViewport(0,0,output->GetWidth(),output->GetHeight());glDisable(GL_DEPTH_TEST);glDisable(GL_BLEND);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);m_gatherTemporalShader->Bind();
        const GLuint textures[]={m_indirectGatherTarget->GetColorTextureID(),m_gatherHistoryTargets[m_gatherHistoryIndex]->GetColorTextureID(),m_gatherHistoryMetadataTargets[m_gatherHistoryIndex]->GetColorTextureID(),context.renderContext.gBuffer->GetMotionTextureID(),context.renderContext.gBuffer->GetPositionTextureID(),context.renderContext.gBuffer->GetNormalTextureID()};const char *names[]={"uCurrent","uHistory","uHistoryMetadata","uMotion","uPosition","uNormal"};for(int i=0;i<6;i++){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,textures[i]);m_gatherTemporalShader->SetUniform(names[i],i);}m_gatherTemporalShader->SetUniform("uView",context.renderContext.cameraData.view);m_gatherTemporalShader->SetUniform("uPreviousView",m_previousGatherView);m_gatherTemporalShader->SetUniform("uHasHistory",m_hasGatherHistory?1:0);m_gatherTemporalShader->SetUniform("uTemporalBlend",m_gatherTemporalBlend);DrawFullscreenTriangle();
        auto *metadata=m_gatherHistoryMetadataTargets[next].get();Graphics::BindRenderTarget(metadata);glViewport(0,0,metadata->GetWidth(),metadata->GetHeight());glClear(GL_COLOR_BUFFER_BIT);m_gatherMetadataShader->Bind();glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,context.renderContext.gBuffer->GetPositionTextureID());m_gatherMetadataShader->SetUniform("uPosition",0);glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,context.renderContext.gBuffer->GetNormalTextureID());m_gatherMetadataShader->SetUniform("uNormal",1);m_gatherMetadataShader->SetUniform("uView",context.renderContext.cameraData.view);DrawFullscreenTriangle();glBindFramebuffer(GL_FRAMEBUFFER,0);m_gatherHistoryIndex=next;m_previousGatherView=context.renderContext.cameraData.view;m_previousGatherCameraPosition=cameraPosition;m_hasPreviousGatherCamera=true;m_hasGatherHistory=true;
    }

    void SurfaceCacheGIEffect::RenderIndirectGather(const PostProcessContext &context)
    {
        if (!m_indirectGatherTarget || !m_indirectGatherShader || !context.renderContext.gBuffer) return;
        Graphics::BindRenderTarget(m_indirectGatherTarget.get());glViewport(0,0,m_indirectGatherTarget->GetWidth(),m_indirectGatherTarget->GetHeight());glDisable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);glDisable(GL_BLEND);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
        m_indirectGatherShader->Bind();
        glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,context.renderContext.gBuffer->GetPositionTextureID());m_indirectGatherShader->SetUniform("uScenePositionTexture",0);
        glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,context.renderContext.gBuffer->GetNormalTextureID());m_indirectGatherShader->SetUniform("uSceneNormalTexture",1);
        glActiveTexture(GL_TEXTURE2);glBindTexture(GL_TEXTURE_2D,m_atlas->GetTexture(m_radianceHistoryIndex==0?SurfaceCacheAtlas::Layer::AccumulatedRadianceA:SurfaceCacheAtlas::Layer::AccumulatedRadianceB));m_indirectGatherShader->SetUniform("uAccumulatedRadianceAtlas",2);
        glActiveTexture(GL_TEXTURE3);glBindTexture(GL_TEXTURE_2D,m_atlas->GetTexture(SurfaceCacheAtlas::Layer::Depth));m_indirectGatherShader->SetUniform("uCardDepthAtlas",3);
        m_indirectGatherShader->SetUniform("uViewProjection",context.renderContext.cameraData.projection*context.renderContext.cameraData.view);m_indirectGatherShader->SetUniform("uCellSize",m_cardSpatialIndex.GetCellSize());m_indirectGatherShader->SetUniform("uTraceDistance",m_screenTraceDistance);m_indirectGatherShader->SetUniform("uTraceSteps",m_screenTraceSteps);m_indirectGatherShader->SetUniform("uRayCount",m_screenRayCount);m_indirectGatherShader->SetUniform("uFrameIndex",static_cast<int>(context.renderContext.frameSequence%1024u));m_indirectGatherShader->SetUniform("uCellCount",m_cardGpuIndex.GetCellCount());m_indirectGatherShader->SetUniform("uCardCount",m_cardGpuIndex.GetCardCount());
        for(int direction=0;direction<6;++direction){glActiveTexture(GL_TEXTURE4+direction);glBindTexture(GL_TEXTURE_3D,m_visibilitySnapshot.directionalVolumeTextures[static_cast<std::size_t>(direction)]);m_indirectGatherShader->SetUniform("uVoxel"+std::to_string(direction),4+direction);}m_indirectGatherShader->SetUniform("uCascadeCount",m_visibilitySnapshot.IsValid()?m_visibilitySnapshot.cascadeCount:0);m_indirectGatherShader->SetUniform("uAtlasCascadeCount",std::max(m_visibilitySnapshot.cascadeCount,1));m_indirectGatherShader->SetUniform("uCascadeResolution",m_visibilitySnapshot.cascadeResolution);for(int cascade=0;cascade<3;++cascade){const std::string suffix="["+std::to_string(cascade)+"]";m_indirectGatherShader->SetUniform("uCascadeOrigins"+suffix,m_visibilitySnapshot.cascades[static_cast<std::size_t>(cascade)].origin);m_indirectGatherShader->SetUniform("uCascadeSizes"+suffix,m_visibilitySnapshot.cascades[static_cast<std::size_t>(cascade)].size);}m_cardGpuIndex.Bind(0);DrawFullscreenTriangle();glBindFramebuffer(GL_FRAMEBUFFER,0);
    }

    void SurfaceCacheGIEffect::RenderGatherInputs(const PostProcessContext &context)
    {
        if (!m_gatherTarget || !m_gatherTarget->IsInitialized() || !m_gatherShader || !context.renderContext.gBuffer) return;
        Graphics::BindRenderTarget(m_gatherTarget.get());
        glViewport(0, 0, m_gatherTarget->GetWidth(), m_gatherTarget->GetHeight());
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND);
        glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
        m_gatherShader->Bind();
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, context.renderContext.gBuffer->GetPositionTextureID());
        m_gatherShader->SetUniform("uScenePositionTexture", 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, context.renderContext.gBuffer->GetNormalTextureID());
        m_gatherShader->SetUniform("uSceneNormalTexture", 1);
        m_gatherShader->SetUniform("uViewProjection", context.renderContext.cameraData.projection * context.renderContext.cameraData.view);
        m_gatherShader->SetUniform("uRayCount", m_screenRayCount);
        m_gatherShader->SetUniform("uTraceSteps", m_screenTraceSteps);
        m_gatherShader->SetUniform("uTraceDistance", m_screenTraceDistance);
        m_gatherShader->SetUniform("uFrameIndex", static_cast<int>(context.renderContext.frameSequence % 1024u));
        for (int direction = 0; direction < 6; ++direction)
        {
            glActiveTexture(GL_TEXTURE2 + direction);
            glBindTexture(GL_TEXTURE_3D, m_visibilitySnapshot.directionalVolumeTextures[static_cast<std::size_t>(direction)]);
            m_gatherShader->SetUniform("uVoxel" + std::to_string(direction), 2 + direction);
        }
        m_gatherShader->SetUniform("uCascadeCount", m_visibilitySnapshot.IsValid() ? m_visibilitySnapshot.cascadeCount : 0);
        m_gatherShader->SetUniform("uAtlasCascadeCount", std::max(m_visibilitySnapshot.cascadeCount, 1));
        m_gatherShader->SetUniform("uCascadeResolution", m_visibilitySnapshot.cascadeResolution);
        for (int cascade = 0; cascade < 3; ++cascade)
        {
            const std::string suffix = "[" + std::to_string(cascade) + "]";
            m_gatherShader->SetUniform("uCascadeOrigins" + suffix, m_visibilitySnapshot.cascades[static_cast<std::size_t>(cascade)].origin);
            m_gatherShader->SetUniform("uCascadeSizes" + suffix, m_visibilitySnapshot.cascades[static_cast<std::size_t>(cascade)].size);
        }
        DrawFullscreenTriangle();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    std::size_t SurfaceCacheGIEffect::ComputeSceneSignature(const PostProcessContext &context) const
    {
        std::size_t signature = 0;
        const auto *commands = context.renderContext.renderer
                                   ? &context.renderContext.renderer->GetSceneRenderCommands()
                                   : context.renderContext.renderCommands;
        if (!commands) return signature;
        for (const auto &command : *commands)
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
        m_hasRadianceHistory = false;
        m_radianceHistoryIndex = 0;
        ResetGatherHistory();
        SurfaceCacheAtlasAllocator allocator(m_atlasSize, m_atlasSize, 2);
        SurfaceCardId nextId = 1;
        int occupiedWidth = 1;
        int occupiedHeight = 1;
        std::vector<SurfaceCardWorldBounds> worldBounds;
        const auto *commands = context.renderContext.renderer
                                   ? &context.renderContext.renderer->GetSceneRenderCommands()
                                   : context.renderContext.renderCommands;
        if (commands)
        {
            for (const auto &command : *commands)
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
                    const glm::vec3 localRight = glm::normalize(glm::cross(card.localUp, card.localNormal));
                    const glm::vec3 localCenter = card.localCenter - card.localNormal * card.halfDepth;
                    glm::vec3 minimum(std::numeric_limits<float>::max());
                    glm::vec3 maximum(std::numeric_limits<float>::lowest());
                    for (int depthSign : {-1, 1})
                        for (int verticalSign : {-1, 1})
                            for (int horizontalSign : {-1, 1})
                            {
                                const glm::vec3 localCorner = localCenter +
                                    localRight * card.halfExtent.x * static_cast<float>(horizontalSign) +
                                    card.localUp * card.halfExtent.y * static_cast<float>(verticalSign) +
                                    card.localNormal * card.halfDepth * static_cast<float>(depthSign);
                                const glm::vec3 worldCorner = glm::vec3(command.model * glm::vec4(localCorner, 1.0f));
                                minimum = glm::min(minimum, worldCorner);
                                maximum = glm::max(maximum, worldCorner);
                            }
                    const glm::vec3 worldCardNormal = glm::normalize(glm::transpose(glm::inverse(glm::mat3(command.model))) * card.localNormal);
                    const glm::mat4 worldToCardClip = card.localViewProjection * glm::inverse(command.model);
                    const float inverseAtlasSize = 1.0f / static_cast<float>(m_atlasSize);
                    const glm::vec4 atlasScaleBias(
                        static_cast<float>(card.allocation.width) * inverseAtlasSize,
                        static_cast<float>(card.allocation.height) * inverseAtlasSize,
                        static_cast<float>(card.allocation.x) * inverseAtlasSize,
                        static_cast<float>(card.allocation.y) * inverseAtlasSize);
                    constexpr float boundsTolerance = 0.025f;
                    worldBounds.push_back({card.id, minimum - glm::vec3(boundsTolerance), maximum + glm::vec3(boundsTolerance),
                                           worldCardNormal, worldToCardClip, atlasScaleBias});
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
        m_cardSpatialIndex.Rebuild(worldBounds);
        m_cardGpuIndex.Upload(m_cardSpatialIndex.BuildGpuTables());
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

    void SurfaceCacheGIEffect::ResolveAccumulatedRadiance()
    {
        if (!m_atlas || !m_radianceResolveShader) return;
        const std::uint8_t previousIndex = m_radianceHistoryIndex;
        const std::uint8_t nextIndex = static_cast<std::uint8_t>((previousIndex + 1u) % 2u);
        const auto historyLayer = [](std::uint8_t index) {
            return index == 0 ? SurfaceCacheAtlas::Layer::AccumulatedRadianceA : SurfaceCacheAtlas::Layer::AccumulatedRadianceB;
        };
        m_atlas->BindLayerForWrite(historyLayer(nextIndex));
        glViewport(0, 0, m_atlas->GetSize(), m_atlas->GetSize());
        glDisable(GL_DEPTH_TEST); glDisable(GL_CULL_FACE); glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST);
        m_radianceResolveShader->Bind();
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m_atlas->GetTexture(SurfaceCacheAtlas::Layer::DirectRadiance));
        m_radianceResolveShader->SetUniform("uDirectRadiance", 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m_atlas->GetTexture(historyLayer(previousIndex)));
        m_radianceResolveShader->SetUniform("uPreviousRadiance", 1);
        m_radianceResolveShader->SetUniform("uHistoryBlend", m_radianceHistoryBlend);
        m_radianceResolveShader->SetUniform("uHasHistory", m_hasRadianceHistory ? 1 : 0);
        DrawFullscreenTriangle();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        m_radianceHistoryIndex = nextIndex;
        m_hasRadianceHistory = true;
    }

    void SurfaceCacheGIEffect::UpdateVisibility(const PostProcessContext &context)
    {
        const auto snapshotReady = [](const WorldVisibilitySnapshot &snapshot) {
            return snapshot.IsValid() && std::any_of(snapshot.cascades.begin(), snapshot.cascades.end(),
                [](const WorldVisibilityCascade &cascade) { return cascade.valid; });
        };
        m_visibilitySnapshot = {};
        m_visibilityStatus = 0;
        if (context.renderContext.postProcessEffects)
            for (auto *effect : *context.renderContext.postProcessEffects)
                if (effect && effect != this && effect->IsEnabled())
                    if (auto *provider = dynamic_cast<IWorldVisibilityProvider *>(effect))
                    {
                        m_visibilitySnapshot = provider->GetWorldVisibilitySnapshot();
                        m_visibilityStatus = snapshotReady(m_visibilitySnapshot) ? 2 : 1;
                        if (snapshotReady(m_visibilitySnapshot)) return;
                    }

        if (context.renderContext.renderer)
        {
            if (auto *provider = context.renderContext.renderer->UpdateWorldVisibility(context, m_visibilityCascadeCount))
            {
                m_visibilitySnapshot = provider->GetWorldVisibilitySnapshot();
                m_visibilityStatus = snapshotReady(m_visibilitySnapshot) ? 2 : 1;
            }
        }
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
        ResolveAccumulatedRadiance();
        UpdateVisibility(context);
        EnsureGatherTarget(context.sourceRenderTarget->GetWidth(), context.sourceRenderTarget->GetHeight());
        RenderGatherInputs(context);
        RenderIndirectGather(context);
        ResolveGatherHistory(context);
        FilterGather(context);
        if (context.renderContext.renderer)
            context.renderContext.renderer->RecordSurfaceCacheStats(
                m_stats.cardCount, m_stats.residentCardCount, m_stats.capturedCardCount,
                m_stats.atlasUsedPixels, m_stats.atlasTotalPixels);

        BeginApply(context);
        if(m_debugView==0&&m_composeIndirect&&m_compositeShader&&m_filteredGatherTarget){m_compositeShader->Bind();const GLuint textures[]={context.sourceRenderTarget->GetColorTextureID(),m_filteredGatherTarget->GetColorTextureID(),context.renderContext.gBuffer?context.renderContext.gBuffer->GetPositionTextureID():0,context.renderContext.gBuffer?context.renderContext.gBuffer->GetNormalTextureID():0,context.renderContext.gBuffer?context.renderContext.gBuffer->GetAlbedoTextureID():0};const char* names[]={"uScene","uIndirect","uPosition","uNormal","uAlbedo"};for(int i=0;i<5;i++){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,textures[i]);m_compositeShader->SetUniform(names[i],i);}m_compositeShader->SetUniform("uIntensity",m_indirectIntensity);DrawFullscreenTriangle();EndApply();return;}
        if (m_debugView >= 8 && m_debugView <= 10 && m_cardLookupDebugShader)
        {
            m_cardLookupDebugShader->Bind();
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, context.renderContext.gBuffer ? context.renderContext.gBuffer->GetPositionTextureID() : 0);
            m_cardLookupDebugShader->SetUniform("uScenePositionTexture", 0);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, context.renderContext.gBuffer ? context.renderContext.gBuffer->GetNormalTextureID() : 0);
            m_cardLookupDebugShader->SetUniform("uSceneNormalTexture", 1);
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_2D, m_atlas->GetTexture(SurfaceCacheAtlas::Layer::Depth));
            m_cardLookupDebugShader->SetUniform("uCardDepthAtlas", 2);
            m_cardLookupDebugShader->SetUniform("uCellSize", m_cardSpatialIndex.GetCellSize());
            m_cardLookupDebugShader->SetUniform("uCellCount", m_cardGpuIndex.GetCellCount());
            m_cardLookupDebugShader->SetUniform("uCardCount", m_cardGpuIndex.GetCardCount());
            m_cardLookupDebugShader->SetUniform("uLookupDebugMode", m_debugView - 8);
            m_cardGpuIndex.Bind(0);
            DrawFullscreenTriangle();
            EndApply();
            return;
        }
        m_debugShader->Bind();
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, context.sourceRenderTarget->GetColorTextureID());
        m_debugShader->SetUniform("uSceneTexture", 0);
        SurfaceCacheAtlas::Layer layer = SurfaceCacheAtlas::Layer::AlbedoMetallic;
        if (m_debugView == 2) layer = SurfaceCacheAtlas::Layer::NormalRoughness;
        else if (m_debugView == 3) layer = SurfaceCacheAtlas::Layer::Emission;
        else if (m_debugView == 4) layer = SurfaceCacheAtlas::Layer::Depth;
        else if (m_debugView == 5) layer = SurfaceCacheAtlas::Layer::DirectRadiance;
        else if (m_debugView == 6) layer = m_radianceHistoryIndex == 0
                                                 ? SurfaceCacheAtlas::Layer::AccumulatedRadianceA
                                                 : SurfaceCacheAtlas::Layer::AccumulatedRadianceB;
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, m_atlas->GetTexture(layer));
        m_debugShader->SetUniform("uAtlasTexture", 1); m_debugShader->SetUniform("uDebugView", m_debugView);
        m_debugShader->SetUniform("uResidentCardCount", m_stats.residentCardCount);
        m_debugShader->SetUniform("uAtlasExtent", m_debugAtlasExtent);
        m_debugShader->SetUniform("uViewportAspect", static_cast<float>(context.destinationRenderTarget->GetWidth()) /
                                                       static_cast<float>(std::max(context.destinationRenderTarget->GetHeight(), 1)));
        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, context.renderContext.gBuffer ? context.renderContext.gBuffer->GetPositionTextureID() : 0);
        m_debugShader->SetUniform("uScenePositionTexture", 2);
        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, m_gatherTarget ? m_gatherTarget->GetColorTextureID() : 0);
        m_debugShader->SetUniform("uGatherTexture", 3);
        glActiveTexture(GL_TEXTURE4);
        glBindTexture(GL_TEXTURE_2D, m_debugView == 15 && m_hasGatherHistory ? m_gatherHistoryTargets[m_gatherHistoryIndex]->GetColorTextureID() : (m_indirectGatherTarget ? m_indirectGatherTarget->GetColorTextureID() : 0));
        m_debugShader->SetUniform("uIndirectGatherTexture", 4);
        glActiveTexture(GL_TEXTURE5);glBindTexture(GL_TEXTURE_2D,m_filteredGatherTarget?m_filteredGatherTarget->GetColorTextureID():0);m_debugShader->SetUniform("uFilteredGatherTexture",5);
        m_debugShader->SetUniform("uVisibilityCascadeCount", m_visibilitySnapshot.cascadeCount);
        m_debugShader->SetUniform("uVisibilityStatus", m_visibilityStatus);
        for (int index = 0; index < 3; ++index)
        {
            const std::string suffix = "[" + std::to_string(index) + "]";
            m_debugShader->SetUniform("uVisibilityOrigins" + suffix, m_visibilitySnapshot.cascades[index].origin);
            m_debugShader->SetUniform("uVisibilitySizes" + suffix, m_visibilitySnapshot.cascades[index].size);
            m_debugShader->SetUniform("uVisibilityValid" + suffix, m_visibilitySnapshot.cascades[index].valid ? 1 : 0);
        }
        DrawFullscreenTriangle(); EndApply();
    }
}
