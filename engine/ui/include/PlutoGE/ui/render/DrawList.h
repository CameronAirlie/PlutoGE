#pragma once

#include "PlutoGE/ui/Geometry.h"

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Backend-neutral UI geometry. Painters append shapes, images and glyphs; a
// renderer (render/UIRhiRenderer) resolves the texture keys and draws the
// batches. Nothing here depends on the RHI or on scene types.
namespace PlutoGE::ui
{
    // Identifies a texture without owning it; the renderer decides how to load it.
    struct TextureKey
    {
        enum class Kind : std::uint8_t
        {
            White,      // 1x1 opaque white; untextured geometry
            Asset,      // texture asset path or reference
            GlyphAtlas, // page of the shared glyph atlas
        };

        Kind kind = Kind::White;
        std::string path;
        std::uint32_t page = 0;

        static TextureKey White() { return {}; }
        static TextureKey Asset(std::string path) { return {Kind::Asset, std::move(path), 0}; }
        static TextureKey AtlasPage(std::uint32_t page) { return {Kind::GlyphAtlas, {}, page}; }

        friend bool operator==(const TextureKey &, const TextureKey &) = default;
    };

    // How the fragment shader evaluates a primitive.
    enum class PrimitiveKind : std::uint32_t
    {
        Shape = 0,  // rounded rect SDF with optional border, texture and soft edge
        Glyph = 1,  // signed-distance glyph with optional outline and weight
    };

    // One vertex. Every attribute is float because the OpenGL RHI binds all
    // vertex attributes as GL_FLOAT. Layout mirrors shaders/NativeUI.slang.
    struct DrawVertex
    {
        glm::vec2 position{0.0f};    // target pixels, y-up
        glm::vec2 uv{0.0f};
        glm::vec4 color{1.0f};       // straight alpha
        glm::vec4 local{0.0f};       // xy: point relative to the shape centre, zw: half size
        glm::vec4 radii{0.0f};       // shape corner radii (tl, tr, br, bl); glyph: x = weight
        glm::vec4 params{0.0f};      // x: border/outline width, y: softness, z: PrimitiveKind, w: 1 = inverted
        glm::vec4 borderColor{0.0f}; // border or glyph outline colour, straight alpha
        glm::vec4 clipRect{-1e6f, -1e6f, 1e6f, 1e6f}; // rounded clip rectangle in target pixels
        glm::vec4 clipRadii{0.0f};
    };

    struct DrawCommand
    {
        std::uint32_t texture = 0; // index into DrawList::GetTextures()
        Rect scissor;              // target pixels, y-up
        std::uint32_t indexOffset = 0;
        std::uint32_t indexCount = 0;
    };

    // Parameters shared by every vertex of one shape.
    struct ShapeParams
    {
        Rect rect;                 // the shape's own rectangle, before the transform
        CornerRadii radii;
        float borderWidth = 0.0f;
        glm::vec4 borderColor{0.0f};
        float softness = 0.0f;     // extra edge blur in local units (shadows)
        bool invert = false;       // cover the outside of the shape (inset shadows)
        PrimitiveKind kind = PrimitiveKind::Shape;
        glm::vec4 glyphParams{0.0f}; // glyph-only: x = weight offset
    };

    class DrawList
    {
    public:
        void Clear();
        [[nodiscard]] bool Empty() const { return m_indices.empty(); }

        // Rect transform from local layout units to target pixels.
        void SetTransform(const Transform2D &transform) { m_transform = transform; }
        [[nodiscard]] const Transform2D &GetTransform() const { return m_transform; }

        // Clipping. Rects are in local units under the current transform.
        // Axis-aligned clips become scissor rects; the innermost rounded clip
        // is evaluated per pixel. Rotated clips fall back to their bounds.
        void PushClip(const Rect &rect, const CornerRadii &radii = {});
        void PopClip();
        // Clip in target pixels, used as the base for the whole list.
        void SetViewportClip(const Rect &pixels);

        std::uint32_t AddTexture(const TextureKey &key);

        // Appends a convex polygon (fan triangulated). Points and UVs are in
        // local units; colours are per point so gradients come from interpolation.
        void AddConvexPolygon(std::span<const glm::vec2> points,
                              std::span<const glm::vec2> uvs,
                              std::span<const glm::vec4> colors,
                              const ShapeParams &shape,
                              std::uint32_t texture);
        // Convenience for an axis-aligned quad in local units.
        void AddQuad(const Rect &rect, const Rect &uvRect, const glm::vec4 (&cornerColors)[4],
                     const ShapeParams &shape, std::uint32_t texture);

        [[nodiscard]] const std::vector<DrawVertex> &GetVertices() const { return m_vertices; }
        [[nodiscard]] const std::vector<std::uint32_t> &GetIndices() const { return m_indices; }
        [[nodiscard]] const std::vector<DrawCommand> &GetCommands() const { return m_commands; }
        [[nodiscard]] const std::vector<TextureKey> &GetTextures() const { return m_textures; }

    private:
        struct ClipState
        {
            Rect scissor;        // target pixels
            glm::vec4 rounded{-1e6f, -1e6f, 1e6f, 1e6f};
            glm::vec4 roundedRadii{0.0f};
        };

        [[nodiscard]] const ClipState &CurrentClip() const;
        void PrepareCommand(std::uint32_t texture);

        std::vector<DrawVertex> m_vertices;
        std::vector<std::uint32_t> m_indices;
        std::vector<DrawCommand> m_commands;
        std::vector<TextureKey> m_textures;
        std::vector<ClipState> m_clips;
        ClipState m_viewportClip{.scissor = {glm::vec2(-1e6f), glm::vec2(1e6f)}};
        Transform2D m_transform;
    };
}
