float2 shaderGraphScreenUV(float3 worldPosition);
// This evaluator is also translated to GLSL for the legacy geometry path.
// Keep this file free of backend-specific resource declarations.
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
float3 evaluateShaderGraphProgram(ShaderGraphData graph, float3 worldPosition, float3 worldNormal,
                         float3 viewDirection, float time, float2 uv,
                         inout float4 color, inout float3 normal, inout float metallicValue,
                         inout float roughnessValue, inout float3 emissionValue, bool vertexOnly,
                         bool lightingOnly, float3 lightDirection, float3 lightColor,
                         float lightAttenuation, float shadowAttenuation)
{
    int count = vertexOnly ? graph.header.z : graph.header.x;
    if(!vertexOnly && !lightingOnly && graph.outputs1.w!=0) count=graph.outputs1.w>>8;
    if(count==0) return float3(0.0);
    float4 r[64];
    for(int i=0;i<count;++i)
    {
        int4 op=graph.instructions[i];
        // Input/constant instructions do not read registers. Every other operand
        // was checked and topologically ordered by the CPU compiler.
        float4 a=float4(0.0), b=float4(0.0), c=float4(0.0), v=float4(0.0);
        if((op.x>=2 && op.x<=16) || op.x==18 || op.x==19 || op.x==20 || op.x==21) a=r[op.y];
        if((op.x>=2 && op.x<=7) || op.x==9 || op.x==10 || op.x==12 || op.x==18 || op.x==19 || op.x==21) b=r[op.z];
        if(op.x==6 || op.x==7 || op.x==9 || op.x==18 || op.x==21) c=r[op.w];
        if(op.x==0 || op.x==17) v=graph.values[i];
        else if(op.x==1)
        {
            if(op.y==0) v=color;
            else if(op.y==1) v=float4(normal,1.0);
            else if(op.y==2) v=float4(metallicValue);
            else if(op.y==3) v=float4(roughnessValue);
            else if(op.y==4) v=float4(color.a);
            else if(op.y==5) v=float4(uv,0.0,1.0);
            else if(op.y==6) v=float4(emissionValue,1.0);
            else if(op.y==7) v=float4(time);
            else if(op.y==8) v=float4(worldPosition,1.0);
            else if(op.y==9) v=float4(worldNormal,1.0);
            else if(op.y==10) v=float4(viewDirection,1.0);
            else if(op.y==12)v=float4(lightDirection,1.0);
            else if(op.y==13)v=float4(lightColor,1.0);
            else if(op.y==14)v=float4(lightAttenuation);
            else if(op.y==15)v=float4(shadowAttenuation);
            else if(op.y==11)v=float4(shaderGraphScreenUV(worldPosition),0.0,1.0);
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
        else if(op.x==18) v=float4(a.x,b.x,c.x,r[int(graph.values[i].x)].x);
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
        r[i]=v;
    }
    if(lightingOnly) return max(r[(graph.outputs1.w & 255)-1].xyz,float3(0.0));
    if(vertexOnly) return r[graph.outputs1.z].xyz;
    color=float4(r[graph.outputs0.x].xyz,clamp(r[graph.outputs1.x].x,0.0,1.0));
    float3 finalNormal=r[graph.outputs0.y].xyz;
    if(dot(finalNormal,finalNormal)>0.000001) normal=normalize(finalNormal);
    metallicValue=clamp(r[graph.outputs0.z].x,0.0,1.0);
    roughnessValue=clamp(r[graph.outputs0.w].x,0.04,1.0);
    emissionValue=max(r[graph.outputs1.y].xyz,float3(0.0));
    return float3(0.0);
}

void evaluateShaderGraph(ShaderGraphData graph, float3 worldPosition, float3 worldNormal,
                         float3 viewDirection, float time, float2 uv,
                         inout float4 color, inout float3 normal, inout float metallicValue,
                         inout float roughnessValue, inout float3 emissionValue)
{
    evaluateShaderGraphProgram(graph,worldPosition,worldNormal,viewDirection,time,uv,
        color,normal,metallicValue,roughnessValue,emissionValue,false,false,float3(0.0),float3(0.0),0.0,1.0);
}

float3 shaderGraphVertexOffset(ShaderGraphData graph, float3 position, float3 normal,
    float3 viewDirection, float time, float2 uv, float4 color, float metallicValue,
    float roughnessValue, float3 emissionValue)
{
    return evaluateShaderGraphProgram(graph,position,normal,viewDirection,time,uv,
        color,normal,metallicValue,roughnessValue,emissionValue,true,false,float3(0.0),float3(0.0),0.0,1.0);
}

// Returns the graph's complete RGB contribution for one light. The graph owns
// N.L, radiance, distance attenuation and shadow response; none are applied twice.
float3 evaluateShaderGraphLighting(ShaderGraphData graph, float3 position, float3 worldNormal,
    float3 viewDirection, float time, float2 uv, float4 color, float3 normal,
    float metallicValue, float roughnessValue, float3 emissionValue,
    float3 lightDirection, float3 lightColor, float lightAttenuation, float shadowAttenuation)
{
    if(graph.outputs1.w==0) return float3(0.0);
    return evaluateShaderGraphProgram(graph,position,worldNormal,viewDirection,time,uv,
        color,normal,metallicValue,roughnessValue,emissionValue,false,true,
        lightDirection,lightColor,lightAttenuation,shadowAttenuation);
}
