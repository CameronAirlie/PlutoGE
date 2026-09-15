# Capturing complete metrics and comparing geometry costs

The debugger may remain attached. Keep the same executable, debugger setup, camera, viewport dimensions and quality settings across comparisons.

## Copy multiple frames

1. Open the Profiler panel and set **History** to the number of frames to retain, for example **600** (about ten seconds at 60 FPS).
2. Press **Record**. The history is a rolling window; once full, it replaces the oldest frames. The existing trace-memory budget may retain fewer frames for very large traces, which the panel and report identify.
3. Press **Stop** when the desired interval has been captured.
4. Press **Copy all captured metrics** and paste the result into a text file or conversation.

The report contains a CPU/GPU timing summary followed by every retained frame's metrics, RmlUi measurements and all recorded CPU trace samples, including context and parent information. Single-frame **Copy metrics** remains available and keeps its short trace listing. Copying does not clear the history, change the selected frame or stop an active recording. Stopping first avoids recording the export operation itself in a later frame.

Summary percentiles use nearest rank. GPU summaries describe asynchronous observations, not unique GPU frames aligned to the CPU captures. Missing GPU results are excluded from the summary. Parent scopes include child work and must not be summed with their children. Captures mixing diagnostic modes or resolutions should be split for comparisons; each frame records its own mode and dimensions.

## Geometry comparisons

Expand **Geometry diagnostics** in the Profiler and choose **Comparison mode**:

- **Normal rendering**: production rendering with precomputed sky samples.
- **Original sky evaluation**: use the original per-fragment diffuse-sky calculation and skip the sky-sample generation pass. Shadow rendering is unchanged.
- **Bypass directional shadow sampling**: keep shadow-page production active but bypass directional surface shadow sampling. This intentionally changes appearance to measure the receiver-shading cost. It does not disable point-light shadows or volumetric shadow sampling.

These modes apply only to the editor viewport, are not saved in project/scene quality settings, and are labelled in each captured report. Return to **Normal rendering** after comparing. An active-mode indicator remains visible in the Profiler when either diagnostic is selected.

For each mode, use the same view and animation segment, allow temporal history and asynchronous GPU observations to settle, then record and copy a separate capture. Compare geometry timings first: changes to shadows can also affect downstream screen-space effects. A difference in total frame time alone does not isolate surface shading.

## Additional metrics

- Actual internal rendering dimensions and output dimensions.
- Submitted triangle counts including instances, split into opaque, alpha-tested, transparent and outline draws. These are submission counts, not visible-pixel or overdraw counts.
- `RHI Geometry / Opaque and alpha-tested` and `RHI Geometry / Outlines` GPU timings nested inside the existing geometry scope. Transparent rendering remains outside that scope.

These diagnostics do not yet measure fragment invocations or per-material GPU costs.

## Validation

The RelWithDebInfo editor and affected tests built successfully. All 14 relevant checks passed after fixing the report's debugger-status omission when OS CPU accounting is unavailable: the two profiler tests and Vulkan/OpenGL RHI, sky, VSM-only, instancing and triangle-count, outline and temporal-motion checks. Tests cover exporting the complete retained history beyond 20 trace samples, summary calculations, excluding pending GPU observations, clipboard output, and restoring normal rendering after diagnostic modes.

Rebuilt editor: `out/build/msvc-nvidia/editor/RelWithDebInfo/PlutoGEEditor.exe`.
Build logs: `out/build/profiler-diagnostics-build.log`, `out/build/profiler-diagnostics-final-build.log`.
Test logs: `out/build/profiler-diagnostics-tests.log` (initial run, including the corrected export failure), `out/build/profiler-diagnostics-final-tests.log` (profiler rerun).
