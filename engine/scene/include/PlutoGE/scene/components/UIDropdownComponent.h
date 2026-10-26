#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/ui/UIPopup.h"
#include "PlutoGE/scene/ui/UISelectable.h"

#include <string>
#include <vector>

namespace PlutoGE::scene
{
    // Picks one of a list of options. Clicking (or Submit) opens a list
    // below the control; the chosen option is shown by its caption text
    // (a UITextComponent on the caption element, or on this entity).
    class UIDropdownComponent : public TypedComponent<UIDropdownComponent>,
                                public UISelectable,
                                public IUIPopup,
                                public IUIRectDriver
    {
    public:
        void Update(float) override {}
        bool RequiresFrameUpdate() const override { return false; }
        std::vector<Property> Serialize() const override;
        void Deserialize(const std::vector<Property> &properties) override;

        void OnUIEvent(UIEvent &event, UIEventContext &context) override;
        void DriveRects(UIEventContext &context) override;

        [[nodiscard]] bool IsPopupOpen() const override { return m_open; }
        [[nodiscard]] ui::Rect GetPopupRect(const ui::Rect &ownerRect) const override;
        void PaintPopup(UIPaintContext &context) const override;
        void OnPopupEvent(UIEvent &event, UIEventContext &context, const ui::Rect &ownerRect, glm::vec2 local) override;
        void ClosePopup() override { m_open = false; }

        [[nodiscard]] const std::vector<std::string> &GetOptions() const { return m_options; }
        void SetOptions(std::vector<std::string> options);
        [[nodiscard]] int GetValue() const { return m_value; }
        // Clamped to the options; -1 when there are none.
        void SetValue(int value);
        [[nodiscard]] UIEntityID GetCaption() const { return m_caption; }
        void SetCaption(UIEntityID value) { m_caption = value; }

    private:
        [[nodiscard]] int VisibleCount() const;
        [[nodiscard]] int ItemAt(const ui::Rect &popup, glm::vec2 local) const;
        void Choose(int index, UIEventContext &context);
        void ScrollTo(int index);

        std::vector<std::string> m_options;
        int m_value = 0;
        UIEntityID m_caption = 0;
        float m_itemHeight = 32.0f;
        int m_maxVisibleItems = 6;
        float m_fontSize = 18.0f;
        float m_cornerRadius = 6.0f;
        glm::vec4 m_popupColor{0.12f, 0.13f, 0.16f, 0.98f};
        glm::vec4 m_highlightColor{0.25f, 0.45f, 0.9f, 0.9f};
        glm::vec4 m_textColor{1.0f};

        bool m_open = false;
        int m_hovered = 0;
        int m_firstVisible = 0;
    };
}
