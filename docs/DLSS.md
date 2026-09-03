# Vulkan DLSS

PlutoGE integrates DLSS Super Resolution through NVIDIA Streamline. The public
renderer-facing API is vendor-neutral (`TemporalUpscalerOptions` and
`TemporalUpscalerFrame`); Streamline and Vulkan handles remain inside the
Vulkan backend. Builds without Streamline, unsupported adapters, and missing or
invalid runtime modules continue through the existing full-resolution/TAA path.

## SDK setup

1. Register the application with NVIDIA and obtain its project GUID.
2. Download an official Streamline SDK release. Do not use unsigned or renamed
   runtime modules: PlutoGE verifies `sl.interposer.dll` before loading it.
3. Configure PlutoGE with:

   ```powershell
   cmake -S . -B build `
     -DPLUTO_ENABLE_STREAMLINE=ON `
     -DPLUTO_STREAMLINE_SDK_ROOT=C:/SDKs/Streamline `
     -DPLUTO_STREAMLINE_PROJECT_ID=<registered-project-guid>
   ```

The SDK root must contain `include/sl.h`. A distributable SDK release should
also contain its signed DLLs under `bin/x64`; CMake copies these beside the
editor and runtime executables and includes them in installation packages.
DLSS Super Resolution needs at least `sl.interposer.dll`, `sl.common.dll`,
`sl.dlss.dll`, and `nvngx_dlss.dll`. Keep NVIDIA's accompanying license files
with distributions.

The Windows presets keep this opt-in: `gcc` is the FSR2-only configuration and
`gcc-nvidia` enables Streamline as well. PlutoGE disables Streamline's OTA
plug-in loading so only the signed modules selected and staged by the build are
loaded.

## Project configuration

Select **DLSS** under Project Settings > Runtime Upscaler and choose one of
Performance, Balanced, Quality, Ultra Performance, or DLAA. The project file
stores these as:

```text
RUNTIME_UPSCALER	DLSS
RUNTIME_UPSCALER_QUALITY	Quality
```

DLSS currently applies to Vulkan runtime rendering only. At startup, support is
checked against the selected adapter and driver. Quality mode determines the
internal render resolution using Streamline's optimal-settings query; UI and
presentation remain at output resolution.

## Frame contract

The scene renderer supplies jittered HDR input color, non-jittered row-major
camera matrices, D32 depth, dense signed RG32 motion vectors in normalized UV
units, and a full-resolution RGBA16F output. DLSS runs at the temporal-resolve
stage before output-resolution post-processing and replaces the built-in TAA
only after a successful evaluation. Resolution changes explicitly reset
temporal history.

## Future features

The device-owned Streamline lifetime, feature-requirement negotiation, frame
tokens, and generic temporal frame description are intended to be reused for
Ray Reconstruction and Frame Generation. Those features still require separate
work: ray-tracing acceleration structures and ray-query/pipeline support,
additional denoiser inputs for Ray Reconstruction, and swapchain/present plus
optical-flow queue integration for Frame Generation. They should be added as
new capability adapters rather than extending the DLSS Super Resolution call
with feature-specific state.
