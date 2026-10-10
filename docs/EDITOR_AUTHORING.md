# Faster authoring workflows

Editor modules live under `editor/ui`. Reusable managed services live under
`PlutoGE.ScriptCore.Authoring`; inspector-ready behaviours live under
`PlutoGE.ScriptCore.Gameplay`. These modules extend existing scene, scripting,
asset and RmlUi systems rather than introducing another runtime.

## Project templates and kits

Starting without a project opens the project launcher, with recent projects,
Open Project and template-based creation. Scene tools and workspace viewports are
initialized only after a project loads. Missing recent files are marked unavailable;
failed loads remain in the launcher with their error. Passing a project manifest
on the command line opens it directly; invalid paths fall back to the launcher.

Choose **File > New Project from Template**. Empty creates the usual scaffold;
First-person and Third-person create a test room, kinematic player, parented
camera, controller, health/inventory and menu. Vehicle creates a rigidbody
chassis, suspension anchors, raycast controller, chase camera and menu. Object
viewer supplies orbit/pan/zoom navigation. UI application supplies a camera and
localized settings interface. Nonempty templates build scripts once on creation;
the .NET SDK must be available. Placeholder visuals can be replaced normally.

**Authoring** inserts undoable kits into the current scene:

| Kit | Setup |
|---|---|
| Interactive door | Collider, DoorBehaviour and an E-key action link |
| Inventory pickup | Configurable item/amount; Receiver defaults to the Player tag |
| Checkpoint | Trigger collider; assign Player and connect a health DeathTarget to Respawn |
| Pause and settings menu | Escape menu, interface scale, language selection and F6/F9 save/load |

The UI kit copies RML/RCSS, Martian Mono and its OFL notice to Assets/UI. Existing
files are not overwritten. FPS/third-person templates use the existing example
controllers (WASD, mouse, Shift, Space); inspect their source for other controls.
Vehicle uses the existing raycast controller's controls.

Health and Inventory are independent managed models. Health.Current is observable
and death fires on an alive-to-dead transition. Inventory bounds distinct stacks
and rejects invalid/overflowing quantities. Their behaviour adapters are ordinary
serialized scripts, usable with project-specific UI and networking.

ActionLinkBehaviour connects Source (Key, CollisionEnter, CollisionExit or
RmlClick) to Target/Method. Methods are public and parameterless. Key links use
Interactor (or the Player tag) and MaximumDistance; CollisionFilter optionally
restricts the other entity. RmlClick uses Document/Element IDs. These use existing
collision callbacks, not new separate trigger callbacks. Multiple links support
multiple actions. Code-side ActionRegistry instances provide named actions with
disposable registration tokens.

## Visual UI and binding

Open an RML asset in UI Document Editor and expand **Visual UI builder**. Select
from the source hierarchy or click a stable-ID element in the preview. Edit
ID/class/style/value/src with Enter, apply a layout/style property, set escaped
leaf text, insert a widget or delete a subtree. Computed layout and selected
computed styles are displayed alongside the source properties.

Edits share RmlDocumentEditSession undo/redo and remain unsaved until Save. The
tool edits source spans, never serializes generated DOM over authored files.
Style actions append a local inline override; external stylesheets remain in
source tabs. Linked templates remain source-editable, but picking only selects
elements in the primary document. The visual parser requires quoted XML
attributes and balanced tags. Declarations, non-XML named attribute entities and
malformed source require source editing. Leaf text editing refuses to erase child
widgets. This is a hierarchy/property builder, not a freeform drag/resize tool.
Use dp and preserve viewport roots/anchors as described in RMLUI_AUTHORING.md.

RmlViewBinder provides escaped text, public-property, localized-text, two-way
value and named-action bindings. Include PlutoGE.ScriptCore.Authoring:

```csharp
document = new RmlDocument("UI/screen.rml");
view = new RmlViewBinder();
view.BindText(document.Element("health"), () => health.Current.Value);
view.BindProperty(document.Element("name"), playerModel, "Name", twoWay: true);
// OnUpdate:
view.Refresh();
// OnDestroy, before disposing document:
view.Dispose();
```

Two-way properties support string/bool/int/float/double through the value
attribute, not checkbox checked-state. Invalid numeric input restores the model
value. Private data-pluto-binding-* markers detect recreated elements and reapply
unchanged values after reload. Dispose only removes this binder's subscriptions.
This does not register native RmlUi data-model expressions.

