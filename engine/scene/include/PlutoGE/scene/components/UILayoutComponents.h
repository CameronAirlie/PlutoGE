#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/ui/layout/Layout.h"

#include <string>
#include <vector>

namespace PlutoGE::scene
{
    // Arranges the entity's children as a flex row/column or a grid. Children
    // without a layout of their own keep using their RectTransform anchors.
    class UILayoutComponent : public TypedComponent<UILayoutComponent>
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        [[nodiscard]] const ui::ContainerLayout &GetLayout() const { return m_layout; }
        void SetLayout(const ui::ContainerLayout &layout) { m_layout = layout; }

        [[nodiscard]] ui::LayoutMode GetMode() const { return m_layout.mode; }
        void SetMode(ui::LayoutMode value) { m_layout.mode = value; }
        [[nodiscard]] ui::FlexDirection GetDirection() const { return m_layout.direction; }
        void SetDirection(ui::FlexDirection value) { m_layout.direction = value; }
        [[nodiscard]] ui::FlexWrap GetWrap() const { return m_layout.wrap; }
        void SetWrap(ui::FlexWrap value) { m_layout.wrap = value; }
        [[nodiscard]] ui::JustifyContent GetJustify() const { return m_layout.justify; }
        void SetJustify(ui::JustifyContent value) { m_layout.justify = value; }
        [[nodiscard]] ui::AlignItems GetAlignItems() const { return m_layout.alignItems; }
        void SetAlignItems(ui::AlignItems value) { m_layout.alignItems = value; }
        [[nodiscard]] ui::AlignContent GetAlignContent() const { return m_layout.alignContent; }
        void SetAlignContent(ui::AlignContent value) { m_layout.alignContent = value; }
        [[nodiscard]] ui::AlignItems GetJustifyItems() const { return m_layout.justifyItems; }
        void SetJustifyItems(ui::AlignItems value) { m_layout.justifyItems = value; }
        [[nodiscard]] ui::Edges GetPadding() const { return m_layout.padding; }
        void SetPadding(const ui::Edges &value);
        [[nodiscard]] glm::vec2 GetGap() const { return m_layout.gap; }
        void SetGap(glm::vec2 value) { m_layout.gap = glm::max(value, glm::vec2(0.0f)); }
        [[nodiscard]] const std::vector<ui::GridTrack> &GetColumns() const { return m_layout.columns; }
        void SetColumns(std::vector<ui::GridTrack> value) { m_layout.columns = std::move(value); }
        [[nodiscard]] const std::vector<ui::GridTrack> &GetRows() const { return m_layout.rows; }
        void SetRows(std::vector<ui::GridTrack> value) { m_layout.rows = std::move(value); }
        [[nodiscard]] ui::GridTrack GetAutoRow() const { return m_layout.autoRow; }
        void SetAutoRow(ui::GridTrack value) { m_layout.autoRow = value; }

    private:
        ui::ContainerLayout m_layout{.mode = ui::LayoutMode::Flex};
    };

    // Per-item layout options: content sizing, flex grow/shrink, alignment
    // overrides and grid placement. Optional; without it items keep their
    // authored RectTransform size.
    class UILayoutItemComponent : public TypedComponent<UILayoutItemComponent>
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        [[nodiscard]] ui::SizeMode GetWidthMode() const { return m_widthMode; }
        void SetWidthMode(ui::SizeMode value) { m_widthMode = value; }
        [[nodiscard]] ui::SizeMode GetHeightMode() const { return m_heightMode; }
        void SetHeightMode(ui::SizeMode value) { m_heightMode = value; }
        [[nodiscard]] float GetGrow() const { return m_grow; }
        void SetGrow(float value) { m_grow = std::max(value, 0.0f); }
        [[nodiscard]] float GetShrink() const { return m_shrink; }
        void SetShrink(float value) { m_shrink = std::max(value, 0.0f); }
        [[nodiscard]] float GetBasis() const { return m_basis; }
        void SetBasis(float value) { m_basis = value; }
        [[nodiscard]] ui::AlignSelf GetAlignSelf() const { return m_alignSelf; }
        void SetAlignSelf(ui::AlignSelf value) { m_alignSelf = value; }
        [[nodiscard]] float GetAspectRatio() const { return m_aspectRatio; }
        void SetAspectRatio(float value) { m_aspectRatio = std::max(value, 0.0f); }
        [[nodiscard]] glm::ivec2 GetCell() const { return m_cell; }
        void SetCell(glm::ivec2 value) { m_cell = glm::max(value, glm::ivec2(-1)); }
        [[nodiscard]] glm::ivec2 GetSpan() const { return m_span; }
        void SetSpan(glm::ivec2 value) { m_span = glm::max(value, glm::ivec2(1)); }
        [[nodiscard]] bool GetIgnoreLayout() const { return m_ignoreLayout; }
        void SetIgnoreLayout(bool value) { m_ignoreLayout = value; }

        // Copies these options into a solver item.
        void ApplyTo(ui::ItemLayout &item) const;

    private:
        ui::SizeMode m_widthMode = ui::SizeMode::Fixed;
        ui::SizeMode m_heightMode = ui::SizeMode::Fixed;
        float m_grow = 0.0f;
        float m_shrink = 1.0f;
        float m_basis = -1.0f;
        ui::AlignSelf m_alignSelf = ui::AlignSelf::Auto;
        float m_aspectRatio = 0.0f;
        glm::ivec2 m_cell{-1};  // column, row; -1 auto-places
        glm::ivec2 m_span{1};   // column span, row span
        bool m_ignoreLayout = false;
    };

    // "1fr 120 auto" <-> grid tracks.
    [[nodiscard]] std::string FormatGridTracks(const std::vector<ui::GridTrack> &tracks);
    [[nodiscard]] std::vector<ui::GridTrack> ParseGridTracks(std::string_view text);
}
