#include <RmlUi/Core.h>
#include "PlutoGE/render/RmlElementLookup.h"
#include <fstream>
#include <cmath>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <cstdlib>

// Asset-driven layout regression harness; no graphics device or project code required.
class LayoutRenderer final : public Rml::RenderInterface
{
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>, Rml::Span<const int>) override { return 1; }
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f, Rml::TextureHandle) override {}
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return {}; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 1; }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};

class DropProbe final : public Rml::EventListener
{
public:
    bool received = false;
    void ProcessEvent(Rml::Event& event) override
    {
        auto* source = static_cast<Rml::Element*>(event.GetParameter<void*>("drag_element", nullptr));
        received = source && source->GetId() == "bag-0" && event.GetCurrentElement()->GetId() == "bag-1";
    }
};

int main(int argc, char** argv) try
{
    if (argc != 5) throw std::runtime_error("Usage: document.rml font.ttf font-family panel-id");
    // Default Win32 assertion logging opens a modal dialog, which hangs CI.
    struct TestSystem final : Rml::SystemInterface
    {
        bool LogMessage(Rml::Log::Type type, const Rml::String& message) override
        {
            std::cerr << message << '\n';
            if (type == Rml::Log::LT_ASSERT) std::exit(EXIT_FAILURE);
            return true;
        }
    } system;
    Rml::SetSystemInterface(&system);
    LayoutRenderer renderer;
    Rml::SetRenderInterface(&renderer);
    if (!Rml::Initialise()) throw std::runtime_error("RmlUi initialization failed");
    std::ifstream stream(argv[2], std::ios::binary);
    const std::vector<Rml::byte> font((std::istreambuf_iterator<char>(stream)), {});
    if (!Rml::LoadFontFace(font, argv[3], Rml::Style::FontStyle::Normal)) throw std::runtime_error("Font load failed");
    auto* context = Rml::CreateContext("Document layout", {1280, 720});
    auto* document = context->LoadDocument(argv[1]);
    if (!document) throw std::runtime_error("Document load failed");
    // Exercise stable IDs, destruction, rename, replacement and a missing ID
    // becoming available, without retaining ownership through the lookup cache.
    {
        PlutoGE::render::RmlElementLookup lookup;
        auto container = document->CreateElement("div");
        auto* parent = document->AppendChild(std::move(container));
        parent->SetInnerRML("<div id=\"lookup-probe\"></div>");
        auto* first = lookup.Find(document, "lookup-probe");
        if (!first || lookup.Find(document, "lookup-probe") != first) throw std::runtime_error("Cached ID lookup failed");
        first->SetId("lookup-renamed");
        if (lookup.Find(document, "lookup-probe")) throw std::runtime_error("Renamed element returned for stale ID");
        if (lookup.Find(document, "lookup-renamed") != first) throw std::runtime_error("Renamed element missing");
        parent->SetInnerRML("<div id=\"lookup-probe\"></div>");
        if (lookup.Find(document, "lookup-renamed")) throw std::runtime_error("Destroyed element retained by cache");
        if (!lookup.Find(document, "lookup-probe")) throw std::runtime_error("Replacement element missing");
        lookup.Invalidate(document);
        if (!lookup.Find(document, "lookup-probe")) throw std::runtime_error("Invalidated lookup failed");
        document->RemoveChild(parent);
        if (lookup.Find(document, "lookup-probe")) throw std::runtime_error("Removed subtree retained by cache");
        lookup.Clear();
    }
    auto* panel = document->GetElementById(argv[4]);
    if (!panel) throw std::runtime_error("Panel missing");
    panel->SetProperty("display", "block");
    document->Show();
    if (Rml::String(argv[4]) == "minimap-panel")
    {
        auto* board = document->GetElementById("minimap-board");
        auto* player = document->GetElementById("minimap-player");
        auto* terrain = document->GetElementById("minimap-terrain");
        document->GetElementById("minimap-floor")->SetInnerRML("Lower crypt / floor 2");
        terrain->SetInnerRML("<div class=\"minimap-cell\" style=\"left:-40%;top:-40%;width:180%;height:180%;\"></div>");
        for (const auto size : {Rml::Vector2i(960, 540), Rml::Vector2i(1280, 720), Rml::Vector2i(1920, 1080)})
            for (const float scale : {.8f, 1.f, 1.25f})
                for (const int pixels : {160, 200, 240})
                {
                    // Canvas scaling reduces the document's logical viewport.
                    const Rml::Vector2i logical(int(size.x / scale), int(size.y / scale));
                    context->SetDimensions(logical);
                    board->SetProperty("width", std::to_string(pixels) + "px");
                    board->SetProperty("height", std::to_string(pixels) + "px");
                    context->Update(); context->Update();
                    const auto origin = panel->GetAbsoluteOffset(Rml::BoxArea::Border);
                    const auto bounds = panel->GetBox().GetSize(Rml::BoxArea::Border);
                    const auto boardOrigin = board->GetAbsoluteOffset(Rml::BoxArea::Content);
                    const auto boardSize = board->GetBox().GetSize(Rml::BoxArea::Content);
                    const auto playerCenter = player->GetAbsoluteOffset(Rml::BoxArea::Border) + player->GetBox().GetSize(Rml::BoxArea::Border) * .5f;
                    if (origin.x < 0 || origin.y < 0 || origin.x + bounds.x > logical.x || origin.y + bounds.y > logical.y)
                        throw std::runtime_error("Minimap escapes scaled viewport");
                    if (std::abs(boardSize.x - boardSize.y) > 1 || boardSize.x < 100)
                        throw std::runtime_error("Minimap lost square/readable viewport");
                    if ((playerCenter - (boardOrigin + boardSize * .5f)).Magnitude() > 2)
                        throw std::runtime_error("Minimap player is not centered");
                    if (board->GetComputedValues().overflow_x() != Rml::Style::Overflow::Hidden ||
                        board->GetComputedValues().overflow_y() != Rml::Style::Overflow::Hidden)
                        throw std::runtime_error("Minimap geometry is not clipped");
                    auto* controls = document->GetElementById("top-right");
                    if (origin.y < controls->GetAbsoluteOffset().y + controls->GetBox().GetSize().y)
                        throw std::runtime_error("Minimap overlaps menu controls");
                    auto* feedback = document->GetElementById("combat-feedback");
                    if (origin.y + bounds.y > feedback->GetAbsoluteOffset().y)
                        throw std::runtime_error("Minimap overlaps combat feedback");
                }
        Rml::Shutdown(); Rml::SetRenderInterface(nullptr);
        std::cout << "PASS: minimap square, centered, clipped and bounded at three resolutions, UI scales and sizes.\n";
        return 0;
    }
    if (Rml::String(argv[4]) == "gameplay-hud")
    {
        auto fail = [](const Rml::String& message) { Rml::Shutdown(); throw std::runtime_error(message); };
        auto set = [&](const char* id, const char* value) { document->GetElementById(id)->SetInnerRML(value); };
        set("save-status", "SAVE FAILED: latest progress is not saved. Hover for details.");
        document->GetElementById("save-status")->SetProperty("display", "block");
        set("room", "The Archivist Rotunda"); set("objective", "Find the Archivist and open the descent");
        set("experience", "LV 20"); set("skill-points", "+ 19 SKILLS");
        set("health", "HP 9999/9999"); set("mana", "MP 9999/9999");
        set("interaction", "RS: Pick up a legendary weapon"); set("combat-feedback", "DRAW 100% / RELEASE");
        set("boss", "THE BRIAR KING"); document->GetElementById("boss-track")->SetProperty("display", "block");
        document->GetElementById("minimap-panel")->SetProperty("display", "block");
        set("minimap-floor", "Ground");
        set("hud-notices", "<div class=\"hud-notice\">COMBAT Guard broken</div><div class=\"hud-notice\">PROGRESS Level 20! Skill points available.</div><div class=\"hud-notice\">LOOT Picked up an enchanted sword</div>");
        for (const auto* slot : {"primary", "secondary", "special"})
        {
            const auto id = Rml::String("attack-") + slot;
            document->GetElementById(id + "-name")->SetInnerRML("Piercing Arrow");
            document->GetElementById(id + "-state")->SetInnerRML("HOLD / RELEASE");
        }
        for (const auto size : {Rml::Vector2i(960,540), Rml::Vector2i(1280,720), Rml::Vector2i(1920,1080)})
            for (float scale : {.8f, 1.f, 1.25f})
            {
                const Rml::Vector2i logical(int(size.x/scale), int(size.y/scale));
                context->SetDimensions(logical); context->Update(); context->Update();
                const char* ids[] = {"header", "top-right", "boss", "vitals", "abilities", "interaction", "hud-notices", "minimap-panel", "combat-feedback", "save-status"};
                for (int i=0;i<10;++i)
                {
                    auto* a=document->GetElementById(ids[i]);
                    const auto p=a->GetAbsoluteOffset(Rml::BoxArea::Border), d=a->GetBox().GetSize(Rml::BoxArea::Border);
                    if(p.x<0 || p.y<0 || p.x+d.x>logical.x+1 || p.y+d.y>logical.y+1)
                        fail(Rml::String("HUD escapes viewport: ")+ids[i]);
                    for(int j=i+1;j<10;++j)
                    {
                        auto* b=document->GetElementById(ids[j]);
                        const auto q=b->GetAbsoluteOffset(Rml::BoxArea::Border), e=b->GetBox().GetSize(Rml::BoxArea::Border);
                        if(p.x<q.x+e.x && p.x+d.x>q.x && p.y<q.y+e.y && p.y+d.y>q.y)
                            fail(Rml::String("HUD overlaps: ")+ids[i]+" / "+ids[j]);
                    }
                }
                for(const auto* slot : {"primary","secondary","special"})
                {
                    auto* card=document->GetElementById(Rml::String("attack-")+slot);
                    auto* bar=document->GetElementById("abilities");
                    if(std::abs(card->GetAbsoluteOffset(Rml::BoxArea::Border).y-bar->GetAbsoluteOffset(Rml::BoxArea::Border).y)>1)
                        fail("Ability cards wrap outside their row");
                }
            }
        Rml::Shutdown(); Rml::SetRenderInterface(nullptr);
        std::cout << "PASS: gameplay HUD bounds and separation at three resolutions and UI scales.\n";
        return 0;
    }
    const bool journal = Rml::String(argv[4]) == "journal-panel";
    auto* scroll = journal ? document->GetElementById("backpack-scroll") : panel;
    for (const auto size : {Rml::Vector2i(1280, 720), Rml::Vector2i(1280, 960), Rml::Vector2i(1920, 1080)})
    {
        context->SetDimensions(size);
        // Force overflow, as a populated journal does after equipment and quests are loaded.
        // Exercise authored bounds, not a test-only height that masks auto-height overflow.
        Rml::String longContent;
        for (int i = 0; i < 40; ++i) longContent += "<p>Journal entry: Wanderer's Blade &amp; equipment. Explore the western wall to find the pilgrim cache.</p>";
        auto* overflow = document->CreateElement("div").release();
        overflow->SetInnerRML(longContent);
        scroll->AppendChild(Rml::ElementPtr(overflow));
        Rml::ElementList buttons;
        panel->GetElementsByTagName(buttons, "button");
        for (auto* button : buttons)
            if (!journal) button->SetInnerRML("Equipment slot: Wanderer's Blade / owned item details");
        context->Update();
        context->Update();
        const float width = panel->GetClientWidth();
        const float outerWidth = panel->GetBox().GetSize(Rml::BoxArea::Border).x;
        const float left = panel->GetAbsoluteOffset(Rml::BoxArea::Border).x;
        std::cout << size.x << 'x' << size.y << ": panel=" << outerWidth << " client=" << width << " left=" << left << '\n';
        const float top = panel->GetAbsoluteOffset(Rml::BoxArea::Border).y;
        const float height = panel->GetBox().GetSize(Rml::BoxArea::Border).y;
        std::cout << "top=" << top << " height=" << height << " bottom=" << top + height << '\n';
        if (top < 0 || top + height > size.y) throw std::runtime_error("Panel escapes the viewport vertically");
        if (width < 500) throw std::runtime_error("Scrollbar collapsed the panel content width");
        if (left < 0 || left + outerWidth > size.x) throw std::runtime_error("Panel escapes the viewport");
        if (std::abs(left + outerWidth / 2 - size.x / 2) > 2) throw std::runtime_error("Panel is not centered");
        if (scroll->GetScrollHeight() <= scroll->GetClientHeight()) throw std::runtime_error("Fixture did not exercise overflow");
        for (auto* button : buttons)
        {
            bool hidden = false;
            for (auto* ancestor = button; ancestor; ancestor = ancestor->GetParentNode())
                hidden |= ancestor->GetComputedValues().display() == Rml::Style::Display::None;
            if (hidden) continue; // Inactive tab pages deliberately have no layout boxes.
            float minimum = !journal ? 450 : button->GetId() == "close-journal" ? 24 : button->IsClassSet("inventory-slot") ? 44 : 85;
            if (button->GetBox().GetSize().x < minimum) throw std::runtime_error("Button content collapsed");
        }
        if (auto* source = document->GetElementById("bag-0"); source && Rml::String(argv[4]) == "journal-panel")
        {
            auto* target = document->GetElementById("bag-1");
            // A drag clone lives outside #journal-panel. Reproduce that ancestry
            // so a regression cannot hide behind the source slot's grid styles.
            auto preview = source->Clone();
            preview->SetId("drag-preview-probe");
            preview->SetInnerRML("<span class=\"slot-caption\">1</span><br/>Cinder Staff");
            preview->SetPseudoClass("drag", true);
            preview->SetProperty("position", "absolute");
            preview->SetProperty("left", "0px"); preview->SetProperty("top", "0px");
            auto* card = document->AppendChild(std::move(preview));
            context->Update();
            const auto previewSize = card->GetBox().GetSize(Rml::BoxArea::Border);
            if (std::abs(previewSize.x - 144) > 0.5f || std::abs(previewSize.y - 84) > 0.5f)
                throw std::runtime_error("Reparented drag preview lost its card dimensions");
            if (card->GetComputedValues().overflow_x() != Rml::Style::Overflow::Hidden ||
                card->GetComputedValues().overflow_y() != Rml::Style::Overflow::Hidden ||
                std::abs(card->GetComputedValues().font_size() - 16) > 0.5f)
                throw std::runtime_error("Reparented drag preview lost its font or clipping");
            document->RemoveChild(card);

            scroll->SetScrollTop(0);
            document->GetElementById("inventory-grid")->SetScrollTop(0);
            source->SetProperty("drag", "clone");
            context->Update();
            source->ScrollIntoView(Rml::ScrollIntoViewOptions(Rml::ScrollAlignment::Nearest));
            context->Update();
            const auto a = source->GetAbsoluteOffset(Rml::BoxArea::Border) + Rml::Vector2f(12, 12);
            const auto b = target->GetAbsoluteOffset(Rml::BoxArea::Border) + Rml::Vector2f(12, 12);
            DropProbe probe;
            target->AddEventListener(Rml::EventId::Dragdrop, &probe);
            context->ProcessMouseMove(int(a.x), int(a.y), 0);
            context->ProcessMouseButtonDown(0, 0);
            context->ProcessMouseMove(int(a.x + 10), int(a.y), 0);
            context->ProcessMouseMove(int(b.x), int(b.y), 0);
            context->ProcessMouseButtonUp(0, 0);
            target->RemoveEventListener(Rml::EventId::Dragdrop, &probe);
            if (!probe.received) throw std::runtime_error("Native slot drag did not deliver its source to the destination");
            std::cout << "Native drag source bag-0 -> destination bag-1 passed.\n";
        }
        if (journal)
        {
            auto* tabs = document->GetElementById("journal-tabs");
            auto* footer = document->GetElementById("journal-footer");
            auto* inventoryPage = document->GetElementById("inventory-page");
            const float contentTop = inventoryPage->GetAbsoluteOffset(Rml::BoxArea::Border).y;
            const float contentBottom = contentTop + inventoryPage->GetBox().GetSize(Rml::BoxArea::Border).y;
            if (tabs->GetAbsoluteOffset(Rml::BoxArea::Border).y + tabs->GetBox().GetSize(Rml::BoxArea::Border).y > contentTop ||
                contentBottom > footer->GetAbsoluteOffset(Rml::BoxArea::Border).y)
                throw std::runtime_error("Tab contents overlap fixed navigation");
            const auto first = document->GetElementById("bag-0")->GetAbsoluteOffset();
            const auto eighth = document->GetElementById("bag-7")->GetAbsoluteOffset();
            const auto ninth = document->GetElementById("bag-8")->GetAbsoluteOffset();
            if (std::abs(first.y - eighth.y) > 1 || ninth.y <= first.y) throw std::runtime_error("Backpack is not an eight-column grid");
            auto* main = document->GetElementById("inventory-main");
            auto* inspector = document->GetElementById("selected-item-panel");
            if (main->GetAbsoluteOffset().x + main->GetBox().GetSize(Rml::BoxArea::Border).x > inspector->GetAbsoluteOffset().x)
                throw std::runtime_error("Selected item inspector overlaps the character sheet");
            auto* inspection = document->GetElementById("item-inspection");
            document->GetElementById("item-detail")->SetInnerRML(longContent);
            context->Update(); inspection->SetScrollTop(10000); context->Update();
            if (inspection->GetScrollTop() <= 0) throw std::runtime_error("Long item stats cannot scroll independently");
            const float inspectionBottom = inspection->GetAbsoluteOffset().y + inspection->GetBox().GetSize(Rml::BoxArea::Border).y;
            if (inspectionBottom > document->GetElementById("item-actions")->GetAbsoluteOffset().y)
                throw std::runtime_error("Item stats overlap equip/drop actions");
            inventoryPage->SetProperty("display", "none");
            auto* quests = document->GetElementById("quests-page");
            quests->SetProperty("display", "block");
            document->GetElementById("quests")->SetInnerRML(longContent);
            context->Update();
            quests->SetScrollTop(10000); context->Update();
            if (quests->GetScrollTop() <= 0) throw std::runtime_error("Quest tab cannot scroll");
            quests->SetProperty("display", "none"); inventoryPage->SetProperty("display", "block");
        }
        scroll->SetScrollTop(10000);
        context->Update();
        if (scroll->GetScrollTop() <= 0) throw std::runtime_error("Overflow cannot be scrolled");
    }
    Rml::Shutdown();
    Rml::SetRenderInterface(nullptr);
    std::cout << "PASS: centered, readable, scrollable panel at three viewport sizes.\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
