# CPU profiler

Open the Profiler panel and press **Record**. Select a **History** capacity (1–2000
frames, default 240). Recording continues until you press **Stop**, discarding
the oldest frames as new ones arrive. The trace memory budget may shorten the
retained history. **Record** replaces the previous capture; **Clear** discards it.
Recording continues when the panel is hidden. **Follow latest** keeps the newest
captured frame in view; selecting a frame or sample pauses following while
recording continues. The selected frame stays selected as its index shifts;
if it is discarded, selection moves to the oldest retained frame.

The stacked **CPU Usage** chart shows exclusive time by category: scripts,
rendering, physics, animation, audio, UI, waits/presentation, and other work.
Reference lines mark 60 FPS and 30 FPS. Hover for category durations, click/drag
to select a frame, use the index slider or arrows to step through frames, or
jump to **Slowest** / **Next hitch** using the configurable threshold.

## Inspecting a frame

- **Timeline** shows timestamped, nested CPU samples on the main thread. Wheel
  to zoom around the pointer, middle-drag to pan, or double-click a sample to
  focus it. **Fit frame** restores the full range. The scrollbar handles deep
  nesting. Select a sample to see its total/self time, start time, category,
  and entity context; **Copy sample** exports that individual call.
- **Hierarchy** groups repeated samples by call path. Expand parents to inspect
  children, ordered by total time, with self time, calls, and percentage of the
  CPU frame. Search names or entity context. Selecting a group selects its first
  matching call, which can then be inspected in the timeline.
- **GPU observations** retains asynchronous GPU measurements. These are durations,
  not a GPU execution timeline, and may originate from earlier CPU frames.
- **Detailed frame metrics** retains the complete original metrics, including
  per-component/script aggregates, slowest entities, RHI and renderer workloads,
  panel costs, lighting and post-processing. **Copy metrics** exports the selected
  frame's aggregate report. Live statistics are explicitly labeled.

## Instrumentation and architecture

`core::CpuTrace` and `core::CpuScope` implement a thread-local, bounded RAII
recorder using a monotonic clock. Scopes own their labels and record start time,
duration, parent, depth, category, and optional context. Scope completion restores
the parent through returns and exceptions. There is no locking; only the editor's
main thread is currently attached. Disabled scopes avoid clock reads and label
copies. Additional instrumentation can use `CpuScope` without a UI dependency.

The editor attaches a recorder at the start of each frame while recording and
closes the root scope at the measured frame end. `EditorProfiler` owns completed
snapshots and recording lifecycle. `ProfilerPanel` owns view state; its timeline,
chart and hierarchy implementation lives in `ProfilerTimeline.cpp`. CPU category
costs are calculated once from exclusive sample times at capture submission.

Instrumentation covers the editor loop, scene phases, individual component and
managed script update/fixed/late calls, animation, viewport rendering, legacy
render passes, RHI translation/setup/recording, shadows, geometry, post-processing,
Vulkan frame/presentation fence waits and swapchain acquisition, editor panels,
presentation, and event polling. It does not instrument arbitrary managed method
bodies or worker threads. A distinct render-thread lane is not shown because the
current instrumentation does not record one.

Each frame retains at most 4096 CPU samples, with labels/context capped at 160
bytes. Omitted samples are counted and displayed; their work remains included
in recorded ancestors. A conservative 64 MiB trace-storage budget evicts older frames as needed.
A single trace larger than the entire budget is skipped without stopping recording
or discarding the existing history. The budget
covers trace storage; existing aggregate metric storage is separately bounded by
capture frame count. The first frame of a recording initiated mid-frame has
aggregate metrics but no CPU trace. Tracing begins at the next frame boundary.

Timings are inclusive unless labeled self. Do not add parent and child totals.
Uninstrumented time stays in the parent's self time (or Other). Tiny differences
between trace and frame clock boundaries are normalized in the stacked chart.
Recorded GPU observations and viewport aggregation retain their backend semantics.
Prefab latest/session-event statistics remain live-only, rather than being
misrepresented as per-frame events. Capture overhead affects measured throughput;
use the optimized profiling build for representative performance measurements.

The timeline uses ImGui drawing and input, with actual timestamped intervals;
there is no additional ImSequencer dependency.

## Validation

- `PlutoGECpuTraceTests`: timestamps, parentage, exclusive times, exception unwinding,
  recorder attachment, disabled scopes, bounded labels and overflow.
- `PlutoGEEditorProfilerTests`: recording lifecycle, frame/memory limits, owned
  snapshots, exclusive chart categories, invalid inputs, and historical reports.
- `PlutoGEEditorProfilerPanelTests`: renders the real panel in a hidden OpenGL
  window at narrow and wide sizes, including an empty capture. Returns CTest skip
  code 77 if no OpenGL display is available. An optional output path writes a PPM
  screenshot of its synthetic fixture for visual QA without loading a project.
  A second optional argument selects `timeline`, `hierarchy`, `search`, `focus`,
  or `narrow` for repeatable visual checks.
