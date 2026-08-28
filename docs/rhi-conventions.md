# RHI Conventions

These conventions are part of the public rendering contract and apply to every backend.

- World and view space are right-handed.
- Clip-space depth is `0..1` and uses reversed Z: the near plane maps to `1`, the far plane maps to `0`, depth clears to `0`, and the normal opaque depth comparison is greater-or-equal.
- Matrices are column-major and vectors are multiplied on the right (`matrix * vector`), matching GLM's default layout.
- Front faces use counter-clockwise winding after projection. Texture coordinates use a top-left logical origin; a backend is responsible for any framebuffer-origin conversion.
- Linear render targets use `R8G8B8A8Unorm`; display and colour textures use `R8G8B8A8Srgb`; the initial depth format is `D32Float`.
- Shader parameter spaces are stable: space 0 is frame/camera, space 1 is material, space 2 is object/draw, and space 3 is pass-local.
- A graphics API change is applied on the next editor or runtime start. Live backend replacement is not supported.

Public RHI headers must not include GLAD, Vulkan headers, or expose backend-native handles. Backend implementations translate opaque generational handles through private resource registries.
