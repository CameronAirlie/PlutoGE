// Bytecode interpreter for shader graphs. Renderers use it until a graph's
// generated variant (ShaderGraphCodegen.cpp) is available, and on backends
// without runtime specialisation. Instruction semantics live in
// ShaderGraphCommon.slang. The legacy GLSL path inlines that include textually.
#include "ShaderGraphCommon.slang"

// Interpreter state for one surface sample. A Direct Lighting program is
// compiled after the surface instructions and may read their registers, so
// the state outlives the surface stage and each light executes only the
// lighting instructions.
struct ShaderGraphState
{
    float4 registers[64];
    ShaderGraphInputs inputs;
};

// Executes instructions [first, last) into state.registers.
void executeShaderGraph(ShaderGraphData graph, int first, int last, inout ShaderGraphState state)
{
    for(int i=first;i<last;++i)
    {
        int4 op=graph.instructions[i];
        // Every register operand was checked and topologically ordered by the CPU compiler.
        float4 a=float4(0.0), b=float4(0.0), c=float4(0.0), d=float4(0.0);
        if(shaderGraphReadsA(op.x)) a=state.registers[op.y];
        if(shaderGraphReadsB(op.x)) b=state.registers[op.z];
        if(shaderGraphReadsC(op.x)) c=state.registers[op.w];
        if(op.x==18) d=state.registers[int(graph.values[i].x)];
        state.registers[i]=shaderGraphInstruction(op,a,b,c,d,graph.values[i],state.inputs);
    }
}

// Evaluates the surface stage. `state` retains its registers for
// evaluateShaderGraphLighting; the material values passed in are the graph inputs.
void evaluateShaderGraph(ShaderGraphData graph, float3 worldPosition, float3 worldNormal,
                         float3 viewDirection, float time, float2 uv,
                         inout float4 color, inout float3 normal, inout float metallicValue,
                         inout float roughnessValue, inout float3 emissionValue,
                         out ShaderGraphState state)
{
    state.inputs=shaderGraphInputs(worldPosition,worldNormal,viewDirection,time,uv,
        color,normal,metallicValue,roughnessValue,emissionValue);
    int count=shaderGraphSurfaceCount(graph);
    if(count==0) return;
    executeShaderGraph(graph,0,count,state);
    shaderGraphApplySurfaceOutputs(state.registers[graph.outputs0.x],state.registers[graph.outputs0.y],
        state.registers[graph.outputs0.z],state.registers[graph.outputs0.w],
        state.registers[graph.outputs1.x],state.registers[graph.outputs1.y],
        color,normal,metallicValue,roughnessValue,emissionValue);
}

void evaluateShaderGraph(ShaderGraphData graph, float3 worldPosition, float3 worldNormal,
                         float3 viewDirection, float time, float2 uv,
                         inout float4 color, inout float3 normal, inout float metallicValue,
                         inout float roughnessValue, inout float3 emissionValue)
{
    ShaderGraphState state;
    evaluateShaderGraph(graph,worldPosition,worldNormal,viewDirection,time,uv,
        color,normal,metallicValue,roughnessValue,emissionValue,state);
}

float3 shaderGraphVertexOffset(ShaderGraphData graph, float3 position, float3 normal,
    float3 viewDirection, float time, float2 uv, float4 color, float metallicValue,
    float roughnessValue, float3 emissionValue)
{
    if(graph.header.z==0) return float3(0.0);
    ShaderGraphState state;
    state.inputs=shaderGraphInputs(position,normal,viewDirection,time,uv,
        color,normal,metallicValue,roughnessValue,emissionValue);
    executeShaderGraph(graph,0,graph.header.z,state);
    return state.registers[graph.outputs1.z].xyz;
}

// Returns the graph's complete RGB contribution for one light. The graph owns
// N.L, radiance, distance attenuation and shadow response; none are applied twice.
// `state` must come from the surface-stage evaluateShaderGraph of the same sample.
float3 evaluateShaderGraphLighting(ShaderGraphData graph, inout ShaderGraphState state,
    float3 lightDirection, float3 lightColor, float lightAttenuation, float shadowAttenuation)
{
    if(graph.outputs1.w==0) return float3(0.0);
    state.inputs.lightDirection=lightDirection;
    state.inputs.lightColor=lightColor;
    state.inputs.lightAttenuation=lightAttenuation;
    state.inputs.shadowAttenuation=shadowAttenuation;
    executeShaderGraph(graph,shaderGraphSurfaceCount(graph),graph.header.x,state);
    return max(state.registers[(graph.outputs1.w & 255)-1].xyz,float3(0.0));
}
