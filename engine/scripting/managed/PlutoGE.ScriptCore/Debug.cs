using System.Runtime.CompilerServices;
using PlutoGE.ScriptCore.Native;

namespace PlutoGE.ScriptCore;

/// <summary>
/// Messages for the Console (and the log of a built game). Each message records the line that logged it, so the
/// editor Console opens that line on double-click; pass <c>context</c> to tie the message to an entity the
/// Console can select.
/// </summary>
public static class Debug
{
    public static void Log(object? message, GameObject? context = null,
                           [CallerFilePath] string callerFile = "", [CallerLineNumber] int callerLine = 0) =>
        ScriptBridge.LogMessage(0, message?.ToString(), context?.EntityId ?? 0, callerFile, callerLine);

    public static void LogWarning(object? message, GameObject? context = null,
                                  [CallerFilePath] string callerFile = "", [CallerLineNumber] int callerLine = 0) =>
        ScriptBridge.LogMessage(1, message?.ToString(), context?.EntityId ?? 0, callerFile, callerLine);

    public static void LogError(object? message, GameObject? context = null,
                                [CallerFilePath] string callerFile = "", [CallerLineNumber] int callerLine = 0) =>
        ScriptBridge.LogMessage(2, message?.ToString(), context?.EntityId ?? 0, callerFile, callerLine);

    /// <summary>
    /// Logs a caught exception as an error with its full stack trace. The Console opens the exception's own source
    /// line (the first frame in your code) rather than this call.
    /// </summary>
    public static void LogException(Exception exception, GameObject? context = null)
    {
        ArgumentNullException.ThrowIfNull(exception);
        ScriptBridge.LogMessage(2, exception.ToString(), context?.EntityId ?? 0, null, 0);
    }
}
