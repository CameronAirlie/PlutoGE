using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>Optional CPU trace scopes for synchronous, same-frame script work.</summary>
public static class Profiler
{
    public static CpuScope Scope(string name) => new(name);

    public ref struct CpuScope
    {
        private int encodedToken;
        internal CpuScope(string name)
        {
            ArgumentException.ThrowIfNullOrWhiteSpace(name);
            encodedToken = ScriptBridge.BeginCpuScope(name) + 1;
        }
        public void Dispose()
        {
            if (encodedToken == 0) return;
            ScriptBridge.EndCpuScope(encodedToken - 1);
            encodedToken = 0;
        }
    }
}
