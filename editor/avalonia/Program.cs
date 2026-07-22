using Avalonia;
using Avalonia.OpenGL;
using System.Runtime.InteropServices;

namespace PlutoGE.Editor.Avalonia;

internal static class Program
{
    private const uint WindowsTimerResolutionMilliseconds = 1;

    internal static bool ViewportValidationMode { get; private set; }

    [STAThread]
    public static void Main(string[] args)
    {
        ViewportValidationMode = args.Contains("--viewport-validation", StringComparer.OrdinalIgnoreCase);
        var timerResolutionActive = OperatingSystem.IsWindows() &&
                                    TimeBeginPeriod(WindowsTimerResolutionMilliseconds) == 0;
        try
        {
            BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
        }
        finally
        {
            if (timerResolutionActive)
            {
                TimeEndPeriod(WindowsTimerResolutionMilliseconds);
            }
        }
    }

    public static AppBuilder BuildAvaloniaApp()
        => AppBuilder.Configure<App>()
            .UsePlatformDetect()
            .With(new Win32PlatformOptions
            {
                RenderingMode = [Win32RenderingMode.Wgl],
                WglProfiles = [new GlVersion(GlProfileType.OpenGL, 4, 3)],
                // Desktop OpenGL requires the redirection-surface path. Keep
                // Avalonia's compositor on its background render loop; the
                // process-wide 1 ms timer request above prevents that loop's
                // 16.67 ms waits from oversleeping on Windows.
                CompositionMode = [Win32CompositionMode.RedirectionSurface],
                ShouldRenderOnUIThread = false,
            })
            .With(new X11PlatformOptions
            {
                RenderingMode = [X11RenderingMode.Glx],
                GlProfiles = [new GlVersion(GlProfileType.OpenGL, 4, 3)],
            })
            .LogToTrace();

    [DllImport("winmm.dll", EntryPoint = "timeBeginPeriod")]
    private static extern uint TimeBeginPeriod(uint periodMilliseconds);

    [DllImport("winmm.dll", EntryPoint = "timeEndPeriod")]
    private static extern uint TimeEndPeriod(uint periodMilliseconds);
}
