# Frame pacing

A steady frame rate feels smooth only when frames *start* at even intervals and each
frame simulates exactly the time it stays on screen. PlutoGE's runtime paces its own
frames, reads locked-cursor mouse motion off the main thread, and spreads cache
upkeep across frames so no single frame pays for it.

## Frame rate limit

Set **Project Settings > Display > Frame rate limit** (0 = unlimited). It is saved as
`MAX_FRAME_RATE <fps>` in the project file and applies to built games. Scripts can
change it at run time, for example from a settings menu:

```csharp
if (GraphicsSettings.TrySetMaxFrameRate(120))
    Debug.Log($"Limited to {GraphicsSettings.MaxFrameRate} FPS");
GraphicsSettings.TrySetMaxFrameRate(0); // unlimited
```

Limits are clamped to 10–1000 FPS. Set `PLUTO_MAX_FPS=<fps>` in the environment to
override the project's limit for one run (benchmarks are never limited).

Prefer this limit to a driver or overlay limiter. A driver limiter waits inside
present, after the frame has read input and simulated, so the frame's measured time
includes a wait it did not simulate. The engine waits **before** reading input, which
keeps input fresh and makes the frame interval the time from one frame start to the
next.

With VSync on, the display refresh is already a limit; a frame rate limit above the
refresh rate has no effect. On a variable-refresh (G-Sync/FreeSync) display, a limit a
few FPS below the refresh rate with VSync on gives the most even pacing.

## Delta time

`core::FramePacer` (owned by `core::Engine`, started with `Engine::BeginFrame`)
produces each frame's delta:

1. **Measured** from frame start to frame start, after the limit's wait. Work inside a
   frame, including input polling, never shifts it.
2. **Clamped** to 0.25 s, so a stall (breakpoint, window drag, hitch) does not advance
   the game in one jump. Scene loads call `FramePacer::Rebase` and are excluded.
3. **Snapped** to the cadence: with a frame limit, or VSync at the display's refresh
   rate, a delta within 15% of a whole number of frame intervals is reported as
   exactly that many intervals. Timer and scheduling noise then never reaches motion.
   The difference is carried into later frames, so game time never drifts from real
   time; a frame that is genuinely late reports its measured time.

The pieces are separate so each can change on its own: `FrameRateLimiter` decides
when a frame may start, `FrameDeltaSmoother` turns measured times into simulated
ones, `PreciseWaitUntil` waits on a high-resolution timer (sleeping until about 1 ms
before the deadline, then yielding), and `FramePacer` composes them over an
injectable time source (see `tests/FramePacerTests.cpp`).

The editor paces its own frames (its viewports and VSync), so the limit and the
snapping apply to the standalone runtime.

## Mouse input

High polling-rate mice (1–8 kHz) send a raw input message per report. Read through
the game window's message queue, each report is a message the frame's event poll
must dispatch, which can cost several milliseconds a frame while the mouse moves.

While the cursor is locked, `platform::Window` reads raw mouse motion through a
`platform::RelativeMouseInput` source instead. On Windows it is a dedicated thread
with a message-only window registered as the process's raw mouse target, which
drains reports in batches with `GetRawInputBuffer`; the main thread takes one
accumulated, unaccelerated delta per frame. Motion is captured only while the game
window has focus and the cursor is locked in disabled mode (remote sessions confine
the cursor and use its position instead). Raw input has one target window per
process, so GLFW's own raw motion is enabled only when no source is available.
`PLUTO_RAW_MOUSE_THREAD=0` reads raw motion through the window's queue instead, for
comparisons.

## Profiler lines

Runtime profiler captures (`--profiler`) report, per frame:

- `Event polling` — time spent in the window's event poll.
- `Mouse reports polled` — raw mouse reports folded into the frame's motion. Divided
  by the frame time, this is the mouse's report rate.
- `Frame pacing` — the limit, the time waited before the frame began (not part of
  the captured frame time), the interval since the previous frame started and the
  delta the frame simulated.

The profiler records frames serially (it disables the render thread), so captured
frame times are longer than the same scene's frames in normal play.

## Cache upkeep

Renderer caches keyed by preparation revisions (shadow draw signatures and VSM caster
packets) gain an entry whenever a draw changes, abandoning the old one. Erasing every
expired entry at fixed intervals made one frame in 64 pay for thousands of evictions.
They use `core::FrameRetainedCache`, which keeps entries for a retention period and
sweeps a fixed share of them every frame, so eviction cost is spread evenly. Use it
for any per-frame cache whose keys churn.
