#include "PlutoGE/scene/components/UILayoutComponents.h"

#include "../ui/UIPropertyCodec.h"

namespace PlutoGE::scene
{
    using namespace uiprops;

    namespace
    {
        const std::vector<std::string> kAlignItems{"Start", "Center", "End", "Stretch"};
    }

    std::string FormatGridTracks(const std::vector<ui::GridTrack> &tracks)
    {
        std::string text;
        for (const auto &track : tracks)
        {
            if (!text.empty()) text += ' ';
            switch (track.kind)
            {
            case ui::GridTrack::Kind::Auto: text += "auto"; break;
            case ui::GridTrack::Kind::Fixed: text += FormatFloat(track.value); break;
            case ui::GridTrack::Kind::Fraction: text += FormatFloat(track.value) + "fr"; break;
            }
        }
        return text;
    }

    std::vector<ui::GridTrack> ParseGridTracks(std::string_view text)
    {
        std::vector<ui::GridTrack> tracks;
        while (!text.empty())
        {
            const auto start = text.find_first_not_of(" ,");
            if (start == std::string_view::npos)
                break;
            text.remove_prefix(start);
            const auto end = text.find_first_of(" ,");
            std::string_view token = text.substr(0, end);
            text.remove_prefix(end == std::string_view::npos ? text.size() : end);

            if (token == "auto")
            {
                tracks.push_back(ui::GridTrack::Auto());
                continue;
            }
            bool fraction = false;
            if (token.ends_with("fr")) { fraction = true; token.remove_suffix(2); }
            else if (token.ends_with("px")) token.remove_suffix(2);
            float value = 0.0f;
            if (token.empty() && fraction) value = 1.0f;
            else if (!ParseFloat(token, value)) continue;
            tracks.push_back(fraction ? ui::GridTrack::Fraction(value) : ui::GridTrack::Fixed(value));
        }
        return tracks;
    }

    void UILayoutComponent::SetPadding(const ui::Edges &value)
    {
        m_layout.padding = ui::Edges::FromVec4(glm::max(value.ToVec4(), glm::vec4(0.0f)));
    }

    std::vector<Property> UILayoutComponent::Serialize() const
    {
        return {
            BoolProperty("Enabled", IsEnabled()),
            EnumProperty("Mode", m_layout.mode, {"None", "Flex", "Grid"}),
            EnumProperty("Direction", m_layout.direction, {"Row", "Column", "Row Reverse", "Column Reverse"}),
            EnumProperty("Wrap", m_layout.wrap, {"No Wrap", "Wrap"}),
            EnumProperty("Justify", m_layout.justify, {"Start", "Center", "End", "Space Between", "Space Around", "Space Evenly"}),
            EnumProperty("AlignItems", m_layout.alignItems, kAlignItems),
            EnumProperty("AlignContent", m_layout.alignContent, {"Start", "Center", "End", "Stretch", "Space Between", "Space Around"}),
            EnumProperty("JustifyItems", m_layout.justifyItems, kAlignItems),
            Vec4Property("Padding", m_layout.padding.ToVec4()),
            Vec2Property("Gap", m_layout.gap),
            {"Columns", PropertyType::String, FormatGridTracks(m_layout.columns)},
            {"Rows", PropertyType::String, FormatGridTracks(m_layout.rows)},
            {"AutoRow", PropertyType::String, FormatGridTracks({m_layout.autoRow})},
        };
    }

