#include "PlutoGE/ui/layout/Layout.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::ui
{
    namespace
    {
        bool IsRow(FlexDirection direction)
        {
            return direction == FlexDirection::Row || direction == FlexDirection::RowReverse;
        }

        // Axis index: 0 = x, 1 = y.
        int MainAxis(FlexDirection direction) { return IsRow(direction) ? 0 : 1; }
        int CrossAxis(FlexDirection direction) { return IsRow(direction) ? 1 : 0; }

        // Margins along the flow. Main runs left-to-right or top-to-bottom
        // (reversed for the *Reverse directions); cross starts at the top or left.
        float MainStartMargin(const Edges &m, FlexDirection d)
        {
            switch (d)
            {
            case FlexDirection::Row: return m.left;
            case FlexDirection::RowReverse: return m.right;
            case FlexDirection::Column: return m.top;
            default: return m.bottom;
            }
        }
        float MainMargins(const Edges &m, FlexDirection d) { return IsRow(d) ? m.Horizontal() : m.Vertical(); }
        float CrossStartMargin(const Edges &m, FlexDirection d) { return IsRow(d) ? m.top : m.left; }
        float CrossMargins(const Edges &m, FlexDirection d) { return IsRow(d) ? m.Vertical() : m.Horizontal(); }

        // Converts flow offsets inside `inner` into a y-up rectangle.
        Rect FlowRect(const Rect &inner, FlexDirection direction, float mainOffset, float mainSize,
                      float crossOffset, float crossSize)
        {
            switch (direction)
            {
            case FlexDirection::Row:
            {
                const float x = inner.min.x + mainOffset, top = inner.max.y - crossOffset;
                return {{x, top - crossSize}, {x + mainSize, top}};
            }
            case FlexDirection::RowReverse:
            {
                const float right = inner.max.x - mainOffset, top = inner.max.y - crossOffset;
                return {{right - mainSize, top - crossSize}, {right, top}};
            }
            case FlexDirection::Column:
            {
                const float top = inner.max.y - mainOffset, x = inner.min.x + crossOffset;
                return {{x, top - mainSize}, {x + crossSize, top}};
            }
            default:
            {
                const float bottom = inner.min.y + mainOffset, x = inner.min.x + crossOffset;
                return {{x, bottom}, {x + crossSize, bottom + mainSize}};
            }
            }
        }

        float Subtract(float available, float amount)
        {
            return std::isfinite(available) ? std::max(available - amount, 0.0f) : available;
        }

        glm::vec2 InnerAvailable(glm::vec2 available, const Edges &padding)
        {
            return {Subtract(available.x, padding.Horizontal()), Subtract(available.y, padding.Vertical())};
        }

        AlignSelf ResolveAlign(const ItemLayout &item, AlignItems containerAlign)
        {
            return item.alignSelf == AlignSelf::Auto ? static_cast<AlignSelf>(static_cast<int>(containerAlign) + 1)
                                                     : item.alignSelf;
        }

        // Offset of an item of `size` inside `space` for an alignment.
        float AlignOffset(AlignSelf align, float space, float size)
        {
            switch (align)
            {
            case AlignSelf::Center: return (space - size) * 0.5f;
            case AlignSelf::End: return space - size;
            default: return 0.0f;
            }
        }

        struct Distribution
        {
            float start = 0.0f;
            float between = 0.0f;
        };

        Distribution Distribute(JustifyContent justify, float freeSpace, std::size_t count, float gap)
        {
            Distribution result{0.0f, gap};
            if (count == 0)
                return result;
            const float free = freeSpace;
            const float n = static_cast<float>(count);
            switch (justify)
            {
            case JustifyContent::Center: result.start = free * 0.5f; break;
            case JustifyContent::End: result.start = free; break;
            case JustifyContent::SpaceBetween:
                if (free > 0.0f && count > 1) result.between = gap + free / (n - 1.0f);
                break;
            case JustifyContent::SpaceAround:
                if (free > 0.0f) { result.start = free / n * 0.5f; result.between = gap + free / n; }
                break;
            case JustifyContent::SpaceEvenly:
                if (free > 0.0f) { result.start = free / (n + 1.0f); result.between = gap + free / (n + 1.0f); }
                break;
            default: break;
            }
            return result;
        }

        bool InFlow(const LayoutNode &node) { return !node.item.ignoreLayout; }
    }

    Rect ResolveAnchoredRect(const ItemLayout &item, const Rect &parent, glm::vec2 contentSize)
    {
        const glm::vec2 parentSize = parent.Size();
        const glm::vec2 anchorMinPosition = parent.min + parentSize * item.anchorMin;
        const glm::vec2 anchorMaxPosition = parent.min + parentSize * item.anchorMax;
        const glm::vec2 anchorReference = glm::mix(anchorMinPosition, anchorMaxPosition, item.pivot);
        glm::vec2 size = glm::max((anchorMaxPosition - anchorMinPosition) + item.sizeDelta, glm::vec2(0.0f));
        // Content-sized axes include their margins so the visible rect fits the content.
        if (item.widthMode == SizeMode::Content) size.x = contentSize.x + item.margin.Horizontal();
        if (item.heightMode == SizeMode::Content) size.y = contentSize.y + item.margin.Vertical();
        size = glm::clamp(size, item.minSize, glm::max(item.maxSize, item.minSize));
        const glm::vec2 outerMin = anchorReference + item.anchoredPosition - size * item.pivot;
        return {outerMin + glm::vec2(item.margin.left, item.margin.bottom),
                outerMin + size - glm::vec2(item.margin.right, item.margin.top)};
    }

    std::uint32_t LayoutTree::Add(LayoutNode node)
    {
        m_nodes.push_back(std::move(node));
        return static_cast<std::uint32_t>(m_nodes.size() - 1);
    }

    void LayoutTree::Compute(std::uint32_t root, const Rect &rect)
    {
        m_cache.assign(m_nodes.size(), MeasureCache{});
        if (root < m_nodes.size())
            Arrange(root, rect);
    }

    glm::vec2 LayoutTree::Measure(std::uint32_t index, glm::vec2 available)
    {
        if (m_cache.size() != m_nodes.size())
            m_cache.assign(m_nodes.size(), MeasureCache{});
        auto &cache = m_cache[index];
        if (cache.available == available)
            return cache.result;

        const ItemLayout &item = m_nodes[index].item;
        glm::vec2 size = item.sizeDelta;
        const bool contentWidth = item.widthMode == SizeMode::Content;
        const bool contentHeight = item.heightMode == SizeMode::Content;
        if (contentWidth || contentHeight)
        {
            // A fixed width constrains how content (wrapped text) grows in height.
            const glm::vec2 constraint{contentWidth ? available.x : item.sizeDelta.x,
                                       contentHeight ? available.y : item.sizeDelta.y};
            const glm::vec2 content = MeasureContent(index, constraint);
            if (contentWidth) size.x = content.x;
            if (contentHeight) size.y = content.y;
        }
        if (item.aspectRatio > 0.0f)
        {
            if (contentHeight && !contentWidth) size.y = size.x / item.aspectRatio;
            else if (contentWidth && !contentHeight) size.x = size.y * item.aspectRatio;
        }
        size = glm::clamp(glm::max(size, glm::vec2(0.0f)), item.minSize, glm::max(item.maxSize, item.minSize));
        cache = {available, size};
        return size;
    }

    glm::vec2 LayoutTree::MeasureContent(std::uint32_t index, glm::vec2 available)
    {
        const LayoutNode &node = m_nodes[index];
        const Edges &padding = node.container.padding;
        switch (node.container.mode)
        {
        case LayoutMode::Flex: return MeasureFlex(index, available);
        case LayoutMode::Grid: return MeasureGrid(index, available);
        default: break;
        }
        const glm::vec2 inner = InnerAvailable(available, padding);
        if (node.measure)
            return node.measure(inner) + padding.Total();
        // Anchored children: the content box must hold the largest child.
        glm::vec2 size{0.0f};
        const std::vector<std::uint32_t> children = node.children;
        for (const std::uint32_t child : children)
            size = glm::max(size, Measure(child, inner) + m_nodes[child].item.margin.Total());
        return size + padding.Total();
    }

    glm::vec2 LayoutTree::MeasureFlex(std::uint32_t index, glm::vec2 available)
    {
        const ContainerLayout container = m_nodes[index].container;
        const std::vector<std::uint32_t> children = m_nodes[index].children;
        const int main = MainAxis(container.direction), cross = CrossAxis(container.direction);
        const glm::vec2 inner = InnerAvailable(available, container.padding);
        const float mainGap = container.gap[main], crossGap = container.gap[cross];

        float mainTotal = 0.0f, crossTotal = 0.0f;
        float lineMain = 0.0f, lineCross = 0.0f;
        std::size_t lineCount = 0;
        bool lineStarted = false;
        for (const std::uint32_t child : children)
        {
            const ItemLayout &item = m_nodes[child].item;
            if (!InFlow(m_nodes[child]))
                continue;
            const glm::vec2 size = Measure(child, inner);
            const float itemMain = (item.basis >= 0.0f ? item.basis : size[main]) + MainMargins(item.margin, container.direction);
            const float itemCross = size[cross] + CrossMargins(item.margin, container.direction);
            const bool wraps = container.wrap == FlexWrap::Wrap && std::isfinite(inner[main]) && lineStarted &&
                               lineMain + mainGap + itemMain > inner[main];
            if (wraps)
            {
                mainTotal = std::max(mainTotal, lineMain);
                crossTotal += lineCross + (lineCount > 0 ? crossGap : 0.0f);
                ++lineCount;
                lineMain = lineCross = 0.0f;
                lineStarted = false;
            }
            lineMain += (lineStarted ? mainGap : 0.0f) + itemMain;
            lineCross = std::max(lineCross, itemCross);
            lineStarted = true;
        }
        if (lineStarted)
        {
            mainTotal = std::max(mainTotal, lineMain);
            crossTotal += lineCross + (lineCount > 0 ? crossGap : 0.0f);
        }
        glm::vec2 result{0.0f};
        result[main] = mainTotal;
        result[cross] = crossTotal;
        return result + container.padding.Total();
    }

    namespace
    {
        struct GridPlacement
        {
            std::uint32_t node = 0;
            int column = 0;
            int row = 0;
            int columnSpan = 1;
            int rowSpan = 1;
        };

        std::vector<GridPlacement> PlaceGridItems(const std::vector<LayoutNode> &nodes,
                                                  const std::vector<std::uint32_t> &children, int columns, int &rowCount)
        {
            std::vector<GridPlacement> placements;
            std::vector<std::vector<bool>> occupied;
            const auto isFree = [&](int row, int column, int columnSpan, int rowSpan)
            {
                if (column + columnSpan > columns)
                    return false;
                for (int r = row; r < row + rowSpan; ++r)
                    for (int c = column; c < column + columnSpan; ++c)
                        if (r < static_cast<int>(occupied.size()) && occupied[r][c])
                            return false;
                return true;
            };
            const auto mark = [&](const GridPlacement &placement)
            {
                for (int r = placement.row; r < placement.row + placement.rowSpan; ++r)
                {
                    while (static_cast<int>(occupied.size()) <= r)
                        occupied.emplace_back(columns, false);
                    for (int c = placement.column; c < placement.column + placement.columnSpan; ++c)
                        occupied[r][c] = true;
                }
            };

            // Like CSS grid: fully placed items first, then items fixed to a
            // row or column, then auto-flow, so auto items never take a cell
            // that an explicit item names.
            const auto pass = [&](const ItemLayout &item)
            {
                if (item.column >= 0 && item.row >= 0) return 0;
                return item.column >= 0 || item.row >= 0 ? 1 : 2;
            };
            std::vector<std::uint32_t> ordered;
            for (int stage = 0; stage < 3; ++stage)
                for (const std::uint32_t child : children)
                    if (InFlow(nodes[child]) && pass(nodes[child].item) == stage)
                        ordered.push_back(child);

            int cursorRow = 0, cursorColumn = 0;
            for (const std::uint32_t child : ordered)
            {
                const ItemLayout &item = nodes[child].item;
                GridPlacement placement{.node = child,
                                        .columnSpan = std::clamp(item.columnSpan, 1, columns),
                                        .rowSpan = std::max(item.rowSpan, 1)};
                if (item.column >= 0 && item.row >= 0)
                {
                    placement.column = std::min(item.column, columns - placement.columnSpan);
                    placement.row = item.row;
                }
                else if (item.row >= 0)
                {
                    placement.row = item.row;
                    placement.column = 0;
                    while (placement.column + placement.columnSpan <= columns &&
                           !isFree(placement.row, placement.column, placement.columnSpan, placement.rowSpan))
                        ++placement.column;
                    placement.column = std::min(placement.column, columns - placement.columnSpan);
                }
                else if (item.column >= 0)
                {
                    placement.column = std::min(item.column, columns - placement.columnSpan);
                    placement.row = 0;
                    while (!isFree(placement.row, placement.column, placement.columnSpan, placement.rowSpan))
                        ++placement.row;
                }
                else
                {
                    // Row-major auto flow from the cursor.
                    while (!isFree(cursorRow, cursorColumn, placement.columnSpan, placement.rowSpan))
                    {
                        if (++cursorColumn + placement.columnSpan > columns)
                        {
                            cursorColumn = 0;
                            ++cursorRow;
                        }
                    }
                    placement.row = cursorRow;
                    placement.column = cursorColumn;
                    cursorColumn += placement.columnSpan;
                }
                mark(placement);
                placements.push_back(placement);
            }
            rowCount = static_cast<int>(occupied.size());
            return placements;
        }

        // Resolves track sizes. `contentSizes[i]` is the largest single-span
        // item in track i; fractional tracks share what is left of `available`
        // (or size to content when it is unbounded).
        std::vector<float> ResolveTracks(const std::vector<GridTrack> &tracks, const std::vector<float> &contentSizes,
                                         float available, float gap)
        {
            std::vector<float> sizes(tracks.size(), 0.0f);
            float used = gap * static_cast<float>(std::max<std::size_t>(tracks.size(), 1) - 1);
            float fractions = 0.0f;
            for (std::size_t index = 0; index < tracks.size(); ++index)
            {
                switch (tracks[index].kind)
                {
                case GridTrack::Kind::Fixed: sizes[index] = std::max(tracks[index].value, 0.0f); break;
                case GridTrack::Kind::Auto: sizes[index] = contentSizes[index]; break;
                case GridTrack::Kind::Fraction: fractions += std::max(tracks[index].value, 0.0f); continue;
                }
                used += sizes[index];
            }
            if (fractions <= 0.0f)
                return sizes;
            if (!std::isfinite(available))
            {
                // Unbounded: every fraction unit is as large as the largest content per unit.
                float unit = 0.0f;
                for (std::size_t index = 0; index < tracks.size(); ++index)
                    if (tracks[index].kind == GridTrack::Kind::Fraction && tracks[index].value > 0.0f)
                        unit = std::max(unit, contentSizes[index] / tracks[index].value);
                for (std::size_t index = 0; index < tracks.size(); ++index)
                    if (tracks[index].kind == GridTrack::Kind::Fraction)
                        sizes[index] = unit * std::max(tracks[index].value, 0.0f);
                return sizes;
            }
            const float remaining = std::max(available - used, 0.0f);
            for (std::size_t index = 0; index < tracks.size(); ++index)
                if (tracks[index].kind == GridTrack::Kind::Fraction)
                    sizes[index] = remaining * std::max(tracks[index].value, 0.0f) / fractions;
            return sizes;
        }

        std::vector<GridTrack> RowTracks(const ContainerLayout &container, int rowCount)
        {
            std::vector<GridTrack> rows = container.rows;
            while (static_cast<int>(rows.size()) < rowCount)
                rows.push_back(container.autoRow);
            return rows;
        }

        float SpanSize(const std::vector<float> &sizes, int start, int span, float gap)
        {
            float total = gap * static_cast<float>(span - 1);
            for (int index = start; index < start + span && index < static_cast<int>(sizes.size()); ++index)
                total += sizes[index];
            return total;
        }
    }

    glm::vec2 LayoutTree::MeasureGrid(std::uint32_t index, glm::vec2 available)
    {
        const ContainerLayout container = m_nodes[index].container;
        const std::vector<std::uint32_t> children = m_nodes[index].children;
        const std::vector<GridTrack> columns = container.columns.empty() ? std::vector<GridTrack>{GridTrack::Fraction()}
                                                                         : container.columns;
        const glm::vec2 inner = InnerAvailable(available, container.padding);
        int rowCount = 0;
        const auto placements = PlaceGridItems(m_nodes, children, static_cast<int>(columns.size()), rowCount);
        const auto rows = RowTracks(container, rowCount);

        std::vector<float> columnContent(columns.size(), 0.0f), rowContent(rows.size(), 0.0f);
        for (const auto &placement : placements)
        {
            const glm::vec2 size = Measure(placement.node, {kUnbounded, kUnbounded}) + m_nodes[placement.node].item.margin.Total();
            if (placement.columnSpan == 1)
                columnContent[placement.column] = std::max(columnContent[placement.column], size.x);
        }
        const auto columnSizes = ResolveTracks(columns, columnContent, inner.x, container.gap.x);
        for (const auto &placement : placements)
        {
            const ItemLayout &item = m_nodes[placement.node].item;
            const float width = SpanSize(columnSizes, placement.column, placement.columnSpan, container.gap.x) - item.margin.Horizontal();
            const float height = Measure(placement.node, {std::max(width, 0.0f), kUnbounded}).y + item.margin.Vertical();
            if (placement.rowSpan == 1)
                rowContent[placement.row] = std::max(rowContent[placement.row], height);
        }
        const auto rowSizes = ResolveTracks(rows, rowContent, inner.y, container.gap.y);
        return glm::vec2(SpanSize(columnSizes, 0, static_cast<int>(columnSizes.size()), container.gap.x),
                         rowSizes.empty() ? 0.0f : SpanSize(rowSizes, 0, static_cast<int>(rowSizes.size()), container.gap.y)) +
               container.padding.Total();
    }

    void LayoutTree::Arrange(std::uint32_t index, const Rect &rect)
    {
        m_nodes[index].rect = rect;
        const ContainerLayout &container = m_nodes[index].container;
        const Rect inner = Inset(rect, container.padding);
        switch (container.mode)
        {
        case LayoutMode::Flex: ArrangeFlex(index, inner); break;
        case LayoutMode::Grid: ArrangeGrid(index, inner); break;
        default:
        {
            const std::vector<std::uint32_t> children = m_nodes[index].children;
            for (const std::uint32_t child : children)
                ArrangeAnchored(child, rect);
            return;
        }
        }
        // Children opting out of the flow are anchored to the container.
        const std::vector<std::uint32_t> children = m_nodes[index].children;
        for (const std::uint32_t child : children)
            if (!InFlow(m_nodes[child]))
                ArrangeAnchored(child, rect);
    }

    void LayoutTree::ArrangeAnchored(std::uint32_t child, const Rect &parent)
    {
        const ItemLayout &item = m_nodes[child].item;
        glm::vec2 content{0.0f};
        if (item.widthMode == SizeMode::Content || item.heightMode == SizeMode::Content)
            content = Measure(child, parent.Size());
        Arrange(child, ResolveAnchoredRect(item, parent, content));
    }

    void LayoutTree::ArrangeFlex(std::uint32_t index, const Rect &inner)
    {
        const ContainerLayout container = m_nodes[index].container;
        const FlexDirection direction = container.direction;
        const int main = MainAxis(direction), cross = CrossAxis(direction);
        const float mainSize = inner.Size()[main], crossSize = inner.Size()[cross];
        const float mainGap = container.gap[main], crossGap = container.gap[cross];

        struct FlexItem
        {
            std::uint32_t node = 0;
            float hypothetical = 0.0f; // clamped base size
            float minMain = 0.0f;
            float maxMain = kUnbounded;
            float main = 0.0f;         // resolved size
            float cross = 0.0f;
            float marginMain = 0.0f;
            float marginCross = 0.0f;
            AlignSelf align = AlignSelf::Start;
        };
        std::vector<FlexItem> items;
        glm::vec2 measureAvailable{0.0f};
        measureAvailable[main] = mainSize;
        measureAvailable[cross] = crossSize;
        for (const std::uint32_t child : m_nodes[index].children)
        {
            if (!InFlow(m_nodes[child]))
                continue;
            const ItemLayout &item = m_nodes[child].item;
            FlexItem flex{.node = child,
                          .minMain = item.minSize[main],
                          .maxMain = std::max(item.maxSize[main], item.minSize[main]),
                          .marginMain = MainMargins(item.margin, direction),
                          .marginCross = CrossMargins(item.margin, direction),
                          .align = ResolveAlign(item, container.alignItems)};
            const float base = item.basis >= 0.0f ? item.basis : Measure(child, measureAvailable)[main];
            flex.hypothetical = std::clamp(base, flex.minMain, flex.maxMain);
            items.push_back(flex);
        }
        if (items.empty())
            return;

        // 1. Collect lines.
        struct Line
        {
            std::size_t begin = 0;
            std::size_t end = 0;
            float cross = 0.0f;
            float offset = 0.0f;
        };
        std::vector<Line> lines;
        {
            Line line;
            float used = 0.0f;
            for (std::size_t i = 0; i < items.size(); ++i)
            {
                const float outer = items[i].hypothetical + items[i].marginMain;
                if (container.wrap == FlexWrap::Wrap && i > line.begin && used + mainGap + outer > mainSize)
                {
                    line.end = i;
                    lines.push_back(line);
                    line = Line{.begin = i};
                    used = 0.0f;
                }
                used += (i > line.begin ? mainGap : 0.0f) + outer;
            }
            line.end = items.size();
            lines.push_back(line);
        }

        // 2. Resolve flexible lengths per line.
        for (auto &line : lines)
        {
            const std::size_t count = line.end - line.begin;
            const float gaps = mainGap * static_cast<float>(count - 1);
            float hypotheticalTotal = gaps;
            for (std::size_t i = line.begin; i < line.end; ++i)
                hypotheticalTotal += items[i].hypothetical + items[i].marginMain;
            const bool growing = mainSize - hypotheticalTotal > 0.0f;
            std::vector<bool> frozen(count, false);
            for (std::size_t i = line.begin; i < line.end; ++i)
                items[i].main = items[i].hypothetical;
            for (std::size_t iteration = 0; iteration <= count; ++iteration)
            {
                float used = gaps, weight = 0.0f;
                for (std::size_t i = line.begin; i < line.end; ++i)
                {
                    const ItemLayout &item = m_nodes[items[i].node].item;
                    const bool isFrozen = frozen[i - line.begin];
                    used += (isFrozen ? items[i].main : items[i].hypothetical) + items[i].marginMain;
                    if (!isFrozen)
                        weight += growing ? item.grow : item.shrink * items[i].hypothetical;
                }
                const float free = mainSize - used;
                if (weight <= 0.0f || (growing ? free <= 0.0f : free >= 0.0f))
                    break;
                bool clamped = false;
                for (std::size_t i = line.begin; i < line.end; ++i)
                {
                    if (frozen[i - line.begin])
                        continue;
                    const ItemLayout &item = m_nodes[items[i].node].item;
                    const float share = growing ? item.grow : item.shrink * items[i].hypothetical;
                    const float target = items[i].hypothetical + free * share / weight;
                    const float limited = std::clamp(target, std::max(items[i].minMain, 0.0f), items[i].maxMain);
                    items[i].main = limited;
                    if (limited != target)
                    {
                        frozen[i - line.begin] = true;
                        clamped = true;
                    }
                }
                if (!clamped)
                    break;
            }

            // Cross sizes from the resolved main size (wrapped text gets taller).
            for (std::size_t i = line.begin; i < line.end; ++i)
            {
                glm::vec2 available{0.0f};
                available[main] = items[i].main;
                available[cross] = crossSize;
                items[i].cross = Measure(items[i].node, available)[cross];
                line.cross = std::max(line.cross, items[i].cross + items[i].marginCross);
            }
        }

        // 3. Distribute lines on the cross axis.
        if (container.wrap == FlexWrap::NoWrap)
            lines.front().cross = crossSize;
        else
        {
            float used = crossGap * static_cast<float>(lines.size() - 1);
            for (const auto &line : lines)
                used += line.cross;
            const float free = crossSize - used;
            float offset = 0.0f, between = crossGap;
            const float n = static_cast<float>(lines.size());
            switch (container.alignContent)
            {
            case AlignContent::Center: offset = free * 0.5f; break;
            case AlignContent::End: offset = free; break;
            case AlignContent::Stretch:
                if (free > 0.0f)
                    for (auto &line : lines)
                        line.cross += free / n;
                break;
            case AlignContent::SpaceBetween:
                if (free > 0.0f && lines.size() > 1) between += free / (n - 1.0f);
                break;
            case AlignContent::SpaceAround:
                if (free > 0.0f) { offset = free / n * 0.5f; between += free / n; }
                break;
            default: break;
            }
            for (auto &line : lines)
            {
                line.offset = offset;
                offset += line.cross + between;
            }
        }

        // 4. Position items.
        for (const auto &line : lines)
        {
            const std::size_t count = line.end - line.begin;
            float used = mainGap * static_cast<float>(count - 1);
            for (std::size_t i = line.begin; i < line.end; ++i)
                used += items[i].main + items[i].marginMain;
            const Distribution distribution = Distribute(container.justify, mainSize - used, count, mainGap);
            float cursor = distribution.start;
            for (std::size_t i = line.begin; i < line.end; ++i)
            {
                const FlexItem &item = items[i];
                const ItemLayout &layout = m_nodes[item.node].item;
                float crossExtent = item.cross;
                if (item.align == AlignSelf::Stretch)
                    crossExtent = std::clamp(line.cross - item.marginCross, layout.minSize[cross],
                                             std::max(layout.maxSize[cross], layout.minSize[cross]));
                const float crossOffset = line.offset + CrossStartMargin(layout.margin, direction) +
                                          AlignOffset(item.align, line.cross - item.marginCross, crossExtent);
                const float mainOffset = cursor + MainStartMargin(layout.margin, direction);
                Arrange(item.node, FlowRect(inner, direction, mainOffset, item.main, crossOffset, crossExtent));
                cursor += item.main + item.marginMain + distribution.between;
            }
        }
    }

    void LayoutTree::ArrangeGrid(std::uint32_t index, const Rect &inner)
    {
        const ContainerLayout container = m_nodes[index].container;
        const std::vector<std::uint32_t> children = m_nodes[index].children;
        const std::vector<GridTrack> columns = container.columns.empty() ? std::vector<GridTrack>{GridTrack::Fraction()}
                                                                         : container.columns;
        int rowCount = 0;
        const auto placements = PlaceGridItems(m_nodes, children, static_cast<int>(columns.size()), rowCount);
        const auto rows = RowTracks(container, rowCount);

        std::vector<float> columnContent(columns.size(), 0.0f), rowContent(rows.size(), 0.0f);
        for (const auto &placement : placements)
            if (placement.columnSpan == 1)
            {
                const float width = Measure(placement.node, {kUnbounded, kUnbounded}).x +
                                    m_nodes[placement.node].item.margin.Horizontal();
                columnContent[placement.column] = std::max(columnContent[placement.column], width);
            }
        const auto columnSizes = ResolveTracks(columns, columnContent, inner.Width(), container.gap.x);
        for (const auto &placement : placements)
            if (placement.rowSpan == 1)
            {
                const ItemLayout &item = m_nodes[placement.node].item;
                const float width = SpanSize(columnSizes, placement.column, placement.columnSpan, container.gap.x) -
                                    item.margin.Horizontal();
                const float height = Measure(placement.node, {std::max(width, 0.0f), kUnbounded}).y + item.margin.Vertical();
                rowContent[placement.row] = std::max(rowContent[placement.row], height);
            }
        const auto rowSizes = ResolveTracks(rows, rowContent, inner.Height(), container.gap.y);

        std::vector<float> columnStarts(columnSizes.size(), 0.0f), rowStarts(rowSizes.size(), 0.0f);
        for (std::size_t i = 1; i < columnSizes.size(); ++i)
            columnStarts[i] = columnStarts[i - 1] + columnSizes[i - 1] + container.gap.x;
        for (std::size_t i = 1; i < rowSizes.size(); ++i)
            rowStarts[i] = rowStarts[i - 1] + rowSizes[i - 1] + container.gap.y;

        for (const auto &placement : placements)
        {
            const ItemLayout &item = m_nodes[placement.node].item;
            const float cellWidth = SpanSize(columnSizes, placement.column, placement.columnSpan, container.gap.x);
            const float cellHeight = SpanSize(rowSizes, placement.row, placement.rowSpan, container.gap.y);
            const Rect cell{{inner.min.x + columnStarts[placement.column], inner.max.y - rowStarts[placement.row] - cellHeight},
                            {inner.min.x + columnStarts[placement.column] + cellWidth, inner.max.y - rowStarts[placement.row]}};
            const Rect slot = Inset(cell, item.margin);
            const glm::vec2 preferred = Measure(placement.node, slot.Size());

            const AlignSelf horizontal = static_cast<AlignSelf>(static_cast<int>(container.justifyItems) + 1);
            const AlignSelf vertical = ResolveAlign(item, container.alignItems);
            const glm::vec2 maxSize = glm::max(item.maxSize, item.minSize);
            const float width = horizontal == AlignSelf::Stretch ? std::clamp(slot.Width(), item.minSize.x, maxSize.x)
                                                                 : std::min(preferred.x, slot.Width());
            const float height = vertical == AlignSelf::Stretch ? std::clamp(slot.Height(), item.minSize.y, maxSize.y)
                                                                : std::min(preferred.y, slot.Height());
            const float x = slot.min.x + AlignOffset(horizontal, slot.Width(), width);
            const float top = slot.max.y - AlignOffset(vertical, slot.Height(), height);
            Arrange(placement.node, {{x, top - height}, {x + width, top}});
        }
    }
}
