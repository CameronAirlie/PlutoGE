# Audio output volume

## API

`Audio.OutputVolume` (0..1) is the player's master volume: the engine applies it at the output stage, after every
listener and emitter gain (the XAudio2 mastering voice; the OpenAL listener gain, whether or not a scene has a listener).
Writes are clamped; a non-finite value restores 1. It belongs to the application rather than a scene, so it survives
scene loads, and the engine does not save it: games own their preferences and set it again on start.
`Audio.OutputVolumeSupported` is false in hosts without the bridge (the property then reads 1 and ignores writes).

Scene `SoundListenerComponent.MasterVolume` stays an authored, per-scene gain; the two multiply. Per-category volumes
(effects, voices, music) remain game code: scale the volume you pass to `PlayOneShot` or set on the emitter.

The managed API registers through a separate `RegisterAudioOutputApi` export, preserving the audio preparation
registration ABI. Rebuild ScriptCore and all native hosts together.

## Verification

`tests/managed/PlutoGE.PresentationPreparationTests` covers the unregistered default, registration, round trip,
clamping and non-finite rejection. The ThirdPersonProject sample uses it for its Master volume setting.
