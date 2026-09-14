#include "PlutoGE/render/RhiSceneRenderer.h"
#include "PlutoGE/render/RhiOcean.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/render/rhi/opengl/OpenGLDevice.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "PlutoGE/platform/Window.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/OceanComponent.h"
#include "PlutoGE/render/Material.h"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <string_view>
void Check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char **argv) try
{
    using namespace PlutoGE;
    const bool vulkan=argc>1&&std::string_view(argv[1])=="--vulkan";
    const int resolution=argc>2 ? 512 : 128;
    platform::Window window;
    std::unique_ptr<render::rhi::IRenderDevice> device;
    if(vulkan)device=std::make_unique<render::rhi::vulkan::VulkanDevice>();
    else
    {
        Check(window.Create({.title="Ocean GPU regression",.width=resolution,.height=resolution,.visible=false}),"Window failed");
        Check(window.EnsureOpenGLContextCurrent(true),"OpenGL context failed");
        device=std::make_unique<render::rhi::opengl::OpenGLDevice>();
    }
    render::ShaderArtifactLibrary library(PLUTO_OCEAN_SHADER_DIR);
    render::BasicRendererShaderPackage shaders;
    shaders.vertex=library.Load("BasicLit","vertex");shaders.fragment=library.Load("BasicLit","fragment");
    shaders.instancedVertex=library.Load("BasicLitInstanced","vertex");
    shaders.shadowVertex=library.Load("DirectionalShadow","vertex");
    shaders.shadowFragment=library.Load("DirectionalShadow","fragment");
    shaders.shadowInstancedVertex=library.Load("DirectionalShadowInstanced","vertex");
    shaders.maskedShadowFragment=library.Load("DirectionalShadowMasked","fragment");
    shaders.displayOutput={library.Load("DisplayOutput","vertex"),library.Load("DisplayOutput","fragment")};
    shaders.postProcess[static_cast<unsigned>(render::BasicPostProcessEffectType::Ocean)]={library.Load("Ocean","vertex"),library.Load("Ocean","fragment")};
    shaders.postProcess[static_cast<unsigned>(render::BasicPostProcessEffectType::PhysicalSky)]={library.Load("PhysicalSky","vertex"),library.Load("PhysicalSky","fragment")};
    shaders.postProcess[static_cast<unsigned>(render::BasicPostProcessEffectType::GammaCorrection)]={library.Load("GammaCorrection","vertex"),library.Load("GammaCorrection","fragment")};
    shaders.virtualShadows=library.LoadBasicRendererPackage().virtualShadows;
    render::RhiSceneRenderer renderer;Check(renderer.Initialize(*device,shaders),"Renderer initialization failed");
    scene::Scene scene;
    auto *owner=scene.AddEntity(std::make_unique<scene::Entity>());
    auto *ocean=owner->CreateComponent<scene::OceanComponent>();
    render::BasicLighting lighting;lighting.cameraPosition={0,5,9};lighting.directionalIntensity=2;
    render::CameraData camera{glm::lookAt(lighting.cameraPosition,glm::vec3(0),glm::vec3(0,1,0)),glm::perspective(glm::radians(60.f),1.f,100.f,.1f)};
    std::vector<render::BasicPostProcessEffect> atmosphere;
    std::span<const render::RenderCommand> shadowCasters;
    auto render=[&](std::span<const render::RenderCommand> commands=std::span<const render::RenderCommand>{})
    {
        Check(renderer.Render(resolution,resolution,camera,lighting,commands,shadowCasters, {}, atmosphere, {}, render::PostProcessDebugView::None,true,&scene),"Ocean render failed");
        if(vulkan)return static_cast<render::rhi::vulkan::VulkanDevice &>(*device).ReadTextureRgba8(renderer.GetColorTexture());
        std::vector<std::byte> pixels(resolution*resolution*4);
        glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(static_cast<render::rhi::opengl::OpenGLDevice &>(*device).GetTextureNativeHandle(renderer.GetColorTexture())));
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());return pixels;
    };
    // Optional review artifacts, kept out of ordinary regression runs.
    const auto capture=[&](const char *name,const auto &pixels)
    {
        if(argc<3)return;
        const auto path=std::filesystem::path(argv[2])/(std::string(name)+".ppm");
        std::ofstream output(path,std::ios::binary);
        Check(bool(output),"Cannot write ocean review image");
        output<<"P6\n"<<resolution<<" "<<resolution<<"\n255\n";
        for(int y=resolution-1;y>=0;--y)
            for(int x=0;x<resolution;++x)
                output.write(reinterpret_cast<const char*>(pixels.data()+(y*resolution+x)*4),3);
    };
    const auto center=[&](const auto &pixels){return std::array{pixels[((resolution/2)*resolution+resolution/2)*4],pixels[((resolution/2)*resolution+resolution/2)*4+1],pixels[((resolution/2)*resolution+resolution/2)*4+2]};};
    ocean->SetEnabled(false);const auto dry=render();
    ocean->SetEnabled(true);const auto wet=render();Check(center(wet)!=center(dry),"Ocean absent from RHI output");
    ocean->Update(.8f);const auto waves=render();Check(waves!=wet,"Ocean waves do not animate");
    ocean->AddArea({{-20,-20},{20,-20},{20,20},{-20,20}});
    Check(center(render())==center(dry),"Ocean exclusion mask ignored");
    auto properties=ocean->Serialize();
    for(auto &p:properties)if(p.name=="InvertAreaMask")p.value="true";
    ocean->Deserialize(properties);Check(center(render())!=center(dry),"Inverted ocean mask ignored");
    owner->SetPosition({100,0,0});Check(center(render())==center(dry),"Ocean mask ignores owner transform");
    owner->SetPosition({0,0,0});owner->SetActive(false);Check(render()==dry,"Inactive ocean still renders");owner->SetActive(true);
    owner->SetScale({0,1,1});Check(render::CollectRhiOceans(scene,lighting).empty(),"Singular ocean transform accepted");owner->SetScale({1,1,1});
    ocean->RemoveArea(0);
    std::unique_ptr<render::Mesh> cube(render::Mesh::Cube());render::Material material({.color={.8f,.2f,.1f,1}});
    render::RenderCommand box;box.mesh=cube.get();box.material=&material;box.model=glm::scale(glm::translate(glm::mat4(1),glm::vec3(0,2,0)),glm::vec3(12,2,12));
    box.previousModel=box.model;box.worldBounds={{0,2,0},10};box.previousWorldBounds=box.worldBounds;
    ocean->SetEnabled(false);const auto foreground=render(std::span(&box,1));
    ocean->SetEnabled(true);Check(center(render(std::span(&box,1)))==center(foreground),"Ocean overlays foreground geometry");
    const auto setOcean=[&](const char *name,const char *value)
    {
        ocean->Deserialize({{name,scene::PropertyType::Float,value}});
    };
    const auto originalSettings=ocean->Serialize();
    setOcean("WindDirection","135");
    Check(render()!=waves,"Wind direction does not alter water");
    setOcean("WaveAmplitude","1.2");setOcean("CrestFoamThreshold","0");
    setOcean("CrestFoamIntensity","0");const auto noWhitecaps=render();
    setOcean("CrestFoamIntensity","5");Check(render()!=noWhitecaps,"Crest whitecaps do not render");
    capture("ocean-whitecaps",render());
    ocean->Deserialize(originalSettings);
    // Put opaque geometry beneath the surface to exercise shallow-water effects.
    auto bottom=box;
    bottom.model=glm::scale(glm::translate(glm::mat4(1),glm::vec3(0,-2,0)),glm::vec3(12,2,12));
    bottom.previousModel=bottom.model;bottom.worldBounds={{0,-2,0},10};bottom.previousWorldBounds=bottom.worldBounds;
    setOcean("CausticsIntensity","0");const auto noCaustics=render(std::span(&bottom,1));
    setOcean("CausticsIntensity","5");Check(render(std::span(&bottom,1))!=noCaustics,"Shallow-water caustics do not render");
    setOcean("FoamIntensity","0");const auto noShoreFoam=render(std::span(&bottom,1));
    setOcean("FoamIntensity","5");setOcean("FoamDistance","5");
    Check(render(std::span(&bottom,1))!=noShoreFoam,"Shore foam does not render");
    capture("ocean-shore",render(std::span(&bottom,1)));
    ocean->Deserialize(originalSettings);
    // A shadow-only overhead caster isolates water lighting from opaque scene pixels.
    lighting.directionalDirection={0,-1,0};
    lighting.ambientIntensity=.1f;
    const auto brightness=[&](const auto &pixels)
    {
        const auto rgb=center(pixels);
        return std::to_integer<int>(rgb[0])+std::to_integer<int>(rgb[1])+std::to_integer<int>(rgb[2]);
    };
    const auto sunlit=render();
    lighting.directionalIntensity=0;
    Check(brightness(render())<brightness(sunlit),"Water scattering ignores sun intensity");
    lighting.ambientIntensity=0;
    Check(brightness(render())<brightness(sunlit),"Water emits light in an unlit scene");
    lighting.directionalIntensity=2;lighting.ambientIntensity=.1f;
    shadowCasters=std::span(&box,1);
    lighting.shadowsEnabled=true;lighting.shadowResolution=512;lighting.shadowDistance=60;
    Check(brightness(render())<brightness(sunlit),"Ocean does not receive caster shadows");
    if(device->GetImmediateContext().SupportsGpuDrivenShadows())
    {
        lighting.shadowMethod=render::ShadowMethod::Virtual;
        // Let budgeted virtual pages settle before comparing visibility.
        for(int frame=0;frame<8;++frame)render();
        Check(renderer.GetTimingStats().virtualShadowsActive,"Virtual ocean test fell back to cascades");
        Check(brightness(render())<brightness(sunlit),"Ocean does not receive virtual shadows");
        lighting.shadowMethod=render::ShadowMethod::Cascaded;
    }
    shadowCasters={};
    Check(brightness(render())>0,"Empty shadow maps black out water");
    lighting.shadowsEnabled=false;
    lighting.physicalSkyEnabled=true;
    lighting.physicalSkyParameters[0]={0,1,0,1};
    lighting.physicalSkyParameters[1]={1,.9f,.7f,1};
    lighting.physicalSkyParameters[2]={.2f,.3f,.5f,.76f};
    lighting.physicalSkyParameters[3]={.1f,.1f,.1f,1};
    lighting.physicalSkyParameters[4]={10,.53f,.1f,0};
    const auto skyLit=render();
    capture("ocean-sky",skyLit);
    ocean->ApplyStylizedSeaPreset();
    Check(render()!=skyLit,"Stylized sea preset has no visible effect");
    if(argc>2)
    {
        const auto oldCamera=camera;const auto oldLighting=lighting;
        lighting.cameraPosition={0,3.5f,15};
        lighting.directionalDirection=glm::normalize(glm::vec3(-.3f,-.45f,.8f));
        lighting.physicalSkyParameters[0]=glm::vec4(-lighting.directionalDirection,1);
        lighting.directionalIntensity=3;
        camera.view=glm::lookAt(lighting.cameraPosition,glm::vec3(0,0,-20),glm::vec3(0,1,0));
        render::BasicPostProcessEffect sky{render::BasicPostProcessEffectType::PhysicalSky};
        sky.parameters=lighting.physicalSkyParameters;atmosphere.push_back(sky);
        atmosphere.push_back(render::BasicPostProcessEffect{render::BasicPostProcessEffectType::GammaCorrection});
        capture("ocean-stylized-sea",render());
        lighting.cameraPosition={0,65,65};
        camera.view=glm::lookAt(lighting.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));
        capture("ocean-distant-sea",render());
        atmosphere.clear();camera=oldCamera;lighting=oldLighting;
    }
    ocean->Deserialize(originalSettings);
    lighting.physicalSkyExposure=.1f;
    Check(brightness(render())<brightness(skyLit),"Ocean ignores physical sky exposure");
    lighting.physicalSkyEnabled=false;
    lighting.cameraPosition={0,-2,9};camera.view=glm::lookAt(lighting.cameraPosition,glm::vec3(0,-2,0),glm::vec3(0,1,0));
    ocean->SetEnabled(false);const auto belowDry=render();ocean->SetEnabled(true);Check(center(render())!=center(belowDry),"Underwater fading missing");
    // A floor's color must disappear at the same water-path distance from either side.
    const auto savedCamera=camera;
    const auto savedLighting=lighting;
    setOcean("WaveAmplitude","0");setOcean("FoamIntensity","0");setOcean("CrestFoamIntensity","0");
    setOcean("CausticsIntensity","0");setOcean("RefractionStrength","0");setOcean("MaxVisibilityDepth","10");
    setOcean("Opacity","0");
    lighting.directionalIntensity=0;lighting.ambientIntensity=1;
    render::Material redFloor({.color={1,0,0,1}}), blueFloor({.color={0,0,1,1}});
    const auto floorContrast=[&](bool submerged,float path)
    {
        lighting.cameraPosition={0,submerged ? -.1f : 5.f,0};
        camera.view=glm::lookAt(lighting.cameraPosition,lighting.cameraPosition+glm::vec3(0,-1,0),glm::vec3(0,0,-1));
        auto floor=box;
        float top=-(path+(submerged ? .1f : 0.f));
        floor.model=glm::scale(glm::translate(glm::mat4(1),glm::vec3(0,top-1,0)),glm::vec3(200,2,200));
        floor.previousModel=floor.model;floor.worldBounds={{0,top-1,0},150};floor.previousWorldBounds=floor.worldBounds;
        floor.material=&redFloor;const auto red=center(render(std::span(&floor,1)));
        floor.material=&blueFloor;const auto blue=center(render(std::span(&floor,1)));
        glm::ivec3 contrast;
        for(int channel=0;channel<3;++channel)
            contrast[channel]=std::abs(std::to_integer<int>(red[channel])-std::to_integer<int>(blue[channel]));
        return contrast;
    };
    const auto nearAbove=floorContrast(false,2);
    const auto nearBelow=floorContrast(true,2);
    Check(nearAbove.r+nearAbove.b>10 && nearBelow.r+nearBelow.b>10,"Shallow floors should remain visible");
    Check(glm::all(glm::lessThanEqual(glm::abs(nearAbove-nearBelow),glm::ivec3(3))),"Surface and underwater visibility curves differ");
    for(const char *opacity:{"0","0.15","0.82","1"})
    {
        setOcean("Opacity",opacity);
        Check(floorContrast(false,20)==glm::ivec3(0),"Opacity leaks deep ocean floor through the surface");
        Check(floorContrast(true,20)==glm::ivec3(0),"Deep floor remains visible underwater");
    }
    setOcean("UnderwaterTurbidity","0");setOcean("UnderwaterDepthFalloff","0");
    Check(floorContrast(false,20)==glm::ivec3(0) && floorContrast(true,20)==glm::ivec3(0),"Zero turbidity bypasses maximum visibility");
    camera=savedCamera;lighting=savedLighting;ocean->Deserialize(originalSettings);
    // A visible quay supplies both reflection hits and wall contacts.
    ocean->ApplyHarbourPreset();
    setOcean("WaveAmplitude","0");setOcean("FoamIntensity","0");setOcean("CrestFoamIntensity","0");
    setOcean("ContactDistance","0");setOcean("ReflectionStrength","0");
    lighting.cameraPosition={0,3,9};lighting.physicalSkyEnabled=false;lighting.ambientIntensity=.8f;lighting.directionalIntensity=1;
    camera.view=glm::lookAt(lighting.cameraPosition,glm::vec3(0,0,-3),glm::vec3(0,1,0));
    auto quay=box;
    quay.model=glm::scale(glm::translate(glm::mat4(1),glm::vec3(0,2,-4)),glm::vec3(8,6,2));
    quay.previousModel=quay.model;quay.worldBounds={{0,2,-4},8};quay.previousWorldBounds=quay.worldBounds;
    const auto noBuildingReflection=render(std::span(&quay,1));
    setOcean("ReflectionStrength","1");
    Check(render(std::span(&quay,1))!=noBuildingReflection,"Building reflections do not contribute to water");
    const auto reflectionMiss=render();setOcean("ReflectionStrength","0");
    Check(render()==reflectionMiss,"Missed reflection rays do not preserve sky fallback");
    setOcean("ContactDistance","3");setOcean("ContactDamping","0");setOcean("ContactDarkening","0");setOcean("ContactFoam","0");
    const auto noContact=render(std::span(&quay,1));
    setOcean("ContactDarkening","1");Check(render(std::span(&quay,1))!=noContact,"Wall contact darkening is absent");
    setOcean("ContactDarkening","0");setOcean("ContactFoam","1");
    Check(render(std::span(&quay,1))!=noContact,"Localized wall foam is absent");
    setOcean("ContactFoam","0");setOcean("WaveAmplitude","1");
    const auto undamped=render(std::span(&quay,1));setOcean("ContactDamping","1");
    Check(render(std::span(&quay,1))!=undamped,"Near-wall waves are not damped");
    ocean->ApplyHarbourPreset();
    capture("ocean-harbour",render(std::span(&quay,1)));
    camera=savedCamera;lighting=savedLighting;ocean->Deserialize(originalSettings);
    renderer.Shutdown();
    shaders.postProcess[static_cast<unsigned>(render::BasicPostProcessEffectType::Ocean)]={library.Load("OceanWaveProbe","vertex"),library.Load("OceanWaveProbe","fragment")};
    Check(renderer.Initialize(*device,shaders),"Wave probe initialization failed");
    for(int frame=0;frame<5;++frame)
    {
        ocean->Update(.37f);
        setOcean("WindDirection",frame%2 ? "90" : "-35");
        const auto expected=ocean->SampleLocalSurface({2.3f,-4.1f});
        const glm::vec3 encoded=.5f+.2f*glm::vec3(expected.height,expected.gradient);
        const auto actual=center(render());
        for(int channel=0;channel<3;++channel)
            Check(std::abs(std::to_integer<int>(actual[channel])/255.f-encoded[channel])<.008f,
                  "CPU and GPU ocean heights/slopes disagree");
    }
    renderer.Shutdown();
    std::cout<<(vulkan?"Vulkan":"OpenGL")<<" ocean: surface, animation, masks, transforms, disable, depth occlusion, sun lighting, shadows, physical sky and underwater passed\n";
}
catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
