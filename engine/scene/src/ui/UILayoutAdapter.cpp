#include "UILayoutAdapter.h"

#include "PlutoGE/scene/Entity.h"
#include "PlutoGE/scene/components/UIComponent.h"
#include "PlutoGE/scene/components/UILayoutComponents.h"

#include <cmath>
#include <vector>

namespace PlutoGE::scene
{
    namespace
    {
        bool IsLegacyGroup(const RectTransformComponent &rect, const Entity &entity)
        {
            const auto *layout = entity.GetComponent<UILayoutComponent>();
            return rect.GetLayoutMode() != UILayoutMode::None && !(layout && layout->IsEnabled());
        }

        // Approximates the legacy layout groups with the flex/grid solver.
        ui::ContainerLayout ConvertLegacyLayout(const RectTransformComponent &rect, std::size_t childCount)
        {
            ui::ContainerLayout container;
            container.padding = ui::Edges::FromVec4(rect.GetLayoutPadding());
            container.gap = rect.GetLayoutSpacing();
            switch (rect.GetLayoutMode())
            {
            case UILayoutMode::Horizontal:
                container.mode = ui::LayoutMode::Flex;
                container.direction = ui::FlexDirection::Row;
                container.alignItems = rect.GetControlChildHeight() ? ui::AlignItems::Stretch : ui::AlignItems::Center;
                break;
            case UILayoutMode::Vertical:
                container.mode = ui::LayoutMode::Flex;
                container.direction = ui::FlexDirection::Column;
                container.alignItems = rect.GetControlChildWidth() ? ui::AlignItems::Stretch : ui::AlignItems::Center;
                break;
            case UILayoutMode::Grid:
            {
                container.mode = ui::LayoutMode::Grid;
                const int columns = std::max(rect.GetGridColumns(), 1);
                container.columns.assign(static_cast<std::size_t>(columns), ui::GridTrack::Fraction());
                const std::size_t rows = std::max<std::size_t>((childCount + columns - 1) / columns, 1);
                container.rows.assign(rows, ui::GridTrack::Fraction());
                container.justifyItems = rect.GetControlChildWidth() ? ui::AlignItems::Stretch : ui::AlignItems::Center;
                container.alignItems = rect.GetControlChildHeight() ? ui::AlignItems::Stretch : ui::AlignItems::Center;
                break;
            }
            default:
                break;
            }
            return container;
        }

        std::size_t CountLayoutChildren(const Entity &entity)
        {
            std::size_t count = 0;
            for (const auto *child : entity.GetChildren())
                if (const auto *rect = child->GetComponent<RectTransformComponent>(); child->IsSelfActive() && rect && rect->IsEnabled())
                    ++count;
            return count;
        }

        ui::SizeMode LegacySizeMode(UIContentSizeMode mode)
        {
            return mode == UIContentSizeMode::Preferred ? ui::SizeMode::Content : ui::SizeMode::Fixed;
        }
    }

    ui::LayoutNode BuildLayoutNode(const Entity &entity, const RectTransformComponent &rect, const UIMeasureContext &measure)
    {
        ui::LayoutNode node;
        ui::ItemLayout &item = node.item;
        item.anchorMin = rect.GetAnchorMin();
        item.anchorMax = rect.GetAnchorMax();
        item.pivot = rect.GetPivot();
        item.anchoredPosition = rect.GetAnchoredPosition();
        item.sizeDelta = rect.GetSizeDelta();
        item.margin = ui::Edges::FromVec4(rect.GetMargin());
        item.minSize = rect.GetMinimumSize();
        item.maxSize = rect.GetMaximumSize();
        item.widthMode = LegacySizeMode(rect.GetHorizontalContentSize());
        item.heightMode = LegacySizeMode(rect.GetVerticalContentSize());
        // The legacy "Minimum" content size meant: use the minimum size.
        if (rect.GetHorizontalContentSize() == UIContentSizeMode::Minimum) item.sizeDelta.x = item.minSize.x;
        if (rect.GetVerticalContentSize() == UIContentSizeMode::Minimum) item.sizeDelta.y = item.minSize.y;
        if (const auto *options = entity.GetComponent<UILayoutItemComponent>(); options && options->IsEnabled())
            options->ApplyTo(item);

        if (const auto *layout = entity.GetComponent<UILayoutComponent>(); layout && layout->IsEnabled())
            node.container = layout->GetLayout();
        else if (IsLegacyGroup(rect, entity))
            node.container = ConvertLegacyLayout(rect, CountLayoutChildren(entity));

        // Content measurement: the largest of the entity's content providers.
        std::vector<const IUIContentSize *> providers;
        for (const auto &bucket : entity.GetComponentBuckets())
            for (const auto *component : bucket)
                if (component && component->IsEnabled())
                    if (const auto *provider = dynamic_cast<const IUIContentSize *>(component))
                        providers.push_back(provider);
        if (!providers.empty())
            node.measure = [providers, context = UIMeasureContext(measure)](glm::vec2 available) mutable
            {
                glm::vec2 size{0.0f};
                for (const auto *provider : providers)
                    size = glm::max(size, provider->MeasureContent(available, context));
                return size;
            };
        return node;
    }

    void ApplyLegacyChildOptions(const Entity &parentEntity, ui::ItemLayout &child)
    {
        const auto *rect = parentEntity.GetComponent<RectTransformComponent>();
        if (!rect || !rect->IsEnabled() || !IsLegacyGroup(*rect, parentEntity))
            return;
        const RectTransformComponent &parent = *rect;
        if (parent.GetLayoutMode() == UILayoutMode::Horizontal && parent.GetExpandChildWidth() && parent.GetControlChildWidth())
            child.grow = std::max(child.grow, 1.0f);
        else if (parent.GetLayoutMode() == UILayoutMode::Vertical && parent.GetExpandChildHeight() && parent.GetControlChildHeight())
            child.grow = std::max(child.grow, 1.0f);
    }
}
