#include "PlutoGE/render/ShaderGraph.h"

#include "PlutoGE/render/Shader.h"
#include "ShaderGraphEvaluation.h"
#include <iomanip>
#include <limits>
#include <locale>

#include <algorithm>
#include <functional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace PlutoGE::render
{
    static std::string GraphExtraTexturesGlsl()
    {
        return R"(
uniform sampler2D uGraphSceneColor,uGraphSceneDepth;
uniform mat4 uGraphViewProjection=mat4(1);
vec2 shaderGraphScreenUV(vec3 p){vec4 clip=uGraphViewProjection*vec4(p,1);return clip.xy/max(abs(clip.w),.000001)*.5+.5;}
uniform sampler2D uGraphTexture0,uGraphTexture1,uGraphTexture2,uGraphTexture3;
uniform vec4 uGraphTexturePresent=vec4(0),uGraphSamplerModes=vec4(0);
ivec2 graphTexelCoord(ivec2 p,ivec2 size,int mode){return mode>=2?clamp(p,ivec2(0),size-1):(p%size+size)%size;}
vec4 graphSampleExtra(sampler2D tex,vec2 uv,int mode){
    ivec2 size=textureSize(tex,0);vec2 p=uv*vec2(size)-0.5;ivec2 cell=ivec2(floor(p));vec2 f=fract(p);
    if((mode%2)==1)return texelFetch(tex,graphTexelCoord(ivec2(floor(uv*vec2(size))),size,mode),0);
    return mix(mix(texelFetch(tex,graphTexelCoord(cell,size,mode),0),texelFetch(tex,graphTexelCoord(cell+ivec2(1,0),size,mode),0),f.x),
        mix(texelFetch(tex,graphTexelCoord(cell+ivec2(0,1),size,mode),0),texelFetch(tex,graphTexelCoord(cell+ivec2(1),size,mode),0),f.x),f.y);
}
vec4 graphExtraTexture(int slot,vec2 uv){
    if(slot==8)return textureLod(uGraphSceneColor,uv,0.0);
    if(slot==9)return vec4(textureLod(uGraphSceneDepth,uv,0.0).r);
    int i=slot-4;if(i<0||i>=4||uGraphTexturePresent[i]<0.5)return vec4(1);
    if(i==0)return graphSampleExtra(uGraphTexture0,uv,int(uGraphSamplerModes.x));
    if(i==1)return graphSampleExtra(uGraphTexture1,uv,int(uGraphSamplerModes.y));
    if(i==2)return graphSampleExtra(uGraphTexture2,uv,int(uGraphSamplerModes.z));
    return graphSampleExtra(uGraphTexture3,uv,int(uGraphSamplerModes.w));
}
)";
    }
    std::string ShaderGraphRuntimeGlsl(bool vertexStage)
    {
        std::string evaluator=kShaderGraphEvaluationSource;
        for(const auto &[from,to]:std::initializer_list<std::pair<std::string,std::string>>{
            {"float4","vec4"},{"float3","vec3"},{"float2","vec2"},{"int4","ivec4"},{"frac(","fract("},{"lerp(","mix("}})
            for(size_t at=0;(at=evaluator.find(from,at))!=std::string::npos;at+=to.size())evaluator.replace(at,from.size(),to);
        std::string code=R"(
uniform vec4 uGraphHeader=vec4(0), uGraphOutputs0=vec4(0), uGraphOutputs1=vec4(0);
uniform vec4 uGraphInstructions[64], uGraphValues[64];
uniform float uGraphTime;
uniform vec3 uGraphCameraPosition;
)";
        if(vertexStage) code+=R"(
uniform sampler2D uAlbedoTexture,uNormalTexture,uMetallicTexture,uRoughnessTexture;
uniform float uHasAlbedoTexture,uHasNormalTexture,uHasMetallicTexture,uHasRoughnessTexture;
uniform vec4 uColor;
uniform float uMetallicFactor,uRoughnessFactor;
uniform vec3 uEmission;
)";
        code+=GraphExtraTexturesGlsl();
        code+=R"(
