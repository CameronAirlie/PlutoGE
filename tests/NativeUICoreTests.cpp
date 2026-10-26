#include "PlutoGE/ui/render/DrawList.h"
#include "PlutoGE/ui/render/Painter.h"
#include "PlutoGE/ui/text/FontLibrary.h"
#include "PlutoGE/ui/text/RichText.h"
#include "PlutoGE/ui/text/TextLayout.h"
#include "PlutoGE/ui/text/Utf8.h"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
    using namespace PlutoGE::ui;

    void Require(bool value, const std::string &message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    bool Near(float a, float b, float epsilon = 0.01f) { return std::abs(a - b) <= epsilon; }

    FontFace &TestFont()
    {
        static FontFace *face = FontLibrary::Get().Load(PLUTO_UI_TEST_FONT);
        Require(face != nullptr, "Test font failed to load: " + FontLibrary::Get().GetLastError());
        return *face;
    }

    TextContent Plain(const char *text, float size = 20.0f)
    {
        return ParseRichText(text, TextStyle{.font = &TestFont(), .size = size}, false);
    }

    TextLayout Layout(const TextContent &content, TextLayoutSettings settings = {})
    {
        return LayoutText(content, settings, FontLibrary::Get(), GetTextShaper());
    }

    void TestUtf8()
    {
        // Codepoints are spelled numerically: MSVC without /utf-8 mis-encodes \u escapes in U"" literals.
        const auto decoded = DecodeUtf8("A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80");
        Require(decoded == std::u32string{0x41, 0xE9, 0x20AC, 0x1F600}, "UTF-8 decode failed");
        Require(EncodeUtf8(decoded) == "A\xC3\xA9\xE2\x82\xAC\xF0\x9F\x98\x80", "UTF-8 round trip failed");
        const std::u32string replacement{0xFFFD};
        Require(DecodeUtf8("\xC0\xAF") == replacement, "Overlong encodings must be rejected");
        Require(DecodeUtf8("\xE2\x82") == replacement, "Truncated sequences must be replaced");
    }

    void TestRichText()
    {
        const TextStyle base{.size = 10.0f, .color = {1, 1, 1, 1}};
        const auto content = ParseRichText("a<b>b<color=#FF0000>c</color></b><size=30>d</size><br><x>", base);
        Require(content.text == U"abcd\n<x>", "Rich text literal text mismatch");
        Require(content.spans.size() == 5, "Expected five style spans, got " + std::to_string(content.spans.size()));
        Require(content.styles[content.spans[1].style].bold, "<b> was not applied");
        const auto &red = content.styles[content.spans[2].style];
        Require(red.bold && Near(red.color.r, 1.0f) && Near(red.color.g, 0.0f), "Nested color failed");
        Require(Near(content.styles[content.spans[3].style].size, 30.0f), "<size> failed");
        Require(!content.styles[content.spans[4].style].bold, "Closing tags must restore the base style");

        const auto plain = ParseRichText("<b>x</b>", base, false);
        Require(plain.text == U"<b>x</b>", "Markup must be literal when disabled");
    }

    void TestGlyphAtlas()
    {
        auto &face = TestFont();
        GlyphAtlas atlas(256, 2);
        const AtlasGlyph *glyph = atlas.GetGlyph(face, face.GetGlyphIndex(U'O'));
        Require(glyph && glyph->hasBitmap, "Glyph O was not rasterized");
        Require(atlas.GetPages().size() == 1 && atlas.GetPages()[0].dirty.has_value(), "Atlas page not marked dirty");
        Require(atlas.GetGlyph(face, face.GetGlyphIndex(U'O')) == glyph, "Glyphs must be cached");

        // The stroke of 'l' must be inside (> 128) and its surroundings outside.
        SdfGlyphBitmap bitmap;
        Require(face.RasterizeSdf(face.GetGlyphIndex(U'l'), bitmap) && bitmap.width > 0, "SDF raster failed");
        const auto at = [&](int x, int y) { return bitmap.pixels[static_cast<std::size_t>(y) * bitmap.width + x]; };
        Require(at(bitmap.width / 2, bitmap.height / 2) > 128, "SDF inside must be above the edge value");
        Require(at(0, 0) < 128, "SDF outside must be below the edge value");
        Require(bitmap.planeTop > bitmap.planeBottom && bitmap.planeRight > bitmap.planeLeft, "Bad plane bounds");

        SdfGlyphBitmap space;
        Require(face.RasterizeSdf(face.GetGlyphIndex(U' '), space) && space.width == 0, "Space should be empty");
    }

    void TestLineBreaking()
    {
        const auto content = Plain("hello world again");
        const auto natural = Layout(content);
        Require(natural.lines.size() == 1, "Unconstrained text must stay on one line");
        Require(natural.size.x > 0 && natural.size.y > 0, "Natural size must be positive");

        TextLayoutSettings wrapped;
        wrapped.maxWidth = Layout(Plain("hello world")).size.x + 1.0f;
        const auto layout = Layout(content, wrapped);
        Require(layout.lines.size() == 2, "Expected two wrapped lines, got " + std::to_string(layout.lines.size()));
        for (const auto &line : layout.lines)
            Require(line.width <= wrapped.maxWidth + 0.01f, "Wrapped line exceeds max width");
        Require(Near(layout.lines[1].top, -layout.lines[0].height), "Second line must start below the first");
        // Leading spaces of the wrapped line are dropped: it starts at the left edge.
        Require(Near(layout.glyphs[layout.lines[1].glyphBegin].pen.x, 0.0f), "Wrapped line should start at x = 0");

        TextLayoutSettings tiny;
        tiny.maxWidth = 1.0f;
        tiny.wrap = TextWrap::Character;
        Require(Layout(Plain("abc"), tiny).lines.size() == 3, "Character wrap must break every glyph");

        Require(Layout(Plain("a\n\nb")).lines.size() == 3, "Newlines must force line breaks");
    }

    void TestAlignmentAndEllipsis()
    {
        const auto content = Plain("centered");
        const float width = Layout(content).size.x;
        TextLayoutSettings settings;
        settings.maxWidth = width + 100.0f;
        settings.horizontal = TextHorizontalAlign::Center;
        Require(Near(Layout(content, settings).lines[0].left, 50.0f), "Center alignment offset wrong");
        settings.horizontal = TextHorizontalAlign::Right;
        Require(Near(Layout(content, settings).lines[0].left, 100.0f), "Right alignment offset wrong");

        settings = {};
        settings.maxHeight = 200.0f;
        settings.vertical = TextVerticalAlign::Bottom;
        const auto bottom = Layout(content, settings);
        Require(Near(bottom.lines[0].top - bottom.lines[0].height, -200.0f), "Bottom alignment must touch the box bottom");

        settings = {};
        settings.maxWidth = width * 0.5f;
        settings.wrap = TextWrap::None;
        settings.overflow = TextOverflow::Ellipsis;
        const auto truncated = Layout(content, settings);
        Require(truncated.truncated, "Overlong text must report truncation");
        Require(truncated.lines[0].width <= settings.maxWidth + 0.01f, "Ellipsized line exceeds max width");

        settings = {};
        settings.maxWidth = width * 0.6f;
        settings.maxHeight = Layout(content).size.y * 1.5f;
        settings.overflow = TextOverflow::Ellipsis;
        const auto clipped = Layout(Plain("one two three four five six"), settings);
        Require(clipped.lines.size() == 1 && clipped.truncated, "Height-limited text must keep one ellipsized line");
    }

    void TestDecorations()
    {
        const auto content = ParseRichText("plain <u>under</u> <s>strike</s>", TextStyle{.font = &TestFont(), .size = 20.0f});
        const auto layout = Layout(content);
        Require(layout.decorations.size() == 2, "Expected underline and strikethrough rects");
        const auto &under = layout.decorations[0].rect, &strike = layout.decorations[1].rect;
        Require(under.max.y < layout.lines[0].baseline, "Underline must sit below the baseline");
        Require(strike.min.y > layout.lines[0].baseline, "Strikethrough must sit above the baseline");
    }

    void TestDrawListBatching()
    {
        DrawList list;
        list.SetViewportClip({{0, 0}, {100, 100}});
        DrawBox(list, {{10, 10}, {20, 20}}, BoxStyle{});
        DrawBox(list, {{30, 10}, {40, 20}}, BoxStyle{});
        Require(list.GetCommands().size() == 1, "Untextured boxes with one clip must share a batch");
        Require(list.GetIndices().size() == 12 && list.GetVertices().size() == 8, "Two quads expected");

        list.PushClip({{0, 0}, {50, 50}});
        DrawBox(list, {{10, 10}, {20, 20}}, BoxStyle{});
        list.PopClip();
        Require(list.GetCommands().size() == 2, "A new clip must start a new batch");
        Require(list.GetCommands()[1].scissor == Rect({0, 0}, {50, 50}), "Clip scissor mismatch");

        list.PushClip({{200, 200}, {300, 300}});
        DrawBox(list, {{210, 210}, {220, 220}}, BoxStyle{});
        list.PopClip();
        Require(list.GetCommands().size() == 2, "Geometry outside the viewport clip must be dropped");

        list.SetTransform(Transform2D::Translation({5, 5}));
        list.PushClip({{0, 0}, {10, 10}}, CornerRadii::All(4.0f));
        DrawBox(list, {{0, 0}, {10, 10}}, BoxStyle{});
        list.PopClip();
        const auto &vertex = list.GetVertices().back();
        Require(vertex.clipRect == glm::vec4(5, 5, 15, 15) && Near(vertex.clipRadii.x, 4.0f),
                "Rounded clip must be stored in target pixels");
    }

    void TestPainter()
    {
        DrawList list;
        const Rect rect{{0, 0}, {100, 50}};
        DrawBox(list, rect, BoxStyle{.fill = Paint::Linear({1, 0, 0, 1}, {0, 0, 1, 1}, 90.0f)});
        const auto &vertices = list.GetVertices();
        Require(Near(vertices[0].color.r, 1.0f) && Near(vertices[2].color.b, 1.0f), "Vertical gradient endpoints wrong");

        list.Clear();
        DrawImage(list, rect, ImageStyle{.mode = ImageDrawMode::FilledRadial, .fillAmount = 0.25f});
        Require(list.GetVertices().size() >= 4, "Radial fill must produce a fan");
        Require(list.GetVertices()[0].position == rect.Center(), "Radial fan starts at the centre");

        list.Clear();
        DrawImage(list, rect, ImageStyle{.textureSize = {32, 32}, .mode = ImageDrawMode::Sliced, .sliceBorder = Edges::All(8.0f)});
        Require(list.GetVertices().size() == 36, "Nine-slice must emit nine quads");

        list.Clear();
        DrawImage(list, rect, ImageStyle{.mode = ImageDrawMode::FilledHorizontal, .fillAmount = 0.5f});
        Require(Near(list.GetVertices()[1].position.x, 50.0f), "Horizontal fill must stop at half width");
    }

    void TestTextDrawing()
    {
        const auto content = Plain("Hi");
        const auto layout = Layout(content);
        DrawList list;
        DrawText(list, layout, content, {0, 0}, TextEffects{}, FontLibrary::Get().GetAtlas());
        Require(list.GetVertices().size() == 8, "Two glyph quads expected");
        Require(list.GetTextures().front().kind == TextureKey::Kind::GlyphAtlas, "Glyphs must sample the atlas");
        Require(list.GetVertices()[0].params.z == static_cast<float>(PrimitiveKind::Glyph), "Glyph kind not set");
    }
}

int main()
{
    try
    {
        TestUtf8();
        TestRichText();
        TestGlyphAtlas();
        TestLineBreaking();
        TestAlignmentAndEllipsis();
        TestDecorations();
        TestDrawListBatching();
        TestPainter();
        TestTextDrawing();
    }
    catch (const std::exception &error)
    {
        std::cerr << "NativeUICoreTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "NativeUICoreTests passed\n";
    return 0;
}
