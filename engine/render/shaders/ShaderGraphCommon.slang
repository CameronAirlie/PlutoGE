// Shader-graph bytecode layout and instruction semantics. Shared by the
// bytecode interpreter (ShaderGraphEvaluation.slang), per-graph generated code
// (ShaderGraphCodegen.cpp) and the legacy GLSL path, which translates this file
// textually. Keep it free of backend resources and GLSL-incompatible syntax.
float2 shaderGraphScreenUV(float3 worldPosition);
struct ShaderGraphData
{
    int4 header;
    int4 outputs0;
    int4 outputs1;
    int4 instructions[64];
    float4 values[64];
};
float shaderGraphNoiseHash(float2 cell)
{
    // Match waterNoiseHash and NoiseHash.h; floating sine hashes can develop
    // cell seams when the backend contracts/reassociates corner arithmetic.
    uint h = uint(int(cell.x)) * 0x8da6b343u ^ uint(int(cell.y)) * 0xd8163841u;
    h ^= h >> 16; h *= 0x7feb352du;
    h ^= h >> 15; h *= 0x846ca68bu;
    h ^= h >> 16;
    return float(h >> 8) * (1.0 / 16777216.0);
}
float shaderGraphNoise(float2 p)
{
    float2 cell=floor(p), local=frac(p);
    float2 t=local*local*(3.0-2.0*local);
    float a=shaderGraphNoiseHash(cell);
    float b=shaderGraphNoiseHash(cell+float2(1,0));
    float c=shaderGraphNoiseHash(cell+float2(0,1));
    float d=shaderGraphNoiseHash(cell+float2(1,1));
    return lerp(lerp(a,b,t.x),lerp(c,d,t.x),t.y);
}
float4 shaderGraphSafeDivisor(float4 v)
{
    return float4(v.x<0.0?-max(abs(v.x),0.0001):max(v.x,0.0001),
                  v.y<0.0?-max(abs(v.y),0.0001):max(v.y,0.0001),
                  v.z<0.0?-max(abs(v.z),0.0001):max(v.z,0.0001),
                  v.w<0.0?-max(abs(v.w),0.0001):max(v.w,0.0001));
}

// Per-sample values read by Input instructions (opcode 1). The material
// values are the graph inputs, before any graph output replaced them.
struct ShaderGraphInputs
{
    float3 worldPosition;
    float3 worldNormal;
    float3 viewDirection;
    float time;
    float2 uv;
    float4 color;
    float3 normal;
    float metallic;
    float roughness;
    float3 emission;
    float3 lightDirection;
    float3 lightColor;
    float lightAttenuation;
    float shadowAttenuation;
};

ShaderGraphInputs shaderGraphInputs(float3 worldPosition, float3 worldNormal, float3 viewDirection,
                                    float time, float2 uv, float4 color, float3 normal,
                                    float metallicValue, float roughnessValue, float3 emissionValue)
{
    ShaderGraphInputs inputs;
    inputs.worldPosition=worldPosition;
    inputs.worldNormal=worldNormal;
    inputs.viewDirection=viewDirection;
    inputs.time=time;
    inputs.uv=uv;
    inputs.color=color;
    inputs.normal=normal;
    inputs.metallic=metallicValue;
    inputs.roughness=roughnessValue;
    inputs.emission=emissionValue;
    inputs.lightDirection=float3(0.0);
    inputs.lightColor=float3(0.0);
    inputs.lightAttenuation=0.0;
    inputs.shadowAttenuation=1.0;
    return inputs;
}

// Register operands read by each opcode: instruction.y (A), .z (B), .w (C).
// Opcode 18 also reads the register stored in values[i].x (D). Other fields
// are immediates. ShaderGraphCodegen.cpp mirrors this table.
bool shaderGraphReadsA(int op) { return (op>=2 && op<=16) || op==18 || op==19 || op==20 || op==21; }
bool shaderGraphReadsB(int op) { return (op>=2 && op<=7) || op==9 || op==10 || op==12 || op==18 || op==19 || op==21; }
bool shaderGraphReadsC(int op) { return op==6 || op==7 || op==9 || op==18 || op==21; }

