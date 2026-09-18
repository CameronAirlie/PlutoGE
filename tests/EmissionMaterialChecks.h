#pragma once
#include "PlutoGE/import/MeshImporter.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/ShaderGraph.h"
#include <array>
#include <fstream>
#include <stdexcept>

inline void CheckEmissionMaterial(PlutoGE::assets::AssetManager &assets, const std::filesystem::path &root)
{
    using namespace PlutoGE;
    const auto require=[](bool condition,const char *message){if(!condition)throw std::runtime_error(message);};
    const auto directory=root/"Assets";
    const auto imagePath=directory/"emission.png", modelPath=directory/"emission.gltf";
    constexpr unsigned char png[]{137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,1,8,6,0,0,0,244,34,127,138,0,0,0,17,73,68,65,84,120,156,99,96,96,96,248,223,240,159,225,63,0,11,0,3,126,241,241,238,113,0,0,0,0,73,69,78,68,174,66,96,130};
    { std::ofstream file(imagePath,std::ios::binary); file.write(reinterpret_cast<const char *>(png),sizeof(png)); }
    { std::ofstream file(modelPath); file << R"json({"asset":{"version":"2.0"},"buffers":[{"byteLength":60,"uri":"data:application/octet-stream;base64,AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAAAABAPwAAAD8AAEA/AAAAPwAAQD8AAAA/"}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},{"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"}],"images":[{"uri":"emission.png"}],"textures":[{"source":0}],"materials":[{"emissiveFactor":[0.5,1,0.25],"emissiveTexture":{"index":0,"texCoord":1},"extensions":{"KHR_materials_emissive_strength":{"emissiveStrength":2}}}],"meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_1":1},"material":0}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})json"; }
    assetimport::MeshImporter importer;
    for (int pass=0;pass<2;++pass) {
        const auto imported=importer.ImportMeshSourceAsset(modelPath.string());
        const auto slot=imported.submeshes.at(0).materialIndex;
        const auto &material=imported.materials.at(slot);
        require(material.emissionTexCoord==1&&material.emissionTextureIndex>=0,"Importer/cache lost emissive texture or UV set");
        require(material.emission==glm::vec3(1,2,.5f),"Importer lost emission strength");
        require(imported.textures[material.emissionTextureIndex].colorSpace==assetimport::ImportedTextureColorSpace::SRGB,
            "Emissive texture was not marked sRGB");
        require(imported.meshData.vertices[0].uv2==std::array<float,2>{.75f,.5f},"Importer lost secondary UVs");
    }
    render::MaterialConfig config;
    config.emission={1,1,1};config.emissionTexCoord=1;
    config.emissionTexture=render::Texture::LoadFromFile(imagePath.string().c_str(),render::TextureColorSpace::SRGB);
    require(config.emissionTexture,"Could not load emission map");
    require(assets.SaveMaterialAsset("project://emission.plutomaterial",config),"Could not save emission material");
    auto *loaded=assets.LoadMaterialAsset("project://emission.plutomaterial");
    require(loaded&&loaded->GetConfig().emissionTexture&&loaded->GetConfig().emissionTexCoord==1,"Emission material did not round-trip");
    render::ShaderGraphSample sample;sample.uv={.25f,.5f};sample.uv2={.75f,.5f};
    auto lit=render::EvaluateMaterialShaderGraph(loaded->GetConfig(),sample);
    require(lit.emission.r>.21f&&lit.emission.r<.22f&&lit.emission.g==1,"CPU emission lost UV1 or sRGB");
    sample.uv2=sample.uv;
    require(render::EvaluateMaterialShaderGraph(loaded->GetConfig(),sample).emission==glm::vec3(0),"CPU black emissive texels glow");
    config.emissionTexCoord=0;
    require(assets.SaveMaterialAsset("project://emission.plutomaterial",config),"Could not update emission material");
    assets.ReloadMaterialAssets();
    require(loaded->GetConfig().emissionTexture&&loaded->GetConfig().emissionTexCoord==0,"Reload lost emission map settings");
    config.emissionChannelMask=true;config.emissionTexCoord=1;
    config.emissionChannels={{{0,0,1,.5f},{1,0,0,.25f},{0,1,0,1}}};
    require(assets.SaveMaterialAsset("project://emission.plutomaterial",config),"Could not save emission masks");
    assets.ReloadMaterialAssets();
    require(loaded->GetConfig().emissionChannelMask&&loaded->GetConfig().emissionChannels==config.emissionChannels,
        "Emission masks did not survive save/reload");
    sample.uv2={.75f,.5f};
    const auto mapped=render::EvaluateMaterialShaderGraph(loaded->GetConfig(),sample).emission;
    require(mapped.r==.25f&&mapped.g==0&&mapped.b>.25f&&mapped.b<.252f,
        "CPU emission masks used sRGB decoding or wrong channel assignments");

}
