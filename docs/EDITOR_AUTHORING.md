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

Scripts > Script Build Diagnostics displays compiler output. Selecting a
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

Build, select an entity, then choose Scene > Commands > Project Commands. GameObject is
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

## Scene undo

`SceneEditHistory` owns scene undo and redo, and `EditorShell` forwards to it. It
records an edit in one of two ways, and the edit's reach decides which:

- **Scoped.** Before and after states of the entities that changed
  (`EntityStateCapture`), applied in place. The cost follows the edit, and undo
  keeps the scene object and every entity's ID.
- **Snapshot.** The whole scene's text, for edits that reach further or do not
  say how far they reach. The cost follows the scene, and undo swaps the scene.

| To record | Use | Reach |
| --- | --- | --- |
| A change you are about to make | `ExecuteSceneEdit(label, edit, scope)` | `SceneEditScope`: `Entities`, `Subtrees`, `Additive`, or the default `WholeScene` |
| A change you already made | `MarkSceneDirty()` | The enclosing `SceneEditRegion`, or `Scene` without one |
| A change over several frames | `BeginSceneEdit(label, reach)` and `EndSceneEdit()` | `SceneEditReach::Selection` or the default `Scene` |
| A change you can reverse yourself | `PushSceneEditCommand(label, undo, redo, bytes)` | Yours; commands look entities up by ID, because a snapshot undo replaces them |

A wider reach is always correct, just slower, so only narrow it when you know
the edit stays inside. The inspector and the viewport gizmos narrow theirs with a
`SceneEditRegion`, and a control inside one that reaches further, such as applying
a prefab, passes `SceneEditReach::Scene` itself. Members of linked model instances
and selections above `kMaxScopedEntities` always record by snapshot.

An edit reported after the fact needs a before-state that already exists. If a
scoped step has moved the scene on since the last whole-scene state, a late `Scene`
edit merges with the steps since then into one undo step. Nothing is lost, but
undo gets coarser. To keep that rare, the shell catches the whole-scene state up
after two seconds with no edit or interaction.

### Heavy data

A component holding data too large to copy per edit (foliage instances, baked
probes, terrain heights, capture pixels) overrides the heavy-data methods on
`Component`, and encodes through `HeavyPropertyCache`. Bump the revision in every
write to the data: a missed bump saves stale data, so debug builds re-encode and
compare. Heavy text must not contain tabs, newlines or backslashes, because the
serializer writes it without escaping.

### Finding edits undo misses

Window > Tools > Verify Scene Undo keeps a whole-scene snapshot beside every step,
undoes and redoes each step as it is recorded, compares the results, and logs any
difference with the step's label. A scoped edit that changed something outside its
reach shows up there. It is slow, and off by default.

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

## Controller menus and quality definitions

For shared controller navigation, menu capture/release guards, remapped menu input
and active-device prompts, see [Controller menus and active input device](RMLUI_AUTHORING.md#controller-menus-and-active-input-device).
ScriptCore owns the menu primitives; projects register actions and provide their
own dialog content. `Input.ActiveDevice`, `Input.UsingController` and
`Input.ActiveGamepad` report last meaningful input, independently of connection.

Open **Window > Tools > Quality Settings** for project-authored Low/Medium/High/Ultra
policies. Each tier exposes shadow resolution/cascade/distance ceilings, an option
to use cascaded directional shadows, and the allowed post-process groups. Hover a
control for its exact effect scope. Changes mark the project dirty; **File > Save
Project** (Ctrl+S) persists them and builds carry the definitions into the runtime
manifest. **Reset this tier to engine defaults** affects only the selected tier.

**Preview tier in Scene viewport** applies the selected definition without changing
player preferences. Both viewport Quality popups and
`GraphicsSettings.TryGetPreset`/`TryApplyPreset` use the project definitions.
Editing definitions does not replace an already applied quality policy: reselect
or reapply the tier to update it. Editing is disabled during Play. Full policy,
limits and persistence details are in [graphics presets](GraphicsPresets.md).