// Result of one instruction from its operand registers. `value` is the
// instruction's values[] entry: a constant or material parameter.
float4 shaderGraphInstruction(int4 op, float4 a, float4 b, float4 c, float4 d, float4 value,
                              ShaderGraphInputs inputs)
{
    float4 v=float4(0.0);
    if(op.x==0 || op.x==17) v=value;
    else if(op.x==1)
    {
        if(op.y==0) v=inputs.color;
        else if(op.y==1) v=float4(inputs.normal,1.0);
        else if(op.y==2) v=float4(inputs.metallic);
        else if(op.y==3) v=float4(inputs.roughness);
        else if(op.y==4) v=float4(inputs.color.a);
        else if(op.y==5) v=float4(inputs.uv,0.0,1.0);
        else if(op.y==6) v=float4(inputs.emission,1.0);
        else if(op.y==7) v=float4(inputs.time);
        else if(op.y==8) v=float4(inputs.worldPosition,1.0);
        else if(op.y==9) v=float4(inputs.worldNormal,1.0);
        else if(op.y==10) v=float4(inputs.viewDirection,1.0);
        else if(op.y==12)v=float4(inputs.lightDirection,1.0);
        else if(op.y==13)v=float4(inputs.lightColor,1.0);
        else if(op.y==14)v=float4(inputs.lightAttenuation);
        else if(op.y==15)v=float4(inputs.shadowAttenuation);
        else if(op.y==11)v=float4(shaderGraphScreenUV(inputs.worldPosition),0.0,1.0);
    }
    else if(op.x==2) v=a+b;
    else if(op.x==3) v=a-b;
    else if(op.x==4) v=a*b;
    else if(op.x==5) v=a/shaderGraphSafeDivisor(b);
    else if(op.x==6) v=lerp(a,b,c);
    else if(op.x==7) v=clamp(a,min(b,c),max(b,c));
    else if(op.x==8)
    {
        float4 mask=float4(1.0,op.z>=2?1.0:0.0,op.z>=3?1.0:0.0,op.z>=4?1.0:0.0);
        v=a/max(length(a*mask),0.000001);
    }
    else if(op.x==9) v=float4(shaderGraphNoise(a.xy*b.x)*c.x);
    else if(op.x==10)
    {
        float4 mask=float4(1.0,op.w>=2?1.0:0.0,op.w>=3?1.0:0.0,op.w>=4?1.0:0.0);
        v=float4(dot(a*mask,b));
    }
    else if(op.x==11) v=sin(a);
    else if(op.x==12) v=pow(max(a,float4(0.000001)),b);
    else if(op.x==13) v=1.0-a;
    else if(op.x==14) v=shaderGraphTexture(op.z,a.xy);
    else if(op.x==15) v=float4(a[op.z]);
    else if(op.x==16) v=float4(a.xyz,1.0);
    else if(op.x==18) v=float4(a.x,b.x,c.x,d.x);
    else if(op.x==19) v=step(a,b);
    else if(op.x==20) v=floor(a);
    else if(op.x==21)
    {
        // Ordered edges; coincident edges behave like Step on every backend.
        float4 low=min(a,b), high=max(a,b);
        float4 t=clamp((c-low)/max(high-low,float4(0.000001)),float4(0.0),float4(1.0));
        v=t*t*(3.0-2.0*t);
        for(int j=0;j<4;++j) if(high[j]==low[j]) v[j]=step(low[j],c[j]);
    }
    return v;
}

// Applies the surface output registers to the material values.
void shaderGraphApplySurfaceOutputs(float4 albedo, float4 normalOutput, float4 metallicOutput,
                                    float4 roughnessOutput, float4 opacity, float4 emissionOutput,
                                    inout float4 color, inout float3 normal, inout float metallicValue,
                                    inout float roughnessValue, inout float3 emissionValue)
{
    color=float4(albedo.xyz,clamp(opacity.x,0.0,1.0));
    float3 finalNormal=normalOutput.xyz;
    if(dot(finalNormal,finalNormal)>0.000001) normal=normalize(finalNormal);
    metallicValue=clamp(metallicOutput.x,0.0,1.0);
    roughnessValue=clamp(roughnessOutput.x,0.04,1.0);
    emissionValue=max(emissionOutput.xyz,float3(0.0));
}

// Direct Lighting programs record the surface instruction count in bits 8+.
int shaderGraphSurfaceCount(ShaderGraphData graph)
{
    return graph.outputs1.w!=0 ? graph.outputs1.w>>8 : graph.header.x;
}
