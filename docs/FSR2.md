# Vulkan AMD FidelityFX Super Resolution 2

PlutoGE integrates AMD FidelityFX Super Resolution 2.2.1 as a vendor-neutral
Vulkan temporal upscaler. It works on supported AMD, NVIDIA, and Intel Vulkan
hardware; it does not require an RTX GPU.

## Build setup

Enable the pinned official AMD SDK integration with:

```powershell
cmake --preset gcc
cmake --build --preset gcc --target PlutoGEEditor
```

The `gcc` preset enables FSR2 and leaves NVIDIA Streamline disabled. Use the
separate `gcc-nvidia` preset only on a system where DLSS development is needed.

The Windows build generates AMD's Vulkan shader permutations and links the
FSR2 implementation statically. No FSR runtime DLL needs to ship beside the
application. AMD's MIT license remains in the fetched SDK source and must be
included when redistributing the SDK-derived binary.

## Project configuration

In **File > Project Settings**, select **Vulkan** as the Graphics API, choose
**FSR 2** under **Viewport / Runtime Upscaler**, select a quality mode, and
save. The setting applies to the Editor Viewport, Game Viewport, and standalone
runtime. Each viewport shows an `FSR 2 active` badge over the rendered image;
the **Quality** popup shows its internal and output resolutions or a fallback
reason. The project file stores the selection as:

```text
RUNTIME_UPSCALER	FSR2
RUNTIME_UPSCALER_QUALITY	Quality
```

Old `RUNTIME_DLSS_QUALITY` project entries remain readable for compatibility.
For docked editor viewports, use Quality or Balanced. Ultra Performance renders
at one third resolution on each axis and is intended for very high-resolution
output, so it is a poor preview mode for a small panel. PlutoGE preserves a
640x360 minimum temporal input for small viewports while retaining the selected
quality ratio at normal game resolutions. This prevents a docked Quality view
from discarding most of its source detail before temporal reconstruction; RCAS
sharpness cannot recover detail which was never rendered.

## Frame contract

FSR2 consumes the renderer's jittered HDR color, D32 inverted depth, RG32
normalized unjittered motion vectors, reset state, pre-exposure, render dimensions, and
full-resolution RGBA16F output. The Vulkan adapter converts PlutoGE's motion
and jitter conventions to AMD's expected coordinate system and recreates its
device-owned context after a resolution or initialization-option change.

FSR2 RCAS sharpening uses `RUNTIME_UPSCALE_SHARPNESS`. The renderer also applies
the temporal mip bias `log2(render width / output width) - 1` through its
backend-neutral material sampler, keeping texture detail available at reduced
internal resolutions.

The initial integration does not yet provide optional reactive or transparency
and composition masks. FSR2 remains functional without them, but adding those
masks will improve reconstruction around translucent particles, water, and
other rapidly changing materials.
