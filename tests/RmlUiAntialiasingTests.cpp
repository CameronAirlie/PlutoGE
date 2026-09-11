#include "PlutoGE/render/RmlUiRhiRenderer.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "PlutoGE/render/rhi/opengl/OpenGLDevice.h"
#include "PlutoGE/platform/Window.h"
#include <RmlUi/Core.h>
#include <cmath>
#include <iostream>
#include <stdexcept>

using namespace PlutoGE;
using namespace PlutoGE::render;
using namespace PlutoGE::render::rhi;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template<class Reader>
void CheckUi(IRenderDevice& device, Reader read)
{
    ShaderArtifactLibrary shaders(PLUTO_RHI_TEST_SHADER_DIR);
    RmlUiRhiRenderer ui(device,shaders.Load("RmlUi","vertex"),shaders.Load("RmlUi","fragment"));
    Rml::SetRenderInterface(&ui);
    Require(Rml::Initialise(),"RmlUi initialization failed");
    auto* context=Rml::CreateContext("AA",{80,64});
    auto* doc=context->LoadDocumentFromMemory(R"(<rml><head><style>
div { display: block; }
body { margin: 0; width: 100%; height: 100%; }
#ring { position: absolute; left: 8px; top: 10px; width: 22px; height: 22px; border: 2px white; border-radius: 14px; }
#clip { position: absolute; left: 44px; top: 6px; width: 10px; height: 12px; overflow: hidden; }
#red { width: 24px; height: 24px; background-color: #ff000080; }
</style></head><body><div id="ring"></div><div id="clip"><div id="red"></div></div></body></rml>)");
    Require(doc!=nullptr,"Native border document failed to load");
    doc->Show();
    auto render=[&](bool aa,int width,int height,bool shared) {
        context->SetDimensions({width,height}); context->Update(); ui.SetViewport(width,height); ui.SetAntialiasingEnabled(aa);
        Texture target(device,device.CreateTexture({static_cast<unsigned>(width),static_cast<unsigned>(height),Format::R8G8B8A8Unorm,TextureUsage::ColorAttachment,"UI test",true,1,false,1}));
        auto& commands=device.GetImmediateContext();
        commands.BeginFrame("UI AA test");
        RenderingInfo clear; clear.colorAttachments={target.Get()}; clear.width=width; clear.height=height; clear.clearDepth=false;
        clear.clearColorValue[0]=.2f; clear.clearColorValue[1]=.4f; clear.clearColorValue[2]=.6f;
        commands.BeginRendering(clear); commands.EndRendering();
        if(!shared) commands.Submit();
        ui.BeginFrame(target.Get(),!shared); context->Render(); ui.EndFrame(!shared);
        if(shared) commands.Submit();
        auto pixels=read(target.Get(),width,height);
        auto channel=[&](int x,int y,int c) { return int(std::to_integer<unsigned char>(pixels[((height-1-y)*width+x)*4+c])); };
        Require(std::abs(channel(2,2,0)-51)<=1 && std::abs(channel(2,2,2)-153)<=1,"UI composite changed scene outside UI");
        Require(std::abs(channel(20,22,0)-51)<=1,"Transparent ring centre lost background");
        Require(std::abs(channel(47,9,0)-153)<=2 && std::abs(channel(47,9,1)-51)<=2 && channel(47,9,3)==255,"Premultiplied transparency or UI orientation is wrong");
        Require(std::abs(channel(57,9,0)-51)<=1,"Scaled UI scissor leaked");
        int coverage=0;
        for(int y=8;y<38;y++) for(int x=6;x<36;x++) if(channel(x,y,0)>55 && channel(x,y,0)<250) coverage++;
        return coverage;
    };
    int aliased=render(false,80,64,false);
    int smooth=render(true,80,64,false);
    Require(smooth>aliased+20,"Supersampling did not improve native rounded border coverage");
    Require(render(true,96,72,true)>aliased+20,"Resize or shared submission broke UI AA");
    doc->Hide(); context->Update();
    // With no visible UI, the transparent layer must not retain a previous frame.
    ui.SetViewport(96,72);
    Texture empty(device,device.CreateTexture({96,72,Format::R8G8B8A8Unorm,TextureUsage::ColorAttachment,"Empty UI",true,1,false,1}));
    auto& commands=device.GetImmediateContext(); commands.BeginFrame();
    RenderingInfo clear; clear.colorAttachments={empty.Get()}; clear.width=96; clear.height=72; clear.clearDepth=false;
    commands.BeginRendering(clear); commands.EndRendering();
    ui.BeginFrame(empty.Get(),false); context->Render(); ui.EndFrame(false); commands.Submit();
    auto pixels=read(empty.Get(),96,72);
    for(size_t i=0;i<pixels.size();i+=4) Require(pixels[i]==std::byte{0} && pixels[i+1]==std::byte{0} && pixels[i+2]==std::byte{0},"UI layer retained stale pixels");
    Require(Rml::LoadFontFace(PLUTO_UI_TEST_FONT), "Victory test font failed to load");
    auto* victory=context->LoadDocumentFromMemory(R"(<rml><head><style>
body { margin: 0; width: 100%; height: 100%; font-family: Martian Mono; font-size: 16px; color: white; }
#status { display: none; font-size: 48px; }
</style></head><body><div>HEALTH 100</div><div id="status"></div></body></rml>)");
    victory->Show();
    Texture victoryTarget(device,device.CreateTexture({640,240,Format::R8G8B8A8Unorm,TextureUsage::ColorAttachment,"Victory UI",true,1,false,1}));
    context->SetDimensions({640,240}); ui.SetViewport(640,240);
    for(int frame=0;frame<15;++frame) {
        if(frame==3) { victory->GetElementById("status")->SetInnerRML("HEIR FELLED / R to restart"); victory->GetElementById("status")->SetProperty("display","block"); }
        if(frame==8) victory->GetElementById("status")->SetProperty("display","none");
        context->Update(); commands.BeginFrame("Victory UI regression");
        clear.colorAttachments={victoryTarget.Get()}; clear.width=640; clear.height=240;
        clear.clearColorValue[0]=.2f; clear.clearColorValue[1]=.4f; clear.clearColorValue[2]=.6f;
        commands.BeginRendering(clear); commands.EndRendering();
        ui.BeginFrame(victoryTarget.Get(),false);
        if(frame==5 && device.GetApi()==GraphicsApi::Vulkan) {
            // Reproduce a recoverable binding failure inside UI rendering. The
            // following viewport/frame must not inherit an open render scope.
            bool rejected=false;
            try { commands.BindUniformBuffer(0,{}); }
            catch(const std::exception&) { rejected=true; }
            Require(rejected,"Fault injection did not reject an invalid uniform");
            bool stranded=false;
            try { commands.BeginFrame("Second viewport before recovery"); }
            catch(const std::logic_error&) { stranded=true; }
            Require(stranded,"Interrupted scope did not reproduce the shared-context failure");
            ui.CancelFrame(); commands.RecoverInterruptedFrame();
            continue;
        }
        context->Render(); ui.EndFrame(false); commands.Submit();
        const auto result=read(victoryTarget.Get(),640,240);
        // The bottom-right background must survive lazy creation of the large
        // victory font atlas during the active UI rendering scope.
        const auto offset=(10*640+630)*4;
        Require(std::abs(int(result[offset])-51)<=1 && std::abs(int(result[offset+2])-153)<=1,"Victory font creation corrupted scene background");
    }
    Rml::Shutdown(); Rml::SetRenderInterface(nullptr);
    std::cout<<"Native border partial-coverage pixels: "<<aliased<<" -> "<<smooth<<"; resize, clipping, transparency and shared submission passed.\n";
}
int main(int argc,char** argv) try
{
    if(argc>1 && std::string_view(argv[1])=="--opengl")
    {
        platform::Window window;
        Require(window.Create({.title="UI AA test",.width=96,.height=72,.resizable=false,.visible=false}),"Window creation failed");
        Require(window.EnsureOpenGLContextCurrent(true),"GL context unavailable");
        opengl::OpenGLDevice device;
        CheckUi(device,[&](TextureHandle texture,int width,int height) {
            std::vector<std::byte> pixels(width*height*4);
            glBindTexture(GL_TEXTURE_2D,static_cast<GLuint>(device.GetTextureNativeHandle(texture)));
            glGetTexImage(GL_TEXTURE_2D,0,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data()); return pixels;
        });
    }
    else
    {
        vulkan::VulkanDevice device;
        CheckUi(device,[&](TextureHandle texture,int,int) {return device.ReadTextureRgba8(texture);});
    }
    return 0;
}
catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
