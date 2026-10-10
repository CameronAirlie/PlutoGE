#pragma once
#include "PlutoGE/render/Mesh.h"
#include <memory>

namespace PlutoGE::scene::detail
{
    // Geometry edits own independent buffers; source/native cache meshes stay
    // shared. Copy the live CPU data, including current tangents and bounds input.
    inline std::unique_ptr<render::Mesh> CopyMeshGeometry(const render::Mesh &source)
    {
        render::MeshConfig config;
        config.data=source.GetMeshData();
        config.hasLightmapUvs=source.HasLightmapUvs();
        config.skeleton=source.GetSkeleton();
        config.animationNodes=source.GetAnimationNodes();
        config.animations=source.GetAnimations();
        config.submeshes.reserve(source.GetSubmeshCount());
        for (std::size_t index=0; index<source.GetSubmeshCount(); ++index) config.submeshes.push_back(source.GetSubmesh(index));
        return std::make_unique<render::Mesh>(config);
    }
}
