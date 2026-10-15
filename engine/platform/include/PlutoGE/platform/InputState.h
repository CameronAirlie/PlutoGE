#pragma once

#include <cstdint>
#include <array>
#include <vector>
#include <cmath>

namespace PlutoGE::platform
{
    enum class KeyCode : std::uint16_t
    {
        Escape = 256,
        Enter = 257,
        Tab = 258,
        Backspace = 259,

        Unknown = 0,
        Space = 32,
        Apostrophe = 39, /* ' */
        Comma = 44,      /* , */
        Minus = 45,      /* - */
        Period = 46,     /* . */
        Slash = 47,      /* / */

        D0 = 48, /* 0 */
        D1 = 49, /* 1 */
        D2 = 50, /* 2 */
        D3 = 51, /* 3 */
        D4 = 52, /* 4 */
        D5 = 53, /* 5 */
        D6 = 54, /* 6 */
        D7 = 55, /* 7 */
        D8 = 56, /* 8 */
        D9 = 57, /* 9 */

        Semicolon = 59, /* ; */
        Equal = 61,     /* = */

        A = 65,
        B = 66,
        C = 67,
        D = 68,
        E = 69,
        F = 70,
        G = 71,
        H = 72,
        I = 73,
        J = 74,
        K = 75,
        L = 76,
        M = 77,
        N = 78,
        O = 79,
        P = 80,
        Q = 81,
        R = 82,
        S = 83,
        T = 84,
        U = 85,
        V = 86,
        W = 87,
        X = 88,
        Y = 89,
        Z = 90,
    };

    constexpr std::size_t MaxGamepads = 16;
    constexpr std::size_t MaxGamepadButtons = 15;
    constexpr std::size_t MaxGamepadAxes = 6;

    struct GamepadState
    {
        bool connected = false;
        std::array<bool, MaxGamepadButtons> buttons{};
        std::array<bool, MaxGamepadButtons> previousButtons{};
        std::array<float, MaxGamepadAxes> axes{};
        std::array<float, MaxGamepadAxes> previousAxes{};
    };

    struct MouseState
    {
        bool buttons[8] = {false};
        bool previousButtons[8] = {false};
        double x = 0.0;
        double y = 0.0;
        double deltaX = 0.0;
        double deltaY = 0.0;
        double scrollDeltaX = 0.0;
        double scrollDeltaY = 0.0;
    };

    struct InputState
    {
        // Last meaningful input, independent of controller connection state.
        enum class Device : std::uint8_t { KeyboardMouse, Controller };
        Device activeDevice = Device::KeyboardMouse;
        int activeGamepad = -1;

        void UpdateActiveDevice()
        {
            bool keyboardMouse = !textInput.empty() || !repeatedKeys.empty() ||
                std::abs(mouseState.deltaX) >= 1.0 || std::abs(mouseState.deltaY) >= 1.0 ||
                mouseState.scrollDeltaX != 0 || mouseState.scrollDeltaY != 0;
            for (std::size_t i = 0; i < keys.size(); ++i)
                keyboardMouse |= keys[i] && !previousKeys[i];
            for (std::size_t i = 0; i < 8; ++i)
                keyboardMouse |= mouseState.buttons[i] && !mouseState.previousButtons[i];
            // Deliberate keyboard/mouse activity wins simultaneous samples.
            if (keyboardMouse) { activeDevice = Device::KeyboardMouse; activeGamepad = -1; return; }
            for (std::size_t i = 0; i < gamepads.size(); ++i)
            {
                const auto &pad = gamepads[i];
                if (!pad.connected) continue;
                bool activity = false;
                for (std::size_t b = 0; b < pad.buttons.size(); ++b)
                    activity |= pad.buttons[b] && !pad.previousButtons[b];
                for (std::size_t a = 0; a < pad.axes.size(); ++a)
                    activity |= std::isfinite(pad.axes[a]) && std::abs(pad.axes[a]) > 0.25f &&
                        std::abs(pad.axes[a] - pad.previousAxes[a]) > 0.01f;
                if (activity) { activeDevice = Device::Controller; activeGamepad = static_cast<int>(i); return; }
            }
            if (activeGamepad >= 0 && !gamepads[activeGamepad].connected)
            { activeDevice = Device::KeyboardMouse; activeGamepad = -1; }
        }

