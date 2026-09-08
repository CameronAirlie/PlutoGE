#include "VctVoxelizationShaders.h"
#include "VctCoverage.h"

namespace PlutoGE::render::detail
{
    ShaderSource VctVoxelizationShaderSource()
    {
        ShaderSource voxel;
        voxel.vertexSource = R"(#version 430 core
layout(location=0) in vec3 aPos;layout(location=1) in vec3 aNormal;layout(location=2) in vec2 aUV;
layout(location=5) in mat4 aInstanceModel;
layout(location=14) in ivec4 aJoints;layout(location=15) in vec4 aWeights;
uniform mat4 uModel;uniform vec2 uUVScale;uniform int uUseSkinning,uUseInstancing;uniform mat4 uJointMatrices[128];
out VS { vec3 p; vec3 n; vec2 uv; } v;
void main(){mat4 skin=mat4(1);if(uUseSkinning!=0){float w=aWeights.x+aWeights.y+aWeights.z+aWeights.w;if(w>.0001)skin=uJointMatrices[clamp(aJoints.x,0,127)]*aWeights.x+uJointMatrices[clamp(aJoints.y,0,127)]*aWeights.y+uJointMatrices[clamp(aJoints.z,0,127)]*aWeights.z+uJointMatrices[clamp(aJoints.w,0,127)]*aWeights.w;}
 mat4 model=uUseInstancing!=0?aInstanceModel:uModel;vec4 local=skin*vec4(aPos,1),p=model*local;mat3 model3=mat3(model);vec3 c0=cross(model3[1],model3[2]),c1=cross(model3[2],model3[0]),c2=cross(model3[0],model3[1]);float orientation=dot(model3[0],c0)<0?-1.0:1.0;mat3 normalMatrix=mat3(c0,c1,c2)*orientation;
 v.p=p.xyz;v.n=normalize(normalMatrix*(mat3(skin)*aNormal));v.uv=aUV*uUVScale;gl_Position=p;})";
        voxel.geometrySource = R"(#version 430 core
layout(triangles) in; layout(triangle_strip,max_vertices=4) out;
in VS { vec3 p; vec3 n; vec2 uv; } vin[];
out GS { vec3 p; vec3 n; vec2 uv; vec2 rasterCell; flat vec3 triangleA; flat vec3 triangleB; flat vec3 triangleC; flat vec2 uvA; flat vec2 uvB; flat vec2 uvC; flat vec3 normalA; flat vec3 normalB; flat vec3 normalC; flat int axis; } g;
uniform vec3 uVolumeOrigin,uEmission;uniform float uVolumeSize;uniform int uVoxelResolution;
vec2 projected(vec3 p,int axis){return axis==0?p.zy:axis==1?p.xz:p.xy;}
void main()
{
    vec3 faceNormal=abs(cross(vin[1].p-vin[0].p,vin[2].p-vin[0].p));
    if(max(max(faceNormal.x,faceNormal.y),faceNormal.z)==0.0)return;
    int axis=faceNormal.y>faceNormal.x?(faceNormal.z>faceNormal.y?2:1):(faceNormal.z>faceNormal.x?2:0);
    vec3 positions[3];vec2 projection[3];
    for(int i=0;i<3;++i){positions[i]=(vin[i].p-uVolumeOrigin)*(float(uVoxelResolution)/uVolumeSize);projection[i]=projected(positions[i],axis);}
    bool emissive=any(greaterThan(uEmission,vec3(0)));
    vec2 lo=max(floor(min(projection[0],min(projection[1],projection[2]))),vec2(0));
    vec2 hi=min(ceil(max(projection[0],max(projection[1],projection[2]))),vec2(uVoxelResolution));
    if(emissive && any(lessThanEqual(hi,lo)))return;
    for(int i=0;i<(emissive?4:3);++i)
    {
        int vertex=min(i,2);
        g.rasterCell=emissive?vec2(i%2==0?lo.x:hi.x,i<2?lo.y:hi.y):projection[i];
        gl_Position=vec4(g.rasterCell/float(uVoxelResolution)*2.0-1.0,0,1);
        g.p=vin[vertex].p;g.n=vin[vertex].n;g.uv=vin[vertex].uv;
        g.triangleA=positions[0];g.triangleB=positions[1];g.triangleC=positions[2];
        g.uvA=vin[0].uv;g.uvB=vin[1].uv;g.uvC=vin[2].uv;
        g.normalA=vin[0].n;g.normalB=vin[1].n;g.normalC=vin[2].n;g.axis=axis;
        EmitVertex();
    }
    EndPrimitive();
})";
        voxel.fragmentSource = R"(#version 430 core
