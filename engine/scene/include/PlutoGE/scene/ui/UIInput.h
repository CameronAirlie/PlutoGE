#pragma once

#include "PlutoGE/scene/ui/UIEvents.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

namespace PlutoGE::platform
{
    struct InputState;
}

namespace PlutoGE::scene
{
    struct UINavigationInput
    {
        glm::ivec2 move{0};  // one step this frame (y-up), including key repeat
        bool submit = false; // Enter / gamepad A
        bool cancel = false; // Escape / gamepad B
    };

    // One frame of UI input. Pointer values use target pixels, y-up.
    struct UIInputContext
    {
        glm::vec2 viewportSize{0.0f};
        glm::vec2 pointerPosition{-1.0f};
        glm::vec2 pointerDelta{0.0f};
        bool pointerInside = false;
        bool pointerDown = false;
        bool pointerPressed = false;
        bool pointerReleased = false;
        glm::vec2 scrollDelta{0.0f};
        UINavigationInput navigation;
        std::u32string text;            // characters typed this frame
        std::vector<UIKeyPress> keys;   // editing keys pressed or repeated this frame
    };

    // Translates keyboard and gamepad state into UI navigation, text and
    // editing keys. Holds the repeat timer for held directions.
    class UIInputMapper
    {
    public:
        void Map(const platform::InputState &state, UIInputContext &input, float deltaTime);

    private:
        glm::ivec2 m_heldDirection{0};
        float m_repeatTimer = 0.0f;
    };
}
