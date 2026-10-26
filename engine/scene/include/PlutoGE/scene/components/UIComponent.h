#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/ui/UIGraphic.h"
#include "PlutoGE/scene/ui/UISelectable.h"
#include "PlutoGE/ui/text/TextLayout.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace PlutoGE::scene
{
    enum class CanvasRenderMode
    {
        ScreenSpaceOverlay = 0,
        WorldSpaceOverlay,
        ScreenSpaceCamera,
        WorldSpace,
    };

    enum class CanvasScaleMode
    {
        ConstantPixels = 0,
        ScaleWithScreenSize,
        ConstantPhysicalSize,
    };

    enum class UIRenderBackend
    {
        Native = 0,
        RmlUi,
    };

    enum class RmlUiContentSource
    {
        Document = 0,
        Text,
    };

    enum class UIScreenMatchMode
    {
        MatchWidthOrHeight = 0,
        Expand,
        Shrink,
    };

    enum class UIImageType
    {
        Simple = 0,
        Sliced,
        FilledHorizontal,
        FilledVertical,
        FilledRadial,
        ProceduralCrosshair,
        ProceduralCircle,
        ProceduralArc,
        ProceduralRoundedRect,
        Tiled, // appended: serialized values are indices
    };

    enum class UITextAlignment
    {
        TopLeft = 0,
        TopCenter,
        TopRight,
        MiddleLeft,
        MiddleCenter,
        MiddleRight,
        BottomLeft,
        BottomCenter,
        BottomRight,
    };

    enum class UITextOverflow
    {
        Overflow = 0,
        Clip,
        Ellipsis,
    };

    enum class UIWorldSizeMode
    {
        WorldUnits = 0,
        ConstantScreenSize,
        DistanceScaled,
    };

    enum class UILayoutMode
    {
        None = 0,
        Horizontal,
        Vertical,
        Grid,
    };

    enum class UIContentSizeMode
    {
        Unconstrained = 0,
        Minimum,
        Preferred,
    };

    enum class UIAnchorPreset
    {
        TopLeft = 0,
        TopCenter,
        TopRight,
        MiddleLeft,
        MiddleCenter,
        MiddleRight,
        BottomLeft,
        BottomCenter,
        BottomRight,
        Stretch,
    };

    struct RectTransformLayout
    {
        glm::vec2 min{0.0f};
        glm::vec2 max{0.0f};
    };

    class CanvasComponent : public TypedComponent<CanvasComponent>
    {
    public:
        void Update(float deltaTime) override {}

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        CanvasRenderMode GetRenderMode() const { return m_renderMode; }
        void SetRenderMode(CanvasRenderMode renderMode) { m_renderMode = renderMode; }
        float GetScaleFactor() const { return m_scaleFactor; }
        void SetScaleFactor(float scaleFactor) { m_scaleFactor = scaleFactor; }
        int GetSortingOrder() const { return m_sortingOrder; }
        void SetSortingOrder(int sortingOrder) { m_sortingOrder = sortingOrder; }
        CanvasScaleMode GetScaleMode() const { return m_scaleMode; }
        void SetScaleMode(CanvasScaleMode mode) { m_scaleMode = mode; }
        glm::vec2 GetReferenceResolution() const { return m_referenceResolution; }
        void SetReferenceResolution(glm::vec2 value) { m_referenceResolution = glm::max(value, glm::vec2(1.0f)); }
        float GetMatchWidthOrHeight() const { return m_matchWidthOrHeight; }
        void SetMatchWidthOrHeight(float value) { m_matchWidthOrHeight = glm::clamp(value, 0.0f, 1.0f); }
        UIScreenMatchMode GetScreenMatchMode() const { return m_screenMatchMode; }
        void SetScreenMatchMode(UIScreenMatchMode mode) { m_screenMatchMode = mode; }
        UIWorldSizeMode GetWorldSizeMode() const { return m_worldSizeMode; }
        void SetWorldSizeMode(UIWorldSizeMode mode) { m_worldSizeMode = mode; }
        bool GetFaceCamera() const { return m_faceCamera; }
        void SetFaceCamera(bool value) { m_faceCamera = value; }
        UIRenderBackend GetBackend() const { return m_backend; }
        void SetBackend(UIRenderBackend value) { m_backend = value; }
        const std::string &GetDocumentPath() const { return m_documentPath; }
        void SetDocumentPath(std::string value) { m_documentPath = std::move(value); }
        RmlUiContentSource GetContentSource() const { return m_contentSource; }
        void SetContentSource(RmlUiContentSource value) { m_contentSource = value; }

    private:
        CanvasRenderMode m_renderMode = CanvasRenderMode::ScreenSpaceOverlay;
        CanvasScaleMode m_scaleMode = CanvasScaleMode::ScaleWithScreenSize;
        UIScreenMatchMode m_screenMatchMode = UIScreenMatchMode::MatchWidthOrHeight;
        UIWorldSizeMode m_worldSizeMode = UIWorldSizeMode::WorldUnits;
        float m_scaleFactor = 1.0f;
        glm::vec2 m_referenceResolution{1920.0f, 1080.0f};
        float m_matchWidthOrHeight = 0.5f;
        int m_sortingOrder = 0;
        bool m_faceCamera = true;
        // Keep the construction fallback native so old scenes which predate
        // the Backend property deserialize unchanged. Editor-created canvases
        // explicitly select RmlUi.
        UIRenderBackend m_backend = UIRenderBackend::Native;
        RmlUiContentSource m_contentSource = RmlUiContentSource::Document;
        std::string m_documentPath;
    };

    class RmlWidgetComponent : public TypedComponent<RmlWidgetComponent>
    {
    public:
        void Update(float deltaTime) override {}
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        const std::string &GetSource() const { return m_source; }
        void SetSource(std::string value) { m_source = std::move(value); }
        bool IsVisible() const { return m_visible; }
        void SetVisible(bool value) { m_visible = value; }

    private:
        std::string m_source;
        bool m_visible = true;
    };

    class RectTransformComponent : public TypedComponent<RectTransformComponent>
    {
    public:
        void Update(float deltaTime) override {}

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        glm::vec2 GetAnchoredPosition() const { return m_anchoredPosition; }
        void SetAnchoredPosition(const glm::vec2 &anchoredPosition) { m_anchoredPosition = anchoredPosition; }
        glm::vec2 GetSizeDelta() const { return m_sizeDelta; }
        void SetSizeDelta(const glm::vec2 &sizeDelta) { m_sizeDelta = sizeDelta; }
        glm::vec2 GetPivot() const { return m_pivot; }
        void SetPivot(const glm::vec2 &pivot) { m_pivot = glm::clamp(pivot, glm::vec2(0.0f), glm::vec2(1.0f)); }
        glm::vec2 GetAnchorMin() const { return m_anchorMin; }
        void SetAnchorMin(const glm::vec2 &anchorMin) { m_anchorMin = anchorMin; }
        glm::vec2 GetAnchorMax() const { return m_anchorMax; }
        void SetAnchorMax(const glm::vec2 &anchorMax) { m_anchorMax = anchorMax; }
        UIAnchorPreset GetAnchorPreset() const { return m_anchorPreset; }
        void SetAnchorPreset(UIAnchorPreset preset);
        glm::vec4 GetMargin() const { return m_margin; }
        void SetMargin(glm::vec4 value) { m_margin = value; }
        glm::vec2 GetMinimumSize() const { return m_minimumSize; }
        void SetMinimumSize(glm::vec2 value) { m_minimumSize = glm::max(value, glm::vec2(0.0f)); }
        glm::vec2 GetMaximumSize() const { return m_maximumSize; }
        void SetMaximumSize(glm::vec2 value) { m_maximumSize = glm::max(value, m_minimumSize); }
        float GetRotation() const { return m_rotation; }
        void SetRotation(float value) { m_rotation = value; }
        glm::vec2 GetLocalScale() const { return m_localScale; }
        void SetLocalScale(glm::vec2 value) { m_localScale = value; }
        float GetOpacity() const { return m_opacity; }
        void SetOpacity(float value) { m_opacity = glm::clamp(value, 0.0f, 1.0f); }
        bool GetClipChildren() const { return m_clipChildren; }
        void SetClipChildren(bool value) { m_clipChildren = value; }
        bool GetRaycastTarget() const { return m_raycastTarget; }
        void SetRaycastTarget(bool value) { m_raycastTarget = value; }
        UILayoutMode GetLayoutMode() const { return m_layoutMode; }
        void SetLayoutMode(UILayoutMode value) { m_layoutMode = value; }
        glm::vec4 GetLayoutPadding() const { return m_layoutPadding; }
        void SetLayoutPadding(glm::vec4 value) { m_layoutPadding = glm::max(value, glm::vec4(0.0f)); }
        glm::vec2 GetLayoutSpacing() const { return m_layoutSpacing; }
        void SetLayoutSpacing(glm::vec2 value) { m_layoutSpacing = glm::max(value, glm::vec2(0.0f)); }
        int GetGridColumns() const { return m_gridColumns; }
        void SetGridColumns(int value) { m_gridColumns = std::max(value, 1); }
        bool GetControlChildWidth() const { return m_controlChildWidth; }
        void SetControlChildWidth(bool value) { m_controlChildWidth = value; }
        bool GetControlChildHeight() const { return m_controlChildHeight; }
        void SetControlChildHeight(bool value) { m_controlChildHeight = value; }
        bool GetExpandChildWidth() const { return m_expandChildWidth; }
        void SetExpandChildWidth(bool value) { m_expandChildWidth = value; }
        bool GetExpandChildHeight() const { return m_expandChildHeight; }
        void SetExpandChildHeight(bool value) { m_expandChildHeight = value; }
        UIContentSizeMode GetHorizontalContentSize() const { return m_horizontalContentSize; }
        void SetHorizontalContentSize(UIContentSizeMode value) { m_horizontalContentSize = value; }
        UIContentSizeMode GetVerticalContentSize() const { return m_verticalContentSize; }
        void SetVerticalContentSize(UIContentSizeMode value) { m_verticalContentSize = value; }

    private:
        glm::vec2 m_anchoredPosition{0.0f};
        glm::vec2 m_sizeDelta{100.0f, 40.0f};
        glm::vec2 m_pivot{0.5f, 0.5f};
        glm::vec2 m_anchorMin{0.5f, 0.5f};
        glm::vec2 m_anchorMax{0.5f, 0.5f};
        UIAnchorPreset m_anchorPreset = UIAnchorPreset::MiddleCenter;
        glm::vec4 m_margin{0.0f};
        glm::vec2 m_minimumSize{0.0f};
        glm::vec2 m_maximumSize{100000.0f};
        float m_rotation = 0.0f;
        glm::vec2 m_localScale{1.0f};
        float m_opacity = 1.0f;
        bool m_clipChildren = false;
        bool m_raycastTarget = true;
        UILayoutMode m_layoutMode = UILayoutMode::None;
        glm::vec4 m_layoutPadding{0.0f};
        glm::vec2 m_layoutSpacing{0.0f};
        int m_gridColumns = 1;
        bool m_controlChildWidth = true;
        bool m_controlChildHeight = true;
        bool m_expandChildWidth = false;
        bool m_expandChildHeight = false;
        UIContentSizeMode m_horizontalContentSize = UIContentSizeMode::Unconstrained;
        UIContentSizeMode m_verticalContentSize = UIContentSizeMode::Unconstrained;
    };

    class UIImageComponent : public TypedComponent<UIImageComponent>, public IUIGraphic, public IUIContentSize
    {
    public:
        void Update(float deltaTime) override {}
        bool RequiresFrameUpdate() const override { return false; }

        [[nodiscard]] UIPaintLayer GetPaintLayer() const override { return UIPaintLayer::Image; }
        void Paint(UIPaintContext &context) const override;
        // The texture's pixel size (or zero without a texture).
        [[nodiscard]] glm::vec2 MeasureContent(glm::vec2 available, UIMeasureContext &context) const override;

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        glm::vec4 GetColor() const { return m_color; }
        void SetColor(const glm::vec4 &color) { m_color = glm::clamp(color, glm::vec4(0.0f), glm::vec4(1.0f)); }
        const std::string &GetTexturePath() const { return m_texturePath; }
        void SetTexturePath(std::string texturePath) { m_texturePath = std::move(texturePath); }
        bool GetPreserveAspect() const { return m_preserveAspect; }
        void SetPreserveAspect(bool preserveAspect) { m_preserveAspect = preserveAspect; }
        float GetFillAmount() const { return m_fillAmount; }
        void SetFillAmount(float fillAmount) { m_fillAmount = glm::clamp(fillAmount, 0.0f, 1.0f); }
        UIImageType GetImageType() const { return m_imageType; }
        void SetImageType(UIImageType value) { m_imageType = value; }
        glm::vec4 GetBorder() const { return m_border; }
        void SetBorder(glm::vec4 value) { m_border = glm::max(value, glm::vec4(0.0f)); }
        float GetThickness() const { return m_thickness; }
        void SetThickness(float value) { m_thickness = std::max(value, 0.0f); }
        float GetCornerRadius() const { return m_cornerRadius; }
        void SetCornerRadius(float value) { m_cornerRadius = std::max(value, 0.0f); }
        float GetStartAngle() const { return m_startAngle; }
        void SetStartAngle(float value) { m_startAngle = value; }
        bool GetFillClockwise() const { return m_fillClockwise; }
        void SetFillClockwise(bool value) { m_fillClockwise = value; }
        float GetSliceScale() const { return m_sliceScale; }
        void SetSliceScale(float value) { m_sliceScale = std::max(value, 0.01f); }

    private:
        glm::vec4 m_color{1.0f};
        std::string m_texturePath;
        bool m_preserveAspect = false;
        float m_fillAmount = 1.0f;
        UIImageType m_imageType = UIImageType::Simple;
        glm::vec4 m_border{0.0f};
        float m_thickness = 2.0f;
        float m_cornerRadius = 0.0f;
        float m_startAngle = 0.0f;
        bool m_fillClockwise = false;
        float m_sliceScale = 1.0f;
    };

    class UITextComponent : public TypedComponent<UITextComponent>, public IUIGraphic, public IUIContentSize
    {
    public:
        void Update(float deltaTime) override {}
        bool RequiresFrameUpdate() const override { return false; }

        [[nodiscard]] UIPaintLayer GetPaintLayer() const override { return UIPaintLayer::Text; }
        void Paint(UIPaintContext &context) const override;
        [[nodiscard]] glm::vec2 MeasureContent(glm::vec2 available, UIMeasureContext &context) const override;

        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        const std::string &GetText() const { return m_text; }
        void SetText(std::string text) { m_text = std::move(text); }
        glm::vec4 GetColor() const { return m_color; }
        void SetColor(const glm::vec4 &color) { m_color = color; }
        float GetFontSize() const { return m_fontSize; }
        void SetFontSize(float fontSize) { m_fontSize = fontSize; }
        const std::string &GetFontPath() const { return m_fontPath; }
        void SetFontPath(std::string fontPath) { m_fontPath = std::move(fontPath); }
        bool IsRichText() const { return m_richText; }
        void SetRichText(bool richText) { m_richText = richText; }
        UITextAlignment GetAlignment() const { return m_alignment; }
        void SetAlignment(UITextAlignment value) { m_alignment = value; }
        bool GetWrap() const { return m_wrap; }
        void SetWrap(bool value) { m_wrap = value; }
        float GetLineSpacing() const { return m_lineSpacing; }
        void SetLineSpacing(float value) { m_lineSpacing = std::max(value, 0.1f); }
        glm::vec4 GetOutlineColor() const { return m_outlineColor; }
        void SetOutlineColor(glm::vec4 value) { m_outlineColor = glm::clamp(value, glm::vec4(0.0f), glm::vec4(1.0f)); }
        float GetOutlineWidth() const { return m_outlineWidth; }
        void SetOutlineWidth(float value) { m_outlineWidth = glm::clamp(value, 0.0f, 8.0f); }
        UITextOverflow GetOverflow() const { return m_overflow; }
        void SetOverflow(UITextOverflow value) { m_overflow = value; }
        float GetLetterSpacing() const { return m_letterSpacing; }
        void SetLetterSpacing(float value) { m_letterSpacing = value; }
        bool IsBold() const { return m_bold; }
        void SetBold(bool value) { m_bold = value; }
        bool IsItalic() const { return m_italic; }
        void SetItalic(bool value) { m_italic = value; }
        glm::vec4 GetShadowColor() const { return m_shadowColor; }
        void SetShadowColor(glm::vec4 value) { m_shadowColor = value; }
        glm::vec2 GetShadowOffset() const { return m_shadowOffset; }
        void SetShadowOffset(glm::vec2 value) { m_shadowOffset = value; }
        float GetShadowSoftness() const { return m_shadowSoftness; }
        void SetShadowSoftness(float value) { m_shadowSoftness = std::max(value, 0.0f); }

        // Inset of the text inside its rect: left, bottom, right, top.
        glm::vec4 GetPadding() const { return m_padding; }
        void SetPadding(glm::vec4 value) { m_padding = glm::max(value, glm::vec4(0.0f)); }
        // The area the text lays out in for an element rect.
        [[nodiscard]] ui::Rect GetTextRect(const ui::Rect &rect) const;

        // Runtime-only horizontal scroll (input fields keep the caret in view).
        float GetScrollOffset() const { return m_scrollOffset; }
        void SetScrollOffset(float value) { m_scrollOffset = value; }

        // Lays out the text for a box of the given size (kUnbounded allowed).
        // Results are cached until the text, style or box changes.
        const ui::TextLayout &GetLayout(glm::vec2 box, ui::FontLibrary &fonts) const;
        const ui::TextContent &GetContent(ui::FontLibrary &fonts) const;

    private:
        struct LayoutCache
        {
            ui::TextLayoutSettings settings;
            ui::TextLayout layout;
            std::uint64_t contentRevision = 0;
            bool valid = false;
        };
        [[nodiscard]] ui::TextStyle BaseStyle(ui::FontLibrary &fonts) const;
        [[nodiscard]] ui::TextLayoutSettings Settings(glm::vec2 box) const;

        std::string m_text;
        glm::vec4 m_color{1.0f};
        float m_fontSize = 18.0f;
        std::string m_fontPath;
        bool m_richText = true;
        UITextAlignment m_alignment = UITextAlignment::MiddleCenter;
        bool m_wrap = true;
        float m_lineSpacing = 1.0f;
        glm::vec4 m_outlineColor{0.0f, 0.0f, 0.0f, 0.8f};
        float m_outlineWidth = 0.0f;
        UITextOverflow m_overflow = UITextOverflow::Overflow;
        float m_letterSpacing = 0.0f;
        bool m_bold = false;
        bool m_italic = false;
        glm::vec4 m_shadowColor{0.0f, 0.0f, 0.0f, 0.0f};
        glm::vec2 m_shadowOffset{1.0f, -1.0f};
        float m_shadowSoftness = 0.0f;
        float m_scrollOffset = 0.0f;
        glm::vec4 m_padding{0.0f};

        // Derived state, rebuilt on demand. Measuring and painting usually ask
        // for different boxes, so each keeps its own slot.
        mutable ui::TextContent m_content;
        mutable std::string m_contentText;     // inputs m_content was built from
        mutable ui::TextStyle m_contentStyle;
        mutable bool m_contentRich = false;
        mutable std::uint64_t m_contentRevision = 0;
        mutable LayoutCache m_layoutCache[2];
        mutable std::size_t m_nextCacheSlot = 0;
    };

    // A clickable control. Pointer clicks and Submit (Enter / gamepad A) on
    // the focused button both click it; scripts read the per-frame results.
    class UIButtonComponent : public TypedComponent<UIButtonComponent>, public IUIGraphic, public UISelectable
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        // Draws the default button face when the entity has no other graphic.
        [[nodiscard]] UIPaintLayer GetPaintLayer() const override { return UIPaintLayer::Background; }
        void Paint(UIPaintContext &context) const override;
        void OnUIEvent(UIEvent &event, UIEventContext &context) override;

        // Results of the latest UI update.
        bool WasPressed() const { return IsPressedDown(); }
        bool WasReleased() const { return m_released; }
        bool WasClicked() const { return m_clicked; }
        void ResetFrameState() { m_released = m_clicked = false; }
        // Overrides the pointer state directly (tests and tools).
        void SetRuntimeState(bool hovered, bool pressed, bool released, bool clicked);

        glm::vec4 GetNormalTint() const { return GetStateTint(UISelectableState::Normal); }
        void SetNormalTint(glm::vec4 value) { SetStateTint(UISelectableState::Normal, value); }
        glm::vec4 GetHoveredTint() const { return GetStateTint(UISelectableState::Highlighted); }
        void SetHoveredTint(glm::vec4 value) { SetStateTint(UISelectableState::Highlighted, value); }
        glm::vec4 GetPressedTint() const { return GetStateTint(UISelectableState::Pressed); }
        void SetPressedTint(glm::vec4 value) { SetStateTint(UISelectableState::Pressed, value); }
        glm::vec4 GetDisabledTint() const { return GetStateTint(UISelectableState::Disabled); }
        void SetDisabledTint(glm::vec4 value) { SetStateTint(UISelectableState::Disabled, value); }

    private:
        bool m_released = false;
        bool m_clicked = false;
    };

    RectTransformLayout ResolveRectTransformLayout(const RectTransformComponent &rectTransform,
                                                   const RectTransformLayout &parentRect);
    RectTransformLayout ResolveAutomaticChildLayout(
        const RectTransformComponent &layoutGroup,
        const RectTransformLayout &groupRect,
        const std::vector<const RectTransformComponent *> &children,
        std::size_t childIndex);
    glm::vec2 ResolvePreferredLayoutSize(const RectTransformComponent &layoutGroup,
                                         const std::vector<const RectTransformComponent *> &children);
    float ResolveCanvasScaleFactor(const CanvasComponent &canvas, const glm::vec2 &viewportSize);
}
