#include "PlutoGE/scene/ui/UIInput.h"

#include "PlutoGE/platform/InputState.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::scene
{
    namespace
    {
        // GLFW key codes not named by platform::KeyCode.
        constexpr int kKeyDelete = 261;
        constexpr int kKeyRight = 262;
        constexpr int kKeyLeft = 263;
        constexpr int kKeyDown = 264;
        constexpr int kKeyUp = 265;
        constexpr int kKeyHome = 268;
        constexpr int kKeyEnd = 269;
        constexpr int kKeyPadEnter = 335;
        constexpr int kKeyLeftShift = 340;
        constexpr int kKeyLeftControl = 341;
        constexpr int kKeyRightShift = 344;
        constexpr int kKeyRightControl = 345;

        // GLFW gamepad layout.
        constexpr std::uint16_t kPadA = 0;
        constexpr std::uint16_t kPadB = 1;
        constexpr std::uint16_t kPadUp = 11;
        constexpr std::uint16_t kPadRight = 12;
        constexpr std::uint16_t kPadDown = 13;
        constexpr std::uint16_t kPadLeft = 14;
        constexpr std::uint16_t kAxisLeftX = 0;
        constexpr std::uint16_t kAxisLeftY = 1;
        constexpr float kStickThreshold = 0.5f;
        constexpr float kRepeatDelay = 0.4f;
        constexpr float kRepeatInterval = 0.1f;

        bool KeyDown(const platform::InputState &state, int key)
        {
            return state.IsKeyDown(static_cast<platform::KeyCode>(key));
        }

        // Pressed this frame, or auto-repeated by the OS while held.
        bool KeyTriggered(const platform::InputState &state, int key)
        {
            return state.IsKeyPressed(static_cast<platform::KeyCode>(key)) ||
                   std::find(state.repeatedKeys.begin(), state.repeatedKeys.end(), key) != state.repeatedKeys.end();
        }

        glm::ivec2 GamepadDirection(const platform::InputState &state)
        {
            glm::ivec2 direction{0};
            for (std::uint16_t pad = 0; pad < platform::MaxGamepads; ++pad)
            {
                if (state.IsGamepadButtonDown(pad, kPadUp)) direction.y = 1;
                if (state.IsGamepadButtonDown(pad, kPadDown)) direction.y = -1;
                if (state.IsGamepadButtonDown(pad, kPadLeft)) direction.x = -1;
                if (state.IsGamepadButtonDown(pad, kPadRight)) direction.x = 1;
                const float x = state.GetGamepadAxis(pad, kAxisLeftX);
                const float y = -state.GetGamepadAxis(pad, kAxisLeftY); // GLFW: down is positive
                if (std::abs(x) > kStickThreshold && std::abs(x) >= std::abs(y)) direction.x = x > 0 ? 1 : -1;
                else if (std::abs(y) > kStickThreshold) direction.y = y > 0 ? 1 : -1;
            }
            // One axis at a time: navigation moves to a neighbour, not diagonally.
            if (direction.x != 0 && direction.y != 0)
                direction.y = 0;
            return direction;
        }

        bool AnyGamepadPressed(const platform::InputState &state, std::uint16_t button)
        {
            for (std::uint16_t pad = 0; pad < platform::MaxGamepads; ++pad)
                if (state.IsGamepadButtonPressed(pad, button))
                    return true;
            return false;
        }
    }

    void UIInputMapper::Map(const platform::InputState &state, UIInputContext &input, float deltaTime)
    {
        const bool shift = KeyDown(state, kKeyLeftShift) || KeyDown(state, kKeyRightShift);
        const bool control = KeyDown(state, kKeyLeftControl) || KeyDown(state, kKeyRightControl);

        // Navigation: arrows and Tab use OS key repeat; gamepads repeat here.
        auto &navigation = input.navigation;
        if (KeyTriggered(state, kKeyUp)) navigation.move = {0, 1};
        else if (KeyTriggered(state, kKeyDown)) navigation.move = {0, -1};
        else if (KeyTriggered(state, kKeyLeft)) navigation.move = {-1, 0};
        else if (KeyTriggered(state, kKeyRight)) navigation.move = {1, 0};
        else if (KeyTriggered(state, static_cast<int>(platform::KeyCode::Tab))) navigation.move = shift ? glm::ivec2{0, 1} : glm::ivec2{0, -1};

        const glm::ivec2 pad = GamepadDirection(state);
        if (pad != m_heldDirection)
        {
            m_heldDirection = pad;
            m_repeatTimer = kRepeatDelay;
            if (pad != glm::ivec2{0})
                navigation.move = pad;
        }
        else if (pad != glm::ivec2{0} && (m_repeatTimer -= deltaTime) <= 0.0f)
        {
            m_repeatTimer += kRepeatInterval;
            navigation.move = pad;
        }

        navigation.submit = state.IsKeyPressed(platform::KeyCode::Enter) || KeyTriggered(state, kKeyPadEnter) ||
                            AnyGamepadPressed(state, kPadA);
        navigation.cancel = state.IsKeyPressed(platform::KeyCode::Escape) || AnyGamepadPressed(state, kPadB);

        // Text editing.
        for (const std::uint32_t codepoint : state.textInput)
            input.text.push_back(static_cast<char32_t>(codepoint));
        const auto addKey = [&](UIKey key) { input.keys.push_back({key, shift, control}); };
        if (KeyTriggered(state, static_cast<int>(platform::KeyCode::Backspace))) addKey(UIKey::Backspace);
        if (KeyTriggered(state, kKeyDelete)) addKey(UIKey::Delete);
        if (KeyTriggered(state, kKeyLeft)) addKey(UIKey::Left);
        if (KeyTriggered(state, kKeyRight)) addKey(UIKey::Right);
        if (KeyTriggered(state, kKeyUp)) addKey(UIKey::Up);
        if (KeyTriggered(state, kKeyDown)) addKey(UIKey::Down);
        if (KeyTriggered(state, kKeyHome)) addKey(UIKey::Home);
        if (KeyTriggered(state, kKeyEnd)) addKey(UIKey::End);
        if (KeyTriggered(state, static_cast<int>(platform::KeyCode::Enter)) || KeyTriggered(state, kKeyPadEnter)) addKey(UIKey::Enter);
        if (control)
        {
            if (state.IsKeyPressed(platform::KeyCode::A)) addKey(UIKey::SelectAll);
            if (state.IsKeyPressed(platform::KeyCode::C)) addKey(UIKey::Copy);
            if (state.IsKeyPressed(platform::KeyCode::X)) addKey(UIKey::Cut);
            if (state.IsKeyPressed(platform::KeyCode::V)) addKey(UIKey::Paste);
        }
    }
}
