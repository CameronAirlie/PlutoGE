using System.Text.Json.Serialization;

namespace PlutoGE.ScriptCore;

/// <summary>
/// How a binding turns its control into action activity. To add a trigger, add a value here, an
/// <see cref="IInputTriggerEvaluator"/> below and its factory in <see cref="InputTriggers"/>; the editor's
/// <c>InputMappingDocument</c> lists the same names.
/// </summary>
public enum InputTriggerKind
{
    /// <summary>Fires when the control is pressed and stays active while it is held (the default).</summary>
    Press,
    /// <summary>Fires on release when the control was held no longer than <see cref="InputTrigger.Duration"/>.</summary>
    Tap,
    /// <summary>Fires once the control has been held for <see cref="InputTrigger.Duration"/> and stays active until release.</summary>
    Hold,
    /// <summary>Fires on the <see cref="InputTrigger.Count"/>th press, each within <see cref="InputTrigger.Window"/> of the last.</summary>
    MultiTap,
}

/// <summary>
/// Per-binding trigger settings, stored in a mapping as <c>"Trigger": { "Kind": "MultiTap", ... }</c>.
/// A binding without one behaves as <see cref="InputTriggerKind.Press"/>. Only the fields of the
/// chosen kind are read.
/// </summary>
public sealed class InputTrigger
{
    public InputTriggerKind Kind { get; set; } = InputTriggerKind.Press;
    /// <summary>Seconds. Tap: the longest hold that still counts as a tap. Hold: how long the control must be held.</summary>
    public float Duration { get; set; } = 0.2f;
    /// <summary>MultiTap: presses required (1 with <see cref="Exclusive"/> is a single tap that never starts a multi-tap).</summary>
    public int Count { get; set; } = 2;
    /// <summary>MultiTap: the longest gap (seconds) between consecutive presses.</summary>
    public float Window { get; set; } = 0.25f;
    /// <summary>
    /// MultiTap: wait out <see cref="Window"/> after the last press and fire only if no further press
    /// followed. This lets a single tap and a double tap share a control (at the cost of the window's latency).
    /// </summary>
    public bool Exclusive { get; set; }

    /// <summary>True when the trigger needs <see cref="InputActionMap.Update"/> every frame (anything but Press).</summary>
    [JsonIgnore] public bool IsStateful => Kind != InputTriggerKind.Press;
}

/// <summary>One frame of a binding's control, digitised (an axis is down past half deflection).</summary>
/// <param name="Pressed">Went down this frame (also set for a press and release inside one frame).</param>
public readonly record struct InputControlSample(bool Down, bool Pressed, bool Released);

/// <summary>What a trigger reports for one frame.</summary>
/// <param name="Triggered">The one-frame event read by <see cref="InputActionMap.WasPressed"/>.</param>
/// <param name="Active">The ongoing state read by <see cref="InputActionMap.IsDown"/> and <see cref="InputActionMap.GetAxis"/>.</param>
public readonly record struct InputTriggerResult(bool Triggered, bool Active);

/// <summary>The runtime behaviour of one trigger on one binding: fed every frame, keeps its own state.</summary>
public interface IInputTriggerEvaluator
{
    InputTriggerResult Update(in InputControlSample control, float deltaTime);
}

/// <summary>Creates trigger evaluators by kind; <see cref="Register"/> replaces a built-in one.</summary>
public static class InputTriggers
{
    private static readonly Dictionary<InputTriggerKind, Func<InputTrigger, IInputTriggerEvaluator>> Factories = new()
    {
        [InputTriggerKind.Press] = _ => new PressEvaluator(),
        [InputTriggerKind.Tap] = trigger => new TapEvaluator(trigger),
        [InputTriggerKind.Hold] = trigger => new HoldEvaluator(trigger),
        [InputTriggerKind.MultiTap] = trigger => new MultiTapEvaluator(trigger),
    };

    public static void Register(InputTriggerKind kind, Func<InputTrigger, IInputTriggerEvaluator> factory)
    {
        ArgumentNullException.ThrowIfNull(factory);
        Factories[kind] = factory;
    }

    /// <summary>An evaluator for <paramref name="trigger"/>; it reads the trigger's settings live, so edits apply at once.</summary>
    public static IInputTriggerEvaluator Create(InputTrigger trigger) =>
        Factories.TryGetValue(trigger.Kind, out var factory) ? factory(trigger) : new PressEvaluator();

    private static float Seconds(float value) => float.IsFinite(value) ? MathF.Max(0.0f, value) : 0.0f;

    private sealed class PressEvaluator : IInputTriggerEvaluator
    {
        public InputTriggerResult Update(in InputControlSample control, float deltaTime) =>
            new(control.Pressed, control.Down || control.Pressed);
    }

    /// <summary>Times how long the control has been held since it went down; shared by Tap and Hold.</summary>
    private struct HoldTimer
    {
        public bool Holding;
        public float Held;

        /// <summary>Advances the timer; returns true on the frame the hold ends (a release).</summary>
        public bool Advance(in InputControlSample control, float deltaTime)
        {
            if (control.Pressed && !Holding)
            {
                Holding = true;
                Held = 0.0f;
            }
            else if (Holding)
                Held += Seconds(deltaTime);
            if (!Holding || (control.Down && !control.Released))
                return false;
            Holding = false;
            return true;
        }
    }

    private sealed class TapEvaluator(InputTrigger trigger) : IInputTriggerEvaluator
    {
        private HoldTimer _timer;

        public InputTriggerResult Update(in InputControlSample control, float deltaTime)
        {
            var tapped = _timer.Advance(control, deltaTime) && _timer.Held <= Seconds(trigger.Duration);
            return new(tapped, tapped);
        }
    }

    private sealed class HoldEvaluator(InputTrigger trigger) : IInputTriggerEvaluator
    {
        private HoldTimer _timer;
        private bool _fired;

        public InputTriggerResult Update(in InputControlSample control, float deltaTime)
        {
            var released = _timer.Advance(control, deltaTime);
            var active = !released && _timer.Holding && _timer.Held >= Seconds(trigger.Duration);
            var triggered = active && !_fired;
            _fired = active;
            return new(triggered, active);
        }
    }

    private sealed class MultiTapEvaluator(InputTrigger trigger) : IInputTriggerEvaluator
    {
        private int _presses;
        private float _sinceLastPress;

        public InputTriggerResult Update(in InputControlSample control, float deltaTime)
        {
            var count = Math.Max(1, trigger.Count);
            var window = Seconds(trigger.Window);
            var fired = false;
            _sinceLastPress += Seconds(deltaTime);
            // A sequence ends when the window passes without another press; an exclusive trigger fires
            // then, provided the sequence stopped exactly at its count.
            if (_presses > 0 && _sinceLastPress > window)
            {
                fired = trigger.Exclusive && _presses == count;
                _presses = 0;
            }
            if (control.Pressed)
            {
                _presses++;
                _sinceLastPress = 0.0f;
                if (!trigger.Exclusive && _presses == count)
                {
                    fired = true;
                    _presses = 0;
                }
            }
            return new(fired, fired);
        }
    }
}
