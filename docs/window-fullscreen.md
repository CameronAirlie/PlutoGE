# Borderless fullscreen and script lifecycle

`Window::SetFullscreen(bool)` toggles a decorated window into a borderless desktop-sized window on the monitor with the largest overlap. It does not change the monitor video mode or recreate the graphics context. Disabling restores the window rectangle and maximized state. `WindowConfig.fullscreen` uses the same borderless behavior at creation.

C# scripts can read and write `Application.Fullscreen`:

```csharp
Application.Fullscreen = !Application.Fullscreen;
Application.Quit();
```

In editor play mode, fullscreen affects the hosting editor window. `Application.Quit()` queues a stop-play request; exported projects request application closure. The quit request is handled after script callbacks, avoiding teardown inside a callback.

Rebuild the engine and ScriptCore together: `RegisterWindowApi` is a new managed/native bridge registration.

ShadowSouls opens its pause menu with Escape or controller Start. Resume restores the previous simulation time scale and cursor capture. Camera sensitivity, control hints, and damage-flash preferences are saved to `Application.PersistentDataPath/settings.json`; fullscreen is a window-session setting. Settings and quit can be activated with the mouse or normal RmlUi keyboard navigation.


## Windowed client size

`Application.WindowSizeSupported` reports whether the native sizing API is registered. `WindowedSize` returns logical client dimensions (the restore dimensions while borderless); `WindowedSizeLimit` returns the current monitor work-area bounds minus decoration margin. `TrySetWindowedSize(width, height)` rejects invalid or oversized requests and returns whether the requested size was applied. An unchanged size is a no-op. While borderless it changes only the restore size; desktop resolution stays unchanged. An explicit changed size clears the saved maximized state.

The APIs affect the host editor window during Play, just like fullscreen. They are window sizes, not framebuffer or internal rendering resolutions, and do not perform exclusive monitor mode switches. UI clients should filter presets by the limit and use timed confirmation plus persistence rollback. Rebuild ScriptCore and the native host together (`RegisterWindowSizeApi`); an old host with updated scripts reports unsupported sizing. The no-client-API window test covers invalid requests, sizing and borderless restore dimensions without creating an OpenGL context.