## C# iteration

Scripts > Automatically Build Changed Scripts watches C# and MSBuild inputs,
excluding bin/obj/Managed/.git and symlinks. Scans and automatic compilation run
in background tasks, with debounce and no retry loop after compiler errors.
Automatic builds start outside Play, baking and imports. If Play starts during a
build, publication waits until Play stops. Failed automatic builds retain the
loaded assembly; successful reload happens on the editor thread. Arbitrary live
managed state is not preserved.

Authoring > Script Build Diagnostics displays compiler output. Selecting a
file/line diagnostic opens its source in the associated editor; line/column remain
visible. Processes execute directly without a shell, with bounded combined
output. Manual builds/export remain synchronous, and concurrent manual builds
are rejected while an automatic build is pending. Shutdown joins compiler work
before removing its log sink. Unchanged generated project inputs keep their
timestamps to avoid rebuild loops.

## Editor extensions

Project assemblies can derive from EditorCommand:

```csharp
using System.Numerics;
using PlutoGE.ScriptCore.Authoring;
public sealed class ResetPosition : EditorCommand
{
    protected override void Execute() => GameObject.Position = Vector3.Zero;
}
```

Build, select an entity, then choose Authoring > Project Commands. GameObject is
the selection. Commands run once within one undoable scene edit; failed commands
roll back scene changes. Do not retain entity references, replace the scene or
schedule background scene mutations. Filesystem/network effects are outside undo.

Native extensions register AuthoringRegistry::Entry through
EditorShell::GetAuthoringRegistry(). Kinds are ProjectTemplate, GameplayKit,
Command and Inspector. IDs are unique; menus iterate snapshots and availability
predicates control applicability. Inspector callbacks render in the Inspector;
issue edits through ExecuteSceneEdit. Unregister before unloading code. These
are source-level native hooks, not a binary plugin ABI or a managed custom
inspector UI API.

## Save/load

SaveGameService is session-owned and accepts an ISaveStore. ProjectSaveStore uses
atomic project user-data files and validated slot names. Register ISaveParticipant
objects with authored stable keys and keep/dispose their registration tokens.
Transient entity IDs must not serve as save identities.

Participants implement Capture, Validate and Restore with JsonElement. All
matching states validate before mutation; failed restores attempt reverse-order
rollback and report rollback failures. Callbacks must avoid irreversible effects.
Migrations advance one version at a time. Unsupported versions, missing
migrations, duplicate properties, invalid identities and oversized saves fail
explicitly. The default limit is 4096 participants and 16 million JSON characters.

PersistentObjectBehaviour captures local transform, active state and attached
health/inventory models. Assign a unique Identity and tag the entity Persistent
for the starter menu's save slot. Player templates do this automatically. Change
identities on duplicated persistent objects; duplicates are rejected.

The starter menu restores an already-open scene. For cross-scene loads, Read the
slot, load snapshot.Scene, wait for activation, register its participants, then
Restore. The framework leaves scene ownership and object factories explicit; it
does not implicitly serialize every native component or recreate arbitrary
runtime-spawned objects.

## Localization

LocalizationCatalog.LoadJson(language, json) consumes objects such as
{"menu.play":"Play"}. Supply JSON through the project's content loader so the
same source works in packed games. The catalog owns no disk/renderer dependency.
Lookup checks the selected .NET culture, its parents, then FallbackLanguage.
Unresolved keys display their key. MissingKeys checks the exact requested table.
Duplicate/non-string entries fail without replacing a valid table. Format uses
the selected culture. BindLocalizedText follows language changes during Refresh.

The starter interface demonstrates English/French switching. Add project tables
and licensed font/fallback faces through RML/RCSS. String lookup/validation are
provided; glyph coverage and right-to-left layout remain authoring responsibilities.

## Verification

AuthoringToolsTests covers source editing, escaping, watch debounce and registry
lifetime. AuthoringManagedTests covers save roundtrips/migrations/rollback,
localization, models and UI bindings with mocked native callbacks.
ScriptBuildProcessTests checks literal arguments, combined output, exit codes and
output bounds. AuthoringIntegrationTests creates all templates in an isolated
output directory, builds scripts, exercises runtime lifetimes, loads UI and
checks extension rollback. Existing UI preview and Vulkan/OpenGL interface-scale
suites cover renderer integration.
