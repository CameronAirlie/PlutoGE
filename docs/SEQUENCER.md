# Sequencer runtime and authoring

This is a partial M11 delivery. The runtime component and inspector authoring are
implemented, including render-scoped editor preview and automatic restoration.
Interactive backend validation is still pending; M11 is not marked complete.

Add **Sequencer** through the Inspector's component menu. Add a track, enter the
target entity ID, select its channel and add keys in increasing time order. Entity
IDs refer to the current scene; a missing entity or required component displays a
binding warning and that track is skipped. Repair the ID or add the required
component. Duplicating a prefab hierarchy remaps bindings within that hierarchy.

Tracks support local position, Euler rotation in degrees, scale, camera FOV in
degrees, light color/intensity, sound-emitter volume, sound playback and script
events. Scalar channels use the first value field. Audio-play keys restart the
target emitter's assigned clip. Script-event keys use the existing
`OnAnimationEvent` callback: the event text supplies the name and the first value
supplies the float parameter. Audio and script events run only during runtime;
event tracks do not seek audio playback.

Enable **Play On Start** to start with runtime. The Inspector also exposes runtime
Play/Stop buttons. Native callers can use `Play`, `Stop`, `SetTimeline`,
`MissingBindings` and `RemapBindings` on `SequencerComponent`. `Scrub` is a low-level
native operation that silently applies values and stops the clock; callers are
responsible for restoring authoring values. It is not exposed as an editor preview
button; editor preview uses a separate render-scoped session.

Step holds the preceding key. Linear interpolates each value independently.
Smooth uses smoothstep between adjacent keys. Values before/after the authored
key range hold the first/last key. Euler rotations interpolate their authored
numeric values, allowing deliberate full revolutions. Later tracks win when
multiple tracks write the same property. Other gameplay components can overwrite
sequencer output later in the frame; avoid competing controllers on a binding.

Playback events use `(previous time, current time]`. Time-zero events are included
on the first positive advance after Play and after each loop boundary. A step
ending exactly at the duration emits end keys, resets the loop clock to zero and
defers zero keys to the next positive advance. Events are chronological within
each interval, with authoring order breaking ties. Zero delta and scrubbing emit
no events. Non-looping completion holds the final values; Stop also holds values.
Runtime restart begins at zero.

The versioned timeline encoding uses the classic locale and round-trip precision.
It is embedded in normal scene/component serialization and prefab overrides.
Limits are 256 tracks, 65,536 total keys, 1,024 bytes per event name and a 16 MiB
serialization budget. Keys must have unique increasing finite times in the
duration, and all values must be finite. Invalid edits are rejected atomically.
A playback advance exceeding 1,024 loops or 65,536 events is rejected atomically;
the component stops instead of silently dropping events.

Use Preview Timeline, Stop Preview and Preview Time in the Inspector. Preview
applies only during viewport rendering and restores values before editor UI,
saving and scene-history work. It does not dispatch script/audio events. Capture
Current Value fills a key from its bound authoring value. Editing/replacing the
sequencer, removing its owner or starting runtime invalidates the preview.

Outstanding M11 work: timeline-focused UI polish and interactive OpenGL/Vulkan
checks, including gizmo interaction while a preview is visible. Camera cuts and audio sample-accurate seeking
are not supplied by the current channels.

Checks:

```powershell
cmake --build out/build/msvc-nvidia --config Release --target PlutoGEEditor PlutoGERuntime PlutoGETimelineTests PlutoGESequencerTests -j 1
ctest --test-dir out/build/msvc-nvidia -C Release -R '^PlutoGE(Timeline|Sequencer)Tests$' --output-on-failure
```
