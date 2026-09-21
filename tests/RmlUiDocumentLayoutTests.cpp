#include <RmlUi/Core.h>
#include <fstream>
#include <cmath>
#include <iostream>
#include <iterator>
#include <stdexcept>

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

int main(int argc, char** argv) try
{
    if (argc != 5) throw std::runtime_error("Usage: document.rml font.ttf font-family panel-id");
    LayoutRenderer renderer;
    Rml::SetRenderInterface(&renderer);
    if (!Rml::Initialise()) throw std::runtime_error("RmlUi initialization failed");
    std::ifstream stream(argv[2], std::ios::binary);
    const std::vector<Rml::byte> font((std::istreambuf_iterator<char>(stream)), {});
    if (!Rml::LoadFontFace(font, argv[3], Rml::Style::FontStyle::Normal)) throw std::runtime_error("Font load failed");
    auto* context = Rml::CreateContext("Document layout", {1280, 720});
    auto* document = context->LoadDocument(argv[1]);
    if (!document) throw std::runtime_error("Document load failed");
    auto* panel = document->GetElementById(argv[4]);
    if (!panel) throw std::runtime_error("Panel missing");
    panel->SetProperty("display", "block");
    document->Show();
    for (const auto size : {Rml::Vector2i(1280, 720), Rml::Vector2i(1280, 960), Rml::Vector2i(1920, 1080)})
    {
        context->SetDimensions(size);
        // Force overflow, as a populated journal does after equipment and quests are loaded.
        // Exercise authored bounds, not a test-only height that masks auto-height overflow.
        Rml::String longContent;
        for (int i = 0; i < 40; ++i) longContent += "<p>Journal entry: Wanderer's Blade &amp; equipment. Explore the western wall to find the pilgrim cache.</p>";
        auto* overflow = document->CreateElement("div").release();
        overflow->SetInnerRML(longContent);
        panel->AppendChild(Rml::ElementPtr(overflow));
        Rml::ElementList buttons;
        panel->GetElementsByTagName(buttons, "button");
        for (auto* button : buttons) button->SetInnerRML("Equipment slot: Wanderer's Blade / owned item details");
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
        if (panel->GetScrollHeight() <= panel->GetClientHeight()) throw std::runtime_error("Fixture did not exercise overflow");
        for (auto* button : buttons)
            if (button->GetBox().GetSize().x < 450) throw std::runtime_error("Button content collapsed");
        panel->SetScrollTop(10000);
        context->Update();
        if (panel->GetScrollTop() <= 0) throw std::runtime_error("Overflow cannot be scrolled");
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
