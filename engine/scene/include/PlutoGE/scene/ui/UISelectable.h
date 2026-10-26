#pragma once

#include "PlutoGE/scene/components/Component.h"
#include "PlutoGE/scene/ui/UIEvents.h"

#include <glm/glm.hpp>

#include <array>
#include <vector>

namespace PlutoGE::scene
{
    enum class UISelectableState : std::uint8_t
    {
        Normal,
        Highlighted, // pointer over
        Pressed,
        Selected,    // keyboard/gamepad focus
        Disabled,
    };

    enum class UINavigationMode : std::uint8_t
    {
        Automatic, // nearest selectable in the pressed direction
        Explicit,  // the configured neighbours only
        None,
    };

    // Shared behaviour of interactive controls: interactable flag, pointer
    // and focus state, colour-tint transitions and navigation neighbours.
    // Controls derive from it next to TypedComponent and implement
    // OnUIEvent for their own behaviour.
    class UISelectable : public IUIEventHandler
    {
    public:
        // Neighbour order for explicit navigation.
        enum Direction : std::size_t { Up = 0, Down, Left, Right };

        [[nodiscard]] bool IsInteractable() const { return m_interactable; }
        void SetInteractable(bool value) { m_interactable = value; }
        [[nodiscard]] UISelectableState GetState() const;
        [[nodiscard]] bool IsHovered() const { return m_hovered; }
        [[nodiscard]] bool IsPressedDown() const { return m_pressed; }
        [[nodiscard]] bool IsSelected() const { return m_selected; }

        // Called by the UI system as the pointer and focus move.
        void SetHovered(bool value) { m_hovered = value; }
        void SetPressed(bool value) { m_pressed = value; }
        void SetSelected(bool value) { m_selected = value; }

        [[nodiscard]] UINavigationMode GetNavigationMode() const { return m_navigation; }
        void SetNavigationMode(UINavigationMode value) { m_navigation = value; }
        [[nodiscard]] UIEntityID GetNeighbour(Direction direction) const { return m_neighbours[direction]; }
        void SetNeighbour(Direction direction, UIEntityID entity) { m_neighbours[direction] = entity; }

        // Tint for the current state, eased over the transition duration.
        [[nodiscard]] glm::vec4 GetCurrentTint() const { return m_currentTint; }
        [[nodiscard]] glm::vec4 GetStateTint(UISelectableState state) const { return m_tints[static_cast<std::size_t>(state)]; }
        void SetStateTint(UISelectableState state, glm::vec4 value) { m_tints[static_cast<std::size_t>(state)] = value; }
        [[nodiscard]] float GetTransitionDuration() const { return m_transitionDuration; }
        void SetTransitionDuration(float value) { m_transitionDuration = std::max(value, 0.0f); }
        void UpdateTransition(float deltaTime);
        // Jumps to the state's tint (edit mode, instantiation).
        void SnapTransition();

        // Movement on the focused control; return true to keep focus here
        // (a slider uses left/right to change its value).
        virtual bool HandleMove(glm::ivec2 direction, UIEventContext &context) { return false; }

    protected:
        // Shared property encoding so every control serializes these the same way.
        void AppendSelectableProperties(std::vector<Property> &properties) const;
        // Returns true when the property belonged to the selectable.
        bool ReadSelectableProperty(const Property &property);

        bool m_interactable = true;

    private:
        bool m_hovered = false;
        bool m_pressed = false;
        bool m_selected = false;
        UINavigationMode m_navigation = UINavigationMode::Automatic;
        std::array<UIEntityID, 4> m_neighbours{};
        std::array<glm::vec4, 5> m_tints{glm::vec4(1.0f),
                                          glm::vec4(1.12f, 1.12f, 1.12f, 1.0f),
                                          glm::vec4(0.78f, 0.78f, 0.78f, 1.0f),
                                          glm::vec4(1.08f, 1.08f, 1.08f, 1.0f),
                                          glm::vec4(0.45f, 0.45f, 0.45f, 0.75f)};
        glm::vec4 m_currentTint{1.0f};
        float m_transitionDuration = 0.08f;
    };
}
