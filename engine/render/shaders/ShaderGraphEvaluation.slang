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
float shaderGraphNoise(float2 p)
{
    float2 cell=floor(p), local=frac(p);
    float2 t=local*local*(3.0-2.0*local);
    float a=frac(sin(dot(cell,float2(127.1,311.7)))*43758.5453123);
    float b=frac(sin(dot(cell+float2(1,0),float2(127.1,311.7)))*43758.5453123);
    float c=frac(sin(dot(cell+float2(0,1),float2(127.1,311.7)))*43758.5453123);
    float d=frac(sin(dot(cell+float2(1,1),float2(127.1,311.7)))*43758.5453123);
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
                         inout float roughnessValue, inout float3 emissionValue, bool vertexOnly)
{
    int count = vertexOnly ? graph.header.z : graph.header.x;
    if(count==0) return float3(0.0);
    float4 r[64];
    for(int i=0;i<count;++i)
    {
        int4 op=graph.instructions[i];
        // Input/constant instructions do not read registers. Every other operand
        // was checked and topologically ordered by the CPU compiler.
        float4 a=float4(0.0), b=float4(0.0), c=float4(0.0), v=float4(0.0);
        if((op.x>=2 && op.x<=16) || op.x==18) a=r[op.y];
        if((op.x>=2 && op.x<=7) || op.x==9 || op.x==10 || op.x==12 || op.x==18) b=r[op.z];
        if(op.x==6 || op.x==7 || op.x==9 || op.x==18) c=r[op.w];
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
        r[i]=v;
    }
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
        color,normal,metallicValue,roughnessValue,emissionValue,false);
}

float3 shaderGraphVertexOffset(ShaderGraphData graph, float3 position, float3 normal,
    float3 viewDirection, float time, float2 uv, float4 color, float metallicValue,
    float roughnessValue, float3 emissionValue)
{
    return evaluateShaderGraphProgram(graph,position,normal,viewDirection,time,uv,
        color,normal,metallicValue,roughnessValue,emissionValue,true);
}
