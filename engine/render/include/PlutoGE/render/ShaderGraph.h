#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>
#include <functional>

namespace PlutoGE::render
{
    class Shader;

    enum class ShaderGraphValueType
    {
        Float = 0,
        Vec2 = 1,
        Vec3 = 2,
        Vec4 = 3,
    };

    enum class ShaderGraphNodeKind
    {
        MaterialInput = 0,
        Float = 1,
        Vec2 = 2,
        Vec3 = 3,
        Color = 4,
        Add = 5,
        Subtract = 6,
        Multiply = 7,
        Divide = 8,
        Lerp = 9,
        Clamp = 10,
        Normalize = 11,
        NoiseTexture = 12,
        MeshUV = 13,
        Output = 14,
        Parameter = 15,
        Time = 16,
        WorldPosition = 17,
        WorldNormal = 18,
        ViewDirection = 19,
        Dot = 20,
        Sine = 21,
        Power = 22,
        OneMinus = 23,
        TextureSample = 24,
        Subgraph = 25,
        ScreenUV = 26,
        SceneColor = 27,
        SceneDepth = 28,
        Expression = 29,
        Step = 30,
        Floor = 31,
        Smoothstep = 32,
        LightDirection = 33,
        LightColor = 34,
        LightAttenuation = 35,
        ShadowAttenuation = 36,
    };

    enum class ShaderGraphMaterialInput
    {
        Color = 0,
        Normal = 1,
        Metallic = 2,
        Roughness = 3,
        Opacity = 4,
        UV = 5,
        Emission = 6,
    };

    struct ShaderGraph;
    struct ShaderGraphNode
    {
        int id = 0;
        ShaderGraphNodeKind kind = ShaderGraphNodeKind::Float;
        std::string name;
        ShaderGraphMaterialInput materialInput = ShaderGraphMaterialInput::Color;
        glm::vec4 value{1.0f};
        glm::vec2 position{0.0f};
        glm::vec2 size{0.0f};
        bool componentPins = false;
        bool collapsed = false;
        std::string parameter;
        std::shared_ptr<const ShaderGraph> subgraph;
    };

    struct ShaderGraphLink
    {
        int id = 0;
        int fromNodeId = 0;
        std::string fromPin;
        int toNodeId = 0;
        std::string toPin;
    };

    struct ShaderGraphVariable
    {
        std::string name;
        ShaderGraphValueType type = ShaderGraphValueType::Float;
        glm::vec4 value{0.0f};
    };

    struct ShaderGraphOutline
    {
        bool enabled = false;
        float width = 0.02f; // World units.
        glm::vec3 color{0.0f};
    };

    struct ShaderGraphTextureParameter
    {
        std::string name, reference;
        bool nearest=false, clamp=false;
    };

    struct ShaderGraph
    {
        int version = 1;
        ShaderGraphOutline outline;
        bool unlit = false;
        int tessellation = 0;
        std::vector<ShaderGraphNode> nodes;
        std::vector<ShaderGraphLink> links;
        std::vector<ShaderGraphVariable> variables;
        std::vector<ShaderGraphTextureParameter> textures;
        std::vector<std::string> passes;
    };

    // std140-compatible bytecode, shared by the OpenGL and Vulkan surface paths.
    constexpr int kMaxShaderGraphInstructions = 64;
    struct alignas(16) ShaderGraphProgramData
    {
        glm::ivec4 header{0}; // instruction count, unlit, vertex instruction count, reserved
        glm::ivec4 outputs0{0}; // albedo, normal, metallic, roughness
        glm::ivec4 outputs1{0}; // opacity, emission, world-space vertex offset, packed surface count << 8 | direct lighting register + 1 (0 = PBR)
        std::array<glm::ivec4, kMaxShaderGraphInstructions> instructions{};
        std::array<glm::vec4, kMaxShaderGraphInstructions> values{};
    };
    static_assert(sizeof(ShaderGraphProgramData) == 2096);
    struct ShaderGraphProgram
    {
        ShaderGraphProgramData data;
        std::vector<ShaderGraphTextureParameter> textures;
        std::uint64_t hash = 0;
        bool requiresSceneTextures=false, usesTime=false, usesViewDirection=false;
    };
    std::shared_ptr<const ShaderGraphProgram> BuildShaderGraphProgram(
        const ShaderGraph &graph, std::span<const ShaderGraphVariable> overrides = {}, std::string *errorMessage = nullptr);
    bool ValidateShaderGraph(const ShaderGraph &graph, std::string *errorMessage = nullptr);
    float ShaderGraphTimeSeconds();
    void SetShaderGraphTimeSeconds(float seconds);
    float ShaderGraphPreviousTimeSeconds();
    void ResetShaderGraphClock();
    std::string ShaderGraphRuntimeGlsl(bool vertexStage = false);
    struct ShaderGraphSample
    {
        glm::vec3 worldPosition{0}, worldNormal{0,0,1}, viewDirection{0,0,1};
        glm::vec2 uv{0}, screenUV{0};
        glm::vec2 uv2{0};
        float time=0;
        glm::vec4 color{1};
        glm::vec3 normal{0,0,1}, emission{0}, vertexOffset{0};
        // Per-light inputs are only valid in the Direct Lighting branch.
        glm::vec3 lightDirection{0,0,1}, lightColor{1}, directLighting{0};
        float lightAttenuation=1, shadowAttenuation=1;
        float metallic=0, roughness=1;
    };
    using ShaderGraphTextureSampler = std::function<glm::vec4(int,glm::vec2)>;
    ShaderGraphSample EvaluateShaderGraph(const ShaderGraphProgram &program, ShaderGraphSample sample,
        const ShaderGraphTextureSampler &textures = {}, bool vertexOnly = false);
    struct MaterialConfig;
    ShaderGraphSample EvaluateMaterialShaderGraph(const MaterialConfig &material, ShaderGraphSample sample,
        bool vertexOnly = false);
    std::vector<std::string_view> ShaderGraphInputPins(const ShaderGraphNode &node);
    std::vector<std::string_view> ShaderGraphOutputPins(const ShaderGraphNode &node);

    ShaderGraph CreateDefaultShaderGraph();
    ShaderGraph CreateDefaultUnlitShaderGraph();
    std::uint64_t HashShaderGraph(const ShaderGraph &graph);
    Shader *CompileShaderGraphToGeometryShader(const ShaderGraph &graph, bool unlit = false, std::string *errorMessage = nullptr);

    const char *ToString(ShaderGraphNodeKind kind);
    const char *ToString(ShaderGraphMaterialInput input);
    ShaderGraphNodeKind ParseShaderGraphNodeKind(std::string_view value);
    ShaderGraphMaterialInput ParseShaderGraphMaterialInput(std::string_view value);
}
