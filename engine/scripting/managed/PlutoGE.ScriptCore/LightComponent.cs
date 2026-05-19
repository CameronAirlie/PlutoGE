using System.Numerics;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public sealed class LightComponent : ComponentReference
{
    internal LightComponent(uint entityId)
        : base(entityId)
    {
    }

    internal override ScriptBridge.NativeComponentType ComponentType => ScriptBridge.NativeComponentType.Light;

    public float Intensity
    {
        get => ScriptBridge.GetLightIntensity(EntityId);
        set => ScriptBridge.SetLightIntensity(EntityId, value);
    }

    public Vector3 Color
    {
        get => ScriptBridge.GetLightColor(EntityId);
        set => ScriptBridge.SetLightColor(EntityId, value);
    }
}
