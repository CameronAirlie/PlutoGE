#include "PlutoGE/ui/input/InputBindingCapture.h"

#include <cmath>
#include <string_view>

namespace PlutoGE::ui
{
    void CapturedInput::ApplyTo(InputMappingBinding &binding) const
    {
        binding.kind = kind;
        binding.value = value;
        binding.gamepad = gamepad;
        if (InputBindingIsAxis(kind))
            binding.scale = std::copysign(std::abs(binding.scale), direction);
    }

    void InputBindingCapture::Begin(int action, int binding, const platform::InputState &input)
    {
        m_action = action;
        m_binding = binding;
        for (std::size_t pad = 0; pad < input.gamepads.size(); ++pad)
            m_restingAxes[pad] = input.gamepads[pad].axes;
    }

    std::optional<CapturedInput> InputBindingCapture::Poll(const platform::InputState &input)
    {
        if (!IsActive())
            return std::nullopt;
        const auto heard = [this](CapturedInput captured)
        {
            Cancel();
            return std::optional<CapturedInput>(std::move(captured));
        };

        for (const auto &key : InputKeyNames())
        {
            if (key.code < 0 || key.code >= static_cast<int>(input.keys.size()) || !input.keys[key.code] || input.previousKeys[key.code])
                continue;
            // Escape cancels; bind Escape from the key list instead.
            if (std::string_view(key.name) == "Escape")
            {
                Cancel();
                return std::nullopt;
            }
            return heard({InputBindingKind::Key, key.name});
        }
        for (const auto &button : InputMouseButtonNames())
            if (button.code > 0 && input.IsMouseButtonPressed(static_cast<std::uint16_t>(button.code)))
                return heard({InputBindingKind::MouseButton, button.name});
        for (std::size_t pad = 0; pad < input.gamepads.size(); ++pad)
        {
            const auto &gamepad = input.gamepads[pad];
            if (!gamepad.connected)
                continue;
            for (const auto &button : InputGamepadButtonNames())
                if (input.IsGamepadButtonPressed(static_cast<std::uint16_t>(pad), static_cast<std::uint16_t>(button.code)))
                    return heard({InputBindingKind::GamepadButton, button.name, static_cast<int>(pad)});
            for (const auto &axis : InputGamepadAxisNames())
            {
                const float moved = gamepad.axes[axis.code] - m_restingAxes[pad][axis.code];
                if (std::isfinite(moved) && std::abs(moved) >= kAxisThreshold)
                    return heard({InputBindingKind::GamepadAxis, axis.name, static_cast<int>(pad), moved < 0.0f ? -1.0f : 1.0f});
            }
        }
        return std::nullopt;
    }
}
