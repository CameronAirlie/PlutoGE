#include "PlutoGE/render/Mesh.h"
#include <unordered_map>
#include <stdexcept>

namespace PlutoGE::render
{
    Mesh *Mesh::GetTessellated(unsigned level)
    {
        if(level==0)return this;
        if(level>3)throw std::invalid_argument("Tessellation level must be between zero and three.");
        if(m_tessellated[level-1])return m_tessellated[level-1].get();
        Mesh *parent=GetTessellated(level-1);
        // Keep the highest safe subdivision instead of failing a render frame.
        if(parent->m_meshData.indices.size()>3000000/4)return parent;
        MeshConfig config=parent->m_config;config.data=parent->m_meshData;
        config.data.indices.clear();config.data.indices.reserve(parent->m_meshData.indices.size()*4);
        std::unordered_map<std::uint64_t,unsigned> edges;
        const auto midpoint=[&](unsigned a,unsigned b) {
            const std::uint64_t key=(std::uint64_t(std::min(a,b))<<32)|std::max(a,b);
            if(auto found=edges.find(key);found!=edges.end())return found->second;
            const auto va=config.data.vertices.at(a),vb=config.data.vertices.at(b);
            MeshVertexData v{};
            for(int i=0;i<3;++i){v.position[i]=(va.position[i]+vb.position[i])*.5f;v.normal[i]=(va.normal[i]+vb.normal[i])*.5f;}
            for(int i=0;i<2;++i){v.uv[i]=(va.uv[i]+vb.uv[i])*.5f;v.uv2[i]=(va.uv2[i]+vb.uv2[i])*.5f;}
            for(int i=0;i<4;++i)v.tangent[i]=(va.tangent[i]+vb.tangent[i])*.5f;
            std::unordered_map<int,float> influences;
            for(int i=0;i<4;++i){influences[va.joints[i]]+=va.weights[i]*.5f;influences[vb.joints[i]]+=vb.weights[i]*.5f;}
            std::vector<std::pair<int,float>> weights(influences.begin(),influences.end());
            std::sort(weights.begin(),weights.end(),[](auto a,auto b){return a.second==b.second?a.first<b.first:a.second>b.second;});
            float total=0;for(size_t i=0;i<std::min<size_t>(4,weights.size());++i){v.joints[i]=weights[i].first;v.weights[i]=weights[i].second;total+=v.weights[i];}
            if(total>0)for(auto &weight:v.weights)weight/=total;
            const unsigned index=unsigned(config.data.vertices.size());config.data.vertices.push_back(v);edges[key]=index;return index;
        };
        const auto &indices=parent->m_meshData.indices;
        for(size_t i=0;i+2<indices.size();i+=3){
            const unsigned a=indices[i],b=indices[i+1],c=indices[i+2],ab=midpoint(a,b),bc=midpoint(b,c),ca=midpoint(c,a);
            config.data.indices.insert(config.data.indices.end(),{a,ab,ca,ab,b,bc,ca,bc,c,ab,bc,ca});
        }
        for(auto &submesh:config.submeshes){submesh.indexOffset*=4;submesh.indexCount*=4;for(auto &lod:submesh.lods){lod.indexOffset*=4;lod.indexCount*=4;}}
        m_tessellated[level-1].reset(Mesh::FromConfig(std::move(config)));
        return m_tessellated[level-1].get();
    }
}
