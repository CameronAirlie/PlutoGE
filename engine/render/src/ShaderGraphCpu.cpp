#include "PlutoGE/render/ShaderGraph.h"
#include "PlutoGE/render/NoiseHash.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Texture.h"
#include <algorithm>
#include <cmath>

namespace PlutoGE::render
{
    ShaderGraphSample EvaluateShaderGraph(const ShaderGraphProgram &program, ShaderGraphSample s,
        const ShaderGraphTextureSampler &textures, bool vertexOnly)
    {
        const auto &g=program.data;
        const int count=vertexOnly?g.header.z:g.header.x;
        if(count==0) return s;
        std::array<glm::vec4,64> r{};
        const auto noise=[](glm::vec2 p) {
            const glm::vec2 cell=glm::floor(p), local=glm::fract(p), t=local*local*(3.0f-2.0f*local);
            const auto hash=[](glm::vec2 v){return NoiseHash(v.x, v.y);};
            return glm::mix(glm::mix(hash(cell),hash(cell+glm::vec2(1,0)),t.x),
                glm::mix(hash(cell+glm::vec2(0,1)),hash(cell+glm::vec2(1)),t.x),t.y);
        };
        for(int i=0;i<count;++i)
        {
            const auto op=g.instructions[i];
            const auto a=(op.x>=2 && op.x<=16)||op.x==18?r[op.y]:glm::vec4(0);
            const auto b=((op.x>=2&&op.x<=7)||op.x==9||op.x==10||op.x==12||op.x==18)?r[op.z]:glm::vec4(0);
            const auto c=(op.x==6||op.x==7||op.x==9||op.x==18)?r[op.w]:glm::vec4(0);
            glm::vec4 v(0);
            switch(op.x)
            {
            case 0: case 17: v=g.values[i];break;
            case 1:
                switch(op.y) {
                case 0:v=s.color;break; case 1:v={s.normal,1};break;
                case 2:v=glm::vec4(s.metallic);break;case 3:v=glm::vec4(s.roughness);break;
                case 4:v=glm::vec4(s.color.a);break;case 5:v={s.uv,0,1};break;
                case 6:v={s.emission,1};break;case 7:v=glm::vec4(s.time);break;
                case 8:v={s.worldPosition,1};break;case 9:v={s.worldNormal,1};break;case 10:v={s.viewDirection,1};break;case 11:v={s.screenUV,0,1};break;
                } break;
            case 2:v=a+b;break;case 3:v=a-b;break;case 4:v=a*b;break;
            case 5: for(int j=0;j<4;++j)v[j]=a[j]/std::copysign(std::max(std::abs(b[j]),.0001f),b[j]);break;
            case 6:v=glm::mix(a,b,c);break;case 7:v=glm::clamp(a,glm::min(b,c),glm::max(b,c));break;
            case 8: {float n=0;for(int j=0;j<op.z;++j)n+=a[j]*a[j];v=a/std::max(std::sqrt(n),.000001f);break;}
            case 9:v=glm::vec4(noise(glm::vec2(a)*b.x)*c.x);break;
            case 10:{float n=0;for(int j=0;j<op.w;++j)n+=a[j]*b[j];v=glm::vec4(n);break;}
            case 11:v=glm::sin(a);break;case 12:v=glm::pow(glm::max(a,glm::vec4(.000001f)),b);break;
            case 13:v=1.0f-a;break;case 14:v=textures?textures(op.z,glm::vec2(a)):glm::vec4(1);break;
            case 15:v=glm::vec4(a[op.z]);break;case 16:v={glm::vec3(a),1};break;
            case 18:v={a.x,b.x,c.x,r[int(g.values[i].x)].x};break;
            }
            r[i]=v;
        }
        if(vertexOnly) {s.vertexOffset=glm::vec3(r[g.outputs1.z]);return s;}
        s.color={glm::vec3(r[g.outputs0.x]),glm::clamp(r[g.outputs1.x].x,0.0f,1.0f)};
        const auto normal=glm::vec3(r[g.outputs0.y]);
        if(glm::dot(normal,normal)>.000001f)s.normal=glm::normalize(normal);
        s.metallic=glm::clamp(r[g.outputs0.z].x,0.0f,1.0f);
        s.roughness=glm::clamp(r[g.outputs0.w].x,.04f,1.0f);
        s.emission=glm::max(glm::vec3(r[g.outputs1.y]),glm::vec3(0));
        return s;
    }

    ShaderGraphSample EvaluateMaterialShaderGraph(const MaterialConfig &material, ShaderGraphSample s, bool vertexOnly)
    {
        const auto sample=[&](int slot,glm::vec2 uv) {
            const Texture *textures[]{material.albedoTexture,material.normalTexture,material.metallicTexture,material.roughnessTexture};
            const auto *texture=slot>=0 && slot<4?textures[slot]:slot>=4&&slot<8?material.graphTextures[slot-4]:nullptr;
            if(!texture || texture->GetRgba8Pixels().empty())return slot==1?glm::vec4(.5f,.5f,1,1):glm::vec4(1);
            const auto pixels=texture->GetRgba8Pixels();
            const int w=texture->GetWidth(),h=texture->GetHeight();
            const unsigned mode=slot>=4?material.graphSamplers[slot-4]:0;
            const glm::vec2 p=(mode>=2?glm::clamp(uv,glm::vec2(0),glm::vec2(1)):glm::fract(uv))*glm::vec2(w,h)-.5f, f=glm::fract(p);
            const int x=int(std::floor(p.x)),y=int(std::floor(p.y));
            const auto texel=[&](int x,int y){
                const int at=((mode>=2?std::clamp(y,0,h-1):(y%h+h)%h)*w+(mode>=2?std::clamp(x,0,w-1):(x%w+w)%w))*4;
                glm::vec4 c{float(pixels[at]),float(pixels[at+1]),float(pixels[at+2]),float(pixels[at+3])};c/=255.0f;
                if(slot==0)for(int j=0;j<3;++j)c[j]=c[j]<=.04045f?c[j]/12.92f:std::pow((c[j]+.055f)/1.055f,2.4f);
                return c;
            };
            if(mode&1)return texel(int(std::floor(p.x+.5f)),int(std::floor(p.y+.5f)));
            return glm::mix(glm::mix(texel(x,y),texel(x+1,y),f.x),glm::mix(texel(x,y+1),texel(x+1,y+1),f.x),f.y);
        };
        s.color=material.color;s.metallic=material.metallic;s.roughness=material.roughness;s.emission=material.emission;
        s.normal=s.worldNormal;
        if(!vertexOnly) {
            s.color*=sample(0,s.uv);
            if(material.metallicTexture)s.metallic*=sample(2,s.uv)[int(material.metallicTextureChannel)];
            if(material.roughnessTexture)s.roughness*=sample(3,s.uv)[int(material.roughnessTextureChannel)];
        }
        return material.shaderGraphProgram?EvaluateShaderGraph(*material.shaderGraphProgram,s,sample,vertexOnly):s;
    }
}
