#include "PlutoGE/render/RmlUiRhiRenderer.h"
#include "PlutoGE/render/ShaderArtifacts.h"
#include "PlutoGE/render/rhi/vulkan/VulkanDevice.h"
#include "PlutoGE/render/rhi/opengl/OpenGLDevice.h"
#include "PlutoGE/platform/Window.h"
#include "PlutoGE/render/ScenePortrait.h"
#include "PlutoGE/render/Camera.h"
#include "PlutoGE/render/Material.h"
#include "PlutoGE/render/Mesh.h"
#include "PlutoGE/render/RenderTexture.h"
#include "PlutoGE/render/RhiRenderTextureRenderer.h"
#include "PlutoGE/render/RmlDocumentPath.h"
#include "PlutoGE/core/Engine.h"
#include "PlutoGE/assets/AssetManager.h"
#include "PlutoGE/scene/Scene.h"
#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/Prefab.h"
#include <filesystem>
#include <RmlUi/Core.h>
#include <RmlUi/Core/Spritesheet.h>
#include <RmlUi/Core/StyleSheet.h>
#include <cmath>
#include <iostream>
#include <fstream>
#include <iterator>
#include <stdexcept>

using namespace PlutoGE;
using namespace PlutoGE::render;
using namespace PlutoGE::render::rhi;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

