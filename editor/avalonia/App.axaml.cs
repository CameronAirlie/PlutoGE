using Avalonia;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Markup.Xaml;
using PlutoGE.Editor.Avalonia.Native;
using PlutoGE.Editor.Avalonia.Services;
using PlutoGE.Editor.Avalonia.Windows;

namespace PlutoGE.Editor.Avalonia;

public sealed partial class App : Application
{
    private readonly EngineHost _engineHost = new();
    private EditorWindowService? _windows;

    public override void Initialize() => AvaloniaXamlLoader.Load(this);

    public override void OnFrameworkInitializationCompleted()
    {
        if (ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
        {
            _windows = new EditorWindowService(_engineHost);
            var shell = _windows.CreateShell();
            desktop.MainWindow = shell;
            shell.Closed += (_, _) => desktop.Shutdown();
            if (Program.ViewportValidationMode)
            {
                shell.Opened += (_, _) =>
                {
                    var screens = shell.Screens.All;
                    for (var index = 0; index < screens.Count; ++index)
                    {
                        var validationWindow = _windows.CreateViewportWindow();
                        validationWindow.Title = $"PlutoGE Viewport Validation — Display {index + 1}";
                        validationWindow.Position = screens[index].WorkingArea.Position + new PixelPoint(40, 40);
                        validationWindow.EnableViewportValidation();
                        validationWindow.Show();
                    }
                };
            }
            desktop.Exit += (_, _) =>
            {
                _windows.Dispose();
                _engineHost.Dispose();
            };
        }
        base.OnFrameworkInitializationCompleted();
    }
}
