#pragma once
#include "PlutoGE/render/RhiSceneRenderer.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Texture.h"
#include "PlutoGE/render/RenderCommand.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/components/IblCaptureComponent.h"
#include <cmath>
#include <stdexcept>

inline void CheckIblRendering(PlutoGE::render::rhi::IRenderDevice &device,
                             PlutoGE::render::BasicRendererShaderPackage shaders)
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
    component.SetResolution(resolution);
    require(component.SetCapturePixels(faces({2,0,0})), "Cannot retain HDR capture");
    scene::IblCaptureComponent restored;
    restored.Deserialize(component.Serialize());
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
    BasicLighting lighting; lighting.shadowsEnabled = false; lighting.ambientIntensity = lighting.directionalIntensity = 0;
    CameraData camera{glm::mat4(1),glm::mat4(1)};
    const auto frame = [&] {
        require(renderer.Render(32,32,camera,lighting,std::span(&command,1),{},{},{},{},
            PostProcessDebugView::None,true,&scene,std::nullopt,{},true), "IBL scene render failed");
        const auto image = device.ReadTextureRgbaFloat(renderer.GetColorTexture());
        require(image.size() == 32*32*4, "HDR readback dimensions differ");
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
    material.SetRoughness(1);
    require(frame().r < .01f, "Rough IBL mips leak neighbouring atlas faces");
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
    renderer.Shutdown();
}
