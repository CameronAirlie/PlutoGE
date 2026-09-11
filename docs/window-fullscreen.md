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
