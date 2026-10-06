#include "PlutoGE/core/Engine.h"
#include "PlutoGE/render/RmlUiRuntime.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/render/rhi/RenderDevice.h"
#include "PlutoGE/render/rhi/Resource.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include <filesystem>
#include <fstream>
#include <RmlUi/Core.h>
#include <RmlUi/Core/Elements/ElementFormControlInput.h>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Near(float actual, float expected, const char* message) { Check(std::abs(actual - expected) < .2f, message); }
}
int main(int argc, char** argv) try
{
    using namespace PlutoGE;
    auto& engine = core::Engine::GetInstance();
    core::EngineConfig config;
    config.graphicsApi = argc > 1 && std::string_view(argv[1]) == "--opengl"
        ? render::rhi::GraphicsApi::OpenGL : render::rhi::GraphicsApi::Vulkan;
    config.windowConfig.visible = false;
    config.windowConfig.width = 960; config.windowConfig.height = 640;
    Check(engine.Initialize(config), "Engine initialization failed");
    struct Scope { core::Engine& engine; ~Scope() { engine.Shutdown(); } } scope{engine};
    auto& ui = render::RmlUiRuntime::Get();
    Check(ui.SetInterfaceScale(1.25f), "Pre-initialization preference rejected");
    Check(ui.Initialize(engine.GetWindow(), engine.GetRenderDevice()), "UI initialization failed");
    auto* screen = ui.GetContext(); auto* world = ui.GetWorldContext();
    Near(screen->GetDensityIndependentPixelRatio(), 1.25f, "Preference lost on initialization");
    const char* markup = R"(<rml><head><style>
        body { width: 100%; height: 100%; margin: 0; }
        div { display: block; }
        #overlay { position: absolute; left: 0; top: 0; right: 0; bottom: 0; box-sizing: border-box; border: 4dp red; pointer-events: none; }
        #corner { position: absolute; right: 24dp; bottom: 24dp; width: 160dp; height: 80dp; background-color: blue; }
        #projected { position: absolute; left: 50%; top: 50%; margin-left: 5vh; width: 2px; height: 2px; }
        #stack { position: absolute; left: 24dp; top: 24dp; width: 180dp; }
        .row { width: 100%; height: 40dp; margin-bottom: 8dp; }
        input { display: block; width: 160dp; height: 24dp; }
        input.range slidertrack { height: 6dp; }
        input.range sliderbar { width: 12dp; height: 24dp; }
        input.range sliderarrowdec, input.range sliderarrowinc { display: none; }
        </style></head><body><div id="overlay"/><div id="corner"/><div id="projected"/>
        <div id="stack"><div id="first" class="row"/><div id="second" class="row"/>
        <input id="slider" type="range" min="0" max="1" step="0.01" value="0.5"/></div></body></rml>)";
    auto* document = screen->LoadDocumentFromMemory(markup);
    auto* label = world->LoadDocumentFromMemory(markup);
    Check(document && label, "Fixture parse failed"); document->Show(); label->Show();
    for (auto size : {Rml::Vector2i{960,640}, Rml::Vector2i{1280,720}, Rml::Vector2i{640,480}})
    {
        screen->SetDimensions(size); world->SetDimensions(size);
        for (float scale : {.75f, 1.f, 1.25f, 2.f})
        {
            Check(ui.SetInterfaceScale(scale), "Scale rejected"); screen->Update(); world->Update();
            auto* corner = document->GetElementById("corner");
            auto cornerSize = corner->GetBox().GetSize(Rml::BoxArea::Border);
            auto origin = corner->GetAbsoluteOffset(Rml::BoxArea::Border);
            Near(cornerSize.x,160*scale,"dp width did not scale");
            Near(size.x-origin.x-cornerSize.x,24*scale,"Right edge anchor moved");
            Near(size.y-origin.y-cornerSize.y,24*scale,"Bottom edge anchor moved");
            auto* overlay = document->GetElementById("overlay");
            auto coverage = overlay->GetBox().GetSize(Rml::BoxArea::Border);
            Near(coverage.x,float(size.x),"Overlay lost viewport width"); Near(coverage.y,float(size.y),"Overlay lost viewport height");
            auto* projected = document->GetElementById("projected");
            Near(projected->GetAbsoluteOffset().x,size.x*.5f+size.y*.05f,"Projected spread changed with interface scale");
            Near(projected->GetBox().GetSize().x,2,"Physical crosshair width scaled");
            auto y1=document->GetElementById("first")->GetAbsoluteOffset().y;
            auto y2=document->GetElementById("second")->GetAbsoluteOffset().y;
            Near(y2-y1,48*scale,"Stack spacing failed to reflow");
            Near(label->GetElementById("corner")->GetBox().GetSize().x,160,"World document changed size");
            Near(world->GetDensityIndependentPixelRatio(),1,"World density changed");
            auto* hit = screen->GetElementAtPoint(origin + cornerSize * .5f);
            if (hit != corner) std::cerr << "Hit " << (hit ? hit->GetTagName() + ":" + hit->GetId() : "null") << " at scale " << scale << " origin " << origin.x << "," << origin.y << " size " << cornerSize.x << "," << cornerSize.y << '\n';
            Check(hit==corner,"Scaled corner hit test failed");
            auto* slider=rmlui_dynamic_cast<Rml::ElementFormControlInput*>(document->GetElementById("slider"));
            Check(slider,"Slider fixture missing"); slider->SetValue("0.73");
            Near(std::stof(slider->GetValue()),.73f,"Slider value changed under scaling");
        }
    }
    Check(!ui.SetInterfaceScale(std::numeric_limits<float>::quiet_NaN()),"NaN accepted");
    Check(!ui.SetInterfaceScale(0) && !ui.SetInterfaceScale(4),"Invalid range accepted");
    document->Close(); label->Close(); ui.ResetRuntimeState();
    Near(ui.GetInterfaceScale(),1,"Runtime reset leaked player scale");
    // Optional project-backed migration regression and visual capture. No gameplay runs.
    if (argc == 4 && std::string_view(argv[1]) == "--cod")
    {
        const std::filesystem::path project = argv[2], output = argv[3];
        std::filesystem::create_directories(output);
        engine.GetAssetManager().SetProjectContext(project.string());
        scene::Scene scene;
        auto* owner = scene.AddEntity(std::make_unique<scene::Entity>());
        auto* widget = owner->CreateComponent<scene::RmlWidgetComponent>();
        auto* device = engine.GetRenderDevice();
        auto* vk = dynamic_cast<render::rhi::vulkan::VulkanDevice*>(device);
        Check(vk, "Project capture requires Vulkan");
        const int width=1280,height=720;
        render::rhi::Texture target(*device,device->CreateTexture({width,height,render::rhi::Format::R8G8B8A8Unorm,
            render::rhi::TextureUsage::ColorAttachment,"CoD interface scale capture",true,1,false,1}));
        std::uint64_t frame=100;
        auto render = [&] {
            auto& commands=device->GetImmediateContext(); commands.BeginFrame("UI fixture background");
            render::rhi::RenderingInfo clear; clear.colorAttachments={target.Get()}; clear.width=width;clear.height=height;
            clear.clearDepth=false;clear.clearColorValue[0]=.16f;clear.clearColorValue[1]=.20f;clear.clearColorValue[2]=.24f;clear.clearColorValue[3]=1;
            commands.BeginRendering(clear);commands.EndRendering();commands.Submit();
            ui.RenderRhi(scene,*device,target.Get(),width,height,++frame,glm::mat4(1),glm::mat4(1));
        };
        auto capture = [&](const std::string& name) {
            auto pixels=vk->ReadTextureRgba8(target.Get());
            std::ofstream image(output/(name+".ppm"),std::ios::binary);
            image<<"P6\n"<<width<<' '<<height<<"\n255\n";
            for (int y=height-1;y>=0;--y) for(int x=0;x<width;++x)
                image.write(reinterpret_cast<const char*>(pixels.data()+(y*width+x)*4),3);
        };
        for (float scale : {.75f,1.f,1.25f})
        {
            ui.SetInterfaceScale(scale);
            widget->SetSource("project://UI/hud.rml");render();
            ui.SetElementClass("UI/hud.rml","damage-overlay","hidden",false);
            ui.SetElementText("UI/hud.rml","objective-status","ATTACK / PLANT AT A OR B");render();
            auto* hud=screen->GetDocument(0);
            Check(hud && hud->GetElementById("hud"),"CoD HUD did not load");
            auto* weapon=hud->GetElementById("weapon");
            auto weaponSize=weapon->GetBox().GetSize(Rml::BoxArea::Border);
            auto weaponOrigin=weapon->GetAbsoluteOffset(Rml::BoxArea::Border);
            Near(width-weaponOrigin.x-weaponSize.x,32*scale,"CoD weapon right anchor failed");
            Near(height-weaponOrigin.y-weaponSize.y,32*scale,"CoD weapon bottom anchor failed");
            auto overlaySize=hud->GetElementById("damage-overlay")->GetBox().GetSize(Rml::BoxArea::Border);
            Near(overlaySize.x,width,"CoD damage width failed");Near(overlaySize.y,height,"CoD damage height failed");
            Near(hud->GetElementById("crosshair")->GetBox().GetSize().x,2,"CoD crosshair geometry scaled");
            capture("cod-hud-"+std::to_string(scale));
            widget->SetSource("project://UI/pause-menu.rml");render();
            ui.SetElementStyle("UI/pause-menu.rml","settings-overlay","display","flex");
            for (const char* tab : {"display","graphics","audio","interface","controls"})
            {
                ui.SetElementClass("UI/pause-menu.rml",std::string("settings-section-")+tab,"settings-active",std::string_view(tab)=="audio");
                ui.SetElementClass("UI/pause-menu.rml",std::string("settings-tab-")+tab,"selected",std::string_view(tab)=="audio");
            }
            render();
            auto* pause=screen->GetDocument(0);
            auto* panel=pause->GetElementById("settings-panel");
            auto* audio=pause->GetElementById("settings-section-audio");
            Check(audio->GetBox().GetSize().x > width*.3f,"Settings grid collapsed when scrollbar appeared");
            for(const char* name : {"master","effects","feedback"})
            {
                auto* slider=pause->GetElementById(std::string("setting-")+name);
                Check(slider && slider->GetBox().GetSize().x > 100*scale,"Settings slider lost its control column");
            }
            if(panel->GetScrollHeight()>panel->GetClientHeight())
            {
                panel->SetScrollTop(panel->GetScrollHeight());screen->Update();
                auto* back=pause->GetElementById("settings-close");
                const auto backOrigin=back->GetAbsoluteOffset(Rml::BoxArea::Border);
                Check(backOrigin.y+back->GetBox().GetSize(Rml::BoxArea::Border).y<=height,"Settings footer inaccessible after scrolling");
                panel->SetScrollTop(0);render();
            }
            capture("cod-settings-"+std::to_string(scale));
        }
        ui.ResetRuntimeState();
    }
    std::cout << "Interface scale: anchoring, reflow, overlays, projected geometry, sliders, hit testing, world isolation and reset passed.\n";
    return 0;
}
catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
