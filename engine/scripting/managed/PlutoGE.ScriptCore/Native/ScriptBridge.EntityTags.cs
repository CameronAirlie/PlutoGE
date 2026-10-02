using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;

namespace PlutoGE.ScriptCore.Native;

internal static unsafe partial class ScriptBridge
{
    private static delegate* unmanaged[Cdecl]<uint, byte*, int> _addEntityTag;
    private static delegate* unmanaged[Cdecl]<uint, byte*, int> _removeEntityTag;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)], EntryPoint = "RegisterEntityTagApi")]
    public static int RegisterEntityTagApi(
        delegate* unmanaged[Cdecl]<uint, byte*, int> addTag,
        delegate* unmanaged[Cdecl]<uint, byte*, int> removeTag)
    {
        if (addTag == null || removeTag == null)
        {
            SetError("Managed entity tag API registration received a null function pointer.");
            return 0;
        }
        _addEntityTag = addTag;
        _removeEntityTag = removeTag;
        _lastError = string.Empty;
        return 1;
    }

    internal static bool AddEntityTag(uint entityId, string tag) => ChangeEntityTag(_addEntityTag, entityId, tag);
    internal static bool RemoveEntityTag(uint entityId, string tag) => ChangeEntityTag(_removeEntityTag, entityId, tag);

    private static bool ChangeEntityTag(delegate* unmanaged[Cdecl]<uint, byte*, int> change, uint entityId, string tag)
    {
        if (change == null || string.IsNullOrEmpty(tag)) return false;
        var bytes = Encoding.UTF8.GetBytes(tag + '\0');
        fixed (byte* tagPtr = bytes) return change(entityId, tagPtr) != 0;
    }
}