    void UILayoutComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            const auto &name = property.name;
            const auto &value = property.value;
            if (name == "Enabled") SetEnabled(ReadBool(value));
            else if (name == "Mode") m_layout.mode = ReadEnum(value, ui::LayoutMode::Grid, m_layout.mode);
            else if (name == "Direction") m_layout.direction = ReadEnum(value, ui::FlexDirection::ColumnReverse, m_layout.direction);
            else if (name == "Wrap") m_layout.wrap = ReadEnum(value, ui::FlexWrap::Wrap, m_layout.wrap);
            else if (name == "Justify") m_layout.justify = ReadEnum(value, ui::JustifyContent::SpaceEvenly, m_layout.justify);
            else if (name == "AlignItems") m_layout.alignItems = ReadEnum(value, ui::AlignItems::Stretch, m_layout.alignItems);
            else if (name == "AlignContent") m_layout.alignContent = ReadEnum(value, ui::AlignContent::SpaceAround, m_layout.alignContent);
            else if (name == "JustifyItems") m_layout.justifyItems = ReadEnum(value, ui::AlignItems::Stretch, m_layout.justifyItems);
            else if (name == "Padding") SetPadding(ui::Edges::FromVec4(ReadVector(value, m_layout.padding.ToVec4())));
            else if (name == "Gap") SetGap(ReadVector(value, m_layout.gap));
            else if (name == "Columns") m_layout.columns = ParseGridTracks(value);
            else if (name == "Rows") m_layout.rows = ParseGridTracks(value);
            else if (name == "AutoRow")
            {
                const auto tracks = ParseGridTracks(value);
                if (!tracks.empty()) m_layout.autoRow = tracks.front();
            }
        }
    }

    std::vector<Property> UILayoutItemComponent::Serialize() const
    {
        const std::vector<std::string> sizeModes{"Fixed", "Content"};
        return {
            BoolProperty("Enabled", IsEnabled()),
            EnumProperty("WidthMode", m_widthMode, sizeModes),
            EnumProperty("HeightMode", m_heightMode, sizeModes),
            FloatProperty("Grow", m_grow),
            FloatProperty("Shrink", m_shrink),
            FloatProperty("Basis", m_basis),
            EnumProperty("AlignSelf", m_alignSelf, {"Auto", "Start", "Center", "End", "Stretch"}),
            FloatProperty("AspectRatio", m_aspectRatio),
            Vec2Property("Cell", glm::vec2(m_cell)),
            Vec2Property("Span", glm::vec2(m_span)),
            BoolProperty("IgnoreLayout", m_ignoreLayout),
        };
    }

    void UILayoutItemComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            const auto &name = property.name;
            const auto &value = property.value;
            if (name == "Enabled") SetEnabled(ReadBool(value));
            else if (name == "WidthMode") m_widthMode = ReadEnum(value, ui::SizeMode::Content, m_widthMode);
            else if (name == "HeightMode") m_heightMode = ReadEnum(value, ui::SizeMode::Content, m_heightMode);
            else if (name == "Grow") SetGrow(ReadFloat(value, m_grow));
            else if (name == "Shrink") SetShrink(ReadFloat(value, m_shrink));
            else if (name == "Basis") SetBasis(ReadFloat(value, m_basis));
            else if (name == "AlignSelf") m_alignSelf = ReadEnum(value, ui::AlignSelf::Stretch, m_alignSelf);
            else if (name == "AspectRatio") SetAspectRatio(ReadFloat(value, m_aspectRatio));
            else if (name == "Cell") SetCell(glm::ivec2(glm::round(ReadVector(value, glm::vec2(m_cell)))));
            else if (name == "Span") SetSpan(glm::ivec2(glm::round(ReadVector(value, glm::vec2(m_span)))));
            else if (name == "IgnoreLayout") m_ignoreLayout = ReadBool(value);
        }
    }

    void UILayoutItemComponent::ApplyTo(ui::ItemLayout &item) const
    {
        item.widthMode = m_widthMode;
        item.heightMode = m_heightMode;
        item.grow = m_grow;
        item.shrink = m_shrink;
        item.basis = m_basis;
        item.alignSelf = m_alignSelf;
        item.aspectRatio = m_aspectRatio;
        item.column = m_cell.x;
        item.row = m_cell.y;
        item.columnSpan = m_span.x;
        item.rowSpan = m_span.y;
        item.ignoreLayout = m_ignoreLayout;
    }
}
