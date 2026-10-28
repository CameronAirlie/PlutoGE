#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

// Shared GPU checks exercise the actual multipass path on both backends.
inline void CheckSsaoRendering(PlutoGE::render::BasicRenderer &renderer,
                              PlutoGE::render::rhi::IRenderDevice &device)
{
    using namespace PlutoGE::render;
    const auto require = [](bool value,const char *message) { if (!value) throw std::runtime_error(message); };
    constexpr std::array<BasicVertex,8> vertices{{
        {{-2,0,0},{0,1,0},{0,0}},{{2,0,0},{0,1,0},{1,0}},
        {{2,0,3},{0,1,0},{1,1}},{{-2,0,3},{0,1,0},{0,1}},
        {{-2,0,0},{0,0,1},{0,0}},{{2,0,0},{0,0,1},{1,0}},
        {{2,3,0},{0,0,1},{1,1}},{{-2,3,0},{0,0,1},{0,1}}}};
    constexpr std::array<std::uint32_t,12> indices{0,2,1,0,3,2,4,5,6,4,6,7};
    auto corner = renderer.CreateMesh({vertices,indices});
    auto floor = renderer.CreateMesh({std::span(vertices).first(4),std::span(indices).first(6)});
    // A subpixel-width upright occluder exercises silhouettes and disocclusion.
    constexpr std::array<BasicVertex,4> postVertices{{
        {{-.025f,0,1},{0,0,1},{0,0}},{{.025f,0,1},{0,0,1},{1,0}},
        {{.025f,1.2f,1},{0,0,1},{1,1}},{{-.025f,1.2f,1},{0,0,1},{0,1}}}};
    constexpr std::array<std::uint32_t,6> postIndices{0,1,2,0,2,3};
    auto post=renderer.CreateMesh({postVertices,postIndices});
    BasicDraw postDraw{.mesh=&post};
    BasicLighting lighting;
    lighting.shadowsEnabled=false; lighting.ambientIntensity=1; lighting.directionalIntensity=0;
    glm::mat4 projection(1); // Assigned below for every viewport.
    BasicPostProcessEffect ao{BasicPostProcessEffectType::SSAO};
    ao.quality=32; ao.parameters[0]={1.5f,.02f,1.25f,1};
    ao.parameters[1]={2,1,.9f,1}; ao.parameters[2]={.02f,.85f,0,0};
    BasicPostProcessEffect gi{BasicPostProcessEffectType::SSGI};
    gi.quality=8; gi.parameters[0]={1,.5f,0,0};
    const auto render = [&](const BasicDraw &draw, bool enabled=true, bool withGi=false) {
        std::array<BasicDraw,2> draws{draw,postDraw};
        const std::array effects{gi,ao};
        const auto stack=withGi ? std::span<const BasicPostProcessEffect>(effects).first(enabled ? 2 : 1)
            : enabled ? std::span<const BasicPostProcessEffect>(&ao,1) : std::span<const BasicPostProcessEffect>{};
        renderer.Render(projection*lighting.view,lighting,draw.mesh==&corner ? std::span<const BasicDraw>(draws) : std::span<const BasicDraw>(&draw,1),
            stack, {},
            PostProcessDebugView::None,nullptr,nullptr,true,{},{},{},true);
        return device.ReadTextureRgbaFloat(renderer.GetColorTexture());
    };
    for (int estimator=0;estimator<2;++estimator)
    for (int blur : {0,1,2,4})
    for (const auto extent : {rhi::Extent2D{96,64},rhi::Extent2D{95,63}})
    {
    std::vector<float> fullReference;
    for (float half : {0.0f,1.0f,0.0f})
    {
        require(renderer.Resize(extent.width,extent.height),"SSAO viewport resize failed");
        constexpr float nearPlane=.1f,farPlane=100;
        const float f=1.0f/std::tan(glm::radians(50.0f)*.5f);
        projection=glm::mat4(0);
        projection[0][0]=f/(float(extent.width)/extent.height);
        projection[1][1]=f; projection[2][3]=-1;
        projection[2][2]=device.UsesZeroToOneClipDepth() ? nearPlane/(farPlane-nearPlane) : (farPlane+nearPlane)/(farPlane-nearPlane);
        projection[3][2]=device.UsesZeroToOneClipDepth() ? farPlane*nearPlane/(farPlane-nearPlane) : 2*farPlane*nearPlane/(farPlane-nearPlane);
        ao.parameters[1].x=float(blur); ao.parameters[3].y=float(estimator); ao.parameters[1].w=half; ao.parameters[1].y=1;
        lighting.ambientIntensity=1; lighting.directionalIntensity=0;
        BasicDraw draw{.mesh=&corner};
        std::vector<float> image;
        for (int frame=0;frame<12;++frame)
        {
            postDraw.previousModel=postDraw.model;
            postDraw.model=glm::translate(glm::mat4(1),glm::vec3(frame<6 ? -.4f : .4f,0,0));
            const glm::vec3 eye(2.5f+frame*.01f,1.8f,3);
            lighting.view=glm::lookAtRH(eye,glm::vec3(0,.5f,.6f),glm::vec3(0,1,0));
            lighting.cameraPosition=eye;
            image=render(draw);
        }
        require(image.size()==std::size_t(extent.width)*extent.height*4,"SSAO changed viewport extent");
        std::size_t occluded=0;
        for (std::size_t i=0;i<image.size();i+=4)
        {
            require(std::isfinite(image[i]) && image[i]>=0 && image[i]<=1.001f,"SSAO produced invalid visibility");
            occluded += image[i]<.98f;
        }
        require(occluded>10,"SSAO lost contact occlusion");
        if (half==0.0f) fullReference=image;
        else
        {
            double difference=0;
            for (std::size_t i=0;i<image.size();i+=4) difference+=std::abs(image[i]-fullReference[i]);
            difference/=double(image.size()/4);
            if (difference>=.05) std::cerr<<"Reduced AO reference error="<<difference<<"\n";
            require(difference<.05,"Reduced-resolution AO diverged from full-resolution quality reference");
        }
        if (const char *directory=std::getenv("PLUTO_SSAO_CAPTURE_DIR"); directory && blur==2)
        {
            const auto path=std::filesystem::path(directory)/(std::to_string(estimator)+"-"+std::to_string(extent.width)+"-"+std::to_string(int(half))+".ppm");
            std::filesystem::create_directories(path.parent_path());
            std::ofstream out(path,std::ios::binary);
            out<<"P6\n"<<extent.width<<" "<<extent.height<<"\n255\n";
            for (std::size_t i=0;i<image.size();++i) if (i%4!=3)
            { const auto value=static_cast<unsigned char>(std::clamp(image[i],0.0f,1.0f)*255); out.write(reinterpret_cast<const char *>(&value),1); }
        }
        // A single receding plane must not occlude itself, including coarse mips.
        draw.mesh=&floor;
        image=render(draw);
        float minimum=1;
        for (std::size_t i=0;i<image.size();i+=4) minimum=std::min(minimum,image[i]);
        if (minimum<=.97f) std::cerr<<"AO plane estimator="<<estimator<<" radius="<<blur<<" width="<<extent.width<<" half="<<half<<" minimum="<<minimum<<"\n";
        require(minimum>.97f,"SSAO self-occluded a coplanar receiver");
        // Verify scene composition as well as the AO diagnostic. A valid AO
        // texture alone cannot detect a missing indirect-lighting attachment.
        ao.parameters[1].y=0;
        draw.mesh=&corner;
        const auto ambientReference=render(draw,false), ambientAo=render(draw);
        std::size_t darkened=0;
        for (std::size_t i=0;i<ambientReference.size();i+=4)
            darkened += ambientReference[i]-ambientAo[i]>.01f;
        require(darkened>10,"SSAO did not attenuate ambient scene lighting");
        // Direct lighting and emission bypass the indirect AO attenuation.
        ao.parameters[1].y=0; lighting.ambientIntensity=0; lighting.directionalIntensity=1;
        draw.mesh=&corner; draw.emission={.3f,.2f,.1f};
        postDraw.emission=draw.emission;
        const auto reference=render(draw,false), withAo=render(draw);
        for (std::size_t i=0;i<reference.size();++i)
            require(std::isfinite(withAo[i]) && std::abs(reference[i]-withAo[i])<.003f,"SSAO attenuated direct light or emission");
        // GI adds indirect light after geometry, even with no ambient source.
        const auto giReference=render(draw,false,true), giAo=render(draw,true,true);
        darkened=0;
        for (std::size_t i=0;i<giReference.size();i+=4)
        {
            darkened += giReference[i]-giAo[i]>.01f;
            require(giAo[i]>=reference[i]-.003f,"SSAO attenuated the direct/emissive base beneath GI");
        }
        require(darkened>10,"SSAO did not attenuate post-process GI");
        postDraw.emission={0,0,0};
    }
    }
    std::cout<<"SSAO estimators, motion, resolution switching, planar receivers and direct/emissive preservation passed\n";
}

