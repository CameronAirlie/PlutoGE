#pragma once

#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/Material.h"

#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace PlutoGE::assetimport
{
    struct ImportedTextureData
    {
        std::string cacheKey;
        std::string sourcePath;
        int width = 0;
        int height = 0;
        int channels = 0;
        std::vector<unsigned char> pixels;
    };

    struct ImportedMaterialData
    {
        glm::vec4 color{1.0f};
        render::MaterialSurfaceType surfaceType = render::MaterialSurfaceType::Standard;
        render::AlphaMode alphaMode = render::AlphaMode::Opaque;
        float alphaCutoff = 0.5f;
        bool castsShadow = true;
        float metallic = 0.0f;
        float roughness = 1.0f;
        float transmission = 0.0f;
        float ior = 1.45f;
        float thickness = 0.01f;
        glm::vec3 attenuationColor{1.0f, 1.0f, 1.0f};
        float attenuationDistance = 1.0f;
        int albedoTextureIndex = -1;
        int normalTextureIndex = -1;
        int metallicRoughnessTextureIndex = -1;
        bool metallicRoughnessTextureHasMetallicChannel = true;
        bool flipNormalY = false;
    };

    struct ImportedMeshAsset
    {
        render::Mesh *mesh = nullptr;
        const std::vector<ImportedMaterialData> *materials = nullptr;
        std::vector<ImportedTextureData> *textures = nullptr;
        const std::vector<render::AnimationClip> *animations = nullptr;
    };

    struct ImportedMeshSourceAsset
    {
        render::MeshData meshData;
        std::vector<render::Submesh> submeshes;
        std::vector<ImportedMaterialData> materials;
        std::vector<ImportedTextureData> textures;
        render::Skeleton skeleton;
        std::vector<render::AnimationNode> animationNodes;
        std::vector<render::AnimationClip> animations;
        bool hasLightmapUvs = false;
        bool requiresMissingNormalFallback = false;
    };

    class MeshImporter
    {
    public:
        MeshImporter() = default;
        ~MeshImporter() = default;

        ImportedMeshSourceAsset ImportMeshSourceAsset(const std::string &filePath) const;
        ImportedMeshAsset FinalizeImportedMeshAsset(const std::string &filePath, ImportedMeshSourceAsset meshSourceAsset);
        ImportedMeshAsset ImportMeshAsset(const std::string &filePath);
        render::Mesh *ImportMesh(const std::string &filePath);
        render::MeshData ImportMeshData(const std::string &filePath) const;
        bool SupportsFileType(std::string_view filePath) const;

    private:
        struct CachedImportedMeshAsset
        {
            std::unique_ptr<render::Mesh> mesh;
            std::vector<ImportedMaterialData> materials;
            std::vector<ImportedTextureData> textures;
            std::vector<render::AnimationClip> animations;

            ImportedMeshAsset ToImportedMeshAsset() const
            {
                ImportedMeshAsset importedMeshAsset;
                importedMeshAsset.mesh = mesh.get();
                importedMeshAsset.materials = &materials;
                importedMeshAsset.textures = const_cast<std::vector<ImportedTextureData> *>(&textures);
                importedMeshAsset.animations = &animations;
                return importedMeshAsset;
            }
        };

        std::unordered_map<std::string, CachedImportedMeshAsset> m_meshCache;
    };
}
