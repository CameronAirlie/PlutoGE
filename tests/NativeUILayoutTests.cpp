#include "PlutoGE/ui/layout/Layout.h"

#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace
{
    using namespace PlutoGE::ui;

    bool Near(float a, float b, float epsilon = 0.01f) { return std::abs(a - b) <= epsilon; }

    std::string Describe(const Rect &rect)
    {
        std::ostringstream text;
        text << "(" << rect.min.x << ", " << rect.min.y << ")-(" << rect.max.x << ", " << rect.max.y << ")";
        return text.str();
    }

    void RequireRect(const Rect &actual, const Rect &expected, const std::string &what)
    {
        if (!Near(actual.min.x, expected.min.x) || !Near(actual.min.y, expected.min.y) ||
            !Near(actual.max.x, expected.max.x) || !Near(actual.max.y, expected.max.y))
            throw std::runtime_error(what + ": expected " + Describe(expected) + " got " + Describe(actual));
    }

    void Require(bool value, const std::string &message)
    {
        if (!value)
            throw std::runtime_error(message);
    }

    LayoutNode Fixed(glm::vec2 size)
    {
        LayoutNode node;
        node.item.sizeDelta = size;
        return node;
    }

    LayoutNode Container(LayoutMode mode)
    {
        LayoutNode node;
        node.container.mode = mode;
        return node;
    }

    const Rect kScreen{{0, 0}, {1920, 1080}};

    void TestAnchors()
    {
        LayoutTree tree;
        const auto root = tree.Add({});
        auto centered = Fixed({200, 100});
        const auto child = tree.Add(centered);
        tree.AddChild(root, child);

        auto stretched = Fixed({-20, -20}); // 10 px inset on every side
        stretched.item.anchorMin = {0, 0};
        stretched.item.anchorMax = {1, 1};
        const auto fill = tree.Add(stretched);
        tree.AddChild(root, fill);

        auto topLeft = Fixed({50, 30});
        topLeft.item.anchorMin = topLeft.item.anchorMax = topLeft.item.pivot = {0, 1};
        topLeft.item.anchoredPosition = {10, -10};
        const auto corner = tree.Add(topLeft);
        tree.AddChild(root, corner);

        tree.Compute(root, kScreen);
        RequireRect(tree[child].rect, {{860, 490}, {1060, 590}}, "Centered anchor");
        RequireRect(tree[fill].rect, {{10, 10}, {1910, 1070}}, "Stretched anchor");
        RequireRect(tree[corner].rect, {{10, 1040}, {60, 1070}}, "Top-left anchor");
    }

    void TestFlexRow()
    {
        LayoutTree tree;
        auto container = Container(LayoutMode::Flex);
        container.container.padding = Edges::All(5);
        container.container.gap = {10, 0};
        const auto root = tree.Add(container);
        std::uint32_t items[3];
        for (auto &item : items)
        {
            item = tree.Add(Fixed({100, 40}));
            tree.AddChild(root, item);
        }
        tree.Compute(root, {{0, 0}, {500, 100}});
        RequireRect(tree[items[0]].rect, {{5, 55}, {105, 95}}, "Row item 0");
        RequireRect(tree[items[1]].rect, {{115, 55}, {215, 95}}, "Row item 1");
        RequireRect(tree[items[2]].rect, {{225, 55}, {325, 95}}, "Row item 2");

        tree[root].container.justify = JustifyContent::End;
        tree[root].container.alignItems = AlignItems::Center;
        tree.Compute(root, {{0, 0}, {500, 100}});
        RequireRect(tree[items[2]].rect, {{395, 30}, {495, 70}}, "Justify end, align center");

        tree[root].container.justify = JustifyContent::SpaceBetween;
        tree[root].container.alignItems = AlignItems::Stretch;
        tree.Compute(root, {{0, 0}, {500, 100}});
        RequireRect(tree[items[1]].rect, {{200, 5}, {300, 95}}, "Space between with stretch");

        tree[root].container.direction = FlexDirection::RowReverse;
        tree[root].container.justify = JustifyContent::Start;
        tree.Compute(root, {{0, 0}, {500, 100}});
        RequireRect(tree[items[0]].rect, {{395, 5}, {495, 95}}, "Row reverse starts at the right");
    }

    void TestGrowAndShrink()
    {
        LayoutTree tree;
        const auto root = tree.Add(Container(LayoutMode::Flex));
        auto a = Fixed({100, 10});
        auto b = Fixed({100, 10});
        a.item.grow = 1;
        b.item.grow = 3;
        const auto ia = tree.Add(a), ib = tree.Add(b);
        tree.AddChild(root, ia);
        tree.AddChild(root, ib);
        tree.Compute(root, {{0, 0}, {400, 10}});
        Require(Near(tree[ia].rect.Width(), 150) && Near(tree[ib].rect.Width(), 250), "Grow distribution wrong");

        tree[ib].item.maxSize = {180, 1000};
        tree.Compute(root, {{0, 0}, {400, 10}});
        Require(Near(tree[ib].rect.Width(), 180) && Near(tree[ia].rect.Width(), 220),
                "Max clamp must hand the remaining space to other items");

        tree.Compute(root, {{0, 0}, {150, 10}});
        Require(Near(tree[ia].rect.Width(), 75) && Near(tree[ib].rect.Width(), 75), "Equal shrink expected");

        tree[ia].item.minSize = {90, 0};
        tree.Compute(root, {{0, 0}, {150, 10}});
        Require(Near(tree[ia].rect.Width(), 90) && Near(tree[ib].rect.Width(), 60), "Min clamp during shrink failed");
    }

    void TestColumnAndMargins()
    {
        LayoutTree tree;
        auto container = Container(LayoutMode::Flex);
        container.container.direction = FlexDirection::Column;
        container.container.gap = {0, 5};
        const auto root = tree.Add(container);
        auto first = Fixed({50, 20});
        first.item.margin = {.left = 3, .bottom = 0, .right = 0, .top = 7};
        const auto a = tree.Add(first), b = tree.Add(Fixed({50, 20}));
        tree.AddChild(root, a);
        tree.AddChild(root, b);
        tree.Compute(root, {{0, 0}, {100, 100}});
        RequireRect(tree[a].rect, {{3, 73}, {53, 93}}, "Column item with margins");
        RequireRect(tree[b].rect, {{0, 48}, {50, 68}}, "Column item after gap");
    }

    void TestWrap()
    {
        LayoutTree tree;
        auto container = Container(LayoutMode::Flex);
        container.container.wrap = FlexWrap::Wrap;
        container.container.gap = {10, 10};
        const auto root = tree.Add(container);
        std::uint32_t items[3];
        for (auto &item : items)
        {
            item = tree.Add(Fixed({100, 30}));
            tree.AddChild(root, item);
        }
        tree.Compute(root, {{0, 0}, {250, 200}});
        RequireRect(tree[items[1]].rect, {{110, 170}, {210, 200}}, "Second item on first line");
        RequireRect(tree[items[2]].rect, {{0, 130}, {100, 160}}, "Third item wraps to the second line");

        const glm::vec2 measured = tree.Measure(root, {250, kUnbounded});
        Require(Near(measured.x, 100) && Near(measured.y, 40), "Fixed-size containers measure their authored size");
        tree[root].item.widthMode = tree[root].item.heightMode = SizeMode::Content;
        tree.Compute(root, {{0, 0}, {250, 200}}); // resets measure caches
        const glm::vec2 content = tree.Measure(root, {250, kUnbounded});
        Require(Near(content.x, 210) && Near(content.y, 70), "Wrapped content size wrong");
    }

    void TestContentMeasure()
    {
        // A "text" item: 10 px per character, 200 characters, wraps to the available width.
        const auto text = [](glm::vec2 available)
        {
            const float natural = 2000.0f;
            if (!std::isfinite(available.x) || available.x >= natural)
                return glm::vec2(natural, 20.0f);
            const float lines = std::ceil(natural / std::max(available.x, 1.0f));
            return glm::vec2(available.x, lines * 20.0f);
        };

        LayoutTree tree;
        auto container = Container(LayoutMode::Flex);
        container.container.direction = FlexDirection::Column;
        container.container.alignItems = AlignItems::Stretch;
        container.container.padding = Edges::All(10);
        container.item.widthMode = SizeMode::Fixed;
        container.item.heightMode = SizeMode::Content;
        container.item.sizeDelta = {520, 0};
        const auto root = tree.Add(container);
        LayoutNode label;
        label.item.widthMode = label.item.heightMode = SizeMode::Content;
        label.measure = text;
        const auto labelIndex = tree.Add(label);
        tree.AddChild(root, labelIndex);

        const glm::vec2 size = tree.Measure(root, {kUnbounded, kUnbounded});
        Require(Near(size.x, 520) && Near(size.y, 80 + 20), "Wrapped text must make the panel taller");

        LayoutTree screen;
        const auto screenRoot = screen.Add({});
        auto panel = container;
        panel.children.clear();
        const auto panelIndex = screen.Add(panel);
        const auto panelLabel = screen.Add(label);
        screen.AddChild(screenRoot, panelIndex);
        screen.AddChild(panelIndex, panelLabel);
        screen.Compute(screenRoot, kScreen);
        Require(Near(screen[panelIndex].rect.Height(), 100), "Content-sized anchored panel height wrong");
        Require(Near(screen[panelLabel].rect.Width(), 500), "Stretched label must fill the padded width");
    }

    void TestGrid()
    {
        LayoutTree tree;
        auto container = Container(LayoutMode::Grid);
        container.container.columns = {GridTrack::Fraction(), GridTrack::Fraction(), GridTrack::Fraction()};
        container.container.gap = {0, 10};
        container.container.alignItems = AlignItems::Stretch;
        const auto root = tree.Add(container);
        std::uint32_t items[5];
        for (auto &item : items)
        {
            item = tree.Add(Fixed({20, 30}));
            tree.AddChild(root, item);
        }
        tree[items[3]].item.columnSpan = 2;
        tree.Compute(root, {{0, 0}, {300, 200}});
        RequireRect(tree[items[0]].rect, {{0, 170}, {100, 200}}, "Grid cell 0");
        RequireRect(tree[items[2]].rect, {{200, 170}, {300, 200}}, "Grid cell 2");
        RequireRect(tree[items[3]].rect, {{0, 130}, {200, 160}}, "Grid span 2 on the second row");
        RequireRect(tree[items[4]].rect, {{200, 130}, {300, 160}}, "Grid cell after span");

        tree[root].container.columns = {GridTrack::Fixed(50), GridTrack::Auto(), GridTrack::Fraction()};
        tree[root].container.justifyItems = AlignItems::Start;
        tree[items[1]].item.sizeDelta = {80, 30};
        tree[items[3]].item.columnSpan = 1;
        tree[items[3]].item.column = 2;
        tree[items[3]].item.row = 0;
        tree.Compute(root, {{0, 0}, {300, 200}});
        RequireRect(tree[items[1]].rect, {{50, 170}, {130, 200}}, "Auto column sized to content");
        RequireRect(tree[items[3]].rect, {{130, 170}, {150, 200}}, "Explicit cell in the fraction column");
        RequireRect(tree[items[2]].rect, {{0, 130}, {20, 160}}, "Auto flow skips the explicitly placed cell");
    }

    void TestIgnoreLayout()
    {
        LayoutTree tree;
        const auto root = tree.Add(Container(LayoutMode::Flex));
        auto overlay = Fixed({10, 10});
        overlay.item.ignoreLayout = true;
        overlay.item.anchorMin = overlay.item.anchorMax = overlay.item.pivot = {1, 1};
        const auto badge = tree.Add(overlay);
        const auto item = tree.Add(Fixed({50, 50}));
        tree.AddChild(root, badge);
        tree.AddChild(root, item);
        tree.Compute(root, {{0, 0}, {100, 100}});
        RequireRect(tree[badge].rect, {{90, 90}, {100, 100}}, "Ignore-layout child must be anchored");
        RequireRect(tree[item].rect, {{0, 50}, {50, 100}}, "Flow must skip ignore-layout children");
    }
}

int main()
{
    try
    {
        TestAnchors();
        TestFlexRow();
        TestGrowAndShrink();
        TestColumnAndMargins();
        TestWrap();
        TestContentMeasure();
        TestGrid();
        TestIgnoreLayout();
    }
    catch (const std::exception &error)
    {
        std::cerr << "NativeUILayoutTests failed: " << error.what() << '\n';
        return 1;
    }
    std::cout << "NativeUILayoutTests passed\n";
    return 0;
}
