using PlutoGE.ScriptCore;

namespace PlutoGE.ScriptCore.Examples;

/// <summary>
/// RmlUi replacement for FpsHudController. The document remains presentation;
/// this controller translates gameplay events into DOM state.
/// </summary>
public sealed class RmlFpsHudController : ScriptBehaviour
{
    [SerializedField] private GameObject? player = null;
    [SerializedField] private string documentPath = "UI/hud.rml";
    [SerializedField] private float hitFlashDuration = 0.12f;

    private KinematicFpsController? _controller;
    private RmlDocument? _document;
    private RmlElement? _ammo;
    private RmlElement? _reserve;
    private RmlElement? _interaction;
    private RmlElement[] _arms = [];
    private UITween _spread = new(7.0f, 0.07f, UIEase.EaseOut);
    private float _hitTime;
    private bool _domReady;

    public override void OnCreate()
    {
        _controller = player?.GetComponent<KinematicFpsController>();
        if (_controller is null)
        {
            Debug.LogError("RmlFpsHudController requires a KinematicFpsController.");
            return;
        }

        _document = new RmlDocument(documentPath);
        _ammo = _document.Element("ammo");
        _reserve = _document.Element("reserve");
        _interaction = _document.Element("interaction");
        _arms =
        [
            _document.Element("crosshair-top"),
            _document.Element("crosshair-right"),
            _document.Element("crosshair-bottom"),
            _document.Element("crosshair-left"),
        ];

        _controller.AmmoChanged += OnAmmoChanged;
        _controller.MovementStateChanged += OnMovementChanged;
        _controller.HitConfirmed += OnHit;
        _controller.InteractionTargetChanged += OnInteractionChanged;

        OnAmmoChanged(new FpsAmmoState(
            _controller.Ammo, _controller.ReserveAmmo,
            _controller.MagazineSize, _controller.IsReloading));
        OnMovementChanged(new FpsMovementState(
            _controller.IsGrounded, _controller.IsAiming,
            _controller.IsSprinting, _controller.IsCrouching,
            _controller.IsSliding));
    }

    public override void OnUpdate(float deltaTime)
    {
        if (!_domReady && _document is not null && _controller is not null &&
            _document.Element("crosshair").SetClass("headshot", false))
        {
            _domReady = true;
            OnAmmoChanged(new FpsAmmoState(
                _controller.Ammo, _controller.ReserveAmmo,
                _controller.MagazineSize, _controller.IsReloading));
            OnMovementChanged(new FpsMovementState(
                _controller.IsGrounded, _controller.IsAiming,
                _controller.IsSprinting, _controller.IsCrouching,
                _controller.IsSliding));
            OnInteractionChanged(null);
        }
        if (_arms.Length != 4) return;
        var gap = _spread.Update(deltaTime);
        _arms[0].SetStyle("top", -gap);
        _arms[1].SetStyle("left", gap);
        _arms[2].SetStyle("top", gap);
        _arms[3].SetStyle("left", -gap);

        _hitTime = MathF.Max(0.0f, _hitTime - MathF.Max(deltaTime, 0.0f));
        foreach (var arm in _arms) arm.SetClass("hit", _hitTime > 0.0f);
    }

    private void OnAmmoChanged(FpsAmmoState state)
    {
        if (_ammo is not null) _ammo.Markup = state.IsReloading ? "RELOAD" : state.Magazine.ToString();
        if (_reserve is not null) _reserve.Markup = $" / {state.Reserve}";
    }

    private void OnMovementChanged(FpsMovementState state)
    {
        var gap = state.IsSprinting || state.IsSliding ? 18.0f
            : state.IsAiming ? 3.0f
            : state.IsCrouching ? 5.0f
            : state.IsGrounded ? 7.0f : 14.0f;
        _spread.SetTarget(gap);
        _document?.Element("crosshair").SetClass("hidden", state.IsSprinting);
    }

    private void OnHit(FpsHitEvent hit)
    {
        _hitTime = MathF.Max(0.01f, hitFlashDuration);
        _document?.Element("crosshair").SetClass("headshot", hit.IsHeadshot);
    }

    private void OnInteractionChanged(GameObject? target)
    {
        if (_interaction is not null)
            _interaction.Markup = target is null ? string.Empty : $"[E] {target.Name}";
    }
}
