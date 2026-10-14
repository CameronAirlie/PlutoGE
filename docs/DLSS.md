# Vulkan DLSS

PlutoGE integrates DLSS Super Resolution through NVIDIA Streamline. The public
renderer-facing API is vendor-neutral (`TemporalUpscalerOptions` and
`TemporalUpscalerFrame`); Streamline and Vulkan handles remain inside the
Vulkan backend. Builds without Streamline, unsupported adapters, and missing or
invalid runtime modules continue through the existing full-resolution/TAA path.

## Build setup

Every Windows build compiles DLSS in; no manual SDK setup is required.
`PLUTO_ENABLE_STREAMLINE` defaults to `ON` on Windows (and the `msvc17`/`msvc18`
presets set it explicitly). Streamline is Windows-only, so Linux builds compile
without it and report DLSS as unavailable.

When `PLUTO_STREAMLINE_SDK_ROOT` is empty, CMake downloads the pinned official
Streamline SDK release (v2.12.0, SHA-256 verified) into the build tree's
`_deps` directory. To use an existing extracted SDK instead:

```powershell
cmake --preset msvc18 -DPLUTO_STREAMLINE_SDK_ROOT=C:/SDKs/Streamline-2.12.0
```

The SDK root must contain `include/sl.h` and its signed DLLs under `bin/x64`.
CMake copies these beside the editor and runtime executables and includes them
in installation packages. DLSS Super Resolution needs `sl.interposer.dll`,
`sl.common.dll`, `sl.dlss.dll`, and `nvngx_dlss.dll`. Keep NVIDIA's
accompanying license files with distributions. Do not use unsigned or renamed
runtime modules: PlutoGE verifies `sl.interposer.dll` before loading it.

`PLUTO_STREAMLINE_PROJECT_ID` defaults to PlutoGE's registered NVIDIA project
GUID; override it only for a separately registered application. Configure with
`-DPLUTO_ENABLE_STREAMLINE=OFF` to build without DLSS entirely.

PlutoGE disables Streamline's OTA plug-in loading so only the signed modules
selected and staged by the build are loaded.

## Runtime availability

DLSS is used only when the device supports it. At Vulkan device creation
PlutoGE loads Streamline and checks the adapter, driver, and required Vulkan
extensions. If any check fails (non-NVIDIA or older GPU, outdated driver,
missing modules, OpenGL backend, remote session with reduced extensions), DLSS
stays disabled: rendering continues at full resolution with the built-in TAA,
the viewport Quality popup shows DLSS as **Fallback** with the reason, and the
reason is written to `PlutoGEEditor.log`. Nothing else changes, so a project can
select DLSS and still run on any hardware. Set `PLUTOGE_DISABLE_STREAMLINE=1`
to skip loading Streamline for diagnostics.

## Project configuration

Select **DLSS** under Project Settings > Runtime Upscaler and choose one of
Performance, Balanced, Quality, Ultra Performance, or DLAA. The project file
stores these as:

```text
RUNTIME_UPSCALER	DLSS
RUNTIME_UPSCALER_QUALITY	Quality
```

DLSS currently applies to Vulkan rendering only; see
[Runtime availability](#runtime-availability) for the fallback behavior.
Required device extensions are checked against Vulkan enumeration because
remote sessions can expose fewer extensions even when NVIDIA reports DLSS
support. Quality mode determines the
internal render resolution using Streamline's optimal-settings query; UI and
presentation remain at output resolution.

## Frame contract

The scene renderer supplies jittered HDR input color, non-jittered row-major
camera matrices, D32 depth, dense signed RG32 motion vectors in normalized UV
units, and a full-resolution RGBA16F output. DLSS runs at the temporal-resolve
stage before output-resolution post-processing and replaces the built-in TAA
only after a successful evaluation. Resolution changes explicitly reset
temporal history.

The motion buffer stores `(currentNdc - previousNdc) * 0.5` with Y pointing
up. The Vulkan DLSS adapter applies a `{-1, +1}` scale to obtain
previous-minus-current motion in Y-down texture coordinates. Passing the
buffer unchanged reverses horizontal history reprojection and causes trails
during motion. FSR2 uses the same sign conversion, multiplied by render size
because its API expects pixel units.

## Future features

The device-owned Streamline lifetime, feature-requirement negotiation, frame
tokens, and generic temporal frame description are intended to be reused for
Ray Reconstruction and Frame Generation. Those features still require separate
work: ray-tracing acceleration structures and ray-query/pipeline support,
additional denoiser inputs for Ray Reconstruction, and swapchain/present plus
optical-flow queue integration for Frame Generation. They should be added as
new capability adapters rather than extending the DLSS Super Resolution call
with feature-specific state.

## Orthographic cameras

Orthographic cameras now pass the true projection and inverse projection along
with Streamline's `orthographicProjection` flag. Switching between orthographic
and perspective resets temporal history. Runtime module verification and adapter
support requirements still apply; unsupported DLSS installations report fallback
independently of the projection type.
