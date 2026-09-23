using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace PlutoGE.ScriptCore.Native;

internal static unsafe partial class ScriptBridge
{
    private static delegate* unmanaged[Cdecl]<nint, int> _beginCpuScope;
    private static delegate* unmanaged[Cdecl]<int, void> _endCpuScope;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterProfilingApi")]
    public static int RegisterProfilingApi(delegate* unmanaged[Cdecl]<nint, int> begin, delegate* unmanaged[Cdecl]<int, void> end)
    {
        if (begin == null || end == null)
        {
            SetError("Profiling API registration received a null function pointer.");
            return 0;
        }
        _beginCpuScope = begin;
        _endCpuScope = end;
        _lastError = string.Empty;
        return 1;
    }
    internal static int BeginCpuScope(string name)
    {
        if (_beginCpuScope == null) return -1;
        var bytes = Encoding.UTF8.GetBytes(name + '\0');
        fixed (byte* pointer = bytes) return _beginCpuScope((nint)pointer);
    }
    internal static void EndCpuScope(int token)
    {
        if (token >= 0 && _endCpuScope != null) _endCpuScope(token);
    }
}
