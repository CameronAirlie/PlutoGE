#pragma once

#include "PlutoGE/render/Mesh.h"

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
        float metallic = 0.0f;
        float roughness = 1.0f;
        int albedoTextureIndex = -1;
        int normalTextureIndex = -1;
        int metallicRoughnessTextureIndex = -1;
        bool flipNormalY = false;
    };

    struct ImportedMeshAsset
    {
        render::Mesh *mesh = nullptr;
        const std::vector<ImportedMaterialData> *materials = nullptr;
        const std::vector<ImportedTextureData> *textures = nullptr;
    };

    struct ImportedMeshSourceAsset
    {
        render::MeshData meshData;
        std::vector<render::Submesh> submeshes;
        std::vector<ImportedMaterialData> materials;
        std::vector<ImportedTextureData> textures;
        bool hasLightmapUvs = false;
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

            ImportedMeshAsset ToImportedMeshAsset() const
            {
                ImportedMeshAsset importedMeshAsset;
                importedMeshAsset.mesh = mesh.get();
                importedMeshAsset.materials = &materials;
                importedMeshAsset.textures = &textures;
                return importedMeshAsset;
            }
        };

        std::unordered_map<std::string, CachedImportedMeshAsset> m_meshCache;
    };
}