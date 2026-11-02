#pragma once
#include "PlutoGE/import/MeshImporter.h"
#include "PlutoGE/render/MaterialTextureSlots.h"
#include "PlutoGE/render/Texture.h"
#include <fstream>
#include <stdexcept>

// Clear coat and sheen materials: glTF KHR_materials_clearcoat / KHR_materials_sheen import (the importer's cache
// too), the slots and channels importers map them to, and save/reload of their factors and textures.
inline void CheckLayerMaterial(PlutoGE::assets::AssetManager &assets, const std::filesystem::path &root)
{
    using namespace PlutoGE;
    const auto require = [](bool condition, const char *message) { if (!condition) throw std::runtime_error(message); };
    const auto directory = root / "Assets";
    const auto imagePath = directory / "layers.png", modelPath = directory / "layers.gltf";
    constexpr unsigned char png[]{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,1,8,6,0,0,0,244,34,127,138,0,0,0,17,73,68,65,84,120,156,99,96,96,96,248,223,240,159,225,63,0,11,0,3,126,241,241,238,113,0,0,0,0,73,69,78,68,174,66,96,130};
    { std::ofstream file(imagePath, std::ios::binary); file.write(reinterpret_cast<const char *>(png), sizeof(png)); }
    { std::ofstream file(modelPath); file << R"json({"asset":{"version":"2.0"},"extensionsUsed":["KHR_materials_clearcoat","KHR_materials_sheen"],"buffers":[{"byteLength":36,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]}],"images":[{"uri":"layers.png"}],"textures":[{"source":0}],"materials":[{"extensions":{"KHR_materials_clearcoat":{"clearcoatFactor":0.8,"clearcoatRoughnessFactor":0.2,"clearcoatTexture":{"index":0},"clearcoatRoughnessTexture":{"index":0}},"KHR_materials_sheen":{"sheenColorFactor":[0.5,0.4,0.3],"sheenRoughnessFactor":0.6,"sheenColorTexture":{"index":0},"sheenRoughnessTexture":{"index":0}}}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"material":0}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})json"; }
    assetimport::MeshImporter importer;
    const auto near = [](float a, float b) { return a > b - 0.001f && a < b + 0.001f; };
    for (int pass = 0; pass < 2; ++pass)
    {
        const auto imported = importer.ImportMeshSourceAsset(modelPath.string());
        const auto &material = imported.materials.at(imported.submeshes.at(0).materialIndex);
        require(near(material.clearCoat, .8f) && near(material.clearCoatRoughness, .2f) &&
                material.clearCoatTextureIndex >= 0 && material.clearCoatRoughnessTextureIndex == material.clearCoatTextureIndex,
                "Importer/cache lost KHR_materials_clearcoat");
        require(near(material.sheenColor.r, .5f) && near(material.sheenColor.b, .3f) && near(material.sheenRoughness, .6f) &&
                material.sheenColorTextureIndex >= 0 && material.sheenRoughnessTextureIndex >= 0,
                "Importer/cache lost KHR_materials_sheen");
        require(imported.textures[material.sheenColorTextureIndex].colorSpace == assetimport::ImportedTextureColorSpace::SRGB &&
                imported.textures[material.sheenRoughnessTextureIndex].colorSpace == assetimport::ImportedTextureColorSpace::Linear &&
                imported.textures[material.clearCoatTextureIndex].colorSpace == assetimport::ImportedTextureColorSpace::Linear,
                "Layer textures were imported in the wrong colour space");
        const auto definition = assetimport::DescribeImportedMaterial(material);
        require(definition.TextureIndex(render::MaterialTextureSlot::ClearCoat) == material.clearCoatTextureIndex &&
                definition.config.clearCoatTextureChannel == render::TextureChannel::Red &&
                definition.config.clearCoatRoughnessTextureChannel == render::TextureChannel::Green &&
                definition.TextureIndex(render::MaterialTextureSlot::SheenColor) == material.sheenColorTextureIndex &&
                definition.config.sheenRoughnessTextureChannel == render::TextureChannel::Alpha &&
                near(definition.config.clearCoat, .8f) && near(definition.config.sheenRoughness, .6f),
                "Imported layers did not map to their slots, channels and factors");
    }

    render::MaterialConfig config;
    config.clearCoat = .7f;
    config.clearCoatRoughness = .15f;
    config.sheenColor = {.2f, .3f, .4f};
    config.sheenRoughness = .35f;
    config.clearCoatTexture = render::Texture::LoadFromFile(imagePath.string().c_str(), render::TextureColorSpace::Linear);
    config.sheenColorTexture = render::Texture::LoadFromFile(imagePath.string().c_str(), render::TextureColorSpace::SRGB);
    require(config.clearCoatTexture && config.sheenColorTexture, "Could not load layer maps");
    config.clearCoatTextureChannel = render::TextureChannel::Alpha;
    require(assets.SaveMaterialAsset("project://layers.plutomaterial", config), "Could not save layered material");
    auto *loaded = assets.LoadMaterialAsset("project://layers.plutomaterial");
    require(loaded, "Could not load layered material");
    const auto &reloaded = loaded->GetConfig();
    require(reloaded.clearCoat == .7f && reloaded.clearCoatRoughness == .15f && reloaded.sheenColor == config.sheenColor &&
            reloaded.sheenRoughness == .35f, "Layer factors did not round-trip");
    require(reloaded.clearCoatTexture && reloaded.clearCoatTextureChannel == render::TextureChannel::Alpha &&
            reloaded.sheenColorTexture && !reloaded.clearCoatRoughnessTexture && !reloaded.sheenRoughnessTexture,
            "Layer textures did not round-trip");
}
