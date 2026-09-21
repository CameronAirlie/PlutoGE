#pragma once
#include "PlutoGE/render/BasicRenderer.h"
#include "PlutoGE/render/RhiSceneRenderer.h"
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <stdexcept>

template<class Device>
void CheckOrthographicUpscalers(PlutoGE::render::BasicRenderer &renderer, Device &device,
                               const PlutoGE::render::BasicRendererShaderPackage &shaders)
{
    using namespace PlutoGE::render;
    const auto rowMajor = [](const glm::mat4 &m) {
        std::array<float,16> values{};
        for (int r=0;r<4;++r) for(int c=0;c<4;++c) values[r*4+c]=m[c][r];
        return values;
    };
    const std::array<BasicVertex,4> vertices{{
        {{{-.5f,-.5f,0}},{{0,0,1}},{{0,0}}}, {{{.5f,-.5f,0}},{{0,0,1}},{{1,0}}},
        {{{.5f,.5f,0}},{{0,0,1}},{{1,1}}}, {{{-.5f,.5f,0}},{{0,0,1}},{{0,1}}}}};
    const std::array<std::uint32_t,6> indices{0,1,2,0,2,3};
    auto mesh=renderer.CreateMesh({vertices,indices});
    BasicDraw draw; draw.mesh=&mesh; draw.twoSided=true; draw.emission={.8f,.8f,.8f};
    BasicLighting lighting; lighting.ambientIntensity=lighting.directionalIntensity=0;
    lighting.cameraPosition={0,0,5}; lighting.view=glm::translate(glm::mat4(1),glm::vec3(0,0,-5));
    for (const auto technology : {rhi::TemporalUpscaler::Fsr2,rhi::TemporalUpscaler::Dlss})
    {
        const auto support=device.GetTemporalUpscalerSupport(technology);
        if (!support.supported)
        {
            if(technology==rhi::TemporalUpscaler::Fsr2) throw std::runtime_error(support.reason);
            std::cout << "DLSS hardware/runtime check unavailable: " << support.reason << std::endl;
            continue;
        }
        renderer.SetTemporalUpscalerOptions({.technology=technology,.autoExposure=false,.sharpness=0});
        renderer.Resize(128,128,192,192);
        const auto projection=glm::orthoRH_ZO(-1.f,1.f,-1.f,1.f,10.f,.1f);
        auto previous=projection*lighting.view;
        for(unsigned frame=0;frame<32;++frame)
        {
            const auto view=glm::translate(lighting.view,glm::vec3(.1f*std::sin(frame*.1f),0,0));
            const auto vp=projection*view;
            rhi::TemporalUpscalerFrame input;
            input.contextId=883344; input.frameIndex=frame; input.resetHistory=frame==0;
            input.orthographicProjection=true; input.orthographicViewWidth=input.orthographicViewHeight=2;
            input.cameraNear=.1f;input.cameraFar=10;input.cameraAspectRatio=1;
            input.cameraPosition={0,0,5};input.cameraUp={0,1,0};input.cameraRight={1,0,0};input.cameraForward={0,0,-1};
            input.cameraViewToClip=rowMajor(projection);input.clipToCameraView=rowMajor(glm::inverse(projection));
            input.clipToPreviousClip=rowMajor(previous*glm::inverse(vp));input.previousClipToClip=rowMajor(vp*glm::inverse(previous));
            input.jitterPixels={frame%2 ? .25f : -.25f,frame%3 ? .25f : -.25f};
            renderer.Render(vp,lighting,{&draw,1},{},{},PostProcessDebugView::None,&input,&vp);
            if(!renderer.WasTemporalUpscalerEvaluated()) throw std::runtime_error("Orthographic temporal upscale failed: "+device.GetTemporalUpscalerFailureReason(technology));
            previous=vp;
        }
        auto pixels=device.ReadTextureRgba8(renderer.GetColorTexture());
        if(int(pixels[(96*192+96)*4])<150 || int(pixels[(12*192+12)*4])>20)
            throw std::runtime_error("Orthographic temporal upscale lost foreground/background coverage");
        device.ReleaseTemporalUpscalerContext(883344);
        // Exercise the scene adapter's gate, jitter and camera-switch reset too.
        RhiSceneRenderer scene;
        if(!scene.Initialize(device,shaders)) throw std::runtime_error("Orthographic scene renderer initialization failed");
        scene.SetTemporalUpscalerOptions({.technology=technology,.autoExposure=false});
        CameraData camera; camera.view=lighting.view;camera.nearPlane=.1f;camera.farPlane=10;
        for(bool ortho : {true,false,true})
        {
            camera.projection=ortho ? glm::orthoRH_NO(-1.f,1.f,-1.f,1.f,10.f,.1f) :
                glm::perspectiveRH_NO(glm::radians(60.f),1.f,10.f,.1f);
            if(!scene.Render(192,192,camera,lighting,{},{})) throw std::runtime_error("Camera projection switch failed");
            if(!scene.GetTemporalUpscalerStatus().active) throw std::runtime_error("Scene adapter disabled orthographic temporal upscaling");
        }
        std::cout << (technology==rhi::TemporalUpscaler::Fsr2 ? "FSR2" : "DLSS") << " orthographic motion, coverage and camera switches passed" << std::endl;
    }
}
