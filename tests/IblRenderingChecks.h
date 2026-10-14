#pragma once
#include "PlutoGE/render/RhiSceneRenderer.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/RenderCommand.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/SceneBaker.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include "PlutoGE/scene/components/AmbientLightVolumeComponent.h"
#include "PlutoGE/scene/Entity.h"
#include <cmath>
#include <memory>
#include <stdexcept>

inline void CheckIblRendering(PlutoGE::render::rhi::IRenderDevice &device,
                             PlutoGE::render::BasicRendererShaderPackage shaders,
                             const PlutoGE::render::BasicRendererShaderPackage *referenceShaders = nullptr)
{
    using namespace PlutoGE;
    using namespace render;
    const auto require = [](bool ok, const char *message) { if (!ok) throw std::runtime_error(message); };
    constexpr int resolution = 32;
    const auto faces = [](glm::vec3 color) {
        std::vector<float> pixels(resolution * resolution * 24);
        for (std::size_t i = 0; i < pixels.size(); i += 4) {
            pixels[i] = color.r; pixels[i+1] = color.g; pixels[i+2] = color.b; pixels[i+3] = 1;
        }
        return pixels;
    };
    scene::IblCaptureComponent component;
    component.SetAutomaticallyPlaced(true);
    component.SetResolution(resolution);
    require(component.SetCapturePixels(faces({2,0,0})), "Cannot retain HDR capture");
    scene::IblCaptureComponent restored;
    restored.Deserialize(component.Serialize());
    require(restored.IsAutomaticallyPlaced(), "Generated reflection ownership is not persisted");
    require(restored.GetCaptureTexture() && restored.GetCaptureTexture()->GetTextureID() == 0 &&
        restored.GetCaptureTexture()->GetCubemapPixels()[0] == 2, "Saved HDR capture requires OpenGL or clips highlights");
    require(!restored.SetCapturePixels({}), "Invalid capture replaced the previous result");
    scene::Scene scene;
    auto volume = restored.BuildCaptureVolume();
    volume.origin = {-2,-2,-2}; volume.size = {4,4,4}; volume.blendDistance = 0;
    scene.AddIblCaptureVolume(volume);
    Material material({.castsShadow=false, .twoSided=true});
    MeshConfig config;
    config.data.vertices = {{{-1,-1,.5f},{0,0,1},{0,0},{1,0,0,1}},
        {{3,-1,.5f},{0,0,1},{0,0},{1,0,0,1}}, {{-1,3,.5f},{0,0,1},{0,0},{1,0,0,1}}};
    config.data.indices = {0,1,2};
    Mesh mesh(config);
    RenderCommand command; command.mesh = &mesh; command.material = &material; command.castsShadow = false;
    command.worldBounds = command.previousWorldBounds = mesh.GetBounds();
    shaders.virtualShadows = {};
    RhiSceneRenderer renderer;
    require(renderer.Initialize(device, shaders), "Cannot initialize IBL scene renderer");
    std::unique_ptr<RhiSceneRenderer> reference;
    if (referenceShaders)
    {
        auto package = *referenceShaders;
        package.virtualShadows = {};
        reference = std::make_unique<RhiSceneRenderer>();
        require(reference->Initialize(device, package), "Cannot initialize reference IBL renderer");
    }
    BasicLighting lighting; lighting.shadowsEnabled = false; lighting.ambientIntensity = lighting.directionalIntensity = 0;
    CameraData camera{glm::mat4(1),glm::mat4(1)};
    const auto frame = [&] {
        require(renderer.Render(32,32,camera,lighting,std::span(&command,1),{},{},{},{},
            PostProcessDebugView::None,true,&scene,std::nullopt,{},true), "IBL scene render failed");
        const auto image = device.ReadTextureRgbaFloat(renderer.GetColorTexture());
        require(image.size() == 32*32*4, "HDR readback dimensions differ");
        if (reference)
        {
            require(reference->Render(32,32,camera,lighting,std::span(&command,1),{},{},{},{},
                PostProcessDebugView::None,true,&scene,std::nullopt,{},true), "Reference IBL render failed");
            const auto expected = device.ReadTextureRgbaFloat(reference->GetColorTexture());
            require(expected.size() == image.size(), "Reference IBL dimensions differ");
            for (std::size_t i = 0; i < image.size(); ++i)
                require(std::isfinite(image[i]) && std::abs(image[i] - expected[i]) <= .001f + std::abs(expected[i]) * .0001f,
                        "Optimized ambient lighting differs from the reference image");
        }
        const auto center = (16*32+16)*4;
        return glm::vec3(image[center],image[center+1],image[center+2]);
    };
    const auto red = frame();
    require(red.r > 1.5f && red.g < .01f && red.b < .01f, "Diffuse IBL is absent, miscoloured, or HDR readback clips values");
    require(glm::length(frame() - red) < .01f, "Stable IBL changes across frames");
    auto *texture = restored.GetCaptureTexture();
    require(texture->SetCubemapPixels(faces({0,0,2})), "Cannot update HDR faces");
    const auto blue = frame();
    require(blue.b > 1.5f && blue.r < .01f, "IBL upload ignores content revision");
    std::unique_ptr<Texture> redTexture(Texture::CpuColorCubemap(resolution, faces({2,0,0})));
    auto second = volume; second.environmentMapTexture = redTexture.get();
    scene.AddIblCaptureVolume(second);
    const auto overlap = frame();
    require(std::abs(overlap.r - overlap.b) < .03f && overlap.r > .7f && overlap.r < 1.2f,
        "Overlapping IBL captures are not normalized");
    scene.ClearIblCaptureVolumes();
    volume.origin = {4,4,4}; scene.AddIblCaptureVolume(volume);
    require(glm::length(frame()) < .01f, "IBL leaks beyond volume bounds");
    scene.ClearIblCaptureVolumes();
    volume.origin = {-2,-2,-2}; volume.blendDistance = 4; scene.AddIblCaptureVolume(volume);
    const auto fade = frame();
    require(fade.b > .4f && fade.b < blue.b * .6f, "IBL does not fade at volume edges");
    volume.blendDistance = 0; volume.intensity = .25f;
    scene.ClearIblCaptureVolumes(); scene.AddIblCaptureVolume(volume);
    require(std::abs(frame().b - blue.b * .25f) < .03f, "IBL intensity does not scale radiance");
    // A smooth metal must use the reflected face, rather than diffuse SH or a global sky.
    auto directionalFaces = faces({2,0,0});
    for (std::size_t i = 5 * resolution * resolution * 4; i < directionalFaces.size(); i += 4)
    {
        directionalFaces[i] = 0; directionalFaces[i+2] = 2;
    }
    texture->SetCubemapPixels(directionalFaces);
    volume.intensity = 1;
    scene.ClearIblCaptureVolumes(); scene.AddIblCaptureVolume(volume);
    material.SetMetallic(1); material.SetRoughness(.02f);
    const auto reflection = frame();
    if (!(reflection.b > 1.8f && reflection.r < .01f))
        throw std::runtime_error("Specular IBL samples the wrong cubemap face: " + std::to_string(reflection.r) + "," + std::to_string(reflection.g) + "," + std::to_string(reflection.b));
    command.probeDebug = {2,0};
    lighting.directionalIntensity = 100;
    const auto preview = frame();
    require(preview.b > 1.95f && preview.r < .01f, "Reflection preview adds scene lights or ignores its capture index");
    command.probeDebug = {3,0};
    const auto rejected = frame();
    require(rejected.r > .95f && rejected.g < .04f, "Rejected probe preview is not red or its cached mode is stale");
    command.probeDebug = {0,0};
    lighting.directionalIntensity = 0;
    material.SetRoughness(1);
    const auto roughReflection = frame();
    // GGX convolution deliberately mixes cubemap faces at high roughness.
    require(roughReflection.r > reflection.r + .1f && roughReflection.b < reflection.b,
            "Rough IBL convolution does not broaden the reflected lobe");
    material.SetRoughness(.02f);
    material.SetAlphaMode(AlphaMode::Blend);
    require(frame().b > .9f, "Transparent surfaces lost local IBL");
    material.SetAlphaMode(AlphaMode::Opaque);
    // Non-power-of-two authored captures must remain aligned through the mip chain.
    std::vector<float> oddPixels(33 * 33 * 24, 0);
    for (std::size_t i = 0; i < oddPixels.size(); i += 4) { oddPixels[i+2] = 2; oddPixels[i+3] = 1; }
    std::unique_ptr<Texture> odd(Texture::CpuColorCubemap(33, oddPixels));
    volume.environmentMapTexture = odd.get();
    scene.ClearIblCaptureVolumes(); scene.AddIblCaptureVolume(volume);
    require(frame().b > 1.0f, "Non-power-of-two IBL upload failed");
    lighting.localIblEnabled = false;
    require(glm::length(frame()) < .01f, "Capture pass includes local IBL feedback");
    lighting.localIblEnabled = true;
    scene.ClearIblCaptureVolumes();
    material.SetMetallic(0);
    material.SetRoughness(1);
    scene::BakedProbeVolume grid;
    grid.origin = {-2,-2,-2}; grid.size = {4,4,4}; grid.resolution = {2,2,2};
    grid.irradiance.assign(8, glm::vec3(0,3,0));
    grid.positions.resize(8);
    grid.sphericalHarmonics.resize(8);
    grid.visibility.resize(8);
    for (int i = 0; i < 8; ++i) {
        grid.positions[i] = glm::vec4(-1+2*(i%2),-1+2*(i/2%2),-1+2*(i/4),1);
        grid.sphericalHarmonics[i][0] = glm::vec3(0,3,0)/0.282095f;
        grid.visibility[i].fill(10);
    }
    scene.SetBakedProbeVolume(grid);
    const auto green = frame();
    require(green.g > .8f && green.r < .01f && green.b < .01f, "RHI does not sample the diffuse probe grid");
    material.SetMetallic(1);
    require(glm::length(frame()) < .01f, "A pure metal receives diffuse probe irradiance");
    material.SetMetallic(0);
    // The receiver lies inside a long rotated grid, then outside its narrow
    // axis while still inside its world AABB. Lookup must use grid coordinates.
    auto *rotatedComponent=scene.GetAmbientLightVolumes().front();
    auto *rotatedOwner=rotatedComponent->GetOwner();
    rotatedOwner->SetRotation({0,0,45}); rotatedComponent->SetSize({4,.5f,4});
    auto oriented=grid;
    const auto orientedLayout=rotatedComponent->GetGridLayout();
    oriented.origin=orientedLayout.origin; oriented.size=orientedLayout.size; oriented.gridBasis=orientedLayout.gridBasis;
    for (int i=0;i<8;++i) oriented.positions[i]=glm::vec4(oriented.GridToWorld((glm::vec3(i%2,i/2%2,i/4)+.5f)/2.0f),1);
    scene.SetBakedProbeVolume(oriented);
    require(frame().g>.8f,"Rotated diffuse volume does not light its interior");
    rotatedOwner->SetPosition({.9f,-.9f,0});
    const auto shiftedLayout=rotatedComponent->GetGridLayout();
    oriented.origin=shiftedLayout.origin;
    for (auto &position:oriented.positions) position+=glm::vec4(.9f,-.9f,0,0);
    rotatedComponent->SetBakedVolume(oriented);
    require(glm::length(frame())<.01f,"Rotated diffuse volume lights points outside its oriented box");
    rotatedOwner->SetRotation({0,0,0}); rotatedOwner->SetPosition({0,0,0});
    scene.SetBakedProbeVolume(grid);
    command.probeDebug = {1,0};
    const auto diffusePreview = frame();
    require(std::abs(diffusePreview.g - 3.0f/3.14159265f) < .02f, "Diffuse preview does not evaluate the selected probe's stored SH");
    command.probeDebug = {0,0};
    // A planar receiver must stay lit between the coarse octahedral rays.
    // The old minimum-of-four depths carved repeating dark lobes into this plane.
    const auto constantGrid = grid;
    grid.visibilityNormals.resize(8);
    for (auto &position : grid.positions) position.w = 0;
    grid.positions[7] = glm::vec4(0,0,1,1);
    const auto planeDepths = [&](float planeZ) {
        for (int y = 0; y < 8; ++y) for (int x = 0; x < 8; ++x) {
            const glm::vec2 oct = (glm::vec2(x,y) + .5f) / 4.0f - 1.0f;
            glm::vec3 direction(oct, 1 - std::abs(oct.x) - std::abs(oct.y));
            if (direction.z < 0) {
                direction.x = (1 - std::abs(oct.y)) * (oct.x >= 0 ? 1 : -1);
                direction.y = (1 - std::abs(oct.x)) * (oct.y >= 0 ? 1 : -1);
            }
            direction = glm::normalize(direction);
            grid.visibility[7][x+y*8] = direction.z < 0 ? (planeZ-1)/direction.z : 100;
            grid.visibilityNormals[7][x+y*8] = direction.z < 0 ? glm::vec3(0,0,1) : glm::vec3(0);
        }
        scene.SetBakedProbeVolume(grid);
    };
    for (const auto offset : {glm::vec2(0), glm::vec2(.7f,-.7f), glm::vec2(-.7f,.7f)}) {
        grid.positions[7] = glm::vec4(offset,1,1);
        planeDepths(.5f);
        frame();
        const auto planeImage = device.ReadTextureRgbaFloat(renderer.GetColorTexture());
        for (int y = 1; y < 31; ++y) for (int x = 1; x < 31; ++x)
            if (std::abs(planeImage[(y*32+x)*4+1] - green.g) >= .02f)
                throw std::runtime_error("Coarse probe visibility imprints a pattern on an unobstructed plane: " +
                    std::to_string(x) + "," + std::to_string(y) + " offset " + std::to_string(offset.x) + "," +
                    std::to_string(offset.y) + " green " + std::to_string(planeImage[(y*32+x)*4+1]) +
                    " expected " + std::to_string(green.g));
    }
    grid.positions[7] = glm::vec4(0,0,1,1);
    planeDepths(.8f);
    require(glm::length(frame()) < .01f, "Reconstructed probe visibility leaks through a planar wall");
    // A centimetre-wide slot must not be crossed by a bias derived from a 2m cell.
    grid.positions[7] = glm::vec4(0,0,.505f,1);
    for (int y=0;y<8;++y) for (int x=0;x<8;++x) {
        const glm::vec2 oct=(glm::vec2(x,y)+.5f)/4.0f-1.0f;
        glm::vec3 direction(oct,1-std::abs(oct.x)-std::abs(oct.y));
        if (direction.z<0) {
            direction.x=(1-std::abs(oct.y))*(oct.x>=0 ? 1 : -1);
            direction.y=(1-std::abs(oct.x))*(oct.y>=0 ? 1 : -1);
        }
        direction=glm::normalize(direction);
        grid.visibility[7][x+y*8]=std::abs(direction.z)>1e-6f ? .005f/std::abs(direction.z) : 100;
        grid.visibilityNormals[7][x+y*8]=std::abs(direction.z)>1e-6f ? glm::vec3(0,0,direction.z<0 ? 1 : -1) : glm::vec3(0);
    }
    scene.SetBakedProbeVolume(grid);
    require(frame().g>.8f,"Ambient probe bias or extended hit planes black out a narrow slot");
    // Samples on the other side of a thin receiver wall used to contribute
    // their minimum normal weight even though they cannot light its front.
    grid=constantGrid;
    for (auto &position:grid.positions) position.z=.49f;
    scene.SetBakedProbeVolume(grid);
    require(glm::length(frame())<.01f,"Ambient probes behind a receiver illuminate through its back face");
    grid = constantGrid;
    // Revision updates must replace already resident SH/visibility, not reuse stale data.
    for (auto &depth : grid.visibility) depth.fill(.01f);
    scene.SetBakedProbeVolume(grid);
    require(glm::length(frame()) < .01f, "Diffuse probes leak through their visibility walls");
    for (auto &depth : grid.visibility) depth.fill(10);
    for (auto &position : grid.positions) position.w = 0;
    scene.SetBakedProbeVolume(grid);
    require(glm::length(frame()) < .01f, "Invalid solid-cell probes contribute light");
    require(scene::SceneBaker::GenerateReflectionProbeLayout(grid).empty(), "Automatic reflections use solid cells");
    for (auto &position : grid.positions) position.w = 1;
    for (auto &sh : grid.sphericalHarmonics) {
        sh[0] = glm::vec3(0,1,0)/0.282095f;
        sh[2] = glm::vec3(0,1,0)/0.488603f;
    }
    scene.SetBakedProbeVolume(grid);
    require(frame().g < green.g*.85f && frame().g > green.g*.5f, "Probe SH ignores the surface normal");
    const auto layout = scene::SceneBaker::GenerateReflectionProbeLayout(grid,100);
    require(layout.size() == 4, "Automatic reflection placement exceeds the RHI capture budget");
    for (const auto &capture : layout)
        require(glm::all(glm::greaterThan(capture.size,glm::vec3(0))) &&
            glm::all(glm::greaterThanEqual(capture.position-capture.size*.5f,grid.origin)) &&
            glm::all(glm::lessThanEqual(capture.position+capture.size*.5f,grid.origin+grid.size)), "Generated reflection leaves the grid bounds");
    std::string serialized, error;
    grid.visibilityNormals.resize(8);
    grid.visibilityNormals[0][63] = glm::vec3(0,0,1);
    scene.SetBakedProbeVolume(grid);
    require(scene::SceneSerializer::SaveToString(scene, serialized, &error), "Cannot serialize directional probes");
    auto loaded = scene::SceneSerializer::LoadFromString(serialized,&error);
    require(loaded && loaded->GetBakedProbeVolume().HasDirectionalData() &&
        glm::length(loaded->GetBakedProbeVolume().sphericalHarmonics[0][2]-grid.sphericalHarmonics[0][2]) < .001f &&
        loaded->GetBakedProbeVolume().visibility[0][63] == 10 &&
        loaded->GetBakedProbeVolume().visibilityNormals.size() == 8 &&
        loaded->GetBakedProbeVolume().visibilityNormals[0][63] == glm::vec3(0,0,1), "Directional probe persistence loses SH or visibility");
    require(loaded->GetAmbientLightVolumes().size()==1 && serialized.find("PROBE_DIRECTIONAL\t")==std::string::npos,
        "Baked lighting is not owned exclusively by an entity component");
    scene::Scene emptyScene;
    std::string legacy;
    require(scene::SceneSerializer::SaveToString(emptyScene,legacy,&error),"Cannot prepare legacy scene");
    legacy+="\nPROBE\t-1,-1,-1\t2,2,2\t1,1,1\nPROBE_SAMPLE\t0,3,0\n";
    auto migrated=scene::SceneSerializer::LoadFromString(legacy,&error);
    require(migrated && migrated->GetAmbientLightVolumes().size()==1 && migrated->HasBakedProbeVolume(),
        "Legacy scene-level lighting was not migrated to an ambient component");
    auto *componentVolume=scene.GetAmbientLightVolumes().front();
    const auto revision=scene.GetBakedProbeRevision();
    componentVolume->SetEnabled(false);
    require(!scene.HasBakedProbeVolume() && glm::length(frame())<.01f && scene.GetBakedProbeRevision()!=revision,
        "Disabling the ambient component leaves its baked lighting resident");
    componentVolume->SetEnabled(true);
    componentVolume->GetOwner()->SetPosition({10,0,0});
    require(!scene.HasBakedProbeVolume() && glm::length(frame())<.01f,"Moving a baked volume silently reuses stale geometry lighting");
    componentVolume->GetOwner()->SetPosition(grid.origin+grid.size*.5f);
    // A detailed volume wins in overlapping space and removal restores the coarse volume.
    auto detailEntity=std::make_unique<scene::Entity>();
    auto *detail=detailEntity->CreateComponent<scene::AmbientLightVolumeComponent>();
    auto detailGrid=constantGrid; detailGrid.origin={-1,-1,-1}; detailGrid.size={2,2,2};
    for (int i=0;i<8;++i) {
        detailGrid.positions[i]=glm::vec4(-.5f+(i%2),-.5f+(i/2%2),-.5f+(i/4),1);
        detailGrid.sphericalHarmonics[i][0]=glm::vec3(3,0,0)/.282095f;
    }
    detail->SetSize(detailGrid.size); detail->SetBakedVolume(detailGrid);
    auto *detailOwner=scene.AddEntity(std::move(detailEntity));
    const auto detailLight=frame();
    require(detailLight.r>.8f && detailLight.g<.01f,"Overlapping ambient volumes ignore the denser component");
    command.probeDebug={1,8};
    const auto detailPreview=frame();
    require(std::abs(detailPreview.r-3.0f/3.14159265f)<.02f && detailPreview.g<.01f,
        "Debug spheres use the wrong volume's atlas indices");
    command.probeDebug={0,0};
    require(scene::SceneSerializer::SaveToString(scene,serialized,&error),"Cannot save multiple ambient components");
    loaded=scene::SceneSerializer::LoadFromString(serialized,&error);
    require(loaded && loaded->GetBakedProbeVolumes().size()==2,"Multiple ambient component bakes do not survive loading");
    scene.RemoveEntity(detailOwner);
    require(frame().g>.1f && frame().r<.01f,"Removing an ambient component leaves stale atlas indices");
    lighting.localIblEnabled = false;
    require(glm::length(frame()) < .01f, "Capture pass includes diffuse grid feedback");
    lighting.localIblEnabled = true;
    scene.ClearBakedProbeVolume();
    require(glm::length(frame()) < .01f, "Cleared grid remains resident in RHI lighting");
    renderer.Shutdown();
}
