# Controller vibration

## Plan and implementation

1. Add a reusable timed two-motor ScriptCore API, keeping platform output and controller identity in the engine.
2. Use a small extension compiled into pinned GLFW 3.4 to resolve each logical controller to its actual Windows XInput user index. Do not infer XInput indices from GLFW IDs or controller ordering.
3. Enforce finite normalized strengths, a five-second duration ceiling, focus/input gating and automatic expiry. Stop on focus loss, input disable, device-list changes and window close.
4. Let games mix named effects and own their saved strength preferences. CoD combines shot, damage and reload envelopes and sends short watchdog pulses to the active controller.
5. Validate managed routing, envelope timing/mixing, preference migration, UI wiring and native host builds. Finish with hardware feel testing.

## API

`Input.TrySetGamepadVibration(lowFrequency, highFrequency, seconds, gamepad = 0)` returns false for invalid arguments, unsupported output, disabled input or an unfocused window. Strengths are 0..1 and duration is 0..5 seconds. A zero duration or two zero strengths stops output. `Input.StopGamepadVibration(gamepad)` is the convenience stop call. Requests replace the selected controller's previous output; games should mix concurrent effects before submitting.

The Windows backend supports XInput-compatible devices. DirectInput-only devices and other platforms return false. The extension lives in `third_party/glfw/PlutoRumble.c` and relies on the pinned GLFW internals; review it when upgrading GLFW. No additional system DLL is loaded: output uses XInputSetState from the XInput library GLFW already selected.

Native expiry uses GLFW monotonic time, checked once per input poll. Motors stop when script input is disabled or focus is lost, on controller-list remapping, and before window destruction. Shutdown stops through the script owner and native window lifecycle. If an application stops polling altogether, expiry cannot run until polling resumes; games should keep output durations short.

The managed API registers through a separate `RegisterGamepadVibrationApi` export, preserving the existing input registration ABI. Rebuild ScriptCore and all native hosts together. Older hosts simply leave the vibration callback unavailable.

## CoD feedback

A 0..100% Controls slider persists independently of audio gains. Shot: (.25, .65) for 100ms. Damage: (.9, .45) scaled by damage for 300ms. Reload completion: (.18, .3) for 120ms. Each channel decays linearly; the maximum per motor wins. CoD submits a 100ms native watchdog pulse each frame while an envelope remains active. Gameplay capture, inactive input device, death and destruction clear the mixer and stop output. Spectator playback never creates pulses.

## Verification

CoD Settings tests cover preference migration/clamping/roundtrip, both menu sliders, independent motor mixing, effect expiry, clearing, invalid pulse rejection and frame timing. Hardware QA should cover a sustained automatic burst, damage during firing, armour-only damage, reload completion, pause and resume, strength 0%, mouse takeover, alt-tab, unplug/reconnect, scene exit and play-session stop. Test multiple controllers to verify only the active logical controller vibrates.
