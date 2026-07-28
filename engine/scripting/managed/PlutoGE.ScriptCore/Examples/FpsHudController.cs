using System.Numerics;
using PlutoGE.ScriptCore;

namespace PlutoGE.ScriptCore.Examples;

/// <summary>
/// Event-driven HUD companion for KinematicFpsController. Attach this to a UI
/// entity and assign the player plus any text/image components you want to use.
/// </summary>
public sealed class FpsHudController : ScriptBehaviour
{
    [SerializedField] private GameObject? player = null;
    [SerializedField] private UITextComponent? ammoText = null;
    [SerializedField] private UITextComponent? statusText = null;
    [SerializedField] private UITextComponent? interactionText = null;
    [SerializedField] private UIImageComponent? hitMarker = null;
    [SerializedField] private float hitMarkerDuration = 0.10f;
    [SerializedField] private Vector3 normalHitColor = Vector3.One;
    [SerializedField] private Vector3 headshotColor = new(1.0f, 0.25f, 0.15f);

    private KinematicFpsController? _controller;
    private float _hitMarkerTime;

    public override void OnCreate()
    {
        _controller = player?.GetComponent<KinematicFpsController>();
        if (_controller is null)
        {
            Debug.LogError("FpsHudController requires a player with KinematicFpsController.");
            return;
        }

        _controller.AmmoChanged += OnAmmoChanged;
        _controller.MovementStateChanged += OnMovementStateChanged;
        _controller.HitConfirmed += OnHitConfirmed;
        _controller.InteractionTargetChanged += OnInteractionTargetChanged;

        // Events describe changes, so initialise once from the current snapshot.
        OnAmmoChanged(new FpsAmmoState(
            _controller.Ammo,
            _controller.ReserveAmmo,
            _controller.MagazineSize,
            _controller.IsReloading));
        OnMovementStateChanged(new FpsMovementState(
            _controller.IsGrounded,
            _controller.IsAiming,
            _controller.IsSprinting,
            _controller.IsCrouching,
            _controller.IsSliding));
        OnInteractionTargetChanged(null);

        if (hitMarker is not null)
        {
            hitMarker.Alpha = 0.0f;
        }
    }

    public override void OnUpdate(float deltaTime)
    {
        if (hitMarker is null || _hitMarkerTime <= 0.0f)
        {
            return;
        }

        _hitMarkerTime = MathF.Max(0.0f, _hitMarkerTime - deltaTime);
        hitMarker.Alpha = hitMarkerDuration <= 0.0f
            ? 0.0f
            : Math.Clamp(_hitMarkerTime / hitMarkerDuration, 0.0f, 1.0f);
    }

    private void OnAmmoChanged(FpsAmmoState state)
    {
        if (ammoText is not null)
        {
            ammoText.Text = state.IsReloading
                ? $"RELOADING  {state.Magazine} / {state.Reserve}"
                : $"{state.Magazine} / {state.Reserve}";
        }
    }

    private void OnMovementStateChanged(FpsMovementState state)
    {
        if (statusText is null)
        {
            return;
        }

        statusText.Text = state.IsSliding ? "SLIDING"
            : state.IsSprinting ? "SPRINTING"
            : state.IsAiming ? "AIMING"
            : state.IsCrouching ? "CROUCHED"
            : string.Empty;
    }

    private void OnHitConfirmed(FpsHitEvent hit)
    {
        if (hitMarker is null)
        {
            return;
        }

        hitMarker.Color = hit.IsHeadshot ? headshotColor : normalHitColor;
        hitMarker.Alpha = 1.0f;
        _hitMarkerTime = MathF.Max(0.01f, hitMarkerDuration);
    }

    private void OnInteractionTargetChanged(GameObject? target)
    {
        if (interactionText is not null)
        {
            interactionText.Text = target is null ? string.Empty : $"[E] {target.Name}";
        }
    }
}
