using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace PlutoGE.ScriptCore.Native;

internal static unsafe partial class ScriptBridge
{
    internal enum SoundEmitterRange { MinDistance = 0, MaxDistance = 1, Rolloff = 2 }

    private static delegate* unmanaged[Cdecl]<uint, int, float> _getSoundEmitterRange;
    private static delegate* unmanaged[Cdecl]<uint, int, float, void> _setSoundEmitterRange;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterSoundEmitterRangeApi")]
    public static int RegisterSoundEmitterRangeApi(
        delegate* unmanaged[Cdecl]<uint, int, float> getRange,
        delegate* unmanaged[Cdecl]<uint, int, float, void> setRange)
    {
        if (getRange == null || setRange == null)
        {
            SetError("Managed sound emitter range API registration received a null function pointer.");
            return 0;
        }
        _getSoundEmitterRange = getRange;
        _setSoundEmitterRange = setRange;
        _lastError = string.Empty;
        return 1;
    }

    internal static float GetSoundEmitterRange(uint entityId, SoundEmitterRange property) =>
        _getSoundEmitterRange == null ? 0 : _getSoundEmitterRange(entityId, (int)property);

    internal static void SetSoundEmitterRange(uint entityId, SoundEmitterRange property, float value)
    {
        if (_setSoundEmitterRange != null && float.IsFinite(value)) _setSoundEmitterRange(entityId, (int)property, value);
    }
}