layout(r32ui,binding=0) uniform uimage3D uAccumulationR;layout(r32ui,binding=1) uniform uimage3D uAccumulationG;layout(r32ui,binding=2) uniform uimage3D uAccumulationB;layout(r32ui,binding=3) uniform uimage3D uAccumulationCount;layout(r32ui,binding=4) uniform uimage3D uAccumulationOpacity;in GS { vec3 p; vec3 n; vec2 uv; vec2 rasterCell; flat vec3 triangleA; flat vec3 triangleB; flat vec3 triangleC; flat vec2 uvA; flat vec2 uvB; flat vec2 uvC; flat vec3 normalA; flat vec3 normalB; flat vec3 normalC; flat int axis; } g;
uniform vec3 uVolumeOrigin,uEmission,uLightDirection,uLightColor;uniform float uVolumeSize,uLightIntensity;uniform int uHasInjectionLight,uInjectionLightHasShadow;
const int MAX_LOCAL_LIGHTS=7;uniform int uLocalLightCount,uLocalLightType[MAX_LOCAL_LIGHTS];uniform vec3 uLocalLightPosition[MAX_LOCAL_LIGHTS],uLocalLightDirection[MAX_LOCAL_LIGHTS],uLocalLightColor[MAX_LOCAL_LIGHTS];uniform float uLocalLightIntensity[MAX_LOCAL_LIGHTS],uLocalLightRange[MAX_LOCAL_LIGHTS];
uniform vec4 uColor;uniform sampler2D uAlbedoTexture,uMetallicTexture;uniform float uHasAlbedoTexture,uHasMetallicTexture,uMetallicFactor,uAlphaCutoff;uniform int uMetallicTextureChannel,uAlphaMode,uSurfaceType;
uniform sampler2D uShadow0,uShadow1,uShadow2,uShadow3;uniform mat4 uShadowMatrix[4],uViewMatrix;uniform vec3 uShadowOrigin[4];uniform float uShadowSplit[4];uniform int uShadowCascadeCount;
float shadowSample(int c,vec2 uv){if(c==0)return texture(uShadow0,uv).r;if(c==1)return texture(uShadow1,uv).r;if(c==2)return texture(uShadow2,uv).r;return texture(uShadow3,uv).r;}
vec2 shadowTexel(int c){if(c==0)return 1.0/vec2(textureSize(uShadow0,0));if(c==1)return 1.0/vec2(textureSize(uShadow1,0));if(c==2)return 1.0/vec2(textureSize(uShadow2,0));return 1.0/vec2(textureSize(uShadow3,0));}
const uint MAX_VOXEL_SAMPLES=1048575u;
bool claimSample(ivec3 coord){uint count=imageLoad(uAccumulationCount,coord).r;for(;;){if(count>=MAX_VOXEL_SAMPLES)return false;uint observed=imageAtomicCompSwap(uAccumulationCount,coord,count,count+1u);if(observed==count)return true;count=observed;}}
bool projectShadow(int c,vec3 p,out vec3 q){vec4 lp=uShadowMatrix[c]*vec4(p-uShadowOrigin[c],1);q=lp.xyz/max(lp.w,.0001);q=q*.5+.5;return all(greaterThanEqual(q,vec3(0)))&&all(lessThanEqual(q,vec3(1)));}
float visibility(vec3 p,vec3 n,vec3 lightDirection){if(uShadowCascadeCount<=0)return 1;float d=max(-(uViewMatrix*vec4(p,1)).z,0.0);if(d>uShadowSplit[uShadowCascadeCount-1])return 1;int selected=uShadowCascadeCount-1;for(int i=0;i<4;i++){if(i>=uShadowCascadeCount)break;if(d<=uShadowSplit[i]){selected=i;break;}}
 vec3 normal=normalize(n),lightDir=normalize(-lightDirection);float ndl=max(dot(normal,lightDir),0.0);
 // Voxel injection previously tested the exact rasterized surface against the
 // shadow map. Quantization then produced alternating self-shadowed/lit bands
 // on large coplanar walls and floors. Match the visible lighting receiver bias
 // and include a small voxel-relative floor so it remains meaningful at coarse
 // VCT resolutions without jumping through nearby geometry.
 float voxelSize=uVolumeSize/float(imageSize(uAccumulationR).x);
 float normalBias=max(.004*(1.0-ndl),.00075)+voxelSize*.015;
 vec3 receiver=p+normal*normalBias,q;int covered=-1;
 if(projectShadow(selected,receiver,q))covered=selected;else for(int i=0;i<4;i++){if(i>=uShadowCascadeCount)break;if(i!=selected&&projectShadow(i,receiver,q)){covered=i;break;}}if(covered<0)return 1;
 float cascadeScale=clamp(uShadowSplit[covered]/max(uShadowSplit[0],.0001),1.0,8.0);
 float bias=max(.00012+(1.0-ndl)*.00035,.00004)*cascadeScale;
 vec2 texel=shadowTexel(covered);float lit=0.0;
 for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){vec2 o=vec2(x,y)*texel;lit+=q.z-bias<=shadowSample(covered,q.xy+o)?1.0:0.0;}
 return lit/9.0;}
