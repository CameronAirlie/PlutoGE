using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

public static class Debug
{
    public static void Log(object? message)
    {
        ScriptBridge.LogMessage(0, message?.ToString());
    }

    public static void LogWarning(object? message)
    {
        ScriptBridge.LogMessage(1, message?.ToString());
    }

    public static void LogError(object? message)
    {
        ScriptBridge.LogMessage(2, message?.ToString());
    }
}
