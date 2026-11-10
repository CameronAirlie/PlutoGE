using System.Numerics;

namespace PlutoGE.ScriptCore;

/// <summary>
/// Outlines everything its entity renders, and by default everything its children render (docs/OUTLINES.md): use it
/// for interaction highlights, objective markers or a picked target. Add it with
/// <c>GameObject.AddComponent&lt;OutlineComponent&gt;()</c> and switch it with <see cref="ComponentReference.Enabled"/>;
/// toggling an existing outline is cheaper than adding and removing one.
/// <para>
/// Every outline keeps its own style. Outlines with the same style share one layer of the outline pass; up to four
/// distinct styles draw per frame (further ones take the last style), and the editor selection draws on top.
/// </para>
/// </summary>
[NativeComponent("OutlineComponent")]
public sealed class OutlineComponent : RegisteredComponent
{
    internal OutlineComponent(uint entityId) : base(entityId)
    {
    }

    /// <summary>Linear RGB and opacity.</summary>
    public Vector4 Color { get => GetVector4("Color"); set => SetVector4("Color", value); }
    /// <summary>Width in pixels of a 1080-line output (other sizes scale it), 0.5 to 6.</summary>
    public float Width { get => GetFloat("Width", 2.0f); set => SetFloat("Width", value); }
    /// <summary>Opacity multiplier where other geometry hides the object, 0 to 1: 0 never shows it through walls.</summary>
    public float HiddenOpacity { get => GetFloat("Hidden Opacity"); set => SetFloat("Hidden Opacity", value); }
    /// <summary>True to outline the entity's descendants too (a pickup's visual child, a character's model).</summary>
    public bool IncludeChildren { get => GetBool("Include Children"); set => SetBool("Include Children", value); }
}
