using System.Numerics;

namespace PlutoGE.ScriptCore;

public enum UIEase
{
    Linear,
    EaseIn,
    EaseOut,
    EaseInOut,
    BackOut,
}

public static class UIEasing
{
    public static float Evaluate(UIEase ease, float t)
    {
        t = Math.Clamp(t, 0.0f, 1.0f);
        return ease switch
        {
            UIEase.EaseIn => t * t,
            UIEase.EaseOut => 1.0f - (1.0f - t) * (1.0f - t),
            UIEase.EaseInOut => t < 0.5f ? 2.0f * t * t : 1.0f - MathF.Pow(-2.0f * t + 2.0f, 2.0f) * 0.5f,
            UIEase.BackOut => 1.0f + 2.70158f * MathF.Pow(t - 1.0f, 3.0f) + 1.70158f * MathF.Pow(t - 1.0f, 2.0f),
            _ => t,
        };
    }
}

/// <summary>A small allocation-free tween value suitable for HUD controllers.</summary>
public struct UITween
{
    public float Current { get; private set; }
    public float Target { get; private set; }
    public float Duration { get; set; }
    public UIEase Ease { get; set; }

    private float _start;
    private float _elapsed;

    public UITween(float value, float duration = 0.1f, UIEase ease = UIEase.EaseOut)
    {
        Current = Target = _start = value;
        Duration = duration;
        Ease = ease;
        _elapsed = duration;
    }

    public void SetTarget(float value, bool immediate = false)
    {
        if (value == Target && !immediate) return;
        _start = Current;
        Target = value;
        _elapsed = immediate ? Duration : 0.0f;
        if (immediate) Current = value;
    }

    public float Update(float deltaTime)
    {
        _elapsed = Math.Min(_elapsed + Math.Max(deltaTime, 0.0f), Math.Max(Duration, 0.0001f));
        Current = float.Lerp(_start, Target, UIEasing.Evaluate(Ease, _elapsed / Math.Max(Duration, 0.0001f)));
        return Current;
    }
}

/// <summary>
/// Reusable presentation controller for a four-arm crosshair. Weapon/gameplay
/// code supplies spread and semantic state without depending on UI layout.
/// </summary>
public sealed class ReactiveCrosshair
{
    private readonly RectTransformComponent _top;
    private readonly RectTransformComponent _right;
    private readonly RectTransformComponent _bottom;
    private readonly RectTransformComponent _left;
    private readonly UIImageComponent[] _images;
    private UITween _spread;
    private UITween _alpha;
    private float _hitTime;

    public float HitFlashDuration { get; set; } = 0.12f;
    public Vector3 NormalColor { get; set; } = Vector3.One;
    public Vector3 TargetColor { get; set; } = new(1.0f, 0.2f, 0.15f);
    public Vector3 HitColor { get; set; } = new(1.0f, 0.08f, 0.05f);

    public ReactiveCrosshair(RectTransformComponent top, RectTransformComponent right,
                             RectTransformComponent bottom, RectTransformComponent left,
                             UIImageComponent topImage, UIImageComponent rightImage,
                             UIImageComponent bottomImage, UIImageComponent leftImage,
                             float initialSpread = 6.0f)
    {
        _top = top; _right = right; _bottom = bottom; _left = left;
        _images = [topImage, rightImage, bottomImage, leftImage];
        _spread = new UITween(initialSpread, 0.06f, UIEase.EaseOut);
        _alpha = new UITween(1.0f, 0.1f, UIEase.EaseOut);
    }

    public void SetSpread(float pixels) => _spread.SetTarget(Math.Max(0.0f, pixels));
    public void SetVisible(bool visible) => _alpha.SetTarget(visible ? 1.0f : 0.0f);
    public void TriggerHit() => _hitTime = HitFlashDuration;

    public void Update(float deltaTime, bool hasTarget = false)
    {
        var gap = _spread.Update(deltaTime);
        var alpha = _alpha.Update(deltaTime);
        _top.AnchoredPosition = new Vector2(0.0f, gap);
        _right.AnchoredPosition = new Vector2(gap, 0.0f);
        _bottom.AnchoredPosition = new Vector2(0.0f, -gap);
        _left.AnchoredPosition = new Vector2(-gap, 0.0f);

        _hitTime = Math.Max(0.0f, _hitTime - Math.Max(deltaTime, 0.0f));
        var color = _hitTime > 0.0f ? HitColor : hasTarget ? TargetColor : NormalColor;
        foreach (var image in _images)
        {
            image.Color = color;
            image.Alpha = alpha;
        }
    }
}
