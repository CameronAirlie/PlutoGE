# Debug audio mixer deadline fix

Measured on 24 September 2026 on the development machine (Ryzen 7 5700G).
All sounds were reported to slow down and crackle during low editor FPS.
OpenAL already mixes full clip buffers on its own device thread; game updates
do not supply audio samples. However, the Debug build compiled OpenAL's HRTF
mixer without optimisation, making it unable to meet audio deadlines under load.

`third_party/CMakeLists.txt` now enables optimisation inside the OpenAL dependency
build, including its helper libraries, for Debug configurations. MSVC runtime
checks incompatible with optimisation are removed in that scope. Debug symbols,
the Debug CRT, HRTF, voice capacity and engine Debug flags remain intact.

## Repeatable mixer measurement

From the repository root:

```powershell
python tests/benchmark_audio_mixer.py out/build/msvc-nvidia/_deps/openal_soft-build/Debug/OpenAL32d.dll
```

The benchmark uses OpenAL loopback rendering: 64 looping positional mono voices,
48 kHz stereo float output, HRTF enabled, 10 ms blocks, 20 warm-up blocks and 200
measured blocks. Run without a concurrent build for comparable timing.

| Mixer build | Mean per 10 ms audio | p95 | Blocks exceeding 10 ms |
| --- | ---: | ---: | ---: |
| Original Debug | 31.35 ms | 41.25 ms | 200 / 200 |
| Release reference | 0.73 ms | 1.22 ms | 0 / 200 |
| Corrected Debug | 1.11 ms | 1.44 ms | 0 / 200 |

This reproduces and removes a mixer deadline failure. Loopback timings do not
measure Windows device/driver latency or substitute for listening in Play mode.

## Playback regression

```powershell
cmake --build out/build/msvc-nvidia --config Debug --target PlutoGEAudioBatchLifecycleTests -j 4
ctest --test-dir out/build/msvc-nvidia -C Debug -R '^PlutoGEAudioBatchLifecycleTests$' --output-on-failure
```

The native test passes, covering start, pause/resume, restart, spatial/loop
transitions, retirement and completion. It now also checks a one-second sound
across 300 ms and 900 ms gaps in game updates with a tiny simulation delta,
confirming device-clock completion while loops remain active.

The rebuilt `OpenAL32d.dll` and symbols were copied into the existing
`out/build/msvc-nvidia/editor/Debug` and `runtime/Debug` directories; DLL hashes
were checked against the build output. Restart the editor to load the new DLL.
Future editor/runtime builds use the existing dependency-copy build steps.
