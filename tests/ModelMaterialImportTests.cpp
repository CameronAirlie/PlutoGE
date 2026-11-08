// Material import policy: UV set selection, authored names, slot separation and legacy slot identity.
#include "PlutoGE/asset_import/ModelObjectNaming.h"
#include "PlutoGE/import/MaterialUvSets.h"
#include "PlutoGE/import/MeshImporter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace PlutoGE::assetimport;

    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    std::string Base64(const std::vector<unsigned char> &bytes)
    {
        static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string encoded;
        for (std::size_t index = 0; index < bytes.size(); index += 3)
        {
            const unsigned value = (bytes[index] << 16) | ((index + 1 < bytes.size() ? bytes[index + 1] : 0) << 8) |
                                   (index + 2 < bytes.size() ? bytes[index + 2] : 0);
            encoded += kAlphabet[(value >> 18) & 63];
            encoded += kAlphabet[(value >> 12) & 63];
            encoded += index + 1 < bytes.size() ? kAlphabet[(value >> 6) & 63] : '=';
            encoded += index + 2 < bytes.size() ? kAlphabet[value & 63] : '=';
        }
        return encoded;
    }

    void TestUvSetPolicy()
    {
        const std::array<SourceTextureUvUse, 3> eyes{{{false, 0}, {true, 4}, {true, 2}}};
        Require(SelectPrimaryUvSet(eyes) == 4, "Primary UV set must follow the first used texture");
        Require(SelectPrimaryUvSet(std::array<SourceTextureUvUse, 1>{{{false, 3}}}) == 0, "Untextured material must use set 0");
        Require(ResolveEngineUvStream(4, 4) == EngineUvStream::Primary, "Primary set must map to the primary stream");
        Require(ResolveEngineUvStream(1, 4) == EngineUvStream::Secondary, "Set 1 must map to the secondary stream");
        Require(ResolveEngineUvStream(2, 4) == EngineUvStream::Unavailable, "Unrepresentable set must be reported");
    }

    void TestNaming()
    {
        Require(SanitizeModelObjectName(" a/b:c*. ") == "a_b_c_", "Invalid file name characters must be replaced");
        Require(SanitizeModelObjectName("CON") == "CON_" && SanitizeModelObjectName("  ..").empty(), "Reserved or empty names must be handled");
        ModelObjectNameAllocator names;
        Require(names.Allocate("albedo", "Texture 0") == "albedo", "Authored name must be kept");
        Require(names.Allocate("Albedo", "Texture 1") == "Albedo_1", "Case-insensitive duplicates must be suffixed");
        Require(names.Allocate("", "Texture 2") == "Texture 2", "Fallback must be used without an authored name");
    }

    void TestLegacySlots()
    {
        ImportedMaterialData skin{.name = "skin", .roughness = 0.5f};
        ImportedMaterialData pupil{.name = "pupil"};
        ImportedMaterialData cornea{.name = "cornea"};
        ImportedMaterialData teeth{.name = "teeth", .roughness = 0.2f};
        const auto slots = LegacyModelMaterialSlots({skin, pupil, cornea, teeth});
        Require(slots == std::vector<std::size_t>{0, 1, 1, 2}, "Legacy slots must merge equal parameters regardless of name");
        Require(ModelMaterialSourceKey("pupil", true, 1) == "material/name/pupil" &&
                ModelMaterialSourceKey("M_Model_3", false, 3) == LegacyModelMaterialSourceKey(3),
                "Material source keys changed");
    }

    // Pupil and cornea export identical parameters; the iris samples its texture through TEXCOORD_2.
    void TestGltfImport(const std::filesystem::path &directory)
    {
        std::vector<float> positions{0, 0, 0, 1, 0, 0, 0, 1, 0};
        std::vector<float> uv0{0.1f, 0.1f, 0.2f, 0.1f, 0.1f, 0.2f};
        std::vector<float> uv2{0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
        std::vector<unsigned char> buffer;
        const auto append = [&](const std::vector<float> &values)
        {
            const auto offset = buffer.size();
            buffer.resize(offset + values.size() * sizeof(float));
            std::memcpy(buffer.data() + offset, values.data(), values.size() * sizeof(float));
        };
        append(positions);
        append(uv0);
        append(uv2);
        const std::string png = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=";
        const std::string attributes = R"({"POSITION":0,"TEXCOORD_0":1,"TEXCOORD_2":2})";
        const auto primitive = [&](int material)
        {
            return R"({"attributes":)" + attributes + R"(,"material":)" + std::to_string(material) + "}";
        };
        const std::string gltf = R"({"asset":{"version":"2.0"},"scene":0,"scenes":[{"nodes":[0]}],"nodes":[{"mesh":0}],)"
            R"("meshes":[{"primitives":[)" + primitive(0) + "," + primitive(1) + "," + primitive(2) + R"(]}],)"
            R"("materials":[{"name":"MBlab_pupil","doubleSided":true},{"name":"charmorph_cornea","doubleSided":true},)"
            R"({"name":"charmorph_iris","pbrMetallicRoughness":{"baseColorTexture":{"index":0,"texCoord":2}}}],)"
            R"("images":[{"name":"iris_color","uri":"data:image/png;base64,)" + png + R"("}],"textures":[{"source":0}],)"
            R"("buffers":[{"byteLength":)" + std::to_string(buffer.size()) + R"(,"uri":"data:application/octet-stream;base64,)" + Base64(buffer) + R"("}],)"
            R"("bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":24},{"buffer":0,"byteOffset":60,"byteLength":24}],)"
            R"("accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},)"
            R"({"bufferView":1,"componentType":5126,"count":3,"type":"VEC2"},{"bufferView":2,"componentType":5126,"count":3,"type":"VEC2"}]})";
        const auto path = directory / "Materials.gltf";
        std::ofstream(path, std::ios::binary) << gltf;

        const auto imported = MeshImporter{}.ImportMeshSourceAsset(path.string(), {}, MeshSourceCachePolicy::Bypass);
        const auto slotOf = [&](const std::string &name)
        {
            const auto found = std::find_if(imported.materials.begin(), imported.materials.end(),
                                            [&](const ImportedMaterialData &material) { return material.name == name; });
            Require(found != imported.materials.end(), "Material '" + name + "' was not imported by name");
            return static_cast<std::uint32_t>(std::distance(imported.materials.begin(), found));
        };
        const auto pupil = slotOf("MBlab_pupil");
        const auto cornea = slotOf("charmorph_cornea");
        const auto iris = slotOf("charmorph_iris");
        Require(pupil != cornea, "Differently named materials with equal parameters were merged");
        Require(imported.textures.size() == 1 && imported.textures[0].name == "iris_color", "Texture name was not imported");

        const auto maximumUv = [&](std::uint32_t material)
        {
            const auto submesh = std::find_if(imported.submeshes.begin(), imported.submeshes.end(),
                                              [&](const auto &candidate) { return candidate.materialIndex == material; });
            Require(submesh != imported.submeshes.end(), "Submesh missing for material slot");
            std::array<float, 2> maximum{0.0f, 0.0f};
            for (std::uint32_t index = 0; index < submesh->indexCount; ++index)
            {
                const auto &uv = imported.meshData.vertices[imported.meshData.indices[submesh->indexOffset + index]].uv;
                maximum = {std::max(maximum[0], uv[0]), std::max(maximum[1], uv[1])};
            }
            return maximum;
        };
        const auto irisUv = maximumUv(iris);
        const auto pupilUv = maximumUv(pupil);
        Require(std::abs(irisUv[0] - 1.0f) < 1e-5f && std::abs(irisUv[1] - 1.0f) < 1e-5f,
                "Material sampled through TEXCOORD_2 did not get TEXCOORD_2 as its primary UVs");
        Require(std::abs(pupilUv[0] - 0.2f) < 1e-5f && std::abs(pupilUv[1] - 0.2f) < 1e-5f,
                "Untextured material lost its TEXCOORD_0 UVs");
    }
}

int main()
{
    const auto directory = std::filesystem::temp_directory_path() / "PlutoGE-model-material-import";
    try
    {
        std::filesystem::create_directories(directory);
        TestUvSetPolicy();
        TestNaming();
        TestLegacySlots();
        TestGltfImport(directory);
        std::filesystem::remove_all(directory);
        std::cout << "Model material import tests passed." << std::endl;
        return 0;
    }
    catch (const std::exception &exception)
    {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
        std::cerr << exception.what() << std::endl;
        return 1;
    }
}
