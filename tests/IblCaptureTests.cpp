#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/rhi/opengl/OpenGLDevice.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/RenderCommand.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneBaker.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include "PlutoGE/scene/components/LightComponent.h"
#include "PlutoGE/scene/components/PhysicalSkyComponent.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/AmbientLightVolumeComponent.h"
#include "PlutoGE/ui/EditorSceneRenderService.h"
#include "PlutoGE/ui/DebugDrawUI.h"
#include "IblPrefilter.h"
#include <glm/gtc/matrix_transform.hpp>
#include <array>
#include <cmath>
#include <thread>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <filesystem>
#include <fstream>

int main(int argc, char **argv) try
{
    using namespace PlutoGE;
    using namespace render;
    // A constant HDR environment must conserve radiance at every roughness.
    {
        constexpr unsigned size = 16;
        std::vector<float> faces(size*size*24, 1);
        for (std::size_t i=0; i<faces.size(); i+=4) { faces[i]=2; faces[i+1]=3; faces[i+2]=4; }
        const auto atlas = BuildIblPrefilterAtlas(faces, size);
        for (unsigned level=0; level<=4; ++level) {
            const unsigned side=size>>level, start=size*2-side*2;
            for (unsigned face=0; face<6; ++face) for (unsigned y=0; y<side; ++y) for (unsigned x=0; x<side; ++x) {
                const auto offset=((face*side+y)*size*2+start+x)*4;
                for (unsigned c=0;c<3;++c)
                    if (std::abs(atlas[offset+c]-float(c+2))>.0001f)
                        throw std::runtime_error("IBL convolution changes uniform HDR radiance");
            }
        }
        // A rough +Z reflection must receive radiance from adjacent faces;
        // ordinary per-face mipmaps cannot spread that lighting at all.
        std::fill(faces.begin(),faces.end(),0.0f);
        for (unsigned i=0;i<size*size;++i) faces[i*4]=2;
        const auto filtered = BuildIblPrefilterAtlas(faces,size);
        const auto roughZ=(4*size*2+(size*2-2))*4;
        if (filtered[roughZ]<.05f || filtered[(4*size*size*2)*4]>.0001f)
            throw std::runtime_error("IBL roughness filtering does not broaden reflections across cube faces");
    }
    // Budget pressure must preserve world-space density in elongated volumes,
    // regardless of whether their short axis is vertical or horizontal.
    for (const auto size : {glm::vec3(100,10,100),glm::vec3(10,100,100),glm::vec3(100,100,10)}) {
        const auto resolution=scene::SceneBaker::CalculateProbeGridResolution(size,1,4096);
        const auto cell=size/glm::vec3(resolution);
        const float largest=std::max({cell.x,cell.y,cell.z}), smallest=std::min({cell.x,cell.y,cell.z});
        if (resolution.x*resolution.y*resolution.z>4096 || largest/smallest>1.25f)
            throw std::runtime_error("Probe budget creates anisotropic world-space spacing");
    }
    if (scene::SceneBaker::CalculateProbeGridResolution({8,4,2},1,4096)!=glm::ivec3(8,4,2) ||
        scene::SceneBaker::CalculateProbeGridResolution({100,1,100},.05f,8)!=glm::ivec3(2))
        throw std::runtime_error("Probe resolution ignores requested spacing or minimum budget");
    scene::BakedProbeVolume corridor;
    corridor.origin={-10,-1,-2}; corridor.size={20,2,4}; corridor.resolution={1,1,1};
    corridor.irradiance={{0,0,0}}; corridor.positions={{0,0,0,1}};
    corridor.sphericalHarmonics.resize(1); corridor.visibility.resize(1);
    corridor.visibility[0].fill(100);
    const auto corridorCaptures=scene::SceneBaker::GenerateReflectionProbeLayout(corridor,1);
    if (corridorCaptures.size()!=1 || glm::length(corridorCaptures[0].size-corridor.size)>.0001f)
        throw std::runtime_error("Reflection bounds collapse a long room into a small cube");
    const auto api = argc > 1 && std::string_view(argv[1]) == "--opengl"
        ? rhi::GraphicsApi::OpenGL : rhi::GraphicsApi::Vulkan;
    auto &engine = core::Engine::GetInstance();
    core::EngineConfig config; config.graphicsApi = api;
    config.windowConfig.visible = false; config.windowConfig.width = config.windowConfig.height = 64;
    if (!engine.Initialize(config)) throw std::runtime_error("Cannot initialize capture engine");
    struct Cleanup { core::Engine &engine; ~Cleanup() { engine.Shutdown(); } } cleanup{engine};
    // Authored-scene troubleshooting keeps all asset/bake writes disabled.
    if (argc>3 && std::string_view(argv[1])=="--scene-bake") {
        engine.GetAssetManager().SetProjectContext(argv[2],"Assets",1);
        std::string error;
        auto authored=scene::SceneSerializer::Load(argv[3],&error);
        if (!authored) throw std::runtime_error("Cannot load authored scene: "+error);
        std::cout << "Authored ambient volumes=" << authored->GetAmbientLightVolumes().size() << std::endl;
        scene::SceneBakeSettings options; options.probesOnly=true; options.bakeIndirectBounce=false;
        options.probeBounceStrength=1; options.logProbeDiagnostics=true;
        const auto result=scene::SceneBaker().Bake(*authored,options);
        if (!result.succeeded) throw std::runtime_error(result.message);
        std::cout << "Authored scene diagnostic bake completed without saving: " << result.message << std::endl;
        return 0;
    }
    // Imported sections stored in node space render at their node transform;
    // the bake snapshot must place them there too.
    {
        MeshConfig nodal;
        for (auto p : {glm::vec3(0,0,0),glm::vec3(1,0,0),glm::vec3(0,1,0)})
            nodal.data.vertices.push_back({{p.x,p.y,p.z},{0,0,1},{0,0},{1,0,0,1}});
        nodal.data.indices={0,1,2};
        Submesh section; section.indexCount=3; section.animatedNodeIndex=1;
        nodal.submeshes.push_back(section);
        nodal.animationNodes.push_back({"root",-1,glm::translate(glm::mat4(1),glm::vec3(10,0,0))});
        nodal.animationNodes.push_back({"section",0,glm::scale(glm::mat4(1),glm::vec3(2))});
        Mesh mesh(nodal); Material material;
        scene::Scene imported;
        auto object=std::make_unique<scene::Entity>();
        object->AddComponent(new scene::MeshComponent({&mesh,&material}))->SetStatic(true);
        imported.AddEntity(std::move(object));
        scene::SceneBakeSettings options; options.probesOnly=true; options.bakeIndirectBounce=false;
        options.maxProbeCount=8; options.probeDirectionCount=8;
        const bool baked=scene::SceneBaker().Bake(imported,options).succeeded;
        const auto &volume=imported.GetBakedProbeVolume();
        if (!baked || std::abs(volume.origin.x-(10-2*.05f))>.01f || std::abs(volume.size.x-2*1.1f)>.01f)
            throw std::runtime_error("Bake ignores imported node transforms of model sections");
    }
    // Reproduce split-model imports: each object renders a selected section
    // of a shared mesh. Unselected distant geometry must not enter the bake.
    {
        MeshConfig split;
        for (float x : {0.0f,1000.0f,2000.0f}) {
            const auto base=static_cast<uint32_t>(split.data.vertices.size());
            for (auto p : {glm::vec3(x,0,0),glm::vec3(x+1,0,0),glm::vec3(x,1,0)})
                split.data.vertices.push_back({{p.x,p.y,p.z},{0,0,1},{0,0},{1,0,0,1}});
            Submesh section; section.indexOffset=static_cast<uint32_t>(split.data.indices.size()); section.indexCount=3;
            split.submeshes.push_back(section);
            for (auto i : {0u,1u,2u}) split.data.indices.push_back(base+i);
        }
        Mesh shared(split); Material material;
        for (int count : {1,2}) {
            scene::Scene selected;
            auto object=std::make_unique<scene::Entity>();
            auto *component=object->AddComponent(new scene::MeshComponent({}));
            component->SetMesh(&shared); component->SetMaterial(&material); component->SetStatic(true);
            component->SetSubmeshRange(0,count); component->SetSubmeshPositionOffset(0,{10,0,0});
            selected.AddEntity(std::move(object));
            scene::SceneBakeSettings options; options.probesOnly=true; options.bakeIndirectBounce=false;
            options.maxProbeCount=8; options.probeDirectionCount=8;
            const auto baked=scene::SceneBaker().Bake(selected,options);
            const auto &volume=selected.GetBakedProbeVolume();
            const float expectedExtent=count==1 ? 1.0f : 991.0f;
            if (!baked.succeeded || std::abs(volume.origin.x-(10-expectedExtent*.05f))>.01f ||
                std::abs(volume.size.x-expectedExtent*1.1f)>.01f)
                throw std::runtime_error("Bake includes unselected shared mesh sections or ignores section offsets");
            if (!component->GenerateLightmapUvAtlasForSubmeshes({0}) ||
                component->GetMesh()->GetMeshData().indices.size()!=static_cast<std::size_t>(count*3) ||
                shared.GetMeshData().indices.size()!=9)
                throw std::runtime_error("Fallback UVs clone unselected geometry or mutate shared source");
            if (count==1 && api==rhi::GraphicsApi::OpenGL) {
                auto lamp=std::make_unique<scene::Entity>();
                auto *light=lamp->AddComponent(new scene::LightComponent());
                light->GetLight().type=scene::LightType::Directional;
                light->GetLight().direction={0,0,-1}; light->SetStatic(true);
                selected.AddEntity(std::move(lamp));
                selected.SetFilePath("shared_section_bake_regression.plutoscene");
                options.probesOnly=false; options.bakeProbeVolume=false;
                options.lightmapResolution=8; options.directShadowSampleCount=1;
                auto task=scene::SceneBaker().BeginBake(selected,options);
                if (!task) throw std::runtime_error("Cannot start lightmap regression bake");
                while (!task->IsFinished()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
                const auto result=task->Finalize(selected);
                if (!result.succeeded || result.bakedLightmapCount!=1 ||
                    !component->GetMaterialForSubmesh(0)->GetConfig().lightmapTexture ||
                    task->Finalize(selected).bakedLightmapCount!=1)
                    throw std::runtime_error("Disk-backed lightmap upload or repeated finalization failed: "+result.message);
            }
            component->SetSubmeshIndex(2);
            if (component->GetMesh()!=&shared || component->GetMesh()->GetSubmesh(2).indexCount!=3)
                throw std::runtime_error("Changing selection loses geometry after a compact fallback unwrap");
        }
    }
    // Optional read-only stress check with the real Bistro geometry. No project
    // files or bake outputs are written by an ambient-only bake.
    if (argc>2 && std::string(argv[1])=="--bistro") {
        assets::AssetManager reader;
        reader.SetProjectContext(argv[2],"Assets",1);
        MeshConfig imported; std::vector<std::string> references; assets::MeshAssetMetadata metadata; std::string error;
        if (!reader.LoadMeshAssetData("project://Imported/Bistro_Godot/Bistro_Godot.plutomesh",imported,references,metadata,&error))
            throw std::runtime_error(error);
        Mesh shared(imported); imported=MeshConfig{}; Material material;
        scene::Scene bistro;
        for (std::size_t index=0; index<shared.GetSubmeshCount(); ++index) {
            auto object=std::make_unique<scene::Entity>();
            auto *component=object->AddComponent(new scene::MeshComponent({}));
            component->SetMesh(&shared); component->SetMaterial(&material); component->SetStatic(true);
            component->SetSubmeshIndex(static_cast<int>(index)); bistro.AddEntity(std::move(object));
        }
        scene::SceneBakeSettings options; options.probesOnly=true; options.bakeIndirectBounce=false;
        options.maxProbeCount=4096; options.probeDirectionCount=128; options.probeSpacing=.05f;
        const auto baked=scene::SceneBaker().Bake(bistro,options);
        if (!baked.succeeded) throw std::runtime_error("Bistro bake failed: "+baked.message);
        std::cout << "Bistro shared-section stress bake completed: " << baked.message << std::endl;
        return 0;
    }
    scene::Scene scene;
    ui::EditorSceneRenderService service;
    if (!service.Initialize(api, engine.GetRenderDevice())) throw std::runtime_error("Cannot initialize capture service");
    constexpr glm::vec3 directions[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    constexpr glm::vec3 ups[] = {{0,-1,0},{0,-1,0},{0,0,1},{0,0,-1},{0,-1,0},{0,-1,0}};
    constexpr glm::vec3 colors[] = {{2,0,0},{0,2,0},{0,0,2},{2,2,0},{2,0,2},{0,2,2}};
    std::array<std::unique_ptr<Mesh>,12> meshes;
    std::array<std::unique_ptr<Material>,12> materials;
    std::array<RenderCommand,12> commands;
    for (unsigned face = 0; face < 6; ++face)
    {
        const auto right = glm::cross(directions[face], ups[face]);
        for (unsigned half = 0; half < 2; ++half)
        {
            const auto index = face * 2 + half;
            MeshConfig mesh;
            for (glm::vec2 corner : {glm::vec2(-1,-1),glm::vec2(1,-1),glm::vec2(1,1),glm::vec2(-1,1)})
            {
                const auto position = directions[face]*2.0f +
                    (right*corner.x + ups[face]*(corner.y*.5f + (half == 0 ? -.5f : .5f)))*2.0f;
                const auto normal = -directions[face];
                mesh.data.vertices.push_back({{position.x,position.y,position.z},
                    {normal.x,normal.y,normal.z},{0,0},{1,0,0,1}});
            }
            mesh.data.indices = {0,1,2,0,2,3};
            meshes[index] = std::make_unique<Mesh>(mesh);
            MaterialConfig material; material.emission = colors[face] * (half == 0 ? 1.0f : 2.0f); material.twoSided = true; material.castsShadow = false;
            materials[index] = std::make_unique<Material>(material);
            commands[index].mesh = meshes[index].get(); commands[index].material = materials[index].get();
            commands[index].castsShadow = false;
            commands[index].worldBounds = commands[index].previousWorldBounds = meshes[index]->GetBounds();
        }
    }
    const auto pixels = service.CaptureIbl({0,0,0},32,10,commands,scene);
    if (pixels.size() != 32*32*24) throw std::runtime_error(service.GetLastRenderError());
    for (unsigned face = 0; face < 6; ++face)
    {
        for (unsigned half = 0; half < 2; ++half)
        {
            const auto at = (face*32*32 + (half == 0 ? 8 : 24)*32 + 16)*4;
            const glm::vec3 color(pixels[at],pixels[at+1],pixels[at+2]);
            const auto expected = colors[face] * (half == 0 ? 1.0f : 2.0f);
            for (unsigned channel = 0; channel < 3; ++channel)
                if (std::abs(color[channel] - expected[channel]) > .05f)
                    throw std::runtime_error("Capture lost HDR, face order, vertical orientation, or linear colour");
        }
    }
    if (!service.CaptureIbl({0,0,0},0,10,commands,scene).empty()) throw std::runtime_error("Invalid capture size accepted");
    // Exercise the real asynchronous baker with an emissive room and a closed
    // solid at the central grid cell. No lightmaps or static lights are required.
    for (unsigned i = 0; i < meshes.size(); ++i) {
        auto entity = std::make_unique<scene::Entity>();
        auto *component = entity->AddComponent(new scene::MeshComponent({}));
        materials[i]->SetCastsShadow(true);
        component->SetMesh(meshes[i].get()); component->SetMaterial(materials[i].get()); component->SetStatic(true);
        scene.AddEntity(std::move(entity));
    }
    MeshConfig solid;
    for (unsigned face = 0; face < 6; ++face) {
        const auto right = glm::cross(directions[face],ups[face]);
        const auto base = static_cast<unsigned>(solid.data.vertices.size());
        for (glm::vec2 c : {glm::vec2(-1,-1),glm::vec2(1,-1),glm::vec2(1,1),glm::vec2(-1,1)}) {
            const auto p = (directions[face]+right*c.x+ups[face]*c.y)*.35f;
            const auto n = directions[face];
            solid.data.vertices.push_back({{p.x,p.y,p.z},{n.x,n.y,n.z},{0,0},{1,0,0,1}});
        }
        for (unsigned index : {0u,2u,1u,0u,3u,2u}) solid.data.indices.push_back(base+index);
    }
    Mesh solidMesh(solid);
    Material solidMaterial;
    auto solidEntity = std::make_unique<scene::Entity>();
    auto *solidComponent = solidEntity->AddComponent(new scene::MeshComponent({}));
    solidComponent->SetMesh(&solidMesh); solidComponent->SetMaterial(&solidMaterial); solidComponent->SetStatic(true);
    scene.AddEntity(std::move(solidEntity));
    scene::SceneBakeSettings settings;
    settings.probesOnly = true; settings.bakeIndirectBounce = false;
    settings.probeSpacing = 1; settings.maxProbeCount = 125; settings.probeDirectionCount = 128;
    const auto result = scene::SceneBaker().Bake(scene,settings);
    const auto &grid = scene.GetBakedProbeVolume();
    if (!result.succeeded || !grid.HasDirectionalData() || result.bakedLightmapCount != 0 || grid.positions.size() > 125 ||
        grid.visibilityNormals.size() != grid.positions.size())
        throw std::runtime_error("Ambient-only bake failed, exceeded budget or baked unwanted lightmaps: "+result.message);
    std::size_t closest = 0;
    int valid = 0; float light = 0;
    bool hasVisibilitySurface = false;
    for (std::size_t i = 0; i < grid.positions.size(); ++i) {
        if (glm::length(glm::vec3(grid.positions[i])) < glm::length(glm::vec3(grid.positions[closest]))) closest = i;
        if (grid.positions[i].w > .5f) { ++valid; light += glm::length(grid.sphericalHarmonics[i][0]); }
        for (const auto &normal : grid.visibilityNormals[i])
            hasVisibilitySurface = hasVisibilitySurface || glm::dot(normal,normal) > .9f;
    }
    if (grid.positions[closest].w != 0 || valid == 0 || light <= 0 || !hasVisibilitySurface)
        throw std::runtime_error("Probe placement failed to reject the solid centre or capture emissive room lighting");
    const auto layout = scene::SceneBaker::GenerateReflectionProbeLayout(grid);
    if (layout.size() != 4) throw std::runtime_error("Sparse reflection layout was not generated from valid grid cells");
    // Sunlight must contribute through openings, but never through a sealed
    // room, even when real-time shadows are disabled on that light.
    scene::Scene sealed;
    Material roomMaterial; roomMaterial.SetCastsShadow(false);
    for (const auto &mesh : meshes) {
        auto wall = std::make_unique<scene::Entity>();
        auto *component = wall->AddComponent(new scene::MeshComponent({mesh.get(), &roomMaterial}));
        component->SetStatic(true); sealed.AddEntity(std::move(wall));
    }
    auto sunEntity = std::make_unique<scene::Entity>();
    auto *sun = sunEntity->CreateComponent<scene::LightComponent>();
    sun->GetLight().type = scene::LightType::Directional;
    sun->GetLight().intensity = 5;
    sun->GetLight().castsShadows = false;
    sun->GetLight().isStatic = false;
    sunEntity->SetRotation({-90,0,0});
    sealed.AddEntity(std::move(sunEntity));
    auto roomVolume = std::make_unique<scene::Entity>();
    auto *roomAmbient = roomVolume->CreateComponent<scene::AmbientLightVolumeComponent>();
    roomAmbient->SetSize({3,3,3}); roomAmbient->Configure(1,.02f,64,128);
    sealed.AddEntity(std::move(roomVolume));
    auto sky = std::make_unique<scene::Entity>(); sky->CreateComponent<scene::PhysicalSkyComponent>();
    sealed.AddEntity(std::move(sky));
    if (!scene::SceneBaker().Bake(sealed,settings).succeeded)
        throw std::runtime_error("Sealed room ambient bake failed");
    int sealedValid = 0;
    for (std::size_t i=0; i<roomAmbient->GetBakedVolume().positions.size(); ++i) {
        if (roomAmbient->GetBakedVolume().positions[i].w < .5f) continue;
        ++sealedValid;
        for (const auto &coefficient : roomAmbient->GetBakedVolume().sphericalHarmonics[i])
            if (glm::length(coefficient) > .00001f)
                throw std::runtime_error("Sun or sky leaks into a sealed unlit room");
    }
    if (!sealedValid) throw std::runtime_error("Sealed room has no valid probes");
    // Remove the ceiling: the same dynamic sun now reaches the floor and
    // contributes bounced ambient without any physical sky or emissive source.
    scene::Scene sunlit;
    auto ground = std::make_unique<scene::Entity>();
    auto *groundMesh = ground->AddComponent(new scene::MeshComponent({meshes[6].get(), &solidMaterial}));
    groundMesh->SetStatic(true); sunlit.AddEntity(std::move(ground));
    auto daylight = std::make_unique<scene::Entity>();
    auto *daylightComponent = daylight->CreateComponent<scene::LightComponent>();
    daylightComponent->GetLight().type = scene::LightType::Directional;
    daylightComponent->GetLight().intensity = 5;
    daylightComponent->GetLight().castsShadows = false;
    daylightComponent->GetLight().isStatic = false;
    daylight->SetRotation({-90,0,0});
    sunlit.AddEntity(std::move(daylight));
    if (!scene::SceneBaker().Bake(sunlit,settings).succeeded)
        throw std::runtime_error("Directional bounce bake failed");
    float sunBounce = 0;
    for (const auto &sh : sunlit.GetBakedProbeVolume().sphericalHarmonics) sunBounce += glm::length(sh[0]);
    if (sunBounce <= 0) throw std::runtime_error("Ambient probes discard directional light bounce");
    // A point light inside a bulb mesh that opts out of shadows must still
    // light the surrounding room, matching real-time local-light shadowing.
    scene::Scene lamplit;
    for (const auto &mesh : meshes) {
        auto wall = std::make_unique<scene::Entity>();
        auto *component = wall->AddComponent(new scene::MeshComponent({mesh.get(), &solidMaterial}));
        component->SetStatic(true); lamplit.AddEntity(std::move(wall));
    }
    Material bulbMaterial; bulbMaterial.SetCastsShadow(false);
    auto bulb = std::make_unique<scene::Entity>();
    bulb->AddComponent(new scene::MeshComponent({&solidMesh, &bulbMaterial}))->SetStatic(true);
    auto *lamp = bulb->CreateComponent<scene::LightComponent>();
    lamp->GetLight().type = scene::LightType::Point;
    lamp->GetLight().intensity = 5;
    lamp->GetLight().isStatic = false;
    lamplit.AddEntity(std::move(bulb));
    if (!scene::SceneBaker().Bake(lamplit,settings).succeeded)
        throw std::runtime_error("Point light ambient bake failed");
    float lampBounce = 0;
    const auto &lampGrid = lamplit.GetBakedProbeVolume();
    for (std::size_t i=0; i<lampGrid.positions.size(); ++i)
        if (lampGrid.positions[i].w > .5f) lampBounce += glm::length(lampGrid.sphericalHarmonics[i][0]);
    if (lampBounce <= 0) throw std::runtime_error("Ambient probes discard point light enclosed by a non-shadowing bulb");
    // Bake a component tightly around a 1cm gap. Clearance used to relocate
    // these probes through the opposing surface and invalidate the whole slot.
    MeshConfig slotConfig;
    for (int side=0;side<2;++side) {
        const float z=.5f+side*.01f;
        const unsigned base=static_cast<unsigned>(slotConfig.data.vertices.size());
        const glm::vec3 normal(0,0,side ? -1 : 1);
        for (auto p:{glm::vec3(-.2f,-.2f,z),glm::vec3(.2f,-.2f,z),glm::vec3(.2f,.2f,z),glm::vec3(-.2f,.2f,z)})
            slotConfig.data.vertices.push_back({{p.x,p.y,p.z},{normal.x,normal.y,normal.z},{0,0},{1,0,0,1}});
        if (side) slotConfig.data.indices.insert(slotConfig.data.indices.end(),{base,base+2,base+1,base,base+3,base+2});
        else slotConfig.data.indices.insert(slotConfig.data.indices.end(),{base,base+1,base+2,base,base+2,base+3});
    }
    Mesh slotMesh(slotConfig);
    auto slotGeometry=std::make_unique<scene::Entity>();
    auto *slotComponent=slotGeometry->AddComponent(new scene::MeshComponent({&slotMesh,&solidMaterial}));
    slotComponent->SetStatic(true); scene.AddEntity(std::move(slotGeometry));
    const auto originalRevision=scene.GetAmbientLightVolumes().front()->GetRevision();
    auto slotEntity=std::make_unique<scene::Entity>(); slotEntity->SetPosition({0,0,.505f});
    auto *slot=slotEntity->CreateComponent<scene::AmbientLightVolumeComponent>();
    slot->SetSize({.2f,.2f,.01f}); slot->Configure(.05f,.02f,64,64);
    auto *slotOwner=scene.AddEntity(std::move(slotEntity));
    settings.ambientVolumeEntity=slotOwner->GetID();
    const auto slotResult=scene::SceneBaker().Bake(scene,settings);
    if (!slotResult.succeeded || !slot->HasUsableBake() || scene.GetAmbientLightVolumes().front()->GetRevision()!=originalRevision)
        throw std::runtime_error("Targeted component bake altered another volume or ignored its bounds");
    for (const auto &position:slot->GetBakedVolume().positions)
        if (position.w<.5f || position.z<=.5f || position.z>=.51f)
            throw std::runtime_error("Probe relocation escaped a narrow gap or rejected free space");
    // Grid axes, debug bounds and saved data must follow arbitrary rotations,
    // including a rotated child under a non-uniformly scaled parent.
    auto parent=std::make_unique<scene::Entity>(); parent->SetRotation({17,31,9}); parent->SetScale({2,1,3});
    auto *parentOwner=scene.AddEntity(std::move(parent));
    auto rotated=std::make_unique<scene::Entity>(); rotated->SetPosition({10,20,30}); rotated->SetRotation({23,47,61});
    auto *rotatedVolume=rotated->CreateComponent<scene::AmbientLightVolumeComponent>();
    rotatedVolume->SetSize({4,2,1}); rotatedVolume->Configure(1,.001f,64,64);
    auto *rotatedOwner=scene.AddEntity(std::move(rotated)); rotatedOwner->SetParent(parentOwner);
    const auto expectedLayout=rotatedVolume->GetGridLayout();
    for (auto uvw:{glm::vec3(0),glm::vec3(1),glm::vec3(.2f,.4f,.7f)}) {
        const auto expected=glm::vec3(rotatedVolume->GetVolumeTransform()*glm::vec4(uvw-glm::vec3(.5f),1));
        if (glm::length(expectedLayout.GridToWorld(uvw)-expected)>.0001f ||
            glm::length(expectedLayout.WorldToGrid(expected)-uvw)>.0001f)
            throw std::runtime_error("Rotated/parented grid disagrees with the volume's displayed box");
    }
    settings.ambientVolumeEntity=rotatedOwner->GetID();
    const auto rotatedResult=scene::SceneBaker().Bake(scene,settings);
    if (!rotatedResult.succeeded || !rotatedVolume->HasUsableBake())
        throw std::runtime_error("Rotated component bake is unusable");
    const auto &rotatedGrid=rotatedVolume->GetBakedVolume();
    for (std::size_t i=0;i<rotatedGrid.positions.size();++i) {
        const auto r=rotatedGrid.resolution;
        const auto uvw=(glm::vec3(i%r.x,i/r.x%r.y,i/(r.x*r.y))+.5f)/glm::vec3(r);
        if (glm::length(glm::vec3(rotatedGrid.positions[i])-expectedLayout.GridToWorld(uvw))>.0002f)
            throw std::runtime_error("Baked probes ignore the oriented volume transform");
    }
    for (const auto &capture : scene::SceneBaker::GenerateReflectionProbeLayout(rotatedGrid))
        for (int corner=0; corner<8; ++corner) {
            const glm::vec3 sign(corner&1 ? 1.0f : -1.0f,corner&2 ? 1.0f : -1.0f,corner&4 ? 1.0f : -1.0f);
            const auto uvw=rotatedGrid.WorldToGrid(capture.position+capture.size*.5f*sign);
            if (glm::any(glm::lessThan(uvw,glm::vec3(-.0001f))) || glm::any(glm::greaterThan(uvw,glm::vec3(1.0001f))))
                throw std::runtime_error("Reflection box escapes a rotated or sheared ambient volume");
        }
    scene::AmbientLightVolumeComponent restored; restored.Deserialize(rotatedVolume->Serialize());
    if (glm::length(restored.GetBakedVolume().GridToWorld({.2f,.4f,.7f})-expectedLayout.GridToWorld({.2f,.4f,.7f}))>.0002f)
        throw std::runtime_error("Ambient volume persistence discards grid orientation");
    rotatedOwner->SetRotation({23,48,61});
    if (rotatedVolume->HasUsableBake()) throw std::runtime_error("Rotation reuses stale baked lighting");
    settings.ambientVolumeEntity=0;
    scene::Scene outdoor;
    auto floorEntity = std::make_unique<scene::Entity>();
    // The lower room face supplies upward-facing static geometry with no emission.
    auto *floor = floorEntity->AddComponent(new scene::MeshComponent({meshes[6].get(),&solidMaterial}));
    floor->SetStatic(true);
    outdoor.AddEntity(std::move(floorEntity));
    auto skyEntity = std::make_unique<scene::Entity>();
    skyEntity->AddComponent(new scene::PhysicalSkyComponent());
    outdoor.AddEntity(std::move(skyEntity));
    const auto skyResult = scene::SceneBaker().Bake(outdoor,settings);
    float skyLight = 0;
    for (const auto &sh : outdoor.GetBakedProbeVolume().sphericalHarmonics) skyLight += glm::length(sh[0]);
    if (!skyResult.succeeded || skyLight <= 0) throw std::runtime_error("Unoccluded diffuse probes discard physical sky radiance");
    // A per-volume direction override must sample exactly the same sky as
    // the equivalent global setting, rather than reusing another quadrature.
    auto *skyVolume=outdoor.GetAmbientLightVolumes().front();
    skyVolume->Configure(1,.02f,64,128);
    auto overridden=settings; overridden.probeDirectionCount=64;
    if (!scene::SceneBaker().Bake(outdoor,overridden).succeeded)
        throw std::runtime_error("Cannot bake per-volume sky quadrature");
    const auto overrideSh=skyVolume->GetBakedVolume().sphericalHarmonics;
    overridden.probeDirectionCount=128;
    if (!scene::SceneBaker().Bake(outdoor,overridden).succeeded)
        throw std::runtime_error("Cannot bake matching sky quadrature");
    const auto &matchingSh=skyVolume->GetBakedVolume().sphericalHarmonics;
    if (overrideSh.size()!=matchingSh.size()) throw std::runtime_error("Sky quadrature changes the grid");
    for (std::size_t i=0;i<overrideSh.size();++i) for (int c=0;c<9;++c)
        if (glm::length(overrideSh[i][c]-matchingSh[i][c])>.00001f)
            throw std::runtime_error("Per-volume sky sampling uses radiance from incorrect directions");
    scene::Scene previewScene;
    scene::BakedProbeVolume previewGrid;
    previewGrid.origin = {-1,-1,-1}; previewGrid.size = {2,2,2}; previewGrid.resolution = {1,1,1};
    previewGrid.irradiance = {{0,3,0}}; previewGrid.positions = {{0,0,0,1}};
    previewGrid.sphericalHarmonics.resize(1); previewGrid.sphericalHarmonics[0][0] = glm::vec3(0,3,0)/.282095f;
    previewGrid.visibility.resize(1); previewGrid.visibility[0].fill(10);
    previewScene.SetBakedProbeVolume(previewGrid);
    auto &debug = ui::GetAmbientProbeDebugSettings();
    debug.diffuse = true; debug.diffuseRadius = .5f;
    service.SetAmbientProbeDebugEnabled(true);
    CameraData previewCamera;
    previewCamera.view = glm::lookAt(glm::vec3(0,0,5),glm::vec3(0),glm::vec3(0,1,0));
    previewCamera.projection = api == rhi::GraphicsApi::Vulkan
        ? glm::perspectiveRH_ZO(glm::radians(45.0f),1.0f,.1f,100.0f)
        : glm::perspectiveRH_NO(glm::radians(45.0f),1.0f,.1f,100.0f);
    previewCamera.renderRuntimeUI = false;
    std::vector<RenderCommand> previewCommands;
    const auto sphereFrame = [&] {
        if (!service.Render(64,64,previewCamera,previewCommands,{},{},&previewScene,PostProcessDebugView::None))
            throw std::runtime_error(service.GetLastRenderError());
        std::vector<std::byte> image;
        if (api == rhi::GraphicsApi::Vulkan) {
            image = static_cast<rhi::vulkan::VulkanDevice *>(engine.GetRenderDevice())->ReadTextureRgba8(service.GetViewportTexture());
        } else {
            image.resize(64*64*4);
            const auto texture = static_cast<rhi::opengl::OpenGLDevice *>(engine.GetRenderDevice())->GetTextureNativeHandle(service.GetViewportTexture());
            GLint previous = 0;
            glGetIntegerv(GL_TEXTURE_BINDING_2D,&previous);
            glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(texture));
            glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,image.data());
            glBindTexture(GL_TEXTURE_2D,previous);
        }
        const auto at = (32*64+32)*4;
        return glm::vec3(std::to_integer<unsigned>(image.at(at)),std::to_integer<unsigned>(image.at(at+1)),std::to_integer<unsigned>(image.at(at+2)))/255.0f;
    };
    service.SetAmbientProbeDebugEnabled(false);
    const auto background = sphereFrame();
    service.SetAmbientProbeDebugEnabled(true);
    const auto sphere = sphereFrame();
    if (sphere.g < .8f || sphere.r > .02f) throw std::runtime_error("Scene viewport did not generate the shaded diffuse sphere");
    debug.diffuse = false;
    if (glm::length(sphereFrame()-background) > .02f) throw std::runtime_error("Disabled probe spheres remain in the viewport");
    scene::IblCaptureComponent previewCapture;
    previewCapture.SetResolution(32);
    std::vector<float> blueFaces(32*32*24,0);
    for (std::size_t i=0; i<blueFaces.size(); i+=4) {
        blueFaces[i+2]=(i/(32*32*4)==4) ? 0.8f : 0.0f; blueFaces[i+3]=1;
    }
    if (!previewCapture.SetCapturePixels(blueFaces)) throw std::runtime_error("Cannot prepare reflection preview capture");
    auto previewVolume = previewCapture.BuildCaptureVolume();
    previewVolume.origin = {-1,-1,-1}; previewVolume.size = {2,2,2};
    previewScene.AddIblCaptureVolume(previewVolume);
    debug.reflections = true;
    const auto chrome = sphereFrame();
    if (chrome.b < .7f || chrome.r > .02f || chrome.g > .02f) throw std::runtime_error("Viewport chrome sphere does not show the capture's radiance");
    debug.reflectionRoughness = 1;
    const auto roughReflection = sphereFrame();
    if (roughReflection.b >= chrome.b-.1f || roughReflection.b < .1f)
        throw std::runtime_error("Rough IBL reflection remains sharp or loses convolved radiance");
    debug.reflections = false;
    debug.diffuse = true;
    service.SetAmbientProbeDebugEnabled(false);
    if (glm::length(sphereFrame()-background) > .02f) throw std::runtime_error("Probe overlays escaped into a viewport without debug enabled");
    std::unique_ptr<Mesh> materialSphere(Mesh::Sphere(24,32));
    Material reflectiveMaterial;
    reflectiveMaterial.SetMetallic(1);
    reflectiveMaterial.SetRoughness(0);
    RenderCommand materialCommand;
    materialCommand.mesh=materialSphere.get(); materialCommand.material=&reflectiveMaterial;
    previewCommands.push_back(materialCommand);
    const auto smoothMaterial=sphereFrame();
    reflectiveMaterial.SetRoughness(1);
    const auto roughMaterial=sphereFrame();
    if (roughMaterial.b >= smoothMaterial.b-.1f || roughMaterial.b < .05f)
        throw std::runtime_error("Material IBL ignores GGX roughness or its integrated specular response");
    debug = {};
    std::cout << "PASS: six linear HDR IBL faces captured through RHI\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
