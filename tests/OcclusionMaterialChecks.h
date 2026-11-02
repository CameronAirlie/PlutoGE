#pragma once
#include "PlutoGE/import/MeshImporter.h"
#include "PlutoGE/render/MaterialTextureSlots.h"
#include "PlutoGE/render/Texture.h"
#include <fstream>
#include <stdexcept>

// Material occlusion maps: glTF import of a packed occlusion-roughness-metallic texture (the importer's cache too),
// the slot mapping importers share, and save/reload of the texture, its channel and strength.
inline void CheckOcclusionMaterial(PlutoGE::assets::AssetManager &assets, const std::filesystem::path &root)
{
    using namespace PlutoGE;
    const auto require = [](bool condition, const char *message) { if (!condition) throw std::runtime_error(message); };
    const auto directory = root / "Assets";
    const auto imagePath = directory / "orm.png", modelPath = directory / "orm.gltf";
    constexpr unsigned char png[]{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,1,8,6,0,0,0,244,34,127,138,0,0,0,17,73,68,65,84,120,156,99,96,96,96,248,223,240,159,225,63,0,11,0,3,126,241,241,238,113,0,0,0,0,73,69,78,68,174,66,96,130};
    { std::ofstream file(imagePath, std::ios::binary); file.write(reinterpret_cast<const char *>(png), sizeof(png)); }
    { std::ofstream file(modelPath); file << R"json({"asset":{"version":"2.0"},"buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],"images":[{"uri":"orm.png"}],"textures":[{"source":0}],"materials":[{"pbrMetallicRoughness":{"metallicRoughnessTexture":{"index":0}},"occlusionTexture":{"index":0,"strength":0.7}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})json"; }
    assetimport::MeshImporter importer;
    for (int pass = 0; pass < 2; ++pass)
    {
        const auto imported = importer.ImportMeshSourceAsset(modelPath.string());
        const auto &material = imported.materials.at(imported.submeshes.at(0).materialIndex);
        require(material.occlusionTextureIndex >= 0 && material.occlusionTextureIndex == material.metallicRoughnessTextureIndex,
                "Importer/cache lost the packed occlusion texture");
        require(material.occlusionStrength > .69f && material.occlusionStrength < .71f, "Importer lost occlusion strength");
        require(imported.textures[material.occlusionTextureIndex].colorSpace == assetimport::ImportedTextureColorSpace::Linear,
                "Occlusion texture was not marked linear");
        const auto definition = assetimport::DescribeImportedMaterial(material);
        require(definition.TextureIndex(render::MaterialTextureSlot::Occlusion) == material.occlusionTextureIndex &&
                definition.config.occlusionTextureChannel == render::TextureChannel::Red &&
                definition.TextureIndex(render::MaterialTextureSlot::Roughness) == material.metallicRoughnessTextureIndex &&
                definition.config.roughnessTextureChannel == render::TextureChannel::Green,
                "Imported material did not map packed ORM channels to their slots");
    }

    render::MaterialConfig config;
    config.occlusionTexture = render::Texture::LoadFromFile(imagePath.string().c_str(), render::TextureColorSpace::Linear);
    require(config.occlusionTexture, "Could not load occlusion map");
    config.occlusionTextureChannel = render::TextureChannel::Blue;
    config.occlusionStrength = .4f;
    require(assets.SaveMaterialAsset("project://occlusion.plutomaterial", config), "Could not save occlusion material");
    auto *loaded = assets.LoadMaterialAsset("project://occlusion.plutomaterial");
    require(loaded && loaded->GetConfig().occlusionTexture &&
            loaded->GetConfig().occlusionTextureChannel == render::TextureChannel::Blue &&
            loaded->GetConfig().occlusionStrength == .4f,
            "Occlusion material did not round-trip");
    config.occlusionTexture = nullptr;
    config.occlusionStrength = 1.0f;
    require(assets.SaveMaterialAsset("project://occlusion.plutomaterial", config), "Could not clear occlusion map");
    assets.ReloadMaterialAssets();
    require(!loaded->GetConfig().occlusionTexture && loaded->GetConfig().occlusionStrength == 1.0f,
            "Reload kept a cleared occlusion map");
    require(render::IsMaterialTextureAssetKey("OcclusionTexture") && !render::IsMaterialTextureAssetKey("OcclusionStrength"),
            "Dependency scanning does not recognise the occlusion texture key");
}
