#pragma once

#include "PlutoGE/ui/Geometry.h"

#include <cstdint>
#include <functional>
#include <limits>
#include <vector>

// Retained-mode layout over a flat node tree. Nodes carry plain style data;
// the scene adapter builds one tree per canvas from RectTransform and layout
// components and reads the rectangles back. UI space is y-up.
namespace PlutoGE::ui
{
    inline constexpr float kUnbounded = std::numeric_limits<float>::infinity();

    enum class LayoutMode : std::uint8_t
    {
        None, // children use their anchors
        Flex,
        Grid,
    };

    enum class FlexDirection : std::uint8_t { Row, Column, RowReverse, ColumnReverse };
    enum class FlexWrap : std::uint8_t { NoWrap, Wrap };
    enum class JustifyContent : std::uint8_t { Start, Center, End, SpaceBetween, SpaceAround, SpaceEvenly };
    // Start is the top edge for vertical alignment and the left edge for horizontal.
    enum class AlignItems : std::uint8_t { Start, Center, End, Stretch };
    enum class AlignContent : std::uint8_t { Start, Center, End, Stretch, SpaceBetween, SpaceAround };
    enum class AlignSelf : std::uint8_t { Auto, Start, Center, End, Stretch };

    // How an item's preferred size is chosen on one axis.
    enum class SizeMode : std::uint8_t
    {
        Fixed,   // the authored size (RectTransform size delta)
        Content, // measured from content: text, image or the item's own children
    };

    struct GridTrack
    {
        enum class Kind : std::uint8_t { Fixed, Auto, Fraction };

        Kind kind = Kind::Fraction;
        float value = 1.0f;

        static GridTrack Fixed(float pixels) { return {Kind::Fixed, pixels}; }
        static GridTrack Auto() { return {Kind::Auto, 0.0f}; }
        static GridTrack Fraction(float weight = 1.0f) { return {Kind::Fraction, weight}; }

        friend bool operator==(const GridTrack &, const GridTrack &) = default;
    };

    // How a node arranges its children.
    struct ContainerLayout
    {
        LayoutMode mode = LayoutMode::None;
        Edges padding;
        glm::vec2 gap{0.0f}; // x: horizontal spacing, y: vertical spacing

        FlexDirection direction = FlexDirection::Row;
        FlexWrap wrap = FlexWrap::NoWrap;
        JustifyContent justify = JustifyContent::Start;
        AlignItems alignItems = AlignItems::Start; // cross axis (flex) or vertical (grid)
        AlignContent alignContent = AlignContent::Start;

        std::vector<GridTrack> columns; // empty: one fractional column
        std::vector<GridTrack> rows;    // rows beyond these use autoRow
        GridTrack autoRow = GridTrack::Auto();
        AlignItems justifyItems = AlignItems::Stretch; // horizontal alignment in grid cells
    };

    // How a node is sized and placed inside its parent.
    struct ItemLayout
    {
        // Anchoring, used when the parent's mode is None or ignoreLayout is set.
        glm::vec2 anchorMin{0.5f};
        glm::vec2 anchorMax{0.5f};
        glm::vec2 pivot{0.5f};
        glm::vec2 anchoredPosition{0.0f};
        glm::vec2 sizeDelta{100.0f, 40.0f};

        Edges margin; // outer spacing, inside the slot the parent assigns
        glm::vec2 minSize{0.0f};
        glm::vec2 maxSize{kUnbounded};
        SizeMode widthMode = SizeMode::Fixed;
        SizeMode heightMode = SizeMode::Fixed;
        float aspectRatio = 0.0f; // width / height; zero disables

        float grow = 0.0f;
        float shrink = 1.0f;
        float basis = -1.0f; // main-axis base size; negative uses the preferred size
        AlignSelf alignSelf = AlignSelf::Auto;

        int column = -1; // grid placement; negative auto-places
        int row = -1;
        int columnSpan = 1;
        int rowSpan = 1;

        bool ignoreLayout = false; // anchored even inside a flex or grid parent
    };

    // Intrinsic content size for the given available size (either component
    // may be kUnbounded). Used for SizeMode::Content.
    using MeasureFunction = std::function<glm::vec2(glm::vec2 available)>;

    struct LayoutNode
    {
        ContainerLayout container;
        ItemLayout item;
        MeasureFunction measure;
        std::vector<std::uint32_t> children;

        Rect rect; // output
    };

    class LayoutTree
    {
    public:
        std::uint32_t Add(LayoutNode node);
        void AddChild(std::uint32_t parent, std::uint32_t child) { m_nodes[parent].children.push_back(child); }
        void Clear() { m_nodes.clear(); }

        [[nodiscard]] LayoutNode &operator[](std::uint32_t index) { return m_nodes[index]; }
        [[nodiscard]] const LayoutNode &operator[](std::uint32_t index) const { return m_nodes[index]; }
        [[nodiscard]] std::size_t Size() const { return m_nodes.size(); }

        // Places `root` at `rect` and lays out its subtree.
        void Compute(std::uint32_t root, const Rect &rect);
        // Preferred border-box size of a node (excluding margins).
        [[nodiscard]] glm::vec2 Measure(std::uint32_t node, glm::vec2 available);

    private:
        struct MeasureCache
        {
            glm::vec2 available{-1.0f};
            glm::vec2 result{0.0f};
        };

        void Arrange(std::uint32_t node, const Rect &rect);
        void ArrangeAnchored(std::uint32_t child, const Rect &parent);
        void ArrangeFlex(std::uint32_t node, const Rect &inner);
        void ArrangeGrid(std::uint32_t node, const Rect &inner);
        // Measures with sizes the container has already decided (a stretched cross size, a grown main size) in place
        // of the authored ones; a negative component keeps the authored size. Wrapped text then measures at the width
        // it will get. Not cached.
        glm::vec2 MeasureSized(std::uint32_t node, glm::vec2 available, glm::vec2 decided);
        glm::vec2 MeasureContent(std::uint32_t node, glm::vec2 available);
        glm::vec2 MeasureFlex(std::uint32_t node, glm::vec2 available);
        glm::vec2 MeasureGrid(std::uint32_t node, glm::vec2 available);

        std::vector<LayoutNode> m_nodes;
        std::vector<MeasureCache> m_cache;
    };

    // Anchored placement of an item within `parent` with the given preferred size.
    [[nodiscard]] Rect ResolveAnchoredRect(const ItemLayout &item, const Rect &parent, glm::vec2 preferredSize);
}
