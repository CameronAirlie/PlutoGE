#include "PlutoGE/render/ShaderGraphCodegen.h"

#include <algorithm>
#include <array>
#include <set>
#include <sstream>
#include <stdexcept>

namespace PlutoGE::render
{
    namespace
    {
        // Mirrors shaderGraphReadsA/B/C and opcode 18's D operand in ShaderGraphCommon.slang.
        bool ReadsA(int op) { return (op >= 2 && op <= 16) || op == 18 || op == 19 || op == 20 || op == 21; }
        bool ReadsB(int op) { return (op >= 2 && op <= 7) || op == 9 || op == 10 || op == 12 || op == 18 || op == 19 || op == 21; }
        bool ReadsC(int op) { return op == 6 || op == 7 || op == 9 || op == 18 || op == 21; }
        constexpr int kMaxOpcode = 21;

        struct Layout
        {
            int total = 0, vertexCount = 0, surfaceCount = 0;
            int lightingOutput = -1; // Register, or -1 for PBR lighting.
        };

        // Register indices for an instruction's operands, in A, B, C, D order; -1 when unused.
        std::array<int, 4> Operands(const ShaderGraphProgramData &data, int index)
        {
            const auto &op = data.instructions[static_cast<std::size_t>(index)];
            return {ReadsA(op.x) ? op.y : -1, ReadsB(op.x) ? op.z : -1, ReadsC(op.x) ? op.w : -1,
                    op.x == 18 ? static_cast<int>(data.values[static_cast<std::size_t>(index)].x) : -1};
        }

        Layout Validate(const ShaderGraphProgramData &data)
        {
            const auto require = [](bool condition, const char *message)
            {
                if (!condition) throw std::invalid_argument(message);
            };
            Layout layout;
            layout.total = data.header.x;
            layout.vertexCount = data.header.z;
            require(layout.total >= 0 && layout.total <= kMaxShaderGraphInstructions, "Invalid instruction count");
            require(layout.vertexCount >= 0 && layout.vertexCount <= layout.total, "Invalid vertex instruction count");
            layout.surfaceCount = data.outputs1.w != 0 ? data.outputs1.w >> 8 : layout.total;
            require(layout.surfaceCount >= 0 && layout.surfaceCount <= layout.total, "Invalid surface instruction count");
            if (data.outputs1.w != 0)
            {
                layout.lightingOutput = (data.outputs1.w & 255) - 1;
                require(layout.lightingOutput >= 0 && layout.lightingOutput < layout.total, "Invalid lighting output");
            }
            for (int i = 0; i < layout.total; ++i)
            {
                const int op = data.instructions[static_cast<std::size_t>(i)].x;
                require(op >= 0 && op <= kMaxOpcode, "Unknown shader graph opcode");
                // Operands must be computed earlier; the CPU compiler orders them topologically.
                for (const int operand : Operands(data, i))
                    require(operand < i, "Shader graph operand is not topologically ordered");
            }
            const auto output = [&](int reg, int count) { require(reg >= 0 && reg < count, "Invalid output register"); };
            if (layout.surfaceCount > 0)
                for (const int reg : {data.outputs0.x, data.outputs0.y, data.outputs0.z, data.outputs0.w,
                                      data.outputs1.x, data.outputs1.y})
                    output(reg, layout.surfaceCount);
            if (layout.vertexCount > 0) output(data.outputs1.z, layout.vertexCount);
            return layout;
        }

        std::string Register(int index) { return index < 0 ? "float4(0.0)" : "r" + std::to_string(index); }

        void EmitInstructions(std::ostringstream &out, const ShaderGraphProgramData &data, int first, int last)
        {
            for (int i = first; i < last; ++i)
            {
                const auto &op = data.instructions[static_cast<std::size_t>(i)];
                const auto operands = Operands(data, i);
                out << "    const float4 r" << i << "=shaderGraphInstruction(int4(" << op.x << ',' << op.y << ','
                    << op.z << ',' << op.w << ")," << Register(operands[0]) << ',' << Register(operands[1]) << ','
                    << Register(operands[2]) << ',' << Register(operands[3]) << ",graph.values[" << i << "],inputs);\n";
            }
        }

        // Surface registers that the lighting stage reads, including a
        // Direct Lighting output connected directly to a surface value.
        std::set<int> SurfaceRegistersForLighting(const ShaderGraphProgramData &data, const Layout &layout)
        {
            std::set<int> result;
            if (layout.lightingOutput < 0) return result;
            for (int i = layout.surfaceCount; i < layout.total; ++i)
                for (const int operand : Operands(data, i))
                    if (operand >= 0 && operand < layout.surfaceCount) result.insert(operand);
            if (layout.lightingOutput < layout.surfaceCount) result.insert(layout.lightingOutput);
            return result;
        }
    }