void main(){
 vec3 worldPosition=g.p,worldNormal=g.n;vec2 materialUv=g.uv;
 float areaCoverage=1.0;
 if(any(greaterThan(uEmission,vec3(0)))){
  vec3 barycentric;
  areaCoverage=vctTriangleCoverage(vctProject(g.triangleA,g.axis),vctProject(g.triangleB,g.axis),vctProject(g.triangleC,g.axis),floor(g.rasterCell),barycentric);
  if(areaCoverage<=0.0)discard;
  worldPosition=uVolumeOrigin+(g.triangleA*barycentric.x+g.triangleB*barycentric.y+g.triangleC*barycentric.z)*(uVolumeSize/float(imageSize(uAccumulationR).x));
  worldNormal=g.normalA*barycentric.x+g.normalB*barycentric.y+g.normalC*barycentric.z;
  materialUv=g.uvA*barycentric.x+g.uvB*barycentric.y+g.uvC*barycentric.z;
 }
 vec3 tc=(worldPosition-uVolumeOrigin)/uVolumeSize; if(any(lessThan(tc,vec3(0)))||any(greaterThanEqual(tc,vec3(1))))discard;
 vec4 a=uColor;if(uHasAlbedoTexture>.5)a*=texture(uAlbedoTexture,materialUv);if(uAlphaMode==1&&a.a<uAlphaCutoff)discard;
 float metallic=clamp(uMetallicFactor,0,1);if(uHasMetallicTexture>.5){vec4 packedMetallic=texture(uMetallicTexture,materialUv);metallic*=uMetallicTextureChannel==0?packedMetallic.r:uMetallicTextureChannel==1?packedMetallic.g:uMetallicTextureChannel==2?packedMetallic.b:packedMetallic.a;}
 bool glassSurface=uSurfaceType==1;bool alphaBlend=uAlphaMode==2;float radianceCoverage=areaCoverage*((glassSurface||alphaBlend)?clamp(a.a,0,1):1.0);float opacity=glassSurface?0.0:radianceCoverage;vec3 normal=normalize(worldNormal),directRadiance=vec3(0);if(uHasInjectionLight!=0){vec3 lightDir=normalize(-uLightDirection);float ndl=max(dot(normal,lightDir),0);float shadow=uInjectionLightHasShadow!=0?visibility(worldPosition,worldNormal,uLightDirection):1;directRadiance=uLightColor*uLightIntensity*ndl*shadow;}for(int i=0;i<MAX_LOCAL_LIGHTS;i++){if(i>=uLocalLightCount)break;vec3 toLight=uLocalLightPosition[i]-worldPosition;float distanceToLight=length(toLight),range=max(uLocalLightRange[i],.0001);if(distanceToLight>=range)continue;vec3 lightDir=toLight/max(distanceToLight,.0001);float attenuation=pow(clamp(1.0-distanceToLight/range,0.0,1.0),2.0);if(uLocalLightType[i]==2){float spotEffect=dot(-lightDir,normalize(uLocalLightDirection[i]));attenuation*=smoothstep(.9,.975,spotEffect);}directRadiance+=uLocalLightColor[i]*uLocalLightIntensity[i]*attenuation*max(dot(normal,lightDir),0.0);}vec3 diffuseBounce=uSurfaceType==0?a.rgb*(1-metallic)*directRadiance*(1.0/3.14159265):vec3(0);vec3 r=diffuseBounce+max(uEmission,vec3(0));
 // Keep invalid or extreme material/light values out of the half-float mip chain.
 // RGB is premultiplied by occupancy so partially occupied mip voxels cannot
 // contribute the radiance of a completely filled voxel.
 if(any(isnan(r))||any(isinf(r)))r=vec3(0);r=clamp(r,vec3(0),vec3(16))*radianceCoverage;uvec3 encoded=uvec3(round(r*(4095.0/16.0)));uint encodedOpacity=uint(round(opacity*4095.0));ivec3 coord=clamp(ivec3(tc*imageSize(uAccumulationR)),ivec3(0),imageSize(uAccumulationR)-ivec3(1));if(claimSample(coord)){imageAtomicAdd(uAccumulationR,coord,encoded.r);imageAtomicAdd(uAccumulationG,coord,encoded.g);imageAtomicAdd(uAccumulationB,coord,encoded.b);imageAtomicAdd(uAccumulationOpacity,coord,encodedOpacity);}})";
        voxel.fragmentSource.insert(voxel.fragmentSource.find("void main()"), kVctCoverage);
        return voxel;
    }

    ShaderSource VctResolveShaderSource()
    {
        ShaderSource resolve;
        resolve.computeSource = R"(#version 430 core
layout(local_size_x=4,local_size_y=4,local_size_z=4)in;
layout(r32ui,binding=0)readonly uniform uimage3D uAccumulationR;
layout(r32ui,binding=1)readonly uniform uimage3D uAccumulationG;
layout(r32ui,binding=2)readonly uniform uimage3D uAccumulationB;
layout(r32ui,binding=3)readonly uniform uimage3D uAccumulationCount;
layout(r32ui,binding=4)readonly uniform uimage3D uAccumulationOpacity;
layout(rgba16f,binding=5)writeonly uniform image3D uResolvedVolume;
uniform int uResolution,uDestinationZOffset;
float opacityAt(ivec3 coord,ivec3 size){if(any(lessThan(coord,ivec3(0)))||any(greaterThanEqual(coord,size)))return 0;uint count=imageLoad(uAccumulationCount,coord).r;if(count==0u)return 0;return clamp(float(imageLoad(uAccumulationOpacity,coord).r)/4095.0,0.0,1.0);}
void main(){ivec3 coord=ivec3(gl_GlobalInvocationID),localSize=ivec3(uResolution);if(any(greaterThanEqual(coord,localSize)))return;uint count=imageLoad(uAccumulationCount,coord).r;vec3 radiance=vec3(0);if(count>0u){vec3 sums=vec3(imageLoad(uAccumulationR,coord).r,imageLoad(uAccumulationG,coord).r,imageLoad(uAccumulationB,coord).r);// Match the Slang resolve: fractional coverage must survive normalization.
 radiance=sums*(16.0/max(float(imageLoad(uAccumulationOpacity,coord).r),4095.0));}float opacity=opacityAt(coord,localSize);const ivec3 offsets[6]=ivec3[6](ivec3(1,0,0),ivec3(-1,0,0),ivec3(0,1,0),ivec3(0,-1,0),ivec3(0,0,1),ivec3(0,0,-1));for(int i=0;i<6;i++)opacity=max(opacity,opacityAt(coord+offsets[i],localSize)*.35);imageStore(uResolvedVolume,coord+ivec3(0,0,uDestinationZOffset),vec4(radiance,opacity));})";
        return resolve;
    }
}
