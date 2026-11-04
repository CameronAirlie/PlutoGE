#include "PlutoGE/render/ShaderGraph.h"

#include <functional>
#include <string>

namespace PlutoGE::render
{
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
        case ShaderGraphNodeKind::Step:return "Step";
        case ShaderGraphNodeKind::Floor:return "Floor";
        case ShaderGraphNodeKind::Smoothstep:return "Smoothstep";
        case ShaderGraphNodeKind::LightDirection:return "LightDirection";
        case ShaderGraphNodeKind::LightColor:return "LightColor";
        case ShaderGraphNodeKind::LightAttenuation:return "LightAttenuation";
        case ShaderGraphNodeKind::ShadowAttenuation:return "ShadowAttenuation";
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
        if(value=="Step")return ShaderGraphNodeKind::Step;
        if(value=="Floor")return ShaderGraphNodeKind::Floor;
        if(value=="Smoothstep")return ShaderGraphNodeKind::Smoothstep;
        if(value=="LightDirection")return ShaderGraphNodeKind::LightDirection;
        if(value=="LightColor")return ShaderGraphNodeKind::LightColor;
        if(value=="LightAttenuation")return ShaderGraphNodeKind::LightAttenuation;
        if(value=="ShadowAttenuation")return ShaderGraphNodeKind::ShadowAttenuation;
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
