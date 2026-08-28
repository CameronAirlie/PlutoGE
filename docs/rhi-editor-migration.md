# Editor Vulkan Migration

## Goal

Run the editor, scene renderer, and ImGui on the project-selected RHI backend without exposing OpenGL or Vulkan objects to scene and render-pass code.

## Current bridge

The project manifest selects the viewport RHI. Vulkan renders scene geometry off-screen through `BasicRenderer`, reads RGBA pixels back, and uploads them to the existing OpenGL ImGui texture. This keeps Vulkan output testable while the editor shell remains OpenGL-owned.

## Migration stages

1. **Backend ownership**
   - Pass `ProjectManifest::graphicsApi` into editor viewport creation.
   - Move device creation into an editor render-service/factory owned above panels.
   - Panels receive backend-neutral texture/view handles.
   - Acceptance: changing Graphics API and restarting selects the requested RHI with a clear fallback error.

2. **Native Vulkan window and swapchain**
   - Create the GLFW window with `GLFW_NO_API` when Vulkan is selected.
   - Add surface selection, present queue, swapchain recreation, and frames in flight.
   - Acceptance: clear and present the main editor window without an OpenGL context.

3. **ImGui Vulkan backend**
   - Make `PanelManager` initialize either `imgui_impl_opengl3` or `imgui_impl_vulkan` behind one interface.
   - Register RHI viewport textures through a backend-neutral ImGui texture registry.
   - Remove CPU readback/upload from `ViewportPanel`.
   - Acceptance: Vulkan scene image is sampled directly by ImGui with no GPU-to-CPU transfer.

4. **Renderer service migration**
   - Move mesh/material upload caches out of `ViewportPanel` into render-owned asset caches.
   - Submit backend-neutral scene draw packets.
   - Port geometry, lighting, transparency, shadows, and post-processing in that order.
   - Acceptance: editor and runtime share the same renderer and resource lifetime model.

5. **OpenGL isolation and removal**
   - Remove GL types from `Mesh`, `Texture`, `RenderTarget`, editor panels, and scene code.
   - Keep OpenGL only inside `rhi/opengl` until parity is proven, then decide whether to retain it.

## Immediate next seams

- Introduce an editor-owned `RenderService` containing `IRenderDevice`, renderer, upload caches, and presentation integration.
- Extend `WindowConfig` with the requested graphics API and make context creation conditional.
- Add Vulkan surface/swapchain interfaces without changing scene submission.