vec4 shaderGraphTexture(int slot,vec2 uv) {
    if(slot>=4)return graphExtraTexture(slot,uv);
    if(slot==0)return uHasAlbedoTexture>0.5?textureLod(uAlbedoTexture,uv,0.0):vec4(1);
    if(slot==1)return uHasNormalTexture>0.5?textureLod(uNormalTexture,uv,0.0):vec4(.5,.5,1,1);
    if(slot==2)return uHasMetallicTexture>0.5?textureLod(uMetallicTexture,uv,0.0):vec4(1);
    return uHasRoughnessTexture>0.5?textureLod(uRoughnessTexture,uv,0.0):vec4(1);
}
)" + evaluator + R"(
ShaderGraphData runtimeShaderGraph() {
    ShaderGraphData graph;
    graph.header=ivec4(uGraphHeader);graph.outputs0=ivec4(uGraphOutputs0);graph.outputs1=ivec4(uGraphOutputs1);
    for(int i=0;i<graph.header.x;++i){graph.instructions[i]=ivec4(uGraphInstructions[i]);graph.values[i]=uGraphValues[i];}
    return graph;
}
)";
        return code;
    }
    namespace
    {
        std::string FloatLiteral(float value)
        {
            std::ostringstream output;
            output.imbue(std::locale::classic());
            output << std::setprecision(std::numeric_limits<float>::max_digits10) << value;
            const std::string text = output.str();
            return text.find_first_of(".eE") == std::string::npos ? text + ".0" : text;
        }

        std::string VecLiteral(const glm::vec4 &value, int components)
        {
            std::ostringstream output;
            output << "vec" << components << "(";
            for (int index = 0; index < components; ++index)
            {
                if (index > 0)
                {
                    output << ", ";
                }
                output << FloatLiteral(value[index]);
            }
            output << ")";
            return output.str();
        }

        std::string BuildFragmentSource(const ShaderGraph &graph, bool unlit, std::string &errorMessage)
        {
            auto program = BuildShaderGraphProgram(graph, {}, &errorMessage);
            if (!program) return {};
            std::string evaluator = kShaderGraphEvaluationSource;
            const auto replaceAll = [](std::string &text, const std::string &from, const std::string &to)
            {
                for (size_t at = 0; (at = text.find(from, at)) != std::string::npos; at += to.size()) text.replace(at, from.size(), to);
            };
            for (const auto &[from,to] : std::initializer_list<std::pair<std::string,std::string>>{
                {"float4","vec4"},{"float3","vec3"},{"float2","vec2"},{"int4","ivec4"},{"frac(","fract("},{"lerp(","mix("}})
                replaceAll(evaluator,from,to);
            const auto integerVector = [](glm::ivec4 v) { return "ivec4(" + std::to_string(v.x) + "," + std::to_string(v.y) + "," + std::to_string(v.z) + "," + std::to_string(v.w) + ")"; };
            std::string uniforms = "uniform float uGraphTime;\nuniform vec3 uGraphCameraPosition;\nuniform vec4 uGraphValues[64] = vec4[64](";
            for (int i=0;i<64;++i) uniforms += (i ? "," : "") + VecLiteral(program->data.values[i],4);
            uniforms += ");\n";
            std::string setup = "ShaderGraphData sg;\nsg.header=" + integerVector(program->data.header) + ";\nsg.outputs0=" + integerVector(program->data.outputs0) + ";\nsg.outputs1=" + integerVector(program->data.outputs1) + ";\n";
            for(int i=0;i<program->data.header.x;++i)
            {
                auto index=std::to_string(i);
                setup += "sg.instructions["+index+"]="+integerVector(program->data.instructions[i])+"; sg.values["+index+"]=uGraphValues["+index+"];\n";
            }
            setup += "vec4 sgColor=vec4(graphAlbedo,graphOpacity);\nevaluateShaderGraph(sg,FragPos,normalize(Normal),normalize(uGraphCameraPosition-FragPos),uGraphTime,UV,sgColor,graphNormal,graphMetallic,graphRoughness,graphEmission);\ngraphAlbedo=sgColor.rgb; graphOpacity=sgColor.a;\n";
            const std::string albedo="graphAlbedo", normal="graphNormal", metallic="graphMetallic", roughness="graphRoughness", opacity="graphOpacity", emission="graphEmission";
            unlit = unlit || graph.unlit;
            const char *initialBakedLightingAlpha = unlit ? "2.0" : "0.0";
            const char *allowLightmap = unlit ? "false" : "true";

            return std::string(R"(
            #version 330 core

            layout (location = 0) out vec3 gPosition;
            layout (location = 1) out vec4 gNormalRoughness;
            layout (location = 2) out vec4 gAlbedoMetallic;
            layout (location = 3) out vec2 gMotionVector;
            layout (location = 4) out vec4 gBakedLighting;
            layout (location = 5) out float gDebug;
            layout (location = 6) out vec3 gEmission;
            layout (location = 7) out vec4 gSubsurface;

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
            uniform float uOutlineWidth = 0.0;
            uniform vec3 uOutlineColor = vec3(0.0);
            uniform vec4 uColor = vec4(1.0, 1.0, 1.0, 1.0);
            uniform int uAlphaMode = 0;
            uniform int uTwoSided = 0;
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
            uniform vec3 uEmission = vec3(0.0);
            uniform float uSubsurfaceFactor = 0.0;
            uniform vec3 uSubsurfaceColor = vec3(1.0, 0.35, 0.2);
            uniform float uSubsurfaceRadius = 1.0;

            uniform sampler2D uLightmapTexture;
            uniform float uHasLightmapTexture = 0.0;
            uniform vec2 uUVScale = vec2(1.0);
            uniform vec4 uLightmapUvTransform = vec4(1.0, 1.0, 0.0, 0.0);

            float ReadTextureChannel(vec4 value, int channel)
            {
                if (channel == 1) return value.g;
                if (channel == 2) return value.b;
                if (channel == 3) return value.a;
                return value.r;
            }

            float LodDitherThreshold()
            {
                float pattern[16] = float[16](
                    0.0, 8.0, 2.0, 10.0,
                    12.0, 4.0, 14.0, 6.0,
                    3.0, 11.0, 1.0, 9.0,
                    15.0, 7.0, 13.0, 5.0);
                ivec2 pixel = ivec2(gl_FragCoord.xy) & ivec2(3);
                return (pattern[pixel.y * 4 + pixel.x] + 0.5) / 16.0;
            }

            void ApplyLodDither()
            {
                if (InstanceFlags.x < -0.5) return;
                float packedFade = fract(InstanceFlags.z);
                if (packedFade > 0.0 && packedFade < 0.3)
                {
                    if (LodDitherThreshold() < packedFade * 4.0) discard;
                }
                else if (packedFade > 0.45)
                {
                    if (LodDitherThreshold() >= (packedFade - 0.5) * 4.0) discard;
                }
            }

        )" + uniforms + GraphExtraTexturesGlsl() + R"(
            vec4 shaderGraphTexture(int slot, vec2 uv)
            {
                if(slot>=4)return graphExtraTexture(slot,uv);
                if(slot==0) return uHasAlbedoTexture>0.5 ? texture(uAlbedoTexture,uv) : vec4(1.0);
                if(slot==1) return uHasNormalTexture>0.5 ? texture(uNormalTexture,uv) : vec4(0.5,0.5,1.0,1.0);
                if(slot==2) return uHasMetallicTexture>0.5 ? texture(uMetallicTexture,uv) : vec4(1.0);
                return uHasRoughnessTexture>0.5 ? texture(uRoughnessTexture,uv) : vec4(1.0);
            }
        )" + evaluator + R"(
            float ToFloat(float value) { return value; }
            float ToFloat(vec2 value) { return value.x; }
            float ToFloat(vec3 value) { return value.x; }
            float ToFloat(vec4 value) { return value.x; }
            vec2 ToVec2(float value) { return vec2(value); }
            vec2 ToVec2(vec2 value) { return value; }
            vec2 ToVec2(vec3 value) { return value.xy; }
            vec2 ToVec2(vec4 value) { return value.xy; }
            vec3 ToVec3(float value) { return vec3(value); }
            vec3 ToVec3(vec2 value) { return vec3(value, 0.0); }
            vec3 ToVec3(vec3 value) { return value; }
            vec3 ToVec3(vec4 value) { return value.rgb; }

            float ShaderGraphHash(vec2 value)
            {
                return fract(sin(dot(value, vec2(127.1, 311.7))) * 43758.5453123);
            }

            float ShaderGraphNoise(vec2 value)
            {
                vec2 cell = floor(value);
                vec2 local = fract(value);
                vec2 curve = local * local * (3.0 - 2.0 * local);
                float bottomLeft = ShaderGraphHash(cell);
                float bottomRight = ShaderGraphHash(cell + vec2(1.0, 0.0));
                float topLeft = ShaderGraphHash(cell + vec2(0.0, 1.0));
                float topRight = ShaderGraphHash(cell + vec2(1.0, 1.0));
                return mix(mix(bottomLeft, bottomRight, curve.x), mix(topLeft, topRight, curve.x), curve.y);
            }

            void main()
            {
                ApplyLodDither();
                gPosition = FragPos;
                if (uOutlineWidth > 0.0)
                {
                    gNormalRoughness = vec4(normalize(Normal), 1.0);
                    gAlbedoMetallic = vec4(0.0);
                    gEmission = uOutlineColor;
                    gSubsurface = vec4(0.0);
                    gBakedLighting = vec4(0.0, 0.0, 0.0, 2.0);
                    gDebug = -1.0;
                    gMotionVector = CurrentClipPos.xy / max(abs(CurrentClipPos.w), 0.00001) * 0.5 -
                                    PreviousClipPos.xy / max(abs(PreviousClipPos.w), 0.00001) * 0.5;
                    return;
                }
                vec3 graphAlbedo = uColor.rgb;
                float graphOpacity = uColor.a;
                float graphMetallic = clamp(uMetallicFactor, 0.0, 1.0);
                float graphRoughness = clamp(uRoughnessFactor, 0.04, 1.0);
                vec3 graphEmission = max(uEmission, vec3(0.0));
                vec3 graphNormal = normalize(Normal);

                if (uHasAlbedoTexture > 0.5)
                {
                    vec4 texAlbedo = texture(uAlbedoTexture, UV);
                    graphOpacity *= texAlbedo.a;
                    graphAlbedo *= texAlbedo.rgb;
                }

                if (uHasNormalTexture > 0.5)
                {
                    graphNormal = texture(uNormalTexture, UV).rgb;
                    if (uFlipNormalY > 0.5)
                    {
                        graphNormal.g = 1.0 - graphNormal.g;
                    }
                    graphNormal = normalize(graphNormal * 2.0 - 1.0);
                    graphNormal = normalize(TBN * graphNormal);
                }

                if (uHasMetallicTexture > 0.5)
                {
                    graphMetallic *= ReadTextureChannel(texture(uMetallicTexture, UV), uMetallicTextureChannel);
                }

                if (uHasRoughnessTexture > 0.5)
                {
                    graphRoughness *= ReadTextureChannel(texture(uRoughnessTexture, UV), uRoughnessTextureChannel);
                }

        )" + setup + R"(
                vec3 finalAlbedo = ToVec3()" +
                               albedo + ");\n"
                                        "                vec3 finalNormal = normalize(ToVec3(" +
                               normal + "));\n"
                                        "                float finalMetallic = ToFloat(" +
                               metallic + ");\n"
                                          "                float finalRoughness = ToFloat(" +
                               roughness + ");\n"
                                           "                float finalOpacity = ToFloat(" +
                               opacity + ");\n"
                                         "                vec3 finalEmission = ToVec3(" +
                               emission + ");\n" + R"(

                if (uTwoSided != 0 && !gl_FrontFacing)
                {
                    finalNormal = -finalNormal;
                }

                if (uAlphaMode == 1 && finalOpacity < uAlphaCutoff)
                {
                    discard;
                }

                gNormalRoughness = vec4(normalize(finalNormal), clamp(finalRoughness, 0.04, 1.0));
                gAlbedoMetallic = vec4(finalAlbedo, clamp(finalMetallic, 0.0, 1.0));
                gEmission = max(finalEmission, vec3(0.0));
                gSubsurface = vec4(max(uSubsurfaceColor, vec3(0.0)), clamp(uSubsurfaceFactor, 0.0, 1.0));
                gBakedLighting = vec4(0.0, 0.0, 0.0, )" +
                               std::string(initialBakedLightingAlpha) + R"();
                gDebug = InstanceFlags.w <= 0.5 ? -1.0 : clamp(floor(InstanceFlags.z) / InstanceFlags.w, 0.0, 1.0);

                if ()" + std::string(allowLightmap) +
                               R"( && InstanceFlags.x > 0.5 && uHasLightmapTexture > 0.5)
                {
                    vec2 safeUvScale = max(abs(uUVScale), vec2(0.0001));
                    vec2 sourceLightmapUv =
                        mix(UV2, UV, clamp(InstanceFlags.y, 0.0, 1.0)) / safeUvScale;
                    vec2 lightmapUv = clamp(
                        sourceLightmapUv * uLightmapUvTransform.xy + uLightmapUvTransform.zw,
                        vec2(0.0), vec2(1.0));
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
        )");
        }
    }

    ShaderGraph CreateDefaultShaderGraph()
    {
        ShaderGraph graph;
        graph.nodes = {
            ShaderGraphNode{.id = 1, .kind = ShaderGraphNodeKind::MaterialInput, .name = "Material Color", .materialInput = ShaderGraphMaterialInput::Color, .position = {40.0f, 40.0f}},
            ShaderGraphNode{.id = 2, .kind = ShaderGraphNodeKind::MaterialInput, .name = "Material Normal", .materialInput = ShaderGraphMaterialInput::Normal, .position = {40.0f, 130.0f}},
            ShaderGraphNode{.id = 3, .kind = ShaderGraphNodeKind::MaterialInput, .name = "Material Metallic", .materialInput = ShaderGraphMaterialInput::Metallic, .position = {40.0f, 220.0f}},
            ShaderGraphNode{.id = 4, .kind = ShaderGraphNodeKind::MaterialInput, .name = "Material Roughness", .materialInput = ShaderGraphMaterialInput::Roughness, .position = {40.0f, 310.0f}},
            ShaderGraphNode{.id = 5, .kind = ShaderGraphNodeKind::MaterialInput, .name = "Material Opacity", .materialInput = ShaderGraphMaterialInput::Opacity, .position = {40.0f, 400.0f}},
            ShaderGraphNode{.id = 6, .kind = ShaderGraphNodeKind::MaterialInput, .name = "Material Emission", .materialInput = ShaderGraphMaterialInput::Emission, .position = {40.0f, 490.0f}},
            ShaderGraphNode{.id = 100, .kind = ShaderGraphNodeKind::Output, .name = "Geometry Output", .position = {430.0f, 180.0f}},
        };
        graph.links = {
            ShaderGraphLink{.id = 1, .fromNodeId = 1, .fromPin = "Out", .toNodeId = 100, .toPin = "Albedo"},
            ShaderGraphLink{.id = 2, .fromNodeId = 2, .fromPin = "Out", .toNodeId = 100, .toPin = "Normal"},
            ShaderGraphLink{.id = 3, .fromNodeId = 3, .fromPin = "Out", .toNodeId = 100, .toPin = "Metallic"},
            ShaderGraphLink{.id = 4, .fromNodeId = 4, .fromPin = "Out", .toNodeId = 100, .toPin = "Roughness"},
            ShaderGraphLink{.id = 5, .fromNodeId = 5, .fromPin = "Out", .toNodeId = 100, .toPin = "Opacity"},
            ShaderGraphLink{.id = 6, .fromNodeId = 6, .fromPin = "Out", .toNodeId = 100, .toPin = "Emission"},
        };
        return graph;
    }

    ShaderGraph CreateDefaultUnlitShaderGraph()
    {
        auto graph = CreateDefaultShaderGraph();
        graph.unlit = true;
        return graph;
    }

    std::uint64_t HashShaderGraph(const ShaderGraph &graph)
    {
        std::uint64_t hash = 1469598103934665603ull;
        const auto mix = [&hash](std::uint64_t value)
        {
            hash ^= value;
            hash *= 1099511628211ull;
        };
        mix(graph.unlit ? 1ull : 0ull);mix(graph.tessellation);
        for(const auto &pass:graph.passes)mix(std::hash<std::string>{}(pass));
        for(const auto &t:graph.textures){mix(std::hash<std::string>{}(t.name));mix(std::hash<std::string>{}(t.reference));mix(t.nearest);mix(t.clamp);}
        for (const auto &v : graph.variables)
        {
            mix(std::hash<std::string>{}(v.name));
            mix(static_cast<unsigned>(v.type));
            for(int i=0;i<4;++i) mix(std::hash<float>{}(v.value[i]));
        }
        mix(graph.outline.enabled ? 1ull : 0ull);
        mix(std::hash<float>{}(graph.outline.width));
        for (int i = 0; i < 3; ++i) mix(std::hash<float>{}(graph.outline.color[i]));
        for (const auto &node : graph.nodes)
        {
            mix(static_cast<std::uint64_t>(node.id));
            mix(static_cast<std::uint64_t>(node.kind));
            mix(std::hash<std::string>{}(node.parameter));
            if(node.subgraph)mix(HashShaderGraph(*node.subgraph));
            mix(static_cast<std::uint64_t>(node.materialInput));
            mix(node.componentPins ? 1ull : 0ull);
            for (int index = 0; index < 4; ++index)
            {
                mix(static_cast<std::uint64_t>(std::hash<float>{}(node.value[index])));
            }
        }
        for (const auto &link : graph.links)
        {
            mix(static_cast<std::uint64_t>(link.id));
            mix(static_cast<std::uint64_t>(link.fromNodeId));
            mix(static_cast<std::uint64_t>(link.toNodeId));
            mix(static_cast<std::uint64_t>(std::hash<std::string>{}(link.fromPin)));
            mix(static_cast<std::uint64_t>(std::hash<std::string>{}(link.toPin)));
        }
        return hash;
    }

    Shader *CompileShaderGraphToGeometryShader(const ShaderGraph &graph, bool unlit, std::string *errorMessage)
    {
        std::string compileError;
        if (errorMessage) errorMessage->clear();
        if (!ValidateShaderGraph(graph, errorMessage)) return nullptr;
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
            uniform float uOutlineWidth = 0.0;
            uniform int uUseSkinning = 0;
            uniform mat4 uJointMatrices[128];
            uniform vec2 uUVScale = vec2(1.0, 1.0);

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
                            uJointMatrices[clamp(aJoints.x, 0, 127)] * aWeights.x +
                            uJointMatrices[clamp(aJoints.y, 0, 127)] * aWeights.y +
                            uJointMatrices[clamp(aJoints.z, 0, 127)] * aWeights.z +
                            uJointMatrices[clamp(aJoints.w, 0, 127)] * aWeights.w;
                    }
                }

                vec3 localPosition = aPos;
                if (aInstanceFlags.x < -0.5)
                {
                    int terrainLod = int(floor(aInstanceFlags.z));
                    float morphFactor = clamp(fract(aInstanceFlags.z) * 4.0, 0.0, 1.0);
                    float targetHeight = terrainLod < 4
                                             ? aWeights[terrainLod]
                                             : float(aJoints.x) / 4096.0;
                    localPosition.y = mix(localPosition.y, targetHeight, morphFactor);
                }

                vec4 skinnedPosition = skinMatrix * vec4(localPosition, 1.0);
                vec3 skinnedNormal = mat3(skinMatrix) * aNormal;
                vec3 skinnedTangent = mat3(skinMatrix) * aTangent.xyz;

                vec4 currentWorldPos = aModel * skinnedPosition;
                vec4 previousWorldPos = aPreviousModel * skinnedPosition;
                FragPos = currentWorldPos.xyz;
                // Normalization cancels the inverse determinant. The signed
                // cofactor is exact and considerably cheaper per vertex.
                mat3 model3 = mat3(aModel);
                vec3 cofactor0 = cross(model3[1], model3[2]);
                vec3 cofactor1 = cross(model3[2], model3[0]);
                vec3 cofactor2 = cross(model3[0], model3[1]);
                float orientation = dot(model3[0], cofactor0) < 0.0 ? -1.0 : 1.0;
                mat3 normalMatrix = mat3(cofactor0, cofactor1, cofactor2) * orientation;
                vec3 worldNormal = normalize(normalMatrix * skinnedNormal);
                vec3 worldTangent = normalize(normalMatrix * skinnedTangent);
                worldTangent = normalize(worldTangent - dot(worldTangent, worldNormal) * worldNormal);
                vec3 worldBitangent = cross(worldNormal, worldTangent) * aTangent.w;

                currentWorldPos.xyz += worldNormal * uOutlineWidth;
                mat3 previous3 = mat3(aPreviousModel);
                mat3 previousCofactor = mat3(cross(previous3[1], previous3[2]), cross(previous3[2], previous3[0]), cross(previous3[0], previous3[1]));
                vec3 previousNormal = previousCofactor * skinnedNormal * (determinant(previous3) < 0.0 ? -1.0 : 1.0);
                previousWorldPos.xyz += previousNormal * (uOutlineWidth / max(length(previousNormal), 0.000001));
                FragPos = currentWorldPos.xyz;
                Normal = worldNormal;
                UV = aUV * uUVScale;
                UV2 = aUV2 * uUVScale;
                CurrentClipPos = uCurrentViewProjection * currentWorldPos;
                PreviousClipPos = uPreviousViewProjection * previousWorldPos;
                gl_Position = CurrentClipPos;
                InstanceFlags = aInstanceFlags;
                TBN = mat3(worldTangent, normalize(worldBitangent), worldNormal);
            }
        )";
        source.fragmentSource = BuildFragmentSource(graph, unlit, compileError);
        if (!compileError.empty())
        {
            if (errorMessage) *errorMessage = compileError;
            return nullptr;
        }
        const auto program = BuildShaderGraphProgram(graph);
        if (program && program->data.header.z > 0)
        {
            // Share the generated evaluator and material declarations between stages.
            const auto &fragment = source.fragmentSource;
            auto begin = fragment.find("uniform sampler2D uAlbedoTexture;");
            auto end = fragment.find("float ReadTextureChannel", begin);
            std::string declarations = fragment.substr(begin, end - begin);
            for (const std::string duplicate : {"uniform float uOutlineWidth = 0.0;", "uniform vec2 uUVScale = vec2(1.0);"})
                if (auto at = declarations.find(duplicate); at != std::string::npos) declarations.erase(at, duplicate.size());
            begin = fragment.find("uniform float uGraphTime;");
            end = fragment.find("float ToFloat", begin);
            std::string evaluator = fragment.substr(begin, end - begin);
            // Vertex texture sampling has no derivatives.
            for (const std::string sampler : {"uAlbedoTexture", "uNormalTexture", "uMetallicTexture", "uRoughnessTexture"})
            {
                const std::string from = "texture(" + sampler + ",uv)";
                const std::string to = "textureLod(" + sampler + ",uv,0.0)";
                if (auto at = evaluator.find(from); at != std::string::npos) evaluator.replace(at, from.size(), to);
            }
            begin = fragment.find("ShaderGraphData sg;");
            end = fragment.find("vec4 sgColor=", begin);
            const std::string helper = declarations + evaluator +
                "uniform float uGraphPreviousTime;\nvec3 graphOffset(vec3 p, vec3 n,float time) {\n" + fragment.substr(begin,end-begin) +
                "return shaderGraphVertexOffset(sg,p,n,normalize(uGraphCameraPosition-p),time,aUV*uUVScale,uColor,uMetallicFactor,uRoughnessFactor,uEmission);\n}\n";
            source.vertexSource.insert(source.vertexSource.find("void main()"), helper);
            const auto at = source.vertexSource.find("FragPos = currentWorldPos.xyz;", source.vertexSource.find("previousWorldPos.xyz +="));
            source.vertexSource.insert(at,
                "currentWorldPos.xyz += graphOffset(currentWorldPos.xyz,worldNormal,uGraphTime);\n"
                "previousWorldPos.xyz += graphOffset(previousWorldPos.xyz,normalize(previousNormal),uGraphPreviousTime);\n");
        }
        auto *shader = Shader::Create(source);
        if (!shader && errorMessage) *errorMessage = "GPU shader compilation failed; see the shader compiler log.";
        return shader;
    }

    const char *ToString(ShaderGraphNodeKind kind)
    {
        switch (kind)
        {
        case ShaderGraphNodeKind::MaterialInput:
            return "MaterialInput";
        case ShaderGraphNodeKind::Float:
            return "Float";
        case ShaderGraphNodeKind::Vec2:
            return "Vec2";
        case ShaderGraphNodeKind::Vec3:
            return "Vec3";
        case ShaderGraphNodeKind::Color:
            return "Color";
        case ShaderGraphNodeKind::Add:
            return "Add";
        case ShaderGraphNodeKind::Subtract:
            return "Subtract";
        case ShaderGraphNodeKind::Multiply:
            return "Multiply";
        case ShaderGraphNodeKind::Divide:
            return "Divide";
        case ShaderGraphNodeKind::Lerp:
            return "Lerp";
        case ShaderGraphNodeKind::Clamp:
            return "Clamp";
        case ShaderGraphNodeKind::Normalize:
            return "Normalize";
        case ShaderGraphNodeKind::NoiseTexture:
            return "NoiseTexture";
        case ShaderGraphNodeKind::MeshUV:
            return "MeshUV";
        case ShaderGraphNodeKind::Parameter: return "Parameter";
        case ShaderGraphNodeKind::Time: return "Time";
        case ShaderGraphNodeKind::WorldPosition: return "WorldPosition";
        case ShaderGraphNodeKind::WorldNormal: return "WorldNormal";
        case ShaderGraphNodeKind::ViewDirection: return "ViewDirection";
        case ShaderGraphNodeKind::Dot: return "Dot";
        case ShaderGraphNodeKind::Sine: return "Sine";
        case ShaderGraphNodeKind::Power: return "Power";
        case ShaderGraphNodeKind::OneMinus: return "OneMinus";
        case ShaderGraphNodeKind::TextureSample: return "TextureSample";
        case ShaderGraphNodeKind::Subgraph: return "Subgraph";
        case ShaderGraphNodeKind::ScreenUV:return "ScreenUV";
        case ShaderGraphNodeKind::SceneColor:return "SceneColor";
        case ShaderGraphNodeKind::SceneDepth:return "SceneDepth";
        case ShaderGraphNodeKind::Expression:return "Expression";
        case ShaderGraphNodeKind::Output:
            return "Output";
        default:
            return "Float";
        }
    }

    const char *ToString(ShaderGraphMaterialInput input)
    {
        switch (input)
        {
        case ShaderGraphMaterialInput::Normal:
            return "Normal";
        case ShaderGraphMaterialInput::Metallic:
            return "Metallic";
        case ShaderGraphMaterialInput::Roughness:
            return "Roughness";
        case ShaderGraphMaterialInput::Opacity:
            return "Opacity";
        case ShaderGraphMaterialInput::UV:
            return "UV";
        case ShaderGraphMaterialInput::Emission:
            return "Emission";
        case ShaderGraphMaterialInput::Color:
        default:
            return "Color";
        }
    }

    ShaderGraphNodeKind ParseShaderGraphNodeKind(std::string_view value)
    {
        if (value == "MaterialInput")
            return ShaderGraphNodeKind::MaterialInput;
        if (value == "Vec2")
            return ShaderGraphNodeKind::Vec2;
        if (value == "Vec3")
            return ShaderGraphNodeKind::Vec3;
        if (value == "Color")
            return ShaderGraphNodeKind::Color;
        if (value == "Add")
            return ShaderGraphNodeKind::Add;
        if (value == "Subtract")
            return ShaderGraphNodeKind::Subtract;
        if (value == "Multiply")
            return ShaderGraphNodeKind::Multiply;
        if (value == "Divide")
            return ShaderGraphNodeKind::Divide;
        if (value == "Lerp")
            return ShaderGraphNodeKind::Lerp;
        if (value == "Clamp")
            return ShaderGraphNodeKind::Clamp;
        if (value == "Normalize")
            return ShaderGraphNodeKind::Normalize;
        if (value == "NoiseTexture")
            return ShaderGraphNodeKind::NoiseTexture;
        if (value == "MeshUV")
            return ShaderGraphNodeKind::MeshUV;
        if (value == "Parameter") return ShaderGraphNodeKind::Parameter;
        if (value == "Time") return ShaderGraphNodeKind::Time;
        if (value == "WorldPosition") return ShaderGraphNodeKind::WorldPosition;
        if (value == "WorldNormal") return ShaderGraphNodeKind::WorldNormal;
        if (value == "ViewDirection") return ShaderGraphNodeKind::ViewDirection;
        if (value == "Dot") return ShaderGraphNodeKind::Dot;
        if (value == "Sine") return ShaderGraphNodeKind::Sine;
        if (value == "Power") return ShaderGraphNodeKind::Power;
        if (value == "OneMinus") return ShaderGraphNodeKind::OneMinus;
        if (value == "TextureSample") return ShaderGraphNodeKind::TextureSample;
        if (value == "Subgraph") return ShaderGraphNodeKind::Subgraph;
        if(value=="ScreenUV")return ShaderGraphNodeKind::ScreenUV;
        if(value=="SceneColor")return ShaderGraphNodeKind::SceneColor;
        if(value=="SceneDepth")return ShaderGraphNodeKind::SceneDepth;
        if(value=="Expression")return ShaderGraphNodeKind::Expression;
        if (value == "Output")
            return ShaderGraphNodeKind::Output;
        if (value == "Float") return ShaderGraphNodeKind::Float;
        return static_cast<ShaderGraphNodeKind>(-1);
    }

    ShaderGraphMaterialInput ParseShaderGraphMaterialInput(std::string_view value)
    {
        if (value == "Normal")
            return ShaderGraphMaterialInput::Normal;
        if (value == "Metallic")
            return ShaderGraphMaterialInput::Metallic;
        if (value == "Roughness")
            return ShaderGraphMaterialInput::Roughness;
        if (value == "Opacity")
            return ShaderGraphMaterialInput::Opacity;
        if (value == "UV")
            return ShaderGraphMaterialInput::UV;
        if (value == "Emission")
            return ShaderGraphMaterialInput::Emission;
        if (value == "Color") return ShaderGraphMaterialInput::Color;
        return static_cast<ShaderGraphMaterialInput>(-1);
    }
}
