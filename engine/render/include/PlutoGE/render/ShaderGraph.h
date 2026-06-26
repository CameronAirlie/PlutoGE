#pragma once

#include <glm/glm.hpp>

#include <cstdint>
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
    };

    enum class ShaderGraphMaterialInput
    {
        Color = 0,
        Normal = 1,
        Metallic = 2,
        Roughness = 3,
        Opacity = 4,
        UV = 5,
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

    struct ShaderGraph
    {
        int version = 1;
        std::vector<ShaderGraphNode> nodes;
        std::vector<ShaderGraphLink> links;
        std::vector<ShaderGraphVariable> variables;
    };

    ShaderGraph CreateDefaultShaderGraph();
    std::uint64_t HashShaderGraph(const ShaderGraph &graph);
    Shader *CompileShaderGraphToGeometryShader(const ShaderGraph &graph, std::string *errorMessage = nullptr);

    const char *ToString(ShaderGraphNodeKind kind);
    const char *ToString(ShaderGraphMaterialInput input);
    ShaderGraphNodeKind ParseShaderGraphNodeKind(std::string_view value);
    ShaderGraphMaterialInput ParseShaderGraphMaterialInput(std::string_view value);
}
