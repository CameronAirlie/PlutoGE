// Baked lighting (docs/BAKED_LIGHTING.md), CPU side: lightmap UV generation and packing, the lightmap file, light
// modes and their migration from the old Static flag, Baked Lighting persistence, and scene bakes that must produce
// static shadows in the shadow mask, baked direct light and bounce light.
#include "PlutoGE/render/LightmapUvs.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/SceneEnvironment.h"
#include "PlutoGE/render/SkylinePacker.h"
#include "PlutoGE/scene/BakedProbeVolume.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/LightmapFile.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneBaker.h"
#include "PlutoGE/scene/components/BakedLightingComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"

#include <glm/gtc/packing.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace PlutoGE;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    // Rasterises a range's uv2 triangles at a resolution and counts texels claimed by more than one triangle.
    std::size_t CountUvOverlaps(const render::MeshData &data, render::LightmapUvRange range, int resolution)
    {
        std::vector<int> owner(std::size_t(resolution) * resolution, -1);
        std::size_t overlaps = 0;
        for (std::uint32_t offset = 0; offset + 2 < range.indexCount; offset += 3)
        {
            glm::vec2 p[3];
            for (int k = 0; k < 3; ++k)
            {
                const auto &uv = data.vertices[data.indices[range.indexOffset + offset + k]].uv2;
                p[k] = glm::vec2(uv[0], uv[1]) * float(resolution);
            }
            const float area = (p[1].x - p[0].x) * (p[2].y - p[0].y) - (p[1].y - p[0].y) * (p[2].x - p[0].x);
            if (std::abs(area) < 1e-9f) continue;
            for (int y = 0; y < resolution; ++y)
                for (int x = 0; x < resolution; ++x)
                {
                    const glm::vec2 c(x + 0.5f, y + 0.5f);
                    const float b0 = ((p[1].x - c.x) * (p[2].y - c.y) - (p[1].y - c.y) * (p[2].x - c.x)) / area;
                    const float b1 = ((p[2].x - c.x) * (p[0].y - c.y) - (p[2].y - c.y) * (p[0].x - c.x)) / area;
                    const float b2 = 1.0f - b0 - b1;
                    // Strictly inside: shared edges are not overlaps.
                    if (b0 <= 1e-4f || b1 <= 1e-4f || b2 <= 1e-4f) continue;
                    auto &claimed = owner[std::size_t(y) * resolution + x];
                    if (claimed >= 0 && claimed != int(offset)) ++overlaps;
                    claimed = int(offset);
                }
        }
        return overlaps;
    }

    void CheckLightmapUvGeneration()
    {
        for (const char *shape : {"sphere", "cylinder", "quad"})
        {
            std::unique_ptr<render::Mesh> mesh(std::string(shape) == "sphere" ? render::Mesh::Sphere()
                                               : std::string(shape) == "cylinder" ? render::Mesh::Cylinder() : render::Mesh::Quad());
            Require(mesh->HasLightmapUvs(), std::string(shape) + " has no generated lightmap UVs");
            const auto &data = mesh->GetMeshData();
            const render::LightmapUvRange range{0, static_cast<std::uint32_t>(data.indices.size())};
            const auto check = render::CheckLightmapUvs(data, range);
            Require(check.Usable(), std::string(shape) + " lightmap UVs are unusable");
            Require(check.outside == 0 && check.degenerate == 0, std::string(shape) + " lightmap UVs leave 0..1 or collapse");
            Require(CountUvOverlaps(data, range, 256) == 0, std::string(shape) + " lightmap UV charts overlap");
            // Charts should fill a fair share of the layout (gutters and packing waste the rest).
            Require(check.uvArea > 0.3f, std::string(shape) + " lightmap UVs waste most of the layout: " + std::to_string(check.uvArea));
        }
        // Each submesh gets its own layout; vertices shared across charts are split, never stretched.
        std::unique_ptr<render::Mesh> cube(render::Mesh::Cube());
        render::MeshData data = cube->GetMeshData();
        std::vector<render::LightmapUvRange> ranges{{0, 18}, {18, 18}};
        const auto result = render::GenerateLightmapUvs(data, ranges);
        Require(result.charts >= 6, "Cube faces did not become separate charts");
        for (const auto &range : ranges)
        {
            Require(render::CheckLightmapUvs(data, range).Usable(), "Cube submesh lightmap UVs are unusable");
            Require(CountUvOverlaps(data, range, 128) == 0, "Cube submesh lightmap UV charts overlap");
        }
        std::cout << "PASS lightmap UV generation\n";
    }

    // The lightmap UV at a point of a mesh face: interpolated over the face's triangles (vertex normal `normal`),
    // projected along that normal's dominant axis.
    std::optional<glm::vec2> FaceLightmapUv(const render::Mesh &mesh, const glm::vec3 &point, const glm::vec3 &normal)
    {
        const auto &data = mesh.GetMeshData();
        const int axis = std::abs(normal.x) > 0.5f ? 0 : std::abs(normal.y) > 0.5f ? 1 : 2;
        const auto flat = [&](glm::vec3 p) { return axis == 0 ? glm::vec2(p.y, p.z) : axis == 1 ? glm::vec2(p.x, p.z) : glm::vec2(p.x, p.y); };
        const auto position = [](const render::MeshVertexData &v) { return glm::vec3(v.position[0], v.position[1], v.position[2]); };
        const glm::vec2 q = flat(point);
        for (std::size_t offset = 0; offset + 2 < data.indices.size(); offset += 3)
        {
            const auto &a = data.vertices[data.indices[offset]];
            const auto &b = data.vertices[data.indices[offset + 1]];
            const auto &c = data.vertices[data.indices[offset + 2]];
            if (glm::dot(glm::vec3(a.normal[0], a.normal[1], a.normal[2]), normal) < 0.99f)
                continue;
            const glm::vec2 pa = flat(position(a)), pb = flat(position(b)), pc = flat(position(c));
            const float area = (pb.x - pa.x) * (pc.y - pa.y) - (pb.y - pa.y) * (pc.x - pa.x);
            if (std::abs(area) < 1e-9f)
                continue;
            const float wb = ((q.x - pa.x) * (pc.y - pa.y) - (q.y - pa.y) * (pc.x - pa.x)) / area;
            const float wc = ((pb.x - pa.x) * (q.y - pa.y) - (pb.y - pa.y) * (q.x - pa.x)) / area;
            const float wa = 1.0f - wb - wc;
            if (wa < -1e-4f || wb < -1e-4f || wc < -1e-4f)
                continue;
            return glm::vec2(a.uv2[0], a.uv2[1]) * wa + glm::vec2(b.uv2[0], b.uv2[1]) * wb + glm::vec2(c.uv2[0], c.uv2[1]) * wc;
        }
        return std::nullopt;
    }

    // Summed uv2 area of a mesh's triangles facing `normal`.
    float FaceUvArea(const render::MeshData &data, const glm::vec3 &normal)
    {
        float area = 0.0f;
        for (std::size_t offset = 0; offset + 2 < data.indices.size(); offset += 3)
        {
            const auto &a = data.vertices[data.indices[offset]];
            const auto &b = data.vertices[data.indices[offset + 1]];
            const auto &c = data.vertices[data.indices[offset + 2]];
            if (glm::dot(glm::vec3(a.normal[0], a.normal[1], a.normal[2]), normal) < 0.99f)
                continue;
            area += 0.5f * std::abs((b.uv2[0] - a.uv2[0]) * (c.uv2[1] - a.uv2[1]) - (b.uv2[1] - a.uv2[1]) * (c.uv2[0] - a.uv2[0]));
        }
        return area;
    }

    // Stretched built-in shapes take a copy unwrapped at their scale, so their faces get uv2 area in proportion to
    // their world area; only static meshes in scenes with baked lighting take copies.
    void CheckScaleFittedLightmapUvs()
    {
        std::unique_ptr<render::Mesh> cube(render::Mesh::Cube());
        Require(cube->CanFitLightmapUvsToScale(), "The built-in cube cannot refit its lightmap UVs");
        const auto fitted = cube->CreateScaleFittedLightmapUvs({20.0f, 1.0f, 10.0f});
        Require(fitted && fitted->HasLightmapUvs(), "No scale-fitted cube");
        const auto &data = fitted->GetMeshData();
        const render::LightmapUvRange range{0, static_cast<std::uint32_t>(data.indices.size())};
        Require(render::CheckLightmapUvs(data, range).Usable() && CountUvOverlaps(data, range, 256) == 0,
                "Scale-fitted cube lightmap UVs are unusable or overlap");
        // Top 20x10 m against a side 10x1 m: 20 times the area, so 20 times the texels.
        const float ratio = FaceUvArea(data, {0, 1, 0}) / FaceUvArea(data, {1, 0, 0});
        Require(ratio > 15.0f && ratio < 25.0f, "Scale-fitted cube faces are not sized by world area: " + std::to_string(ratio));

        scene::Scene scene;
        render::Material material;
        const auto add = [&](glm::vec3 scale, bool isStatic) {
            auto entity = std::make_unique<scene::Entity>();
            entity->SetScale(scale);
            auto *mesh = entity->AddComponent(new scene::MeshComponent({cube.get(), &material}));
            mesh->SetStatic(isStatic);
            scene.AddEntity(std::move(entity));
            return mesh;
        };
        auto *stretched = add({20.0f, 1.0f, 10.0f}, true);
        auto *even = add({3.0f, 3.0f, 3.0f}, true);
        auto *moving = add({20.0f, 1.0f, 10.0f}, false);
        Require(stretched->GetLightmapMesh() == cube.get(), "A scene without baked lighting refitted a shared mesh");
        auto bakedEntity = std::make_unique<scene::Entity>();
        bakedEntity->CreateComponent<scene::BakedLightingComponent>();
        scene.AddEntity(std::move(bakedEntity));
        Require(stretched->GetLightmapMesh() != cube.get(), "A stretched static cube kept the shared lightmap layout");
        Require(stretched->GetLightmapMesh() == stretched->GetLightmapMesh(), "The scale-fitted copy is rebuilt on every call");
        Require(even->GetLightmapMesh() == cube.get(), "An evenly scaled cube was refitted");
        Require(moving->GetLightmapMesh() == cube.get(), "A non-static cube was refitted");
        std::cout << "PASS scale-fitted lightmap UVs\n";
    }

    void CheckSkylinePacking()
    {
        std::mt19937 random(7);
        std::vector<render::PackSize> sizes;
        for (int index = 0; index < 300; ++index)
            sizes.push_back({1u + random() % 40u, 1u + random() % 40u});
        std::uint32_t height = 0;
        const auto positions = render::SkylinePacker::Pack(sizes, 256, 4096, &height);
        Require(positions.has_value(), "Skyline packing failed");
        std::vector<int> grid(256u * height, -1);
        double used = 0.0;
        for (std::size_t index = 0; index < sizes.size(); ++index)
        {
            const auto [x, y] = (*positions)[index];
            Require(x + sizes[index].width <= 256 && y + sizes[index].height <= height, "Packed rectangle leaves the strip");
            for (std::uint32_t py = y; py < y + sizes[index].height; ++py)
                for (std::uint32_t px = x; px < x + sizes[index].width; ++px)
                {
                    Require(grid[py * 256u + px] < 0, "Packed rectangles overlap");
                    grid[py * 256u + px] = int(index);
                }
            used += double(sizes[index].width) * sizes[index].height;
        }
        Require(used / (256.0 * height) > 0.7, "Skyline packing wastes over 30% of the strip");
        Require(!render::SkylinePacker::Pack(sizes, 256, 64).has_value(), "Packing ignores the height limit");
        std::cout << "PASS skyline packing\n";
    }

    void CheckLightmapFile()
    {
        render::LightmapAtlas atlas;
        atlas.width = 5;
        atlas.height = 3;
        for (const auto encoding : {render::LightmapEncoding::IrradianceShadowMask, render::LightmapEncoding::SkyVisibility})
        {
            auto &layer = atlas.layers.emplace_back();
            layer.encoding = encoding;
            for (int index = 0; index < 5 * 3 * 4; ++index)
                layer.texels.push_back(glm::packHalf1x16(float(index) * 0.25f + float(atlas.layers.size())));
        }
        const auto path = std::filesystem::temp_directory_path() / "PlutoGEBakedLightingTests" / "atlas.plutolightmap";
        std::string error;
        Require(scene::WriteLightmapFile(path, atlas, &error), "Lightmap write failed: " + error);
        const auto read = scene::ReadLightmapFile(path, &error);
        Require(read.has_value(), "Lightmap read failed: " + error);
        Require(read->width == 5 && read->height == 3 && read->layers.size() == 2 &&
                    read->layers[0].texels == atlas.layers[0].texels &&
                    read->Find(render::LightmapEncoding::SkyVisibility)->texels == atlas.layers[1].texels,
                "Lightmap file does not round trip its layers");

        // The renderer stacks layers in LightmapLayerOrder and says where the optional ones went.
        const auto stacked = render::StackLightmapLayers(atlas);
        Require(stacked.layerCount == 2 && stacked.skyVisibilityLayer == 1 && stacked.texels.size() == 2u * 5 * 3 * 4,
                "Lightmap layers do not stack in order");

        // Version 1 files (one layer named in the header) still read.
        {
            std::ofstream file(path, std::ios::binary | std::ios::trunc);
            const char magic[8] = {'P', 'L', 'M', 'A', 'P', 0, 0, 0};
            const std::uint32_t header[4] = {1, static_cast<std::uint32_t>(render::LightmapEncoding::IrradianceShadowMask), 5, 3};
            file.write(magic, sizeof(magic));
            file.write(reinterpret_cast<const char *>(header), sizeof(header));
            file.write(reinterpret_cast<const char *>(atlas.layers[0].texels.data()),
                       static_cast<std::streamsize>(atlas.layers[0].texels.size() * sizeof(std::uint16_t)));
        }
        const auto legacy = scene::ReadLightmapFile(path, &error);
        Require(legacy.has_value() && legacy->layers.size() == 1 && legacy->layers[0].texels == atlas.layers[0].texels &&
                    render::StackLightmapLayers(*legacy).skyVisibilityLayer == -1,
                "A version 1 lightmap should read as one layer: " + error);
        // Truncated files are rejected, not read as garbage.
        std::filesystem::resize_file(path, 40);
        Require(!scene::ReadLightmapFile(path, &error).has_value(), "Truncated lightmap was accepted");
        std::cout << "PASS lightmap file\n";
    }

    void CheckLightModes()
    {
        scene::LightComponent light;
        light.Deserialize({{"Static", scene::PropertyType::Bool, "true"}});
        Require(light.GetBakeMode() == render::LightBakeMode::Mixed, "Static lights do not migrate to Mixed");
        light.Deserialize({{"Static", scene::PropertyType::Bool, "false"}});
        Require(light.GetBakeMode() == render::LightBakeMode::Realtime, "Non-static lights do not migrate to Realtime");
        light.SetBakeMode(render::LightBakeMode::Baked);
        scene::LightComponent copy;
        copy.Deserialize(light.Serialize());
        Require(copy.GetBakeMode() == render::LightBakeMode::Baked, "Light mode does not persist");
        std::cout << "PASS light modes\n";
    }

    float Channel(const render::LightmapAtlas &atlas, glm::vec2 uv, int channel,
                  render::LightmapEncoding encoding = render::LightmapEncoding::IrradianceShadowMask)
    {
        const auto *layer = atlas.Find(encoding);
        Require(layer != nullptr, "The atlas has no such layer");
        const auto x = std::min(std::uint32_t(uv.x * atlas.width), atlas.width - 1);
        const auto y = std::min(std::uint32_t(uv.y * atlas.height), atlas.height - 1);
        return glm::unpackHalf1x16(layer->texels[(std::size_t(y) * atlas.width + x) * 4 + channel]);
    }
    float Luminance(const render::LightmapAtlas &atlas, glm::vec2 uv)
    {
        return Channel(atlas, uv, 0) * 0.2126f + Channel(atlas, uv, 1) * 0.7152f + Channel(atlas, uv, 2) * 0.0722f;
    }

    // A 10 m floor (the built-in cube, whose top face has its own lightmap chart) under a 2 m box held 2 m above it.
    struct ShadowScene
    {
        scene::Scene scene;
        std::unique_ptr<render::Mesh> cube{render::Mesh::Cube()};
        render::Material material;
        scene::Entity *floor = nullptr;
        scene::LightComponent *sun = nullptr;

        explicit ShadowScene(render::LightBakeMode sunMode)
        {
            material.SetCastsShadow(true);
            auto floorEntity = std::make_unique<scene::Entity>();
            floorEntity->SetName("Floor");
            floorEntity->SetScale({10.0f, 0.2f, 10.0f});
            floorEntity->AddComponent(new scene::MeshComponent({cube.get(), &material}))->SetStatic(true);
            floor = floorEntity.get();
            scene.AddEntity(std::move(floorEntity));
            auto box = std::make_unique<scene::Entity>();
            box->SetName("Box");
            box->SetPosition({0.0f, 2.1f, 0.0f});
            box->SetScale({2.0f, 2.0f, 2.0f});
            box->AddComponent(new scene::MeshComponent({cube.get(), &material}))->SetStatic(true);
            scene.AddEntity(std::move(box));
            auto sunEntity = std::make_unique<scene::Entity>();
            sun = sunEntity->CreateComponent<scene::LightComponent>();
            sun->GetLight().type = scene::LightType::Directional;
            sun->GetLight().intensity = 3.0f;
            sun->GetLight().castsShadows = true;
            sun->GetLight().directionalShadowSettings.softness = 0.0f;
            sun->SetBakeMode(sunMode);
            sunEntity->SetRotation({-90.0f, 0.0f, 0.0f}); // straight down
            scene.AddEntity(std::move(sunEntity));
            auto sky = std::make_unique<scene::Entity>();
            sky->CreateComponent<scene::PhysicalSkyComponent>();
            scene.AddEntity(std::move(sky));
        }

        // The floor's top-face texel at face UV (u, v), through the layout the floor draws with (it is stretched, so a
        // copy fitted to its scale).
        glm::vec2 TopFace(const scene::BakedLightingComponent &baked, float u, float v) const
        {
            auto *mesh = floor->GetComponent<scene::MeshComponent>()->GetLightmapMesh();
            const auto uv = FaceLightmapUv(*mesh, {u - 0.5f, 0.5f, 0.5f - v}, {0, 1, 0});
            Require(uv.has_value(), "The floor's top face has no lightmap UV there");
            const glm::vec2 uv2 = *uv;
            const auto rect = baked.FindRect(floor->GetID(), 0);
            Require(rect.IsValid(), "The floor was not lightmapped");
            return uv2 * rect.scale + rect.offset;
        }
    };

    scene::SceneBakeSettings LightmapOnly()
    {
        auto settings = scene::SceneBakeSettings::Final();
        settings.bakeProbeVolume = false;
        settings.bakeLightmaps = true;
        return settings;
    }

    void CheckMixedShadowMask()
    {
        ShadowScene shadow(render::LightBakeMode::Mixed);
        const auto result = scene::SceneBaker().Bake(shadow.scene, LightmapOnly());
        Require(result.succeeded && result.lightmappedDrawCount == 2, "Mixed bake failed: " + result.message);
        const auto *baked = shadow.scene.GetBakedLighting();
        Require(baked && baked->HasBake() && baked->GetBake().hasShadowMask, "Mixed bake produced no shadow mask");
        const auto &atlas = *baked->GetBake().atlas;
        const float underBox = Channel(atlas, shadow.TopFace(*baked, 0.5f, 0.5f), 3);
        const float open = Channel(atlas, shadow.TopFace(*baked, 0.1f, 0.1f), 3);
        Require(underBox < 0.05f, "The box casts no static shadow in the mask: " + std::to_string(underBox));
        Require(open > 0.95f, "Open floor is shadowed in the mask: " + std::to_string(open));
        Require(Luminance(atlas, shadow.TopFace(*baked, 0.1f, 0.1f)) > 0.0f, "Sky light was not baked");

        // The sky visibility layer, seen along the floor's normal: open floor sees the whole sky above it, the floor
        // under the box almost none.
        const auto skyUp = [&](glm::vec2 uv)
        {
            using render::LightmapEncoding;
            return Channel(atlas, uv, 3, LightmapEncoding::SkyVisibility) + Channel(atlas, uv, 1, LightmapEncoding::SkyVisibility);
        };
        const float skyOpen = skyUp(shadow.TopFace(*baked, 0.1f, 0.1f)), skyUnderBox = skyUp(shadow.TopFace(*baked, 0.5f, 0.5f));
        Require(skyOpen > 0.85f, "Open floor should see the sky: " + std::to_string(skyOpen));
        Require(skyUnderBox < 0.35f, "The box should hide the sky from the floor under it: " + std::to_string(skyUnderBox));
        std::cout << "PASS mixed sun shadow mask and sky visibility\n";
    }

    void CheckBakedDirectLight()
    {
        ShadowScene shadow(render::LightBakeMode::Baked);
        const auto result = scene::SceneBaker().Bake(shadow.scene, LightmapOnly());
        Require(result.succeeded, "Baked bake failed: " + result.message);
        const auto *baked = shadow.scene.GetBakedLighting();
        const auto &atlas = *baked->GetBake().atlas;
        Require(!baked->GetBake().hasShadowMask, "A Baked sun wrote a shadow mask");
        const float lit = Luminance(atlas, shadow.TopFace(*baked, 0.1f, 0.1f));
        const float shadowed = Luminance(atlas, shadow.TopFace(*baked, 0.5f, 0.5f));
        // 3 lux straight down is 3 / pi mean radiance on the open floor. A Mixed sun keeps that light realtime, so
        // the same floor baked with a Mixed sun holds everything but it (sky and bounce are identical).
        ShadowScene mixed(render::LightBakeMode::Mixed);
        Require(scene::SceneBaker().Bake(mixed.scene, LightmapOnly()).succeeded, "Mixed comparison bake failed");
        const auto *mixedBaked = mixed.scene.GetBakedLighting();
        const float mixedLit = Luminance(*mixedBaked->GetBake().atlas, mixed.TopFace(*mixedBaked, 0.1f, 0.1f));
        const float bakedDirect = lit - mixedLit;
        Require(std::abs(bakedDirect - 3.0f / 3.14159f) < 0.1f * 3.0f / 3.14159f,
                "A Baked sun's direct light is not 3/pi: " + std::to_string(bakedDirect));
        Require(shadowed < lit * 0.5f, "The Baked sun's shadow under the box is missing");
        // Under the box, light arrives by bounce and from the sky around it.
        Require(shadowed > 0.0f, "Nothing reaches the floor under the box");
        std::cout << "PASS baked sun direct light\n";
    }

    void CheckBakePersistence()
    {
        ShadowScene shadow(render::LightBakeMode::Mixed);
        Require(scene::SceneBaker().Bake(shadow.scene, LightmapOnly()).succeeded, "Bake for persistence failed");
        auto *baked = shadow.scene.GetBakedLighting();
        const auto path = std::filesystem::temp_directory_path() / "PlutoGEBakedLightingTests" / "scene.plutolightmap";
        std::string error;
        Require(scene::WriteLightmapFile(path, *baked->GetBake().atlas, &error), "Atlas write failed: " + error);
        baked->SetAtlasReference(path.generic_string());
        baked->SetLightingMode(scene::SceneLightingMode::Realtime);
        auto settings = baked->GetBakeSettings();
        settings.texelsPerUnit = 7.5f;
        baked->SetBakeSettings(settings);

        scene::BakedLightingComponent loaded;
        loaded.Deserialize(baked->Serialize());
        Require(loaded.GetLightingMode() == scene::SceneLightingMode::Realtime, "Lighting mode does not persist");
        Require(loaded.GetBakeSettings().texelsPerUnit == 7.5f, "Bake settings do not persist");
        Require(loaded.HasBake(), "The bake does not load from its atlas file");
        Require(loaded.FindRect(shadow.floor->GetID(), 0) == baked->FindRect(shadow.floor->GetID(), 0), "Lightmap rects do not persist");
        Require(loaded.GetBake().hasShadowMask && glm::length(loaded.GetBake().shadowMaskDirection - baked->GetBake().shadowMaskDirection) < 1e-5f,
                "The shadow mask's light does not persist");
        Require(!loaded.IsActive(), "Realtime lighting mode reports baked lighting as active");
        std::cout << "PASS baked lighting persistence\n";
    }

    // A built-in cube stretched into a floor gets the requested texel density on its top face.
    void CheckStretchedFloorDensity()
    {
        ShadowScene shadow(render::LightBakeMode::Mixed);
        shadow.floor->SetScale({40.0f, 0.5f, 20.0f});
        Require(scene::SceneBaker().Bake(shadow.scene, LightmapOnly()).succeeded, "Stretched floor bake failed");
        const auto *bakedLighting = shadow.scene.GetBakedLighting();
        const auto &atlas = *bakedLighting->GetBake().atlas;
        const auto rect = bakedLighting->FindRect(shadow.floor->GetID(), 0);
        auto *mesh = shadow.floor->GetComponent<scene::MeshComponent>()->GetLightmapMesh();
        const double texels = double(FaceUvArea(mesh->GetMeshData(), {0, 1, 0})) * rect.scale.x * rect.scale.y * atlas.width * atlas.height;
        const double perMetre = std::sqrt(texels / (40.0 * 20.0));
        const double requested = bakedLighting->GetBakeSettings().texelsPerUnit;
        Require(perMetre > requested * 0.75 && perMetre < requested * 1.4,
                "The stretched floor's top face has " + std::to_string(perMetre) + " texels per metre, not " + std::to_string(requested));
        std::cout << "PASS stretched floor texel density\n";
    }

    void CheckUnusableTargetsReported()
    {
        // A static mesh without lightmap UVs is reported, not baked into other draws' texels.
        scene::Scene scene;
        render::MeshConfig config;
        std::unique_ptr<render::Mesh> source(render::Mesh::Cube());
        config.data = source->GetMeshData();
        std::unique_ptr<render::Mesh> withoutUvs(render::Mesh::FromConfig(config));
        render::Material material;
        auto entity = std::make_unique<scene::Entity>();
        entity->SetName("No UVs");
        entity->AddComponent(new scene::MeshComponent({withoutUvs.get(), &material}))->SetStatic(true);
        scene.AddEntity(std::move(entity));
        auto sky = std::make_unique<scene::Entity>();
        sky->CreateComponent<scene::PhysicalSkyComponent>();
        scene.AddEntity(std::move(sky));
        scene::SceneBakeResult result = scene::SceneBaker().Bake(scene, LightmapOnly());
        Require(!result.succeeded && result.message.find("lightmap UVs") != std::string::npos,
                "A mesh without lightmap UVs is not reported: " + result.message);
        std::cout << "PASS unusable lightmap targets reported\n";
    }
    // Shadowmask (Mixed Shadows: Moving Objects Only): the probes record the Mixed sun's static shadow for moving
    // objects, and the renderer and the editor's status agree on when it applies.
    void CheckShadowmaskBake()
    {
        ShadowScene shadow(render::LightBakeMode::Mixed);
        Require(!scene::DescribeMixedShadows(shadow.scene).shadowmask, "Shadowmask needs a bake");
        auto settings = scene::SceneBakeSettings::Final();
        settings.probeSpacing = 0.5f;
        const auto result = scene::SceneBaker().Bake(shadow.scene, settings);
        Require(result.succeeded, "Shadowmask bake failed: " + result.message);

        const auto volumes = shadow.scene.GetBakedProbeVolumes();
        Require(!volumes.empty() && volumes.front()->HasSunVisibility(), "The probe bake has no sun visibility");
        const auto &volume = *volumes.front();
        float underBox = 1.0f, open = 0.0f;
        for (std::size_t i = 0; i < volume.positions.size(); ++i)
        {
            const glm::vec4 probe = volume.positions[i];
            if (probe.w < 0.5f || probe.y < 0.15f || probe.y > 1.0f) continue;
            if (std::abs(probe.x) < 0.6f && std::abs(probe.z) < 0.6f) underBox = std::min(underBox, volume.sunVisibility[i]);
            if (std::abs(probe.x) > 3.0f && std::abs(probe.z) > 3.0f) open = std::max(open, volume.sunVisibility[i]);
        }
        Require(underBox < 0.1f, "A probe under the box should be in the sun's static shadow: " + std::to_string(underBox));
        Require(open > 0.9f, "A probe in the open should see the sun: " + std::to_string(open));

        auto *baked = shadow.scene.GetBakedLighting();
        Require(baked && baked->GetMixedShadows() == scene::MixedShadowMode::Shadowmask, "New scenes should default to shadowmask");
        render::CameraData camera;
        Require(render::BuildSceneLighting(camera, &shadow.scene).bakedLighting.shadowmask, "The renderer does not apply shadowmask");
        const auto status = scene::DescribeMixedShadows(shadow.scene);
        Require(status.shadowmask && !status.warning, "The editor should report shadowmask as working: " + status.message);

        // A turned sun no longer matches the bake: every caster is realtime again, and the editor says why.
        shadow.sun->GetOwner()->SetRotation({-60.0f, 0.0f, 0.0f});
        Require(!render::BuildSceneLighting(camera, &shadow.scene).bakedLighting.shadowmask, "A turned sun must not use the old bake");
        Require(scene::DescribeMixedShadows(shadow.scene).warning, "The editor should ask for a rebake after the sun turns");
        shadow.sun->GetOwner()->SetRotation({-90.0f, 0.0f, 0.0f});

        baked->SetMixedShadows(scene::MixedShadowMode::Realtime);
        Require(!render::BuildSceneLighting(camera, &shadow.scene).bakedLighting.shadowmask, "All Realtime must turn shadowmask off");
        std::cout << "PASS shadowmask bake\n";
    }

    void CheckMixedShadowsSetting()
    {
        scene::BakedLightingComponent component;
        component.SetMixedShadows(scene::MixedShadowMode::Realtime);
        scene::BakedLightingComponent loaded;
        loaded.Deserialize(component.Serialize());
        Require(loaded.GetMixedShadows() == scene::MixedShadowMode::Realtime, "Mixed Shadows does not persist");
        component.SetMixedShadows(scene::MixedShadowMode::Shadowmask);
        loaded.Deserialize(component.Serialize());
        Require(loaded.GetMixedShadows() == scene::MixedShadowMode::Shadowmask, "Shadowmask does not persist");
        // Scenes saved before the setting rendered every caster in realtime, and keep doing so.
        scene::BakedLightingComponent legacy;
        legacy.Deserialize({{"Lighting Mode", scene::PropertyType::Enum, "1"}});
        Require(legacy.GetMixedShadows() == scene::MixedShadowMode::Realtime, "Older scenes should load as All Realtime");
        std::cout << "PASS mixed shadows setting\n";
    }

}

int main()
{
    try
    {
        CheckLightmapUvGeneration();
        CheckScaleFittedLightmapUvs();
        CheckSkylinePacking();
        CheckLightmapFile();
        CheckLightModes();
        CheckMixedShadowMask();
        CheckBakedDirectLight();
        CheckBakePersistence();
        CheckStretchedFloorDensity();
        CheckUnusableTargetsReported();
        CheckMixedShadowsSetting();
        CheckShadowmaskBake();
    }
    catch (const std::exception &error)
    {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
    std::cout << "All baked lighting checks passed\n";
    return 0;
}
