Target Architecture
Scene / Renderer / Render Passes
             |
        RenderDevice
             |
      CommandContext
       /           \
OpenGLDevice     VulkanDevice
       \           /
       Slang shader assets
    GLSL          SPIR-V
Engine code must stop seeing GLuint, GLenum, Vulkan handles, descriptor sets, framebuffers, or API-specific synchronization.
Phase 1: Lock Conventions
1. Define the engine’s graphics API enum: OpenGL, Vulkan.
2. Add graphicsApi to ProjectManifest, defaulting existing projects to OpenGL.
3. Decide that changing the API requires restarting the editor/runtime.
4. Standardize coordinate conventions:
   - Right-handed world space.
   - Depth range 0..1.
   - Reversed-Z.
   - Consistent framebuffer origin and winding.
5. Use glClipControl(..., GL_ZERO_TO_ONE) for OpenGL where supported so projection matrices can match Vulkan.
6. Document texture formats, color spaces, binding conventions, and matrix layout before writing either backend.
Acceptance: manifest round-trip test and backend selection unit tests.
Phase 2: Introduce the RHI Types
Create engine/render/rhi with backend-neutral definitions:
- GraphicsApi
- Format
- BufferUsage
- TextureUsage
- ShaderStage
- PrimitiveTopology
- CullMode
- CompareOperation
- BlendState
- Viewport
- Scissor
- BufferHandle
- TextureHandle
- SamplerHandle
- PipelineHandle
- RenderPassHandle
Use opaque generational handles rather than exposing backend pointers or integers.
Define these initial interfaces:
class IRenderDevice;
class ICommandContext;
class ISwapchain;
class IShaderCompiler;
Keep the first API intentionally small: buffer and texture creation, pipeline creation, begin/end rendering, resource binding, indexed drawing, viewport/scissor, and presentation.
Acceptance: the RHI public headers compile without including GLAD or Vulkan.
Phase 3: Resource Ownership
1. Make IRenderDevice own all GPU resources.
2. Add RAII wrappers such as Buffer, Texture, Sampler, and GraphicsPipeline.
3. Store backend objects in private OpenGL/Vulkan registries indexed by opaque handles.
4. Define explicit destruction and deferred destruction semantics.
5. Add debug names to every resource descriptor.
6. Add validation for stale handles, invalid formats, and unsupported usage combinations.
Do not migrate existing Texture, Mesh, or RenderTarget yet. The RHI resources initially live alongside them.
Acceptance: create/destroy resource tests with no leaked GL/Vulkan objects.
Phase 4: Slang Toolchain
1. Add a pinned Slang release through CMake.
2. Create a PlutoGEShaderCompiler library.
3. Compile .slang files offline during the asset/build pipeline.
4. Emit:
   - GLSL for OpenGL.
   - SPIR-V for Vulkan.
   - Reflection metadata shared by both.
5. Use column-major matrix layout to match GLM.
6. Define stable parameter spaces:
   - Space 0: frame and camera.
   - Space 1: material.
   - Space 2: object/draw.
   - Space 3: pass-local resources.
7. Cache compiled artifacts using source, include, compiler-version, target, and options hashes.
8. Report Slang diagnostics through the editor console.
Start with one BasicLit.slang containing vertex and fragment entry points.
Acceptance: one test compiles the shader to GLSL and SPIR-V and verifies reflected bindings match.
Phase 5: OpenGL RHI Backend
Implement the small RHI API over OpenGL:
1. OpenGL device and capability discovery.
2. Buffer, texture, sampler, and pipeline registries.
3. GLSL shader compilation and linking.
4. Vertex/index buffer binding.
5. Uniform-buffer and texture binding from reflected slots.
6. Render-target and default-framebuffer rendering.
7. State caching inside OpenGLCommandContext.
8. Swapchain presentation through the existing GLFW window.
9. Debug output through KHR_debug.
The RHI should be explicit and Vulkan-shaped; OpenGL emulates those semantics. Do not design the abstraction around global GL state.
Acceptance: clear the window and draw one indexed colored triangle through the RHI.
Phase 6: Minimal RHI Renderer
Create a separate BasicRenderer using only RHI interfaces.
Initial feature scope:
- Perspective camera.
- Static mesh vertex/index buffers.
- One texture.
- One directional light.
- Depth testing.
- Opaque objects.
- Window resize.
- No shadows, particles, transparency, post-processing, terrain, ocean, GI, or upscaling.
Add a small deterministic test scene containing a cube, plane, camera, light, and texture.
Acceptance: the scene renders correctly through the OpenGL RHI without calling GL from BasicRenderer.
Phase 7: Vulkan Backend
Recommended dependencies:
- Vulkan SDK headers and loader.
- Volk for function loading.
- Vulkan Memory Allocator for allocation.
- GLFW surface creation.
Implement:
1. Instance, validation layers, debug messenger.
2. Physical-device selection and capability reporting.
3. Logical device and graphics/present queues.
4. Swapchain and resize/recreation.
5. Command pools and per-frame command buffers.
6. Fences and semaphores for two or three frames in flight.
7. VMA-backed buffers and images.
8. Image views and samplers.
9. Descriptor-set layouts generated from Slang reflection.
10. Graphics pipelines using Slang SPIR-V.
11. Explicit image layout transitions.
12. Deferred resource destruction after GPU completion.
Acceptance: the same BasicRenderer and scene render without backend-specific branches.
Phase 8: Project Dropdown
Add Graphics API to Project Settings:
- OpenGL
- Vulkan
Behavior:
1. Persist as GRAPHICS_API OpenGL|Vulkan.
2. Display backend availability and unsupported-driver errors.
3. Require restart after changing it.
4. Pass the choice into EngineConfig.
5. Create the correct window/context:
   - OpenGL: GLFW OpenGL context.
   - Vulkan: GLFW_NO_API plus Vulkan surface.
6. Instantiate the selected IRenderDevice.
7. Fall back to OpenGL only after showing a clear Vulkan initialization error.
During migration, retain a hidden developer option for Legacy OpenGL versus RHI OpenGL. Remove it once parity is reached.
Phase 9: Cross-Backend Test
Automate a rendering test for each backend:
1. Open a hidden 256x256 window.
2. Render the deterministic basic scene.
3. Read back the final color buffer.
4. Verify it is nonblank.
5. Compare against a reference using tolerance rather than exact pixels.
6. Validate resize from 256x256 to 400x240.
7. Run with Vulkan validation layers enabled.
8. Fail on GL debug errors, Vulkan validation errors, shader mismatch, or leaked resources.
This is the first major checkpoint. Stop here and assess architecture before migrating complex effects.
Later Migration Order
After the basic test passes on both APIs, migrate in this order:
1. Material and texture asset upload.
2. Existing mesh batching and instancing.
3. G-buffer geometry.
4. Deferred lighting.
5. Transparent rendering.
6. Shadow maps.
7. Runtime UI, RmlUi, then ImGui backend.
8. Simple post-processing and tone mapping.
9. TAA and temporal upscaling.
10. Particles and compute workloads.
11. Clouds, ocean, terrain.
12. SSAO, SSR, GI, VCT, RSM, and other complex effects.
13. Profiling and debug views.
14. Remove GL types from scene/editor modules.
15. Delete the legacy renderer and direct GL code.
First Implementation Slice
The first development milestone should include only:
- Manifest graphics API setting.
- Backend-neutral RHI headers.
- Slang compiler integration.
- OpenGL RHI.
- Vulkan RHI.
- BasicRenderer.
- Textured cube scene.
- Automated cross-backend screenshot test.