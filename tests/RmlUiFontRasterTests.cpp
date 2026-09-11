#include <PlutoGE_RmlUi_FontRaster.h>
#include <RmlUi/Core.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

namespace
{
    void Require(bool condition, const char* message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    class Recorder final : public Rml::RenderInterface
    {
    public:
        Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int>) override
        {
            const auto id = ++next;
            geometry[id] = {vertices.begin(), vertices.end()};
            return id;
        }
        void RenderGeometry(Rml::CompiledGeometryHandle id, Rml::Vector2f translation, Rml::TextureHandle texture) override
        {
            if (!texture) return;
            const auto& vertices = geometry.at(id);
            const auto size = textures.at(texture);
            for (const auto& vertex : vertices)
            {
                const auto p = vertex.position + translation;
                bounds[0] = std::min(bounds[0], p.x);
                bounds[1] = std::min(bounds[1], p.y);
                bounds[2] = std::max(bounds[2], p.x);
                bounds[3] = std::max(bounds[3], p.y);
            }
            // Font meshes are quads: texel density must rise without enlarging
            // the logical glyph geometry when the canvas is scaled.
            for (size_t i = 0; i + 3 < vertices.size(); i += 4)
            {
                const float width = std::abs(vertices[i + 1].position.x - vertices[i].position.x);
                const float texels = std::abs(vertices[i + 1].tex_coord.x - vertices[i].tex_coord.x) * size.x;
                if (width > 0 && texels > 0)
                    densities.push_back(texels / width);
            }
        }
        void ReleaseGeometry(Rml::CompiledGeometryHandle id) override { geometry.erase(id); }
        Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return {}; }
        Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> pixels, Rml::Vector2i size) override
        {
            Require(pixels.size() == size_t(size.x) * size.y * 4, "Atlas size mismatch");
            Require(std::any_of(pixels.begin(), pixels.end(), [](auto p) { return p != 0; }), "Empty font atlas");
            const auto id = ++next;
            textures[id] = size;
            ++uploads;
            return id;
        }
        void ReleaseTexture(Rml::TextureHandle id) override { textures.erase(id); }
        void EnableScissorRegion(bool) override {}
        void SetScissorRegion(Rml::Rectanglei) override {}
        void SetTransform(const Rml::Matrix4f*) override {}

        std::vector<float> densities;
        std::array<float, 4> bounds{1e6f, 1e6f, -1e6f, -1e6f};
        int uploads = 0;
        size_t next = 0;
        std::unordered_map<size_t, std::vector<Rml::Vertex>> geometry;
        std::unordered_map<size_t, Rml::Vector2i> textures;
    };
}

int main() try
{
    Recorder renderer;
    Rml::SetRenderInterface(&renderer);
    Require(Rml::Initialise(), "RmlUi initialization failed");
    Require(Rml::LoadFontFace(PLUTO_FONT_TEST_FILE), "Test font failed to load");
    auto* context = Rml::CreateContext("Font density", {1200, 800});
    auto* document = context->LoadDocumentFromMemory(R"(<rml><head><style>
        body { margin: 0; width: 400px; height: 250px; font-family: Martian Mono; font-size: 16px;
               transform-origin: 0px 0px; transform: scale(3); }
        #label { position: absolute; left: 24px; top: 24px; width: 180px; }
        </style></head><body><div id="label">Fullscreen UI AV 0123456789</div></body></rml>)");
    Require(document != nullptr, "Test document failed to load");
    document->Show();
    context->Update();
    context->Render();
    auto* label = document->GetElementById("label");
    const auto originalSize = label->GetBox().GetSize();
    const auto* originalHit = context->GetElementAtPoint({90, 90});
    Require(originalHit != nullptr, "Scaled UI hit test failed");

    auto checkDensity = [&](float scale, int expected)
    {
        PlutoGE_SetRmlUiFontRasterScale(scale);
        context->Update();
        renderer.densities.clear();
        context->Render();
        Require(!renderer.densities.empty(), "Text did not render");
        for (float density : renderer.densities)
            Require(std::abs(density - expected) < 0.01f, "Glyph atlas was magnified instead of rasterized at display density");
        Require(label->GetBox().GetSize() == originalSize, "Raster density changed line wrapping or layout");
        Require(context->GetElementAtPoint({90, 90}) == originalHit, "Raster density changed scaled UI hit testing");
    };
    checkDensity(1, 1);
    checkDensity(3, 3);
    const int uploads = renderer.uploads;
    checkDensity(2.5f, 3);
    Require(renderer.uploads == uploads, "Same density bucket rebuilt the atlas");
    checkDensity(6, 4);
    checkDensity(1, 1);
    // Shared-atlas shadows and unique-atlas outlines must retain their authored
    // thickness/offset even though the base glyph atlas has more texels.
    for (const char* effect : {"shadow(3px 3px #333)", "outline(2px #f00)"})
    {
        label->SetProperty("font-effect", effect);
        PlutoGE_SetRmlUiFontRasterScale(1);
        context->Update();
        renderer.bounds = {1e6f, 1e6f, -1e6f, -1e6f};
        context->Render();
        const auto originalBounds = renderer.bounds;
        PlutoGE_SetRmlUiFontRasterScale(3);
        renderer.bounds = {1e6f, 1e6f, -1e6f, -1e6f};
        renderer.densities.clear();
        context->Render();
        Require(std::any_of(renderer.densities.begin(), renderer.densities.end(), [](float d) { return std::abs(d - 3) < .01f; }),
                "Font effect disabled high-density base glyphs");
        for (size_t i = 0; i < originalBounds.size(); ++i)
            Require(std::abs(originalBounds[i] - renderer.bounds[i]) <= 1.0f, "Font effect thickness or offset changed with density");
        Require(label->GetBox().GetSize() == originalSize, "Font effect density changed layout");
    }
    Rml::Shutdown();
    Rml::SetRenderInterface(nullptr);
    Require(renderer.geometry.empty() && renderer.textures.empty(), "Font resources leaked on shutdown");
    std::cout << "Font density, fullscreen scale, layout, hit testing, resize and resource lifetime passed.\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
