# Additive scene loading

M12 now has a native loader and managed API. File reads run asynchronously; parsing,
asset creation and activation run on the owning scene thread. This is asynchronous
I/O, not hitch-free activation: large section deserialization can still stall a
frame. GPU-backed assets are never created by the reader threads.

```csharp
var section = SceneManager.LoadAdditive("project://Scenes/Forest.plutoscene", false);
// Poll on the game thread:
if (section?.Status.State == SceneSectionState.Ready)
    section.Activate();
// Later:
section?.Unload();
section?.Forget();
```

`Status` reports Reading, Ready, Active, Cancelled, Failed or Unloaded, progress and
an error string. Reading progresses to 90%; activation completes the remaining
10%. `Activate` requests publication on the next scene update. At most one section
activates per update. `Cancel` prevents activation; `Unload` disables the section
immediately and uses deferred entity destruction. `Forget` releases a terminal
request, reclaiming cancelled workers after their read exits. Rejected requests
return null. Use the built-in `DistanceSceneSection` example for distance loading
with hysteresis and cancellation.

The persistent scene keeps its environment, baked scene lighting and simulation
world. Each loaded section receives fresh entity IDs. References between roots
inside a section are remapped together, including script fields and timelines.
Serialized references to entities outside the section are rejected, avoiding
accidental binding to a persistent entity with the same numeric ID. Runtime
references must be resolved after activation. On unload, resolve IDs again and
handle a missing entity; references do not keep a section alive.

Entity ownership survives same-section reparenting. Children spawned under a
section entity inherit its section. Cross-section parenting is rejected; detached
section roots still unload with their section. Native callers can inspect
`Scene::GetSectionOwner`. Managed section handles carry a generation that changes
on scene replacement/reset, so old handles cannot operate on a new scene.

Limits: four pending reads/staged sections, 64 request records, and 64 MiB per
section. Forget completed requests when no longer needed. Cancellation is checked
between 64 KiB reads; an OS-level read already in progress must return before its
worker can join. Scene shutdown joins workers. Failed validation publishes no
section entities. Section data must use the supported scene format and registered
component types; Windows CRLF and LF records are accepted.

Automated checks cover activation, cross-root references, ownership, spawned
children, cancellation, failures, reload, physics registration and unload queries.
M12 remains partial pending broader navigation/audio/script lifetime and cooked
project integration checks, activation budgeting and interactive backend tests.
