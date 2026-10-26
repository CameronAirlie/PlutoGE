#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/ui/UIGraphic.h"
#include "PlutoGE/scene/ui/UISelectable.h"

#include <string>
#include <vector>

namespace PlutoGE::ui
{
    struct TextLayout;
}

namespace PlutoGE::scene
{
    class UITextComponent;

    enum class UIInputContentType : std::uint8_t
    {
        Standard,
        Integer,
        Decimal,
        Alphanumeric,
        Password,
    };

    // Editable text. The UITextComponent on the same entity shows the text;
    // this component edits it while focused and draws the caret and
    // selection. An optional placeholder element shows while it is empty.
    class UIInputFieldComponent : public TypedComponent<UIInputFieldComponent>,
                                  public UISelectable,
                                  public IUITextInput,
                                  public IUIRectDriver,
                                  public IUIUpdatable,
                                  public IUIGraphic,
                                  public IUIRuntimeProperties
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void OnUIEvent(UIEvent &event, UIEventContext &context) override;
        bool HandleMove(glm::ivec2 direction, UIEventContext &context) override;
        [[nodiscard]] bool IsEditing() const override { return m_editing; }
        void OnTextInput(std::u32string_view text, std::span<const UIKeyPress> keys, UIEventContext &context) override;
        void DriveRects(UIEventContext &context) override;
        void UpdateUI(UIEventContext &context, float deltaTime) override;
        [[nodiscard]] UIPaintLayer GetPaintLayer() const override { return UIPaintLayer::Overlay; }
        void Paint(UIPaintContext &context) const override;
        // "IsEditing" (read-only).
        bool GetRuntimeProperty(std::string_view name, std::string &value) const override;
        bool SetRuntimeProperty(std::string_view, std::string_view) override { return false; }

        [[nodiscard]] const std::string &GetText() const { return m_text; }
        void SetText(std::string value);
        [[nodiscard]] UIEntityID GetPlaceholder() const { return m_placeholder; }
        void SetPlaceholder(UIEntityID value) { m_placeholder = value; }
        [[nodiscard]] int GetCharacterLimit() const { return m_characterLimit; }
        void SetCharacterLimit(int value) { m_characterLimit = std::max(value, 0); }
        [[nodiscard]] UIInputContentType GetContentType() const { return m_contentType; }
        void SetContentType(UIInputContentType value) { m_contentType = value; }
        [[nodiscard]] bool IsMultiline() const { return m_multiline; }
        void SetMultiline(bool value) { m_multiline = value; }
        [[nodiscard]] bool IsReadOnly() const { return m_readOnly; }
        void SetReadOnly(bool value) { m_readOnly = value; }

        // Caret and selection as codepoint indices (valid while editing).
        [[nodiscard]] std::size_t GetCaret() const { return m_caret; }
        [[nodiscard]] std::size_t GetSelectionAnchor() const { return m_anchor; }

    private:
        void BeginEdit();
        void EndEdit(UIEventContext &context);
        void Insert(std::u32string_view text);
        bool DeleteSelection();
        void Commit(UIEventContext &context);
        [[nodiscard]] bool Accepts(char32_t codepoint, std::size_t position) const;
        [[nodiscard]] std::size_t WordBoundary(std::size_t from, int direction) const;
        [[nodiscard]] UITextComponent *TextComponent() const;
        [[nodiscard]] std::size_t IndexAt(glm::vec2 local, const ui::Rect &rect) const;

        std::string m_text;
        UIEntityID m_placeholder = 0;
        int m_characterLimit = 0;
        UIInputContentType m_contentType = UIInputContentType::Standard;
        bool m_multiline = false;
        bool m_readOnly = false;
        glm::vec4 m_caretColor{1.0f};
        glm::vec4 m_selectionColor{0.3f, 0.55f, 1.0f, 0.45f};
        float m_caretBlinkRate = 0.85f; // seconds per on/off cycle; zero keeps the caret on
        float m_caretWidth = 2.0f;

        bool m_editing = false;
        std::u32string m_codepoints;
        std::string m_original; // restored by Cancel
        std::size_t m_caret = 0;
        std::size_t m_anchor = 0;
        float m_blink = 0.0f;
        float m_scroll = 0.0f;
    };
}
