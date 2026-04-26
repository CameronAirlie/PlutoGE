#pragma once

#include <cstdint>
#include <array>

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

    struct MouseState
    {
        bool buttons[8] = {false};
        double x = 0.0;
        double y = 0.0;
    };

    struct InputState
    {
        bool escapePressed = false;
        bool quitRequested = false;
        MouseState mouseState;
        std::array<bool, 512> keys = {false};

        [[nodiscard]] bool IsKeyPressed(KeyCode key) const
        {
            const auto keyIndex = static_cast<std::uint16_t>(key);
            if (keyIndex < 512)
            {
                return keys[keyIndex];
            }
            return false;
        }

        void ClearKeyStates()
        {
            keys.fill(false);
        }
    };
} // namespace PlutoGE::platform