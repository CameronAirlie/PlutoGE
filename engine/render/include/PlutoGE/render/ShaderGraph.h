#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <array>
#include <memory>
#include <span>
#include <string>
#include <vector>

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

    struct ShaderGraph
    {
        int version = 1;
        ShaderGraphOutline outline;
        bool unlit = false;
        std::vector<ShaderGraphNode> nodes;
        std::vector<ShaderGraphLink> links;
        std::vector<ShaderGraphVariable> variables;
    };

    // std140-compatible bytecode, shared by the OpenGL and Vulkan surface paths.
    constexpr int kMaxShaderGraphInstructions = 64;
    struct alignas(16) ShaderGraphProgramData
    {
        glm::ivec4 header{0}; // instruction count, unlit, reserved
        glm::ivec4 outputs0{0}; // albedo, normal, metallic, roughness
        glm::ivec4 outputs1{0}; // opacity, emission, reserved
        std::array<glm::ivec4, kMaxShaderGraphInstructions> instructions{};
        std::array<glm::vec4, kMaxShaderGraphInstructions> values{};
    };
    static_assert(sizeof(ShaderGraphProgramData) == 2096);
    struct ShaderGraphProgram
    {
        ShaderGraphProgramData data;
        std::uint64_t hash = 0;
    };
    std::shared_ptr<const ShaderGraphProgram> BuildShaderGraphProgram(
        const ShaderGraph &graph, std::span<const ShaderGraphVariable> overrides = {}, std::string *errorMessage = nullptr);
    bool ValidateShaderGraph(const ShaderGraph &graph, std::string *errorMessage = nullptr);
    float ShaderGraphTimeSeconds();
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
