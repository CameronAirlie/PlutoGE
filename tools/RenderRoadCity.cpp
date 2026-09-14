#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/rhi/opengl/OpenGLDevice.h"
#include "PlutoGE/render/RhiSceneRenderer.h"
#include "PlutoGE/render/Renderer.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/scene/SceneSerializer.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/SplineComponent.h"
#include "PlutoGE/scene/components/MeshComponent.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iostream>
int main(int argc,char **argv) try
{
    using namespace PlutoGE;
    if(argc!=3)return 1;
    // Load CPU assets before creating a context, just as the Vulkan host does.
    core::Engine::GetInstance().GetAssetManager().SetProjectContext(std::filesystem::absolute(argv[1]).string());
    std::string error;
    auto scene=scene::SceneSerializer::Load((std::filesystem::path(argv[1])/"Assets/Scenes/ManhattanMini.plutoscene").string(),&error);
    if(!scene)throw std::runtime_error(error);
    std::vector<render::RenderCommand> commands;
    auto collect=[&](auto &&self,scene::Entity *e)->void
    {
        if(auto *s=e->GetComponent<scene::SplineComponent>())s->Update(0);
        if(auto *m=e->GetComponent<scene::MeshComponent>();m&&m->GetMesh())
            for(unsigned i=0;i<m->GetMesh()->GetSubmeshCount();++i)
            {
                render::RenderCommand c;c.mesh=m->GetMesh();c.material=m->GetMaterialForSubmesh(i);c.submeshIndex=i;
                c.model=c.previousModel=e->GetWorldTransform();c.worldBounds=c.mesh->GetSubmesh(i).bounds;
                c.worldBounds.center=glm::vec3(c.model*glm::vec4(c.worldBounds.center,1));c.previousWorldBounds=c.worldBounds;
                commands.push_back(c);
            }
        for(auto *child:e->GetChildren())self(self,child);
    };
    for(auto *e:scene->GetRootEntities())collect(collect,e);
    platform::Window window;
    if(!window.Create({.title="City preview",.width=1280,.height=960,.visible=false})||!window.EnsureOpenGLContextCurrent(true))return 2;
    render::rhi::opengl::OpenGLDevice device;
    render::BasicRendererShaderPackage shaders;
    auto read=[](const char *name){std::ifstream in(std::filesystem::path(PLUTO_CITY_SHADER_DIR)/name);std::ostringstream s;s<<in.rdbuf();return s.str();};
    shaders.vertex.glsl=read("BasicLit.vertex.glsl");shaders.fragment.glsl=read("BasicLit.fragment.glsl");
    shaders.instancedVertex.glsl=read("BasicLitInstanced.vertex.glsl");
    shaders.shadowVertex.glsl=read("DirectionalShadow.vertex.glsl");shaders.shadowFragment.glsl=read("DirectionalShadow.fragment.glsl");
    shaders.shadowInstancedVertex.glsl=read("DirectionalShadowInstanced.vertex.glsl");
    shaders.maskedShadowFragment.glsl=read("DirectionalShadowMasked.fragment.glsl");
    shaders.displayOutput.vertex.glsl=read("DisplayOutput.vertex.glsl");shaders.displayOutput.fragment.glsl=read("DisplayOutput.fragment.glsl");
    render::RhiSceneRenderer renderer;if(!renderer.Initialize(device,shaders))return 3;
    render::CameraData camera;
    const glm::vec3 eye(310,290,370);
    camera.view=glm::lookAt(eye,glm::vec3(-10,15,0),glm::vec3(0,1,0));
    camera.projection=glm::perspective(glm::radians(52.f),1280.f/960.f,1200.f,.1f);
    camera.farPlane=1200;
    render::BasicLighting lighting;lighting.cameraPosition=eye;lighting.view=camera.view;
    lighting.ambientIntensity=.65f;lighting.directionalIntensity=2;lighting.directionalDirection=glm::normalize(glm::vec3(-.6f,-1,-.3f));
    lighting.directionalColor={1,.9f,.8f};
    if(!renderer.Render(1280,960,camera,lighting,commands,{}))return 4;
    std::vector<unsigned char> pixels(1280*960*4);
    glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(device.GetTextureNativeHandle(renderer.GetColorTexture())));
    glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());
    std::ofstream image(argv[2],std::ios::binary);image<<"P6\n1280 960\n255\n";
    for(int y=959;y>=0;--y)for(int x=0;x<1280;++x)image.write(reinterpret_cast<const char*>(pixels.data()+(y*1280+x)*4),3);
    renderer.Shutdown();
    std::cout<<commands.size()<<" scene submeshes rendered\n";
}
catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
