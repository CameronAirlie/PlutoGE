# RHI Conventions

These conventions are part of the public rendering contract and apply to every backend.

- World and view space are right-handed.
- Clip-space depth is `0..1` and uses reversed Z: the near plane maps to `1`, the far plane maps to `0`, depth clears to `0`, and the normal opaque depth comparison is greater-or-equal.
- Matrices are column-major and vectors are multiplied on the right (`matrix * vector`), matching GLM's default layout.
- Front faces use counter-clockwise winding after projection. Texture coordinates use a top-left logical origin; a backend is responsible for any framebuffer-origin conversion.
- Linear render targets use `R8G8B8A8Unorm`; display and colour textures use `R8G8B8A8Srgb`; the initial depth format is `D32Float`.
- Shader parameter spaces are stable: space 0 is frame/camera, space 1 is material, space 2 is object/draw, and space 3 is pass-local.
- A graphics API change is applied on the next editor or runtime start. Live backend replacement is not supported.
- Resource bindings are per slot and resolved at draw time: binding with no pipeline bound is valid, and bindings survive pipeline changes. A draw requires a pipeline.
- A context may hand out parallel recording contexts (`BeginParallelRecording`); their commands execute in order and see the resource state earlier ones leave. Device calls that record commands (buffer uploads) go to the context bound on the calling thread. See [parallel-recording.md](parallel-recording.md).
- `DrawIndexedInstanced` takes a first instance, offsetting the instance index shaders see; nonzero values require `SupportsFirstInstance()`.
- `PipelineBarrier(before, after)` orders buffer and same-layout image accesses by stage and kind (`BarrierAccess`); `before` must cover every earlier conflicting access. `ShaderMemoryBarrier()` remains the conservative all-work barrier. Image layout transitions stay implicit: binds and attachments transition images, ordered after the stages that last accessed them.
- Recorded buffer updates (`UpdateBuffer` on storage, vertex and index buffers while a frame records) are batched: consecutive updates record together before the next command that could read them, in call order.
- Bindless textures occupy descriptor set `BindlessTextureSet` (4) as a runtime `Sampler2D[]`; pipelines opt in with `GraphicsPipelineDescriptor::bindlessTextures`. Only initialized, sampled-only textures can be registered, and every slot must be released before its texture is destroyed. Devices without the capability report `BindlessTextureCapacity() == 0`.
- `BeginAsyncCompute` returns a context for compute work on buffers that runs beside graphics (or this context when the device has no async queue); `EndAsyncCompute(consumers)` makes later work wait for it at the consumers' stages. Use `RenderGraph` rather than calling these directly.

Public RHI headers must not include GLAD, Vulkan headers, or expose backend-native handles. Backend implementations translate opaque generational handles through private resource registries.
