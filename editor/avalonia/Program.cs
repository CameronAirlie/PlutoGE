using Avalonia;
using Avalonia.OpenGL;

namespace PlutoGE.Editor.Avalonia;

internal static class Program
{
    internal static bool ViewportValidationMode { get; private set; }

    [STAThread]
    public static void Main(string[] args)
    {
        ViewportValidationMode = args.Contains("--viewport-validation", StringComparer.OrdinalIgnoreCase);
        BuildAvaloniaApp().StartWithClassicDesktopLifetime(args);
    }

    public static AppBuilder BuildAvaloniaApp()
        => AppBuilder.Configure<App>()
            .UsePlatformDetect()
            .With(new Win32PlatformOptions
            {
                RenderingMode = [Win32RenderingMode.Wgl],
                WglProfiles = [new GlVersion(GlProfileType.OpenGL, 4, 3)],
            })
            .With(new X11PlatformOptions
            {
                RenderingMode = [X11RenderingMode.Glx],
                GlProfiles = [new GlVersion(GlProfileType.OpenGL, 4, 3)],
            })
            .LogToTrace();
}