        bool escapePressed = false;
        bool quitRequested = false;
        MouseState mouseState;
        std::array<bool, 512> keys = {false};
        std::array<bool, 512> previousKeys = {false};
        std::vector<int> repeatedKeys;
        std::vector<std::uint32_t> textInput;
        std::array<GamepadState, MaxGamepads> gamepads{};

        [[nodiscard]] bool IsGamepadButtonDown(std::uint16_t gamepad, std::uint16_t button) const
        { return gamepad < gamepads.size() && button < MaxGamepadButtons && gamepads[gamepad].connected && gamepads[gamepad].buttons[button]; }
        [[nodiscard]] bool IsGamepadButtonPressed(std::uint16_t gamepad, std::uint16_t button) const
        { return IsGamepadButtonDown(gamepad, button) && !gamepads[gamepad].previousButtons[button]; }
        [[nodiscard]] bool IsGamepadButtonReleased(std::uint16_t gamepad, std::uint16_t button) const
        { return gamepad < gamepads.size() && button < MaxGamepadButtons && gamepads[gamepad].connected && !gamepads[gamepad].buttons[button] && gamepads[gamepad].previousButtons[button]; }
        [[nodiscard]] float GetGamepadAxis(std::uint16_t gamepad, std::uint16_t axis) const
        { return gamepad < gamepads.size() && axis < MaxGamepadAxes && gamepads[gamepad].connected ? gamepads[gamepad].axes[axis] : 0.0f; }
        [[nodiscard]] float GetPreviousGamepadAxis(std::uint16_t gamepad, std::uint16_t axis) const
        { return gamepad < gamepads.size() && axis < MaxGamepadAxes && gamepads[gamepad].connected ? gamepads[gamepad].previousAxes[axis] : 0.0f; }

        [[nodiscard]] bool IsKeyDown(KeyCode key) const
        {
            const auto keyIndex = static_cast<std::uint16_t>(key);
            if (keyIndex < 512)
            {
                return keys[keyIndex];
            }
            return false;
        }

        [[nodiscard]] bool IsKeyPressed(KeyCode key) const
        {
            const auto keyIndex = static_cast<std::uint16_t>(key);
            return keyIndex < 512 && keys[keyIndex] && !previousKeys[keyIndex];
        }

        [[nodiscard]] bool IsKeyReleased(KeyCode key) const
        {
            const auto keyIndex = static_cast<std::uint16_t>(key);
            return keyIndex < 512 && !keys[keyIndex] && previousKeys[keyIndex];
        }

        [[nodiscard]] bool IsMouseButtonDown(std::uint16_t button) const
        {
            return button < 8 && mouseState.buttons[button];
        }

        [[nodiscard]] bool IsMouseButtonPressed(std::uint16_t button) const
        {
            return button < 8 && mouseState.buttons[button] && !mouseState.previousButtons[button];
        }

        [[nodiscard]] bool IsMouseButtonReleased(std::uint16_t button) const
        {
            return button < 8 && !mouseState.buttons[button] && mouseState.previousButtons[button];
        }

        void BeginFrame()
        {
            previousKeys = keys;
            for (auto &gamepad : gamepads)
            {
                gamepad.previousButtons = gamepad.buttons;
                gamepad.previousAxes = gamepad.axes;
            }
            for (std::uint16_t button = 0; button < 8; ++button)
            {
                mouseState.previousButtons[button] = mouseState.buttons[button];
            }

            mouseState.deltaX = 0.0;
            mouseState.deltaY = 0.0;
            mouseState.scrollDeltaX = 0.0;
            mouseState.scrollDeltaY = 0.0;
            escapePressed = false;
            quitRequested = false;
            repeatedKeys.clear();
            textInput.clear();
        }

        void ClearKeyStates()
        {
            keys.fill(false);
            previousKeys.fill(false);
            for (auto &button : mouseState.buttons)
            {
                button = false;
            }
            for (auto &button : mouseState.previousButtons)
            {
                button = false;
            }
            mouseState.deltaX = 0.0;
            mouseState.deltaY = 0.0;
            mouseState.scrollDeltaX = 0.0;
            mouseState.scrollDeltaY = 0.0;
            repeatedKeys.clear();
            textInput.clear();
        }
    };
} // namespace PlutoGE::platform
