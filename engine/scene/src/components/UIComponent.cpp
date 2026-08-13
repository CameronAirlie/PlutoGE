#include "PlutoGE/scene/components/UIComponent.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace PlutoGE::scene
{
    float ResolveCanvasScaleFactor(const CanvasComponent &canvas, const glm::vec2 &viewportSize)
    {
        const float userScale = std::max(canvas.GetScaleFactor(), 0.0001f);
        if (canvas.GetScaleMode() == CanvasScaleMode::ConstantPixels)
        {
            return userScale;
        }
        if (canvas.GetScaleMode() == CanvasScaleMode::ConstantPhysicalSize)
        {
            // Until platform DPI reporting is available, 96 DPI is the stable
            // cross-platform baseline and userScale acts as the physical multiplier.
            return userScale;
        }

        const glm::vec2 reference = glm::max(canvas.GetReferenceResolution(), glm::vec2(1.0f));
        const glm::vec2 ratios = glm::max(viewportSize / reference, glm::vec2(0.0001f));
        float resolutionScale = 1.0f;
        switch (canvas.GetScreenMatchMode())
        {
        case UIScreenMatchMode::Expand:
            resolutionScale = std::min(ratios.x, ratios.y);
            break;
        case UIScreenMatchMode::Shrink:
            resolutionScale = std::max(ratios.x, ratios.y);
            break;
        case UIScreenMatchMode::MatchWidthOrHeight:
        default:
            resolutionScale = std::exp2(glm::mix(std::log2(ratios.x), std::log2(ratios.y),
                                                 canvas.GetMatchWidthOrHeight()));
            break;
        }
        return userScale * resolutionScale;
    }

    RectTransformLayout ResolveRectTransformLayout(const RectTransformComponent &rectTransform,
                                                   const RectTransformLayout &parentRect)
    {
        const glm::vec2 parentSize = glm::max(parentRect.max - parentRect.min, glm::vec2(0.0f));
        const glm::vec2 anchorMinPosition = parentRect.min + parentSize * rectTransform.GetAnchorMin();
        const glm::vec2 anchorMaxPosition = parentRect.min + parentSize * rectTransform.GetAnchorMax();
        const glm::vec2 anchorReference = glm::mix(anchorMinPosition, anchorMaxPosition, rectTransform.GetPivot());
        const glm::vec2 unclampedSize = glm::max((anchorMaxPosition - anchorMinPosition) + rectTransform.GetSizeDelta(), glm::vec2(0.0f));
        const glm::vec2 size = glm::clamp(unclampedSize, rectTransform.GetMinimumSize(), rectTransform.GetMaximumSize());
        const glm::vec4 margin = rectTransform.GetMargin();
        const glm::vec2 outerMin = anchorReference + rectTransform.GetAnchoredPosition() - size * rectTransform.GetPivot();
        return {.min = outerMin + glm::vec2(margin.x, margin.y),
                .max = outerMin + size - glm::vec2(margin.z, margin.w)};
    }

    glm::vec2 ResolvePreferredLayoutSize(const RectTransformComponent &layoutGroup,
                                         const std::vector<const RectTransformComponent *> &children)
    {
        const glm::vec4 padding = layoutGroup.GetLayoutPadding();
        const glm::vec2 spacing = layoutGroup.GetLayoutSpacing();
        glm::vec2 result(padding.x + padding.z, padding.y + padding.w);
        if (children.empty())
            return result;

        if (layoutGroup.GetLayoutMode() == UILayoutMode::Horizontal)
        {
            for (const auto *child : children)
            {
                if (!child)
                    continue;
                const glm::vec2 size = glm::clamp(child->GetSizeDelta(), child->GetMinimumSize(), child->GetMaximumSize());
                result.x += size.x;
                result.y = std::max(result.y, padding.y + padding.w + size.y);
            }
            result.x += spacing.x * static_cast<float>(children.size() - 1);
        }
        else if (layoutGroup.GetLayoutMode() == UILayoutMode::Vertical)
        {
            for (const auto *child : children)
            {
                if (!child)
                    continue;
                const glm::vec2 size = glm::clamp(child->GetSizeDelta(), child->GetMinimumSize(), child->GetMaximumSize());
                result.y += size.y;
                result.x = std::max(result.x, padding.x + padding.z + size.x);
            }
            result.y += spacing.y * static_cast<float>(children.size() - 1);
        }
        else if (layoutGroup.GetLayoutMode() == UILayoutMode::Grid)
        {
            glm::vec2 cell(0.0f);
            for (const auto *child : children)
                if (child)
                    cell = glm::max(cell, glm::clamp(child->GetSizeDelta(), child->GetMinimumSize(), child->GetMaximumSize()));
            const int columns = std::max(layoutGroup.GetGridColumns(), 1);
            const int rows = static_cast<int>((children.size() + static_cast<std::size_t>(columns) - 1) /
                                              static_cast<std::size_t>(columns));
            result += glm::vec2(cell.x * columns + spacing.x * std::max(columns - 1, 0),
                                cell.y * rows + spacing.y * std::max(rows - 1, 0));
        }
        return result;
    }

    RectTransformLayout ResolveAutomaticChildLayout(
        const RectTransformComponent &layoutGroup,
        const RectTransformLayout &groupRect,
        const std::vector<const RectTransformComponent *> &children,
        std::size_t childIndex)
    {
        if (layoutGroup.GetLayoutMode() == UILayoutMode::None || childIndex >= children.size() || !children[childIndex])
            return groupRect;

        const glm::vec4 padding = layoutGroup.GetLayoutPadding();
        const glm::vec2 spacing = layoutGroup.GetLayoutSpacing();
        const glm::vec2 innerMin = groupRect.min + glm::vec2(padding.x, padding.y);
        const glm::vec2 innerMax = groupRect.max - glm::vec2(padding.z, padding.w);
        const glm::vec2 innerSize = glm::max(innerMax - innerMin, glm::vec2(0.0f));
        const auto *child = children[childIndex];
        glm::vec2 childSize = glm::clamp(child->GetSizeDelta(), child->GetMinimumSize(), child->GetMaximumSize());

        if (layoutGroup.GetLayoutMode() == UILayoutMode::Horizontal)
        {
            float preferredTotal = 0.0f;
            for (const auto *item : children)
                if (item)
                    preferredTotal += glm::clamp(item->GetSizeDelta(), item->GetMinimumSize(), item->GetMaximumSize()).x;
            const float spacingTotal = spacing.x * static_cast<float>(children.size() > 0 ? children.size() - 1 : 0);
            const float extra = layoutGroup.GetExpandChildWidth()
                                    ? std::max(innerSize.x - preferredTotal - spacingTotal, 0.0f) / children.size()
                                    : 0.0f;
            float x = innerMin.x;
            for (std::size_t index = 0; index < childIndex; ++index)
            {
                const auto *item = children[index];
                x += (item ? glm::clamp(item->GetSizeDelta(), item->GetMinimumSize(), item->GetMaximumSize()).x : 0.0f) +
                     extra + spacing.x;
            }
            if (layoutGroup.GetControlChildWidth())
                childSize.x += extra;
            if (layoutGroup.GetControlChildHeight())
                childSize.y = innerSize.y;
            return {.min = {x, innerMin.y + (innerSize.y - childSize.y) * 0.5f},
                    .max = {x + childSize.x, innerMin.y + (innerSize.y + childSize.y) * 0.5f}};
        }
        if (layoutGroup.GetLayoutMode() == UILayoutMode::Vertical)
        {
            float preferredTotal = 0.0f;
            for (const auto *item : children)
                if (item)
                    preferredTotal += glm::clamp(item->GetSizeDelta(), item->GetMinimumSize(), item->GetMaximumSize()).y;
            const float spacingTotal = spacing.y * static_cast<float>(children.size() > 0 ? children.size() - 1 : 0);
            const float extra = layoutGroup.GetExpandChildHeight()
                                    ? std::max(innerSize.y - preferredTotal - spacingTotal, 0.0f) / children.size()
                                    : 0.0f;
            float top = innerMax.y;
            for (std::size_t index = 0; index < childIndex; ++index)
            {
                const auto *item = children[index];
                top -= (item ? glm::clamp(item->GetSizeDelta(), item->GetMinimumSize(), item->GetMaximumSize()).y : 0.0f) +
                       extra + spacing.y;
            }
            if (layoutGroup.GetControlChildWidth())
                childSize.x = innerSize.x;
            if (layoutGroup.GetControlChildHeight())
                childSize.y += extra;
            return {.min = {innerMin.x + (innerSize.x - childSize.x) * 0.5f, top - childSize.y},
                    .max = {innerMin.x + (innerSize.x + childSize.x) * 0.5f, top}};
        }

        const int columns = std::max(layoutGroup.GetGridColumns(), 1);
        const int rows = std::max(static_cast<int>((children.size() + columns - 1) / columns), 1);
        const glm::vec2 cellSize(
            std::max((innerSize.x - spacing.x * std::max(columns - 1, 0)) / columns, 0.0f),
            std::max((innerSize.y - spacing.y * std::max(rows - 1, 0)) / rows, 0.0f));
        const int column = static_cast<int>(childIndex % columns);
        const int row = static_cast<int>(childIndex / columns);
        if (layoutGroup.GetControlChildWidth())
            childSize.x = cellSize.x;
        if (layoutGroup.GetControlChildHeight())
            childSize.y = cellSize.y;
        const glm::vec2 cellMin(innerMin.x + column * (cellSize.x + spacing.x),
                                innerMax.y - (row + 1) * cellSize.y - row * spacing.y);
        return {.min = cellMin + (cellSize - childSize) * 0.5f,
                .max = cellMin + (cellSize + childSize) * 0.5f};
    }

    namespace
    {
        std::string SerializeVec2(const glm::vec2 &value)
        {
            return std::to_string(value.x) + "," + std::to_string(value.y);
        }

        glm::vec2 ParseVec2(const std::string &value, const glm::vec2 &fallback = glm::vec2(0.0f))
        {
            glm::vec2 parsedValue = fallback;
            std::sscanf(value.c_str(), "%f,%f", &parsedValue.x, &parsedValue.y);
            return parsedValue;
        }

        std::string SerializeColor(const glm::vec4 &value)
        {
            return std::to_string(value.r) + "," + std::to_string(value.g) + "," + std::to_string(value.b) + "," + std::to_string(value.a);
        }

        glm::vec4 ParseColor(const std::string &value, const glm::vec4 &fallback = glm::vec4(1.0f))
        {
            glm::vec4 parsedValue = fallback;
            std::sscanf(value.c_str(), "%f,%f,%f,%f", &parsedValue.r, &parsedValue.g, &parsedValue.b, &parsedValue.a);
            return parsedValue;
        }

        const std::vector<std::string> &AnchorPresetNames()
        {
            static const std::vector<std::string> names = {
                "TopLeft",
                "TopCenter",
                "TopRight",
                "MiddleLeft",
                "MiddleCenter",
                "MiddleRight",
                "BottomLeft",
                "BottomCenter",
                "BottomRight",
                "Stretch",
            };
            return names;
        }

        int ClampAnchorPresetIndex(int index)
        {
            return std::clamp(index, 0, static_cast<int>(AnchorPresetNames().size()) - 1);
        }
    }

    std::vector<Property> CanvasComponent::Serialize() const
    {
        return {
            {"Enabled", PropertyType::Bool, IsEnabled() ? "true" : "false"},
            {"RenderMode", PropertyType::Enum, std::to_string(static_cast<int>(m_renderMode)), {"Screen Space", "World Screen Space", "Screen Space Camera", "World Space"}},
            {"ScaleMode", PropertyType::Enum, std::to_string(static_cast<int>(m_scaleMode)), {"ConstantPixels", "ScaleWithScreenSize", "ConstantPhysicalSize"}},
            {"ScaleFactor", PropertyType::Float, std::to_string(m_scaleFactor)},
            {"ReferenceResolution", PropertyType::Vec2, SerializeVec2(m_referenceResolution)},
            {"MatchWidthOrHeight", PropertyType::Float, std::to_string(m_matchWidthOrHeight)},
            {"ScreenMatchMode", PropertyType::Enum, std::to_string(static_cast<int>(m_screenMatchMode)), {"MatchWidthOrHeight", "Expand", "Shrink"}},
            {"SortingOrder", PropertyType::Int, std::to_string(m_sortingOrder)},
            {"WorldSizeMode", PropertyType::Enum, std::to_string(static_cast<int>(m_worldSizeMode)), {"WorldUnits", "ConstantScreenSize", "DistanceScaled"}},
            {"FaceCamera", PropertyType::Bool, m_faceCamera ? "true" : "false"},
            {"Backend", PropertyType::Enum, std::to_string(static_cast<int>(m_backend)), {"Legacy Native", "RmlUi"}},
            {"ContentSource", PropertyType::Enum, std::to_string(static_cast<int>(m_contentSource)), {"RML Document", "Text"}},
            {"DocumentPath", PropertyType::String, m_documentPath},
        };
    }

    void CanvasComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Enabled")
                SetEnabled(property.value == "true" || property.value == "1");
            else if (property.name == "RenderMode")
            {
                const int renderMode = std::clamp(std::stoi(property.value),
                                                  static_cast<int>(CanvasRenderMode::ScreenSpaceOverlay),
                                                  static_cast<int>(CanvasRenderMode::WorldSpace));
                m_renderMode = static_cast<CanvasRenderMode>(renderMode);
            }
            else if (property.name == "ScaleMode")
                m_scaleMode = static_cast<CanvasScaleMode>(std::clamp(std::stoi(property.value), 0, 2));
            else if (property.name == "ScaleFactor")
                m_scaleFactor = std::stof(property.value);
            else if (property.name == "ReferenceResolution")
                SetReferenceResolution(ParseVec2(property.value, m_referenceResolution));
            else if (property.name == "MatchWidthOrHeight")
                SetMatchWidthOrHeight(std::stof(property.value));
            else if (property.name == "ScreenMatchMode")
                m_screenMatchMode = static_cast<UIScreenMatchMode>(std::clamp(std::stoi(property.value), 0, 2));
            else if (property.name == "SortingOrder")
                m_sortingOrder = std::stoi(property.value);
            else if (property.name == "WorldSizeMode")
                m_worldSizeMode = static_cast<UIWorldSizeMode>(std::clamp(std::stoi(property.value), 0, 2));
            else if (property.name == "FaceCamera")
                m_faceCamera = property.value == "true" || property.value == "1";
            else if (property.name == "Backend")
                m_backend = static_cast<UIRenderBackend>(std::clamp(std::stoi(property.value), 0, 1));
            else if (property.name == "ContentSource")
                m_contentSource = static_cast<RmlUiContentSource>(std::clamp(std::stoi(property.value), 0, 1));
            else if (property.name == "DocumentPath")
                m_documentPath = property.value;
        }
    }

    std::vector<Property> RmlWidgetComponent::Serialize() const
    {
        return {
            {"Enabled", PropertyType::Bool, IsEnabled() ? "true" : "false"},
            {"Source", PropertyType::String, m_source},
            {"Visible", PropertyType::Bool, m_visible ? "true" : "false"},
        };
    }

    void RmlWidgetComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Enabled")
                SetEnabled(property.value == "true" || property.value == "1");
            else if (property.name == "Source")
                m_source = property.value;
            else if (property.name == "Visible")
                m_visible = property.value == "true" || property.value == "1";
        }
    }

    void RectTransformComponent::SetAnchorPreset(UIAnchorPreset preset)
    {
        m_anchorPreset = preset;
        switch (preset)
        {
        case UIAnchorPreset::TopLeft:
            m_anchorMin = m_anchorMax = glm::vec2(0.0f, 1.0f);
            m_pivot = glm::vec2(0.0f, 1.0f);
            break;
        case UIAnchorPreset::TopCenter:
            m_anchorMin = m_anchorMax = glm::vec2(0.5f, 1.0f);
            m_pivot = glm::vec2(0.5f, 1.0f);
            break;
        case UIAnchorPreset::TopRight:
            m_anchorMin = m_anchorMax = glm::vec2(1.0f, 1.0f);
            m_pivot = glm::vec2(1.0f, 1.0f);
            break;
        case UIAnchorPreset::MiddleLeft:
            m_anchorMin = m_anchorMax = glm::vec2(0.0f, 0.5f);
            m_pivot = glm::vec2(0.0f, 0.5f);
            break;
        case UIAnchorPreset::MiddleRight:
            m_anchorMin = m_anchorMax = glm::vec2(1.0f, 0.5f);
            m_pivot = glm::vec2(1.0f, 0.5f);
            break;
        case UIAnchorPreset::BottomLeft:
            m_anchorMin = m_anchorMax = glm::vec2(0.0f, 0.0f);
            m_pivot = glm::vec2(0.0f, 0.0f);
            break;
        case UIAnchorPreset::BottomCenter:
            m_anchorMin = m_anchorMax = glm::vec2(0.5f, 0.0f);
            m_pivot = glm::vec2(0.5f, 0.0f);
            break;
        case UIAnchorPreset::BottomRight:
            m_anchorMin = m_anchorMax = glm::vec2(1.0f, 0.0f);
            m_pivot = glm::vec2(1.0f, 0.0f);
            break;
        case UIAnchorPreset::Stretch:
            m_anchorMin = glm::vec2(0.0f);
            m_anchorMax = glm::vec2(1.0f);
            m_pivot = glm::vec2(0.5f);
            break;
        case UIAnchorPreset::MiddleCenter:
        default:
            m_anchorMin = m_anchorMax = glm::vec2(0.5f);
            m_pivot = glm::vec2(0.5f);
            break;
        }
    }

    std::vector<Property> RectTransformComponent::Serialize() const
    {
        return {
            {"Enabled", PropertyType::Bool, IsEnabled() ? "true" : "false"},
            {"AnchorPreset", PropertyType::Enum, std::to_string(static_cast<int>(m_anchorPreset)), AnchorPresetNames()},
            {"AnchoredPosition", PropertyType::Vec2, SerializeVec2(m_anchoredPosition)},
            {"SizeDelta", PropertyType::Vec2, SerializeVec2(m_sizeDelta)},
            {"Pivot", PropertyType::Vec2, SerializeVec2(m_pivot)},
            {"AnchorMin", PropertyType::Vec2, SerializeVec2(m_anchorMin)},
            {"AnchorMax", PropertyType::Vec2, SerializeVec2(m_anchorMax)},
            {"Margin", PropertyType::Color, SerializeColor(m_margin)},
            {"MinimumSize", PropertyType::Vec2, SerializeVec2(m_minimumSize)},
            {"MaximumSize", PropertyType::Vec2, SerializeVec2(m_maximumSize)},
            {"Rotation", PropertyType::Float, std::to_string(m_rotation)},
            {"LocalScale", PropertyType::Vec2, SerializeVec2(m_localScale)},
            {"Opacity", PropertyType::Float, std::to_string(m_opacity)},
            {"ClipChildren", PropertyType::Bool, m_clipChildren ? "true" : "false"},
            {"RaycastTarget", PropertyType::Bool, m_raycastTarget ? "true" : "false"},
            {"LayoutMode", PropertyType::Enum, std::to_string(static_cast<int>(m_layoutMode)), {"None", "Horizontal", "Vertical", "Grid"}},
            {"LayoutPadding", PropertyType::Color, SerializeColor(m_layoutPadding)},
            {"LayoutSpacing", PropertyType::Vec2, SerializeVec2(m_layoutSpacing)},
            {"GridColumns", PropertyType::Int, std::to_string(m_gridColumns)},
            {"ControlChildWidth", PropertyType::Bool, m_controlChildWidth ? "true" : "false"},
            {"ControlChildHeight", PropertyType::Bool, m_controlChildHeight ? "true" : "false"},
            {"ExpandChildWidth", PropertyType::Bool, m_expandChildWidth ? "true" : "false"},
            {"ExpandChildHeight", PropertyType::Bool, m_expandChildHeight ? "true" : "false"},
            {"HorizontalContentSize", PropertyType::Enum, std::to_string(static_cast<int>(m_horizontalContentSize)), {"Unconstrained", "Minimum", "Preferred"}},
            {"VerticalContentSize", PropertyType::Enum, std::to_string(static_cast<int>(m_verticalContentSize)), {"Unconstrained", "Minimum", "Preferred"}},
        };
    }

    void RectTransformComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Enabled")
                SetEnabled(property.value == "true" || property.value == "1");
            else if (property.name == "AnchorPreset")
                m_anchorPreset = static_cast<UIAnchorPreset>(ClampAnchorPresetIndex(std::stoi(property.value)));
            else if (property.name == "AnchoredPosition")
                m_anchoredPosition = ParseVec2(property.value, m_anchoredPosition);
            else if (property.name == "SizeDelta")
                m_sizeDelta = ParseVec2(property.value, m_sizeDelta);
            else if (property.name == "Pivot")
                m_pivot = ParseVec2(property.value, m_pivot);
            else if (property.name == "AnchorMin")
                m_anchorMin = ParseVec2(property.value, m_anchorMin);
            else if (property.name == "AnchorMax")
                m_anchorMax = ParseVec2(property.value, m_anchorMax);
            else if (property.name == "Margin")
                m_margin = ParseColor(property.value, m_margin);
            else if (property.name == "MinimumSize")
                SetMinimumSize(ParseVec2(property.value, m_minimumSize));
            else if (property.name == "MaximumSize")
                SetMaximumSize(ParseVec2(property.value, m_maximumSize));
            else if (property.name == "Rotation")
                m_rotation = std::stof(property.value);
            else if (property.name == "LocalScale")
                m_localScale = ParseVec2(property.value, m_localScale);
            else if (property.name == "Opacity")
                SetOpacity(std::stof(property.value));
            else if (property.name == "ClipChildren")
                m_clipChildren = property.value == "true" || property.value == "1";
            else if (property.name == "RaycastTarget")
                m_raycastTarget = property.value == "true" || property.value == "1";
            else if (property.name == "LayoutMode")
                m_layoutMode = static_cast<UILayoutMode>(std::clamp(std::stoi(property.value), 0, 3));
            else if (property.name == "LayoutPadding")
                SetLayoutPadding(ParseColor(property.value, m_layoutPadding));
            else if (property.name == "LayoutSpacing")
                SetLayoutSpacing(ParseVec2(property.value, m_layoutSpacing));
            else if (property.name == "GridColumns")
                SetGridColumns(std::stoi(property.value));
            else if (property.name == "ControlChildWidth")
                m_controlChildWidth = property.value == "true" || property.value == "1";
            else if (property.name == "ControlChildHeight")
                m_controlChildHeight = property.value == "true" || property.value == "1";
            else if (property.name == "ExpandChildWidth")
                m_expandChildWidth = property.value == "true" || property.value == "1";
            else if (property.name == "ExpandChildHeight")
                m_expandChildHeight = property.value == "true" || property.value == "1";
            else if (property.name == "HorizontalContentSize")
                m_horizontalContentSize = static_cast<UIContentSizeMode>(std::clamp(std::stoi(property.value), 0, 2));
            else if (property.name == "VerticalContentSize")
                m_verticalContentSize = static_cast<UIContentSizeMode>(std::clamp(std::stoi(property.value), 0, 2));
        }
    }

    std::vector<Property> UIImageComponent::Serialize() const
    {
        return {
            {"Enabled", PropertyType::Bool, IsEnabled() ? "true" : "false"},
            {"Color", PropertyType::Color, SerializeColor(m_color)},
            {"TexturePath", PropertyType::String, m_texturePath},
            {"PreserveAspect", PropertyType::Bool, m_preserveAspect ? "true" : "false"},
            {"FillAmount", PropertyType::Float, std::to_string(m_fillAmount)},
            {"ImageType", PropertyType::Enum, std::to_string(static_cast<int>(m_imageType)), {"Simple", "Sliced", "FilledHorizontal", "FilledVertical", "FilledRadial", "ProceduralCrosshair", "ProceduralCircle", "ProceduralArc", "ProceduralRoundedRect"}},
            {"Border", PropertyType::Color, SerializeColor(m_border)},
            {"Thickness", PropertyType::Float, std::to_string(m_thickness)},
            {"CornerRadius", PropertyType::Float, std::to_string(m_cornerRadius)},
            {"StartAngle", PropertyType::Float, std::to_string(m_startAngle)},
        };
    }

    void UIImageComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Enabled")
                SetEnabled(property.value == "true" || property.value == "1");
            else if (property.name == "Color")
                m_color = ParseColor(property.value, m_color);
            else if (property.name == "TexturePath")
                m_texturePath = property.value;
            else if (property.name == "PreserveAspect")
                m_preserveAspect = property.value == "true" || property.value == "1";
            else if (property.name == "FillAmount")
                SetFillAmount(std::stof(property.value));
            else if (property.name == "ImageType")
                m_imageType = static_cast<UIImageType>(std::clamp(std::stoi(property.value), 0, 8));
            else if (property.name == "Border")
                SetBorder(ParseColor(property.value, m_border));
            else if (property.name == "Thickness")
                SetThickness(std::stof(property.value));
            else if (property.name == "CornerRadius")
                SetCornerRadius(std::stof(property.value));
            else if (property.name == "StartAngle")
                m_startAngle = std::stof(property.value);
        }
    }

    std::vector<Property> UITextComponent::Serialize() const
    {
        return {
            {"Enabled", PropertyType::Bool, IsEnabled() ? "true" : "false"},
            {"Text", PropertyType::String, m_text},
            {"Color", PropertyType::Color, SerializeColor(m_color)},
            {"FontSize", PropertyType::Float, std::to_string(m_fontSize)},
            {"FontPath", PropertyType::String, m_fontPath},
            {"RichText", PropertyType::Bool, m_richText ? "true" : "false"},
            {"Alignment", PropertyType::Enum, std::to_string(static_cast<int>(m_alignment)), {"TopLeft", "TopCenter", "TopRight", "MiddleLeft", "MiddleCenter", "MiddleRight", "BottomLeft", "BottomCenter", "BottomRight"}},
            {"Wrap", PropertyType::Bool, m_wrap ? "true" : "false"},
            {"LineSpacing", PropertyType::Float, std::to_string(m_lineSpacing)},
            {"OutlineColor", PropertyType::Color, SerializeColor(m_outlineColor)},
            {"OutlineWidth", PropertyType::Float, std::to_string(m_outlineWidth)},
        };
    }

    void UITextComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Enabled")
                SetEnabled(property.value == "true" || property.value == "1");
            else if (property.name == "Text")
                m_text = property.value;
            else if (property.name == "Color")
                m_color = ParseColor(property.value, m_color);
            else if (property.name == "FontSize")
                m_fontSize = std::stof(property.value);
            else if (property.name == "FontPath")
                m_fontPath = property.value;
            else if (property.name == "RichText")
                m_richText = property.value == "true" || property.value == "1";
            else if (property.name == "Alignment")
                m_alignment = static_cast<UITextAlignment>(std::clamp(std::stoi(property.value), 0, 8));
            else if (property.name == "Wrap")
                m_wrap = property.value == "true" || property.value == "1";
            else if (property.name == "LineSpacing")
                SetLineSpacing(std::stof(property.value));
            else if (property.name == "OutlineColor")
                SetOutlineColor(ParseColor(property.value, m_outlineColor));
            else if (property.name == "OutlineWidth")
                SetOutlineWidth(std::stof(property.value));
        }
    }

    void UIButtonComponent::SetRuntimeState(bool hovered, bool pressed, bool released, bool clicked)
    {
        m_hovered = hovered;
        m_pressed = pressed;
        m_released = released;
        m_clicked = clicked;
    }

    std::vector<Property> UIButtonComponent::Serialize() const
    {
        return {
            {"Enabled", PropertyType::Bool, IsEnabled() ? "true" : "false"},
            {"Interactable", PropertyType::Bool, m_interactable ? "true" : "false"},
            {"NormalTint", PropertyType::Color, SerializeColor(m_normalTint)},
            {"HoveredTint", PropertyType::Color, SerializeColor(m_hoveredTint)},
            {"PressedTint", PropertyType::Color, SerializeColor(m_pressedTint)},
            {"DisabledTint", PropertyType::Color, SerializeColor(m_disabledTint)},
            {"TransitionDuration", PropertyType::Float, std::to_string(m_transitionDuration)},
        };
    }

    void UIButtonComponent::Deserialize(const std::vector<Property> &properties)
    {
        for (const auto &property : properties)
        {
            if (property.name == "Enabled")
                SetEnabled(property.value == "true" || property.value == "1");
            else if (property.name == "Interactable")
                m_interactable = property.value == "true" || property.value == "1";
            else if (property.name == "NormalTint")
                m_normalTint = ParseColor(property.value, m_normalTint);
            else if (property.name == "HoveredTint")
                m_hoveredTint = ParseColor(property.value, m_hoveredTint);
            else if (property.name == "PressedTint")
                m_pressedTint = ParseColor(property.value, m_pressedTint);
            else if (property.name == "DisabledTint")
                m_disabledTint = ParseColor(property.value, m_disabledTint);
            else if (property.name == "TransitionDuration")
                SetTransitionDuration(std::stof(property.value));
        }
    }
}
