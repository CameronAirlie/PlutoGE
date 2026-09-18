#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <array>
#include <stdexcept>

// Distinct UV sets and a dark albedo catch lost UV1 data and accidental albedo tinting.
template<class ReadPixels>
void CheckEmissionTexture(PlutoGE::render::BasicRenderer &renderer,
                          PlutoGE::render::rhi::IRenderDevice &device, ReadPixels readPixels)
{
    using namespace PlutoGE::render;
    std::array<BasicVertex,4> vertices{};
    const std::array<std::array<float,3>,4> positions{{{-.8f,-.8f,.5f},{.8f,-.8f,.5f},{.8f,.8f,.5f},{-.8f,.8f,.5f}}};
    for (unsigned i=0;i<4;++i) {
        vertices[i].position=positions[i]; vertices[i].normal={0,0,1};
        vertices[i].uv={.25f,.5f}; vertices[i].uv2={.75f,.5f};
    }
    constexpr std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    auto mesh=renderer.CreateMesh({vertices,indices});
    const std::array<std::byte,8> pixels{std::byte{0},std::byte{0},std::byte{0},std::byte{255},
        std::byte{128},std::byte{255},std::byte{0},std::byte{0}};
    rhi::Texture emission(device,device.CreateTexture({2,1,rhi::Format::R8G8B8A8Srgb,
        rhi::TextureUsage::Sampled,"Emission mask",false},pixels));
    const std::array<std::byte,4> black{std::byte{0},std::byte{0},std::byte{0},std::byte{255}};
    rhi::Texture albedo(device,device.CreateTexture({1,1,rhi::Format::R8G8B8A8Srgb,
        rhi::TextureUsage::Sampled,"Dark albedo",false},black));
    BasicLighting lighting;
    lighting.cameraPosition={0,0,2}; lighting.ambientIntensity=lighting.directionalIntensity=0;
    BasicDraw draw; draw.mesh=&mesh; draw.emission={1,1,1}; draw.twoSided=true;
    draw.emissionTexture=emission.Get(); draw.baseColorTexture=albedo.Get();
    const auto sample=[&] {
        renderer.Render(glm::mat4(1),lighting,std::span(&draw,1));
        const auto image=readPixels(renderer.GetColorTexture());
        const auto at=(renderer.GetHeight()/2*renderer.GetWidth()+renderer.GetWidth()/2)*4;
        return std::array<int,3>{int(image[at]),int(image[at+1]),int(image[at+2])};
    };
    const auto require=[](bool condition,const char *message){if(!condition)throw std::runtime_error(message);};
    for (auto mode : {0u,1u,2u}) {
        draw.alphaMode=mode;
        draw.emissionTexCoord=0;
        auto dark=sample();
        require(dark[0]<3&&dark[1]<3&&dark[2]<3,"Black emissive texels glow");
        draw.emissionTexCoord=1;
        auto lit=sample();
        require(lit[0]>=53&&lit[0]<=57&&lit[1]>250&&lit[2]<3,
            "Emission map lost UV1, sRGB decoding, or was multiplied by albedo/alpha");
        draw.instanceModels=std::make_shared<std::vector<glm::mat4>>(1,glm::mat4(1));
        require(sample()==lit,"Instanced emission lost UV1");
        draw.instanceModels.reset();
        draw.emission={.5f,.5f,.5f};
        auto dim=sample();
        require(dim[0]>=26&&dim[0]<=29&&dim[1]>=126&&dim[1]<=129,"Emission factor not applied");
        draw.emission={1,1,1};
    }
}
