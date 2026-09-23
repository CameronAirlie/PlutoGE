using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace PlutoGE.ScriptCore.Native;

internal static unsafe partial class ScriptBridge
{
    private static delegate* unmanaged[Cdecl]<nint, int> _preloadAudioClip;
    private static delegate* unmanaged[Cdecl]<int, void> _prewarmAudioVoices;

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