template<class Reader>
void CheckUi(IRenderDevice& device, Reader read, const char* documentPath = nullptr, const char* fontPath = nullptr, const char* capturePrefix = nullptr)
{
    struct TestLog final : Rml::SystemInterface {
        bool LogMessage(Rml::Log::Type, const Rml::String& message) override
        { std::cerr << message << '\n'; return true; }
    } log;
    Rml::SetSystemInterface(&log);
    ShaderArtifactLibrary shaders(PLUTO_RHI_TEST_SHADER_DIR);
    RmlUiRhiRenderer ui(device,shaders.Load("RmlUi","vertex"),shaders.Load("RmlUi","fragment"));
    Rml::SetRenderInterface(&ui);
    Require(Rml::Initialise(),"RmlUi initialization failed");
    // Failed texture checks must clean up documents before destroying their renderer.
    struct RmlScope {
        ~RmlScope() { Rml::Shutdown(); Rml::SetRenderInterface(nullptr); Rml::SetSystemInterface(nullptr); }
    } rmlScope;
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
        rhi::Texture target(device,device.CreateTexture({static_cast<unsigned>(width),static_cast<unsigned>(height),Format::R8G8B8A8Unorm,TextureUsage::ColorAttachment,"UI test",true,1,false,1}));
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
    doc->Hide();
    auto* clipped = context->LoadDocumentFromMemory(R"(<rml><head><style>
body { margin: 0; width: 128px; height: 100px; transform-origin: 0px 0px; transform: scale(1.25); }
div { display: block; }
#outer { position: absolute; left: 20px; top: 10px; width: 40px; height: 40px; overflow: hidden; transform-origin: 0px 0px; transform: scale(1.5); }
#inner { margin-left: 10px; margin-top: 10px; width: 40px; height: 40px; overflow: hidden; }
#fill { width: 80px; height: 80px; background-color: red; }
</style></head><body><div id="outer"><div id="inner"><div id="fill"></div></div></div></body></rml>)");
    Require(clipped != nullptr, "Transformed clip fixture failed to load");
    clipped->Show();
    for (int variant = 0; variant < 4; ++variant)
    {
        const bool aa = variant % 2 != 0;
        clipped->GetElementById("outer")->SetProperty("border-radius", variant >= 2 ? "8px" : "0px");
        context->SetDimensions({160,128}); context->Update(); ui.SetViewport(160,128); ui.SetAntialiasingEnabled(aa);
        rhi::Texture target(device, device.CreateTexture({160,128,Format::R8G8B8A8Unorm,TextureUsage::ColorAttachment,"Transformed clips",true,1,false,1}));
        auto& cmd = device.GetImmediateContext(); cmd.BeginFrame();
        RenderingInfo info; info.colorAttachments={target.Get()}; info.width=160; info.height=128; info.clearDepth=false;
        cmd.BeginRendering(info); cmd.EndRendering();
        ui.BeginFrame(target.Get(),false); context->Render(); ui.EndFrame(false); cmd.Submit();
        auto result=read(target.Get(),160,128);
        auto red=[&](int x,int y) {return int(result[((127-y)*160+x)*4]);};
        std::cout << "Clip samples: " << red(50,40) << ", " << red(30,40) << ", " << red(105,40) << ", " << red(50,95) << "\n";
        Require(red(50,40)>250,"Nested transformed masks clipped visible content");
        Require(red(30,40)==0 && red(105,40)==0 && red(50,95)==0,"Nested transformed masks leaked outside their intersection");
        Require(variant >= 2 ? red(98,85) < 20 : red(98,85) > 250, "Rounded mask was approximated by a rectangle");
    }
    clipped->Hide(); context->Update();
    ui.SetAntialiasingEnabled(true);
    // With no visible UI, the transparent layer must not retain a previous frame.
    ui.SetViewport(96,72);
    rhi::Texture empty(device,device.CreateTexture({96,72,Format::R8G8B8A8Unorm,TextureUsage::ColorAttachment,"Empty UI",true,1,false,1}));
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
    rhi::Texture victoryTarget(device,device.CreateTexture({640,240,Format::R8G8B8A8Unorm,TextureUsage::ColorAttachment,"Victory UI",true,1,false,1}));
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
    if (documentPath)
    {
        victory->Hide();
        std::ifstream fontStream(fontPath, std::ios::binary);
        const std::vector<Rml::byte> fontBytes((std::istreambuf_iterator<char>(fontStream)), {});
        Require(Rml::LoadFontFace(fontBytes, "Dungeon", Rml::Style::FontStyle::Normal), "Capture font failed to load");
        auto* journal = context->LoadDocument(documentPath);
        Require(journal != nullptr, "Capture document failed to load");
        // Exercise the real player/equipment assets without starting a gameplay scene.
        const auto project = std::filesystem::path(documentPath).parent_path().parent_path().parent_path();
        core::Engine::GetInstance().GetAssetManager().SetProjectContext(project.string());
        scene::Scene portraitScene;
        std::string portraitError;
        auto* hero = scene::Prefab::Instantiate(portraitScene, "project://Prefabs/Player.plutoprefab", nullptr, &portraitError);
        auto* armour = scene::Prefab::Instantiate(portraitScene, "project://Prefabs/Armour/traveler_coat.plutoprefab", nullptr, &portraitError);
        auto* weapon = scene::Prefab::Instantiate(portraitScene, "project://Prefabs/Weapons/earthshaker.plutoprefab", nullptr, &portraitError);
        Require(hero && armour && weapon, portraitError.c_str());
        const auto heroPosition = hero->GetWorldPosition();
        armour->SetWorldPosition(heroPosition);
        weapon->SetWorldPosition(heroPosition + glm::vec3(.65f, .1f, -.25f));
        ScenePortrait portrait;
        const std::array<std::uint32_t, 2> attachments{armour->GetID(), weapon->GetID()};
        Require(portrait.Render(device, portraitScene, hero->GetID(), attachments, 320, 384), "3D portrait failed to render");
        auto portraitPixels = read(portrait.Texture()->resource.Get(), 320, 384);
        int opaque = 0, transparent = 0;
        for (size_t i = 3; i < portraitPixels.size(); i += 4)
        {
            opaque += int(portraitPixels[i]) > 250;
            transparent += int(portraitPixels[i]) == 0;
        }
        Require(opaque > 3000 && transparent > 3000, "Portrait must contain a visible character and transparent background");
        Require(hero->GetWorldPosition() == heroPosition, "Portrait modified gameplay transforms");
        ui.RegisterExternalTexture("portrait://test", portrait.Texture());
        journal->GetElementById("character-portrait")->SetAttribute("src", "portrait://test");
        journal->SetProperty("width", "1280px"); journal->SetProperty("height", "960px");
        journal->SetProperty("transform-origin", "0px 0px"); journal->SetProperty("transform", "scale(0.75)");
        journal->GetElementById("journal-panel")->SetProperty("display", "block");
        // Resolve through the actual stylesheet and renderer: valid sprite markup alone
        // does not prove that its backing image format can be decoded and uploaded.
        const auto* weaponSprite = journal->GetStyleSheet()->GetSprite("weapon-wanderer_blade");
        Require(weaponSprite != nullptr, "Inventory sprite is missing from stylesheet");
        auto atlas = weaponSprite->sprite_sheet->texture_source.GetTexture(context->GetRenderManager());
        const auto atlasSize = atlas.GetDimensions();
        Require(atlasSize.x > 0 && atlasSize.y > 0, "Inventory sprite atlas failed native texture decoding/upload");
        journal->GetElementById("equip-0")->SetInnerRML("<img id=\"inventory-icon-probe\" class=\"item-icon\" sprite=\"weapon-wanderer_blade\"/><span class=\"slot-caption\">WEAPON</span><span class=\"slot-name\">Wanderer's Blade</span>");
        const char* weaponIds[] = {"wanderer_blade", "warden_edge", "iron_sabre", "cinder_staff", "ash_wand", "iron_maul", "frost_hammer", "earthshaker", "spirit_tome", "winter_grimoire", "legion_codex"};
        for (int i = 0; i < 11; ++i)
            journal->GetElementById("bag-" + std::to_string(i))->SetInnerRML("<img class=\"item-icon\" sprite=\"weapon-" + Rml::String(weaponIds[i]) + "\"/><span class=\"slot-name\">" + weaponIds[i] + "</span>");
        journal->GetElementById("item-preview")->SetProperty("display", "block");
        journal->GetElementById("item-preview")->SetInnerRML("<img class=\"item-preview-image\" sprite=\"weapon-earthshaker\"/>");
        const auto* armourSprite = journal->GetStyleSheet()->GetSprite("armour-traveler_coat");
        Require(armourSprite != nullptr, "Armour sprite is missing from stylesheet");
        const auto armourSize = armourSprite->sprite_sheet->texture_source.GetTexture(context->GetRenderManager()).GetDimensions();
        Require(armourSize.x >= 384 && armourSize.x % 128 == 0 && armourSize.y == 128, "Armour atlas failed native decoding/upload");
        journal->GetElementById("equip-1")->SetInnerRML("<img id=\"armour-icon-probe\" class=\"item-icon\" sprite=\"armour-traveler_coat\"/><span class=\"slot-caption\">ARMOR</span>");
        const char* armourIds[] = {"traveler_coat", "sentinel_mail", "quilted_vest"};
        for (int i = 0; i < 3; ++i)
            journal->GetElementById("bag-" + std::to_string(11 + i))->SetInnerRML("<img class=\"item-icon\" sprite=\"armour-" + Rml::String(armourIds[i]) + "\"/>");
        journal->GetElementById("equip-2")->SetInnerRML("<span class=\"slot-fallback\">SIG</span><span class=\"slot-caption\">CHARM</span>");
        std::ifstream inspectorFile(std::string(capturePrefix) + "item.rml");
        const std::string inspectorMarkup((std::istreambuf_iterator<char>(inspectorFile)), {});
        if (!inspectorMarkup.empty()) journal->GetElementById("item-detail")->SetInnerRML(inspectorMarkup);
        const char* statLabels[] = {"Health", "Aether", "Melee damage", "Spell damage", "Damage reduction", "Aether regen", "Primary hit", "Move speed"};
        const char* statValues[] = {"100 / 100", "100 / 100", "x1", "x1", "5%", "12 / s", "24", "6"};
        Rml::String characterStats;
        for (int i = 0; i < 8; ++i)
            characterStats += "<div class=\"character-stat-row" + Rml::String(i % 2 ? " alt" : "") + "\"><span class=\"stat-label\">" + statLabels[i] + "</span><span class=\"stat-value\">" + statValues[i] + "</span></div>";
        if (auto* stats = journal->GetElementById("character-stat-list")) stats->SetInnerRML(characterStats);
        if (inspectorMarkup.empty()) journal->GetElementById("item-detail")->SetInnerRML("Select an item to see its details.");
        journal->GetElementById("inventory-status")->SetInnerRML("Drag an item to move, swap or equip it.");
        journal->GetElementById("inventory")->SetInnerRML("BACKPACK / 14 / 24");
        journal->GetElementById("training")->SetInnerRML("TRAINING / STEEL 0 / ARCANE 1 / VITALITY 0");
        Rml::String entries;
        for (int i=0;i<12;++i) entries += "<p>[ACTIVE] The Last Archivist - After clearing the Ossuary, speak to the blue shrine on its eastern side. The sigil opens the descent.</p>";
        journal->GetElementById("quests")->SetInnerRML(entries);
        journal->Show(); context->SetDimensions({960,720}); ui.SetViewport(960,720);
        std::ifstream minimapFile(std::string(capturePrefix) + "minimap.rml");
        const std::string minimapMarkup((std::istreambuf_iterator<char>(minimapFile)), {});
        for (int page=0;page<(minimapMarkup.empty() ? 4 : journal->GetElementById("settings-tabs") ? 8 : journal->GetElementById("gameplay-hud") ? 6 : 5);++page)
        {
            if (page==1)
            {
                journal->GetElementById("item-inspection")->SetScrollTop(10000);
                auto* mail = scene::Prefab::Instantiate(portraitScene, "project://Prefabs/Armour/sentinel_mail.plutoprefab", nullptr, &portraitError);
                Require(mail != nullptr, portraitError.c_str());
                mail->SetWorldPosition(heroPosition);
                const std::array<std::uint32_t, 2> changed{mail->GetID(), weapon->GetID()};
                Require(portrait.Render(device, portraitScene, hero->GetID(), changed, 320, 384), "Equipment refresh failed");
                Require(read(portrait.Texture()->resource.Get(), 320, 384) != portraitPixels, "Equipment refresh did not change portrait pixels");
                Require(portrait.RenderCount() == 2, "Portrait rendered outside explicit refresh calls");
                journal->GetElementById("equip-1")->SetInnerRML("<img class=\"item-icon\" sprite=\"armour-sentinel_mail\"/><span class=\"slot-caption\">ARMOR</span>");
            }
            if (page==2)
            {
                journal->GetElementById("inventory-page")->SetProperty("display","none");
                journal->GetElementById("quests-page")->SetProperty("display","block");
                journal->GetElementById("inventory-tab")->SetClass("selected",false);
                journal->GetElementById("quests-tab")->SetClass("selected",true);
            }
            if (page==3)
            {
                journal->GetElementById("quests-page")->SetProperty("display","none");
                journal->GetElementById("inventory-page")->SetProperty("display","block");
                journal->GetElementById("inventory-page")->SetScrollTop(0);
                auto* slot=journal->GetElementById("bag-0");
                slot->SetInnerRML("<span class=\"slot-caption\">1</span><br/>Cinder Staff");
                slot->SetProperty("drag","clone");
                context->Update();
                const auto point=slot->GetAbsoluteOffset(Rml::BoxArea::Border)*0.75f+Rml::Vector2f(12,12);
                context->ProcessMouseMove(int(point.x),int(point.y),0);
                context->ProcessMouseButtonDown(0,0);
                context->ProcessMouseMove(int(point.x+30),int(point.y+30),0);
                context->ProcessMouseMove(int(point.x+170),int(point.y+100),0);
            }
            if (page == 4)
            {
                context->ProcessMouseButtonUp(0, 0);
                journal->GetElementById("journal-panel")->SetProperty("display", "none");
                journal->GetElementById("minimap-panel")->SetProperty("display", "block");
                journal->GetElementById("minimap-board")->SetInnerRML(minimapMarkup);
                journal->GetElementById("minimap-floor")->SetInnerRML("Crypt / Ground");
            }
            if (page == 5)
            {
                journal->SetProperty("width", "960px"); journal->SetProperty("height", "720px");
                journal->SetProperty("transform", "scale(1)");
                auto set = [&](const char* id, const char* value) { journal->GetElementById(id)->SetInnerRML(value); };
                set("room", "The Ossuary"); set("objective", "Find the Archivist and open the descent");
                set("experience", "LV 7"); set("skill-points", "+ 3 SKILLS");
                set("health", "HP 84/120"); set("mana", "MP 42/100");
                journal->GetElementById("health-fill")->SetProperty("width", "70%");
                journal->GetElementById("mana-fill")->SetProperty("width", "42%");
                journal->GetElementById("experience-fill")->SetProperty("width", "65%");
                set("interaction", "F: Open the pilgrim cache"); set("combat-feedback", "SHIFT: GUARD");
                set("hud-notices", "<div class=\"hud-notice\"><span class=\"notice-kind\">COMBAT</span> Guard broken</div><div class=\"hud-notice\"><span class=\"notice-kind\">PROGRESS</span> Level 7! Skill points available.</div><div class=\"hud-notice\"><span class=\"notice-kind\">LOOT</span> Picked up Warden's Edge</div>");
                const char* slots[] = {"primary", "secondary", "special"};
                const char* names[] = {"Slash", "Cleave", "Thrust"};
                const char* bindings[] = {"LMB", "RMB", "E"};
                const char* states[] = {"READY", "1.4s", "LOW AETHER"};
                for (int i=0;i<3;++i)
                {
                    const auto id = Rml::String("attack-") + slots[i];
                    journal->GetElementById(id+"-name")->SetInnerRML(names[i]);
                    journal->GetElementById(id+"-binding")->SetInnerRML(bindings[i]);
                    journal->GetElementById(id+"-state")->SetInnerRML(states[i]);
                    journal->GetElementById(id+"-icon")->SetInnerRML("<img sprite=\"weapon-wanderer_blade\"/>");
                }
                journal->GetElementById("attack-secondary-shade")->SetProperty("height", "65%");
            }
            if (page >= 6)
            {
                journal->GetElementById("gameplay-hud")->SetProperty("display", "none");
                journal->GetElementById("minimap-panel")->SetProperty("display", "none");
                journal->GetElementById("hud-notices")->SetProperty("display", "none");
                auto* settings = journal->GetElementById("display-panel"); settings->SetProperty("display", "block");
                Rml::ElementList rows; settings->GetElementsByClassName(rows, "settings-row");
                for (auto* row : rows)
                {
                    const auto& id = row->GetId();
                    bool show = page == 6 ? id.find("audio-") == 0 : id == "display-scale-row" || id == "settings-hud-numbers-row" || id == "settings-text-row" || id.find("settings-minimap") == 0;
                    row->SetProperty("display", show ? "block" : "none");
                }
                journal->GetElementById("settings-tab-audio")->SetClass("active-tab", page == 6);
                journal->GetElementById("settings-tab-interface")->SetClass("active-tab", page == 7);
                const std::pair<const char*, const char*> labels[] = {{"audio-master-label", "MASTER: 80%"}, {"audio-music-label", "MUSIC: 50%"},
                    {"audio-ambience-label", "AMBIENCE: 65%"}, {"audio-combat-label", "COMBAT: 100%"}, {"audio-ui-label", "UI: 75%"},
                    {"display-scale-label", "UI SCALE: 100%"}, {"settings-minimap-size-label", "MINIMAP SIZE: 200 px"}, {"settings-minimap-zoom-label", "MINIMAP ZOOM: 100%"},
                    {"audio-sound", "SOUND: ON"}, {"settings-hud-numbers", "RESOURCE NUMBERS: ON"}, {"settings-text", "COMBAT TEXT: NORMAL"}, {"settings-minimap", "MINIMAP: ON"}};
                for (auto [id, label] : labels) journal->GetElementById(id)->SetInnerRML(label);
                for (auto [id, value] : {std::pair{"audio-master", 80}, {"audio-ambience", 65}, {"audio-ui", 75}}) journal->GetElementById(id)->SetAttribute("value", value);
                journal->GetElementById("display-detail")->SetInnerRML(page == 6 ? "Drag a slider or use Left/Right to adjust. Audio changes preview immediately." : "Scale and minimap sliders preview immediately. North stays at the top.");
                journal->GetElementById("display-status")->SetInnerRML("Unapplied changes. Apply to save; Back discards changes.");
                journal->GetElementById("settings-content")->SetScrollTop(0);
            }
            context->Update(); context->Update();
            rhi::Texture target(device,device.CreateTexture({960,720,Format::R8G8B8A8Unorm,TextureUsage::ColorAttachment,"Journal capture",true,1,false,1}));
            commands.BeginFrame(); clear.colorAttachments={target.Get()}; clear.width=960; clear.height=720;
            commands.BeginRendering(clear); commands.EndRendering();
            ui.BeginFrame(target.Get(),false); context->Render(); ui.EndFrame(false); commands.Submit();
            auto capture=read(target.Get(),960,720);
            if (page == 0)
            {
              for (const auto* probe : {"inventory-icon-probe", "armour-icon-probe"})
              {
                auto* icon = journal->GetElementById(probe);
                const auto origin = icon->GetAbsoluteOffset(Rml::BoxArea::Content) * 0.75f;
                const auto size = icon->GetBox().GetSize(Rml::BoxArea::Content) * 0.75f;
                int white = 0, samples = 0;
                for (int y = int(origin.y)+2; y < int(origin.y+size.y)-2; ++y)
                    for (int x = int(origin.x)+2; x < int(origin.x+size.x)-2; ++x)
                    {
                        Require(x >= 0 && x < 960 && y >= 0 && y < 720, "Inventory icon is outside the capture");
                        const auto offset = ((719-y)*960+x)*4;
                        if (int(capture[offset]) > 245 && int(capture[offset+1]) > 245 && int(capture[offset+2]) > 245) ++white;
                        ++samples;
                    }
                Require(samples > 100 && white < samples / 4, "Inventory icon rendered as an opaque white placeholder");
              }
                std::cout << "Inventory atlas decoded at " << atlasSize.x << 'x' << atlasSize.y << "; GPU icon pixels passed.\n";
            }
            if (page == 4)
            {
                auto* board = journal->GetElementById("minimap-board");
                const auto origin = board->GetAbsoluteOffset(Rml::BoxArea::Content) * .75f;
                const auto size = board->GetBox().GetSize(Rml::BoxArea::Content) * .75f;
                int terrainPixels = 0;
                for (int y = 0; y < 720; ++y) for (int x = 0; x < 960; ++x)
                {
                    const auto offset = ((719 - y) * 960 + x) * 4;
                    const bool terrain = std::abs(int(capture[offset]) - 107) <= 1 &&
                        std::abs(int(capture[offset+1]) - 135) <= 1 && std::abs(int(capture[offset+2]) - 145) <= 1;
                    if (!terrain) continue;
                    ++terrainPixels;
                    Require(x >= origin.x - 1 && x <= origin.x + size.x + 1 && y >= origin.y - 1 && y <= origin.y + size.y + 1,
                        "Minimap terrain escaped its scaled clipping viewport");
                }
                Require(terrainPixels > 100, "Minimap terrain was not rendered");
                std::ifstream legacyFile(std::string(capturePrefix) + "minimap.rml.legacy");
                const std::string legacyMarkup((std::istreambuf_iterator<char>(legacyFile)), {});
                if (!legacyMarkup.empty())
                {
                    board->SetInnerRML(legacyMarkup); context->Update(); context->Update();
                    commands.BeginFrame(); commands.BeginRendering(clear); commands.EndRendering();
                    ui.BeginFrame(target.Get(), false); context->Render(); ui.EndFrame(false); commands.Submit();
                    auto legacyPixels = read(target.Get(), 960, 720);
                    int mismatches = 0;
                    auto terrain = [](const auto& pixels, size_t offset) {
                        return std::abs(int(pixels[offset]) - 107) <= 1 && std::abs(int(pixels[offset+1]) - 135) <= 1 && std::abs(int(pixels[offset+2]) - 145) <= 1;
                    };
                    for (size_t i = 0; i < capture.size(); i += 4) mismatches += terrain(capture, i) != terrain(legacyPixels, i);
                    Require(mismatches <= std::max(8, terrainPixels / 100), "Compressed minimap changed visible terrain coverage");
                    std::cout << "GPU minimap old/new terrain mask mismatch: " << mismatches << " / " << terrainPixels << " pixels\n";
                    board->SetInnerRML(minimapMarkup); context->Update(); context->Update();
                }
                std::cout << "GPU minimap terrain remains inside its scaled viewport.\n";
            }
            std::ofstream output(std::string(capturePrefix)+std::to_string(page)+".ppm",std::ios::binary);
            output << "P6\n960 720\n255\n";
            for (int y=719;y>=0;--y) for(int x=0;x<960;++x)
                output.write(reinterpret_cast<const char*>(capture.data()+(y*960+x)*4),3);
            Require(bool(output),"Could not write journal capture");
            if (page==3) context->ProcessMouseButtonUp(0,0);
        }
    }
    std::cout<<"Native border partial-coverage pixels: "<<aliased<<" -> "<<smooth<<"; resize, clipping, transparency and shared submission passed.\n";
}
// An <img> of a render texture must look exactly like a screenshot of its
// camera's on-screen view: same orientation and the same display colour.
template<class Reader>
void CheckRenderTextureImage(vulkan::VulkanDevice& device, Reader read)
{
    constexpr int size = 32;
    const auto makeMesh = [](float bottom, float top) {
        MeshConfig config;
        for (const auto& corner : std::array<std::array<float,2>,6>{{{-10,bottom},{10,bottom},{10,top},{-10,bottom},{10,top},{-10,top}}})
            config.data.vertices.push_back({{corner[0], corner[1], -5}, {0, 0, 1}, {0, 0}, {1, 0, 0, 1}});
        for (std::uint32_t index = 0; index < 6; ++index) config.data.indices.push_back(index);
        return std::make_unique<Mesh>(config);
    };
    // Red sky over mid-grey ground: grey exposes any sRGB encoding mismatch.
    auto sky = makeMesh(0, 10), ground = makeMesh(-10, 0);
    Material red({.color = {0, 0, 0, 1}, .emission = {1, 0, 0}});
    Material grey({.color = {0, 0, 0, 1}, .emission = {.5f, .5f, .5f}});
    const std::array world{RenderCommand{.material = &red, .mesh = sky.get()}, RenderCommand{.material = &grey, .mesh = ground.get()}};
    const auto camera = Camera(CameraConfig{.fovY = 90.f}).GetCameraDataForTransform(glm::mat4(1), size, size);
    BasicLighting lighting; lighting.ambientIntensity = lighting.directionalIntensity = 0;

    RenderTexture monitor("Monitor.plutorendertexture", {.width = size, .height = size});
    RhiRenderTextureRenderer textures;
    const std::array views{RenderTextureView{&monitor, {.cameraData = camera, .commands = world}}};
    Require(textures.Render(device, views, {}, nullptr), "Render texture pass failed");

    // The screenshot: the camera's display output as the swapchain presents it.
    RhiSceneRenderer direct;
    Require(direct.Initialize(device, ShaderArtifactLibrary(PLUTO_RHI_TEST_SHADER_DIR).LoadBasicRendererPackage()) &&
            direct.Render(size, size, camera, lighting, world, world), "Reference render failed");
    const auto display = device.ReadTextureRgba8(direct.GetColorTexture());
    const auto shotPath = (std::filesystem::temp_directory_path() / "PlutoGE-render-texture-shot.tga").string();
    {
        std::ofstream tga(shotPath, std::ios::binary);
        const unsigned char header[18]{0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, size, 0, size, 0, 32, 8 | 32};
        tga.write(reinterpret_cast<const char*>(header), sizeof(header));
        for (int row = 0; row < size; ++row)
        {
            // Vulkan presents the display output flipped: its last row is the top.
            const int source = size - 1 - row;
            for (int x = 0; x < size; ++x)
            {
                const auto* pixel = &display[(source * size + x) * 4];
                const char bgra[4]{char(pixel[2]), char(pixel[1]), char(pixel[0]), char(255)};
                tga.write(bgra, 4);
            }
        }
    }

    struct TestLog final : Rml::SystemInterface {
        bool LogMessage(Rml::Log::Type, const Rml::String& message) override { std::cerr << message << '\n'; return true; }
    } log;
    Rml::SetSystemInterface(&log);
    RmlUiRhiRenderer ui(device, ShaderArtifactLibrary(PLUTO_RHI_TEST_SHADER_DIR).Load("RmlUi","vertex"),
                        ShaderArtifactLibrary(PLUTO_RHI_TEST_SHADER_DIR).Load("RmlUi","fragment"));
    // Like a project: the document lives in UI/ and names the texture relatively.
    const auto projectAssets = std::filesystem::temp_directory_path() / "PlutoGE-ui-render-texture" / "Assets";
    const auto documentPath = (projectAssets / "UI" / "preview.rml").make_preferred(); // Native backslashes.
    const auto texturePath = projectAssets / "Textures" / "Monitor.plutorendertexture";
    std::string requestedSource;
    // Only the correctly joined absolute path resolves, as with the real texture manager.
    ui.SetRenderTextureResolver([&](const std::string& source) {
        requestedSource = source;
        return std::filesystem::path(source).lexically_normal() == texturePath.lexically_normal() ? &monitor : nullptr; });
    ui.SetAntialiasingEnabled(false);
    Rml::SetRenderInterface(&ui);
    Require(Rml::Initialise(), "RmlUi initialization failed");
    struct RmlScope { ~RmlScope() { Rml::Shutdown(); Rml::SetRenderInterface(nullptr); Rml::SetSystemInterface(nullptr); } } scope;
    auto* context = Rml::CreateContext("RenderTexture", {size * 2, size});
    const std::string document = R"(<rml><head><style>body { margin: 0; } img { position: absolute; top: 0; width: 32px; height: 32px; }</style></head><body>
<img id="live" style="left: 0;" src="../Textures/Monitor.plutorendertexture"/><img id="shot" style="left: 32px;" src=")" + shotPath + R"("/></body></rml>)";
    std::filesystem::create_directories(documentPath.parent_path());
    { std::ofstream(documentPath) << document; }
    auto* doc = context->LoadDocument(ToRmlDocumentPath(documentPath));
    Require(doc != nullptr, "Render texture document failed to load");
    doc->Show(); context->Update(); ui.SetViewport(size * 2, size);
    Require(std::filesystem::path(requestedSource).lexically_normal() == texturePath.lexically_normal(),
            ("A relative <img src> must reach the renderer joined to its document's folder; got '" + requestedSource + "'").c_str());

    rhi::Texture target(device, device.CreateTexture({size * 2, size, Format::R8G8B8A8Unorm, TextureUsage::ColorAttachment, "UI render texture test", true, 1, false, 1}));
    auto& commands = device.GetImmediateContext();
    commands.BeginFrame("UI render texture test");
    RenderingInfo clear; clear.colorAttachments = {target.Get()}; clear.width = size * 2; clear.height = size; clear.clearDepth = false;
    commands.BeginRendering(clear); commands.EndRendering();
    ui.BeginFrame(target.Get(), false); context->Render(); ui.EndFrame(false);
    commands.Submit();
    const auto pixels = read(target.Get(), size * 2, size);

    int mismatches = 0, redPixels = 0, greyPixels = 0;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
        {
            const auto* live = &pixels[(y * size * 2 + x) * 4];
            const auto* shot = &pixels[(y * size * 2 + x + size) * 4];
            for (int c = 0; c < 3; ++c)
                if (std::abs(std::to_integer<int>(live[c]) - std::to_integer<int>(shot[c])) > 2) { ++mismatches; break; }
            const int r = std::to_integer<int>(shot[0]), g = std::to_integer<int>(shot[1]);
            redPixels += r > 200 && g < 30;
            greyPixels += g > 60 && std::abs(r - g) < 4;
        }
    Require(redPixels > size * size / 3 && greyPixels > size * size / 3, "Screenshot did not show both halves");
    // Allow the horizon row to differ by filtering.
    const auto sample = [&](int x, int y) {
        const auto* pixel = &pixels[(y * size * 2 + x) * 4];
        return "(" + std::to_string(std::to_integer<int>(pixel[0])) + "," + std::to_string(std::to_integer<int>(pixel[1])) + "," +
               std::to_string(std::to_integer<int>(pixel[2])) + ")";
    };
    Require(mismatches <= size * 2, ("Render texture image differs from its screenshot in " + std::to_string(mismatches) +
        " pixels; first row live " + sample(4, 2) + " shot " + sample(size + 4, 2) + ", last row live " +
        sample(4, size - 3) + " shot " + sample(size + 4, size - 3)).c_str());
    std::filesystem::remove(shotPath);
    std::filesystem::remove_all(projectAssets.parent_path());
    std::cout << "UI render texture image matched its screenshot (" << mismatches << " horizon pixels differ)\n";
}

int main(int argc,char** argv) try
{
    if(argc>1 && std::string_view(argv[1])=="--render-texture")
    {
        vulkan::VulkanDevice device;
        CheckRenderTextureImage(device,[&](TextureHandle texture,int,int) {return device.ReadTextureRgba8(texture);});
        return 0;
    }
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
        }, argc==5 ? argv[2] : nullptr, argc==5 ? argv[3] : nullptr, argc==5 ? argv[4] : nullptr);
    }
    else
    {
        vulkan::VulkanDevice device;
        CheckUi(device,[&](TextureHandle texture,int,int) {return device.ReadTextureRgba8(texture);},
                argc==5 ? argv[2] : nullptr, argc==5 ? argv[3] : nullptr, argc==5 ? argv[4] : nullptr);
    }
    return 0;
}
catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
