#pragma once
#include "PlutoGE/render/Mesh.h"
#include <memory>
#include <unordered_map>

namespace PlutoGE::scene::detail
{
    // Geometry edits own independent buffers; source/native cache meshes stay
    // shared. Copy the live CPU data, including current tangents and bounds input.
    inline std::unique_ptr<render::Mesh> CopyMeshGeometry(const render::Mesh &source, int firstSubmesh = -1, int submeshCount = 1)
    {
        render::MeshConfig config;
        if (firstSubmesh < 0) config.data=source.GetMeshData();
        config.hasLightmapUvs=source.HasLightmapUvs();
        config.skeleton=source.GetSkeleton();
        config.animationNodes=source.GetAnimationNodes();
        config.animations=source.GetAnimations();
        config.submeshes.reserve(source.GetSubmeshCount());
        for (std::size_t index=0; index<source.GetSubmeshCount(); ++index) config.submeshes.push_back(source.GetSubmesh(index));
        if (firstSubmesh >= 0)
        {
            // Preserve section identities/material slots, but only own the
            // geometry this component renders. Other sections stay empty.
            const auto &data=source.GetMeshData();
            std::unordered_map<uint32_t,uint32_t> vertices;
            for (std::size_t index=0; index<config.submeshes.size(); ++index)
            {
                auto &section=config.submeshes[index];
                const auto offset=section.indexOffset, count=section.indexCount;
                section.indexOffset=static_cast<uint32_t>(config.data.indices.size());
                section.indexCount=0;
                section.lods.clear();
                if (index<static_cast<std::size_t>(firstSubmesh) ||
                    index-static_cast<std::size_t>(firstSubmesh)>=static_cast<std::size_t>(std::max(1,submeshCount))) continue;
                for (std::size_t i=offset; i<static_cast<std::size_t>(offset)+count && i<data.indices.size(); ++i)
                {
                    const auto sourceIndex=data.indices[i];
                    if (sourceIndex>=data.vertices.size()) continue;
                    auto [it,inserted]=vertices.try_emplace(sourceIndex,static_cast<uint32_t>(config.data.vertices.size()));
                    if (inserted) config.data.vertices.push_back(data.vertices[sourceIndex]);
                    config.data.indices.push_back(it->second);
                    ++section.indexCount;
                }
            }
        }
        return std::make_unique<render::Mesh>(config);
    }
}
