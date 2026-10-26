# PlutoGE agent guidance

## Runtime UI work

Before creating or modifying RML, RCSS, runtime UI controllers, or RmlUi integration, read [docs/RMLUI_AUTHORING.md](docs/RMLUI_AUTHORING.md) and [docs/RMLUI_REFERENCE.md](docs/RMLUI_REFERENCE.md). They document the pinned RmlUi version, supported controls/layout features, managed APIs, renderer limits and engine-specific extensions.

Use `dp` for player-scalable screen UI sizes. Preserve full-screen viewport roots and ordinary edge anchors; keep projected aiming geometry in physical/viewport units. `UISettings.TrySetInterfaceScale` controls screen density; world/preview/loading contexts remain independent. Do not add per-widget preference transforms or assume browser CSS Grid, JavaScript or newer RCSS features work in the pinned runtime.

When changing the managed bridge, rebuild native hosts and ScriptCore together. Validate interface-scale behavior on Vulkan/OpenGL with `PlutoGERmlUiInterfaceScaleTests`. Update the authoring guide and pinned inventory when introducing features or upgrading RmlUi. Preserve unrelated user edits.

## Native UI work

Before changing the entity-based UI (Canvas/RectTransform components, `engine/ui`, `UISystem`, `UIRhiRenderer`, the Canvas Editor), read [docs/NATIVE_UI.md](docs/NATIVE_UI.md). Keep `engine/ui` free of scene and renderer dependencies. Add behaviour through the interfaces (`IUIGraphic`, `IUIContentSize`, `UISelectable`/`IUIEventHandler`, `IUIRectDriver`, `IUIPopup`) and register components in `UIComponentTypes.cpp` instead of adding type checks to `UISystem`, the inspector or the serializer. Run the `PlutoGENativeUI*` tests, including the Vulkan and OpenGL render tests, after changes.
