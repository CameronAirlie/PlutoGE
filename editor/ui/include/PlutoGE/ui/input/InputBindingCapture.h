#pragma once

#include "PlutoGE/platform/InputState.h"
#include "PlutoGE/ui/InputMappingDocument.h"

#include <array>
#include <optional>
#include <string>

namespace PlutoGE::ui
{
    // The physical input a capture heard.
    struct CapturedInput
    {
        InputBindingKind kind = InputBindingKind::Key;
        std::string value;
        int gamepad = 0;
        float direction = 1.0f; // Axes: -1 when pushed toward negative values.

        // Writes the input into `binding`, keeping its trigger and dead zone.
        void ApplyTo(InputMappingBinding &binding) const;
    };

    // Press-to-bind: listens for the next key, mouse button (other than the
    // left button, which clicks the editor), gamepad button or deliberate
    // stick/trigger movement. Escape cancels.
    class InputBindingCapture
    {
    public:
        // Starts listening for one binding. Axis movement is measured from
        // where the axes rest now, so resting triggers do not count.
        void Begin(int action, int binding, const platform::InputState &input);
        void Cancel() { m_action = m_binding = -1; }

        [[nodiscard]] bool IsActive() const { return m_action >= 0; }
        [[nodiscard]] bool IsListeningTo(int action, int binding) const { return m_action == action && m_binding == binding; }
        [[nodiscard]] int GetAction() const { return m_action; }
        [[nodiscard]] int GetBinding() const { return m_binding; }

        // The input pressed this frame, if any; listening stops when one is
        // heard or Escape is pressed.
        std::optional<CapturedInput> Poll(const platform::InputState &input);

    private:
        static constexpr float kAxisThreshold = 0.5f;

        int m_action = -1;
        int m_binding = -1;
        std::array<std::array<float, platform::MaxGamepadAxes>, platform::MaxGamepads> m_restingAxes{};
    };
}
