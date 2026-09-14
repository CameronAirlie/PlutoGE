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
#include <stdexcept>
#include <string_view>
void Check(bool ok,const char *message){if(!ok)throw std::runtime_error(message);}
int main(int argc,char **argv) try
{
    using namespace PlutoGE;
    const bool vulkan=argc>1&&std::string_view(argv[1])=="--vulkan";
    platform::Window window;
    std::unique_ptr<render::rhi::IRenderDevice> device;
    if(vulkan)device=std::make_unique<render::rhi::vulkan::VulkanDevice>();
    else
    {
        Check(window.Create({.title="Ocean GPU regression",.width=128,.height=128,.visible=false}),"Window failed");
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
    shaders.virtualShadows=library.LoadBasicRendererPackage().virtualShadows;
    render::RhiSceneRenderer renderer;Check(renderer.Initialize(*device,shaders),"Renderer initialization failed");
    scene::Scene scene;
    auto *owner=scene.AddEntity(std::make_unique<scene::Entity>());
    auto *ocean=owner->CreateComponent<scene::OceanComponent>();
    render::BasicLighting lighting;lighting.cameraPosition={0,5,9};lighting.directionalIntensity=2;
    render::CameraData camera{glm::lookAt(lighting.cameraPosition,glm::vec3(0),glm::vec3(0,1,0)),glm::perspective(glm::radians(60.f),1.f,100.f,.1f)};
    std::span<const render::RenderCommand> shadowCasters;
    auto render=[&](std::span<const render::RenderCommand> commands=std::span<const render::RenderCommand>{})
    {
        Check(renderer.Render(128,128,camera,lighting,commands,shadowCasters, {}, {}, {}, render::PostProcessDebugView::None,true,&scene),"Ocean render failed");
        if(vulkan)return static_cast<render::rhi::vulkan::VulkanDevice &>(*device).ReadTextureRgba8(renderer.GetColorTexture());
        std::vector<std::byte> pixels(128*128*4);
        glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(static_cast<render::rhi::opengl::OpenGLDevice &>(*device).GetTextureNativeHandle(renderer.GetColorTexture())));
        glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());return pixels;
    };
    const auto center=[](const auto &pixels){return std::array{pixels[(64*128+64)*4],pixels[(64*128+64)*4+1],pixels[(64*128+64)*4+2]};};
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
    lighting.physicalSkyExposure=.1f;
    Check(brightness(render())<brightness(skyLit),"Ocean ignores physical sky exposure");
    lighting.physicalSkyEnabled=false;
    lighting.cameraPosition={0,-2,9};camera.view=glm::lookAt(lighting.cameraPosition,glm::vec3(0,-2,0),glm::vec3(0,1,0));
    ocean->SetEnabled(false);const auto belowDry=render();ocean->SetEnabled(true);Check(center(render())!=center(belowDry),"Underwater fading missing");
    renderer.Shutdown();
    std::cout<<(vulkan?"Vulkan":"OpenGL")<<" ocean: surface, animation, masks, transforms, disable, depth occlusion, sun lighting, shadows, physical sky and underwater passed\n";
}
catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
