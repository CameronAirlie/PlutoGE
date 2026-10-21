using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace PlutoGE.ScriptCore.Native;

internal static unsafe partial class ScriptBridge
{
    private static delegate* unmanaged[Cdecl]<nint, int> _preloadAudioClip;
    private static delegate* unmanaged[Cdecl]<int, void> _prewarmAudioVoices;
    private static delegate* unmanaged[Cdecl]<float> _getAudioOutputVolume;
    private static delegate* unmanaged[Cdecl]<float, void> _setAudioOutputVolume;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterAudioPreparationApi")]
    public static int RegisterAudioPreparationApi(
        delegate* unmanaged[Cdecl]<nint, int> preloadClip,
        delegate* unmanaged[Cdecl]<int, void> prewarmVoices)
    {
        if (preloadClip == null || prewarmVoices == null)
        {
            SetError("Audio preparation API registration received a null function pointer.");
            return 0;
        }
        _preloadAudioClip = preloadClip;
        _prewarmAudioVoices = prewarmVoices;
        _lastError = string.Empty;
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterAudioOutputApi")]
    public static int RegisterAudioOutputApi(
        delegate* unmanaged[Cdecl]<float> getOutputVolume,
        delegate* unmanaged[Cdecl]<float, void> setOutputVolume)
    {
        if (getOutputVolume == null || setOutputVolume == null)
        {
            SetError("Audio output API registration received a null function pointer.");
            return 0;
        }
        _getAudioOutputVolume = getOutputVolume;
        _setAudioOutputVolume = setOutputVolume;
        _lastError = string.Empty;
        return 1;
    }

    internal static bool AudioOutputSupported => _setAudioOutputVolume != null;
    internal static float GetAudioOutputVolume() => _getAudioOutputVolume != null ? _getAudioOutputVolume() : 1.0f;
    internal static void SetAudioOutputVolume(float volume)
    {
        if (_setAudioOutputVolume != null) _setAudioOutputVolume(volume);
    }

    internal static bool PreloadAudioClip(string reference)
    {
        if (_preloadAudioClip == null) return false;
        var bytes = Encoding.UTF8.GetBytes(reference + '\0');
        fixed (byte* pointer = bytes) return _preloadAudioClip((nint)pointer) != 0;
    }
    internal static void PrewarmAudioVoices(int count)
    {
        if (_prewarmAudioVoices != null) _prewarmAudioVoices(count);
    }
}