// GPU timings of the horizon estimator per sample budget at `extent`, with the project defaults (half resolution,
// two denoise passes). Stage scopes are reported as recorded; the guide is built inside the SSAO scope.
inline void CheckSsaoPerformance(PlutoGE::render::BasicRenderer &renderer, PlutoGE::render::rhi::IRenderDevice &device,
                                 PlutoGE::render::rhi::Extent2D extent)
{
    using namespace PlutoGE::render;
    if (!renderer.Resize(extent.width, extent.height))
        throw std::runtime_error("SSAO benchmark resize failed");
    constexpr std::array<BasicVertex,8> vertices{{
        {{-2,0,0},{0,1,0},{0,0}},{{2,0,0},{0,1,0},{1,0}},
        {{2,0,3},{0,1,0},{1,1}},{{-2,0,3},{0,1,0},{0,1}},
        {{-2,0,0},{0,0,1},{0,0}},{{2,0,0},{0,0,1},{1,0}},
        {{2,3,0},{0,0,1},{1,1}},{{-2,3,0},{0,0,1},{0,1}}}};
    constexpr std::array<std::uint32_t,12> indices{0,2,1,0,3,2,4,5,6,4,6,7};
    auto corner = renderer.CreateMesh({vertices,indices});
    const BasicDraw draw{.mesh=&corner};
    BasicLighting lighting;
    lighting.shadowsEnabled=false; lighting.ambientIntensity=1; lighting.directionalIntensity=0;
    const glm::vec3 eye(2.5f,1.8f,3);
    lighting.view=glm::lookAtRH(eye,glm::vec3(0,.5f,.6f),glm::vec3(0,1,0));
    lighting.cameraPosition=eye;
    constexpr float nearPlane=.1f,farPlane=100;
    const float f=1.0f/std::tan(glm::radians(50.0f)*.5f);
    glm::mat4 projection(0);
    projection[0][0]=f/(float(extent.width)/extent.height);
    projection[1][1]=f; projection[2][3]=-1;
    projection[2][2]=device.UsesZeroToOneClipDepth() ? nearPlane/(farPlane-nearPlane) : (farPlane+nearPlane)/(farPlane-nearPlane);
    projection[3][2]=device.UsesZeroToOneClipDepth() ? farPlane*nearPlane/(farPlane-nearPlane) : 2*farPlane*nearPlane/(farPlane-nearPlane);
    BasicPostProcessEffect ao{BasicPostProcessEffectType::SSAO};
    ao.parameters[0]={.85f,.08f,1.25f,1.5f};
    ao.parameters[1]={2,0,.9f,1}; ao.parameters[2]={.02f,.85f,0,0};
    ao.parameters[3].y=static_cast<float>(BasicSsaoEstimator::GTAO);
    for (const std::uint32_t budget : {32u, 16u, 8u})
    {
        ao.quality=budget;
        double effectMs=0;
        std::vector<std::pair<std::string,double>> stages;
        int samples=0;
        for (int frame=0; frame<64+256; ++frame)
        {
            renderer.Render(projection*lighting.view,lighting,std::span(&draw,1),std::span(&ao,1));
            const auto timing=device.GetTimingStats("Scene");
            if (frame<64 || !timing.hasGpuResult) continue;
            for (const auto &scope : timing.gpuScopes)
            {
                if (scope.name=="RHI SSAO") effectMs+=scope.milliseconds;
                if (!scope.name.starts_with("RHI SSAO / ") && !scope.name.starts_with("RHI Screen-space guide")) continue;
                const auto stage=std::ranges::find(stages,scope.name,&std::pair<std::string,double>::first);
                if (stage==stages.end()) stages.emplace_back(scope.name,scope.milliseconds);
                else stage->second+=scope.milliseconds;
            }
            ++samples;
        }
        if (!samples) throw std::runtime_error("SSAO benchmark has no GPU timings");
        std::cout<<"SSAO benchmark ("<<extent.width<<" x "<<extent.height<<", budget "<<budget<<"): "
                 <<effectMs/samples<<" ms";
        for (const auto &[name,milliseconds] : stages) std::cout<<", "<<milliseconds/samples<<" ms "<<name.substr(4);
        std::cout<<" ("<<samples<<" samples)\n";
    }
}