    std::uint64_t ShaderGraphStructureHash(const ShaderGraphProgramData &data)
    {
        std::uint64_t hash = 1469598103934665603ull;
        const auto mix = [&hash](std::int64_t value)
        {
            for (int byte = 0; byte < 8; ++byte)
            {
                hash ^= static_cast<std::uint64_t>(value >> (byte * 8)) & 0xffu;
                hash *= 1099511628211ull;
            }
        };
        mix(kShaderGraphCodegenVersion);
        for (const auto &vector : {data.header, data.outputs0, data.outputs1})
            for (int component = 0; component < 4; ++component) mix(vector[component]);
        const int count = std::clamp(data.header.x, 0, kMaxShaderGraphInstructions);
        for (int i = 0; i < count; ++i)
        {
            const auto &op = data.instructions[static_cast<std::size_t>(i)];
            for (int component = 0; component < 4; ++component) mix(op[component]);
            // Opcode 18 stores a register index, not data, in its value slot.
            if (op.x == 18) mix(static_cast<std::int64_t>(data.values[static_cast<std::size_t>(i)].x));
        }
        return hash;
    }

    std::string GenerateShaderGraphSlang(const ShaderGraphProgramData &data)
    {
        const Layout layout = Validate(data);
        const auto carried = SurfaceRegistersForLighting(data, layout);
        std::ostringstream out;
        out << "// Generated by PlutoGE ShaderGraphCodegen v" << kShaderGraphCodegenVersion
            << ". Structure 0x" << std::hex << ShaderGraphStructureHash(data) << std::dec << ".\n"
            << "// Straight-line evaluation with the interpreter's API; values come from the material.\n"
            << "#include \"ShaderGraphCommon.slang\"\n\n"
            << "struct ShaderGraphState\n{\n    ShaderGraphInputs inputs;\n";
        for (const int reg : carried) out << "    float4 s" << reg << ";\n";
        out << "};\n\n";

        out << "void evaluateShaderGraph(ShaderGraphData graph, float3 worldPosition, float3 worldNormal,\n"
               "    float3 viewDirection, float time, float2 uv, inout float4 color, inout float3 normal,\n"
               "    inout float metallicValue, inout float roughnessValue, inout float3 emissionValue,\n"
               "    out ShaderGraphState state)\n{\n"
               "    state.inputs=shaderGraphInputs(worldPosition,worldNormal,viewDirection,time,uv,\n"
               "        color,normal,metallicValue,roughnessValue,emissionValue);\n";
        if (layout.surfaceCount > 0)
        {
            out << "    const ShaderGraphInputs inputs=state.inputs;\n";
            EmitInstructions(out, data, 0, layout.surfaceCount);
            for (const int reg : carried) out << "    state.s" << reg << "=r" << reg << ";\n";
            out << "    shaderGraphApplySurfaceOutputs(" << Register(data.outputs0.x) << ',' << Register(data.outputs0.y)
                << ',' << Register(data.outputs0.z) << ',' << Register(data.outputs0.w) << ','
                << Register(data.outputs1.x) << ',' << Register(data.outputs1.y)
                << ",color,normal,metallicValue,roughnessValue,emissionValue);\n";
        }
        out << "}\n\n";

        out << "void evaluateShaderGraph(ShaderGraphData graph, float3 worldPosition, float3 worldNormal,\n"
               "    float3 viewDirection, float time, float2 uv, inout float4 color, inout float3 normal,\n"
               "    inout float metallicValue, inout float roughnessValue, inout float3 emissionValue)\n{\n"
               "    ShaderGraphState state;\n"
               "    evaluateShaderGraph(graph,worldPosition,worldNormal,viewDirection,time,uv,\n"
               "        color,normal,metallicValue,roughnessValue,emissionValue,state);\n}\n\n";

        out << "float3 shaderGraphVertexOffset(ShaderGraphData graph, float3 position, float3 normal,\n"
               "    float3 viewDirection, float time, float2 uv, float4 color, float metallicValue,\n"
               "    float roughnessValue, float3 emissionValue)\n{\n";
        if (layout.vertexCount == 0) out << "    return float3(0.0);\n";
        else
        {
            out << "    const ShaderGraphInputs inputs=shaderGraphInputs(position,normal,viewDirection,time,uv,\n"
                   "        color,normal,metallicValue,roughnessValue,emissionValue);\n";
            EmitInstructions(out, data, 0, layout.vertexCount);
            out << "    return r" << data.outputs1.z << ".xyz;\n";
        }
        out << "}\n\n";

        out << "float3 evaluateShaderGraphLighting(ShaderGraphData graph, inout ShaderGraphState state,\n"
               "    float3 lightDirection, float3 lightColor, float lightAttenuation, float shadowAttenuation)\n{\n";
        if (layout.lightingOutput < 0) out << "    return float3(0.0);\n";
        else
        {
            out << "    ShaderGraphInputs inputs=state.inputs;\n"
                   "    inputs.lightDirection=lightDirection;\n"
                   "    inputs.lightColor=lightColor;\n"
                   "    inputs.lightAttenuation=lightAttenuation;\n"
                   "    inputs.shadowAttenuation=shadowAttenuation;\n";
            for (const int reg : carried) out << "    const float4 r" << reg << "=state.s" << reg << ";\n";
            EmitInstructions(out, data, layout.surfaceCount, layout.total);
            out << "    return max(r" << layout.lightingOutput << ".xyz,float3(0.0));\n";
        }
        out << "}\n";
        return out.str();
    }
}
