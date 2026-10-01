# Render textures

A render texture is a texture that a camera draws into every frame. Use one
on a material to make security monitors, mirrors, portals, minimaps or
in-world screens. This works like Unity's RenderTexture.

## Setup

1. In the Content Browser, choose **Create > Render Texture**. Give it a
   name and a size. The asset is saved as a `.plutorendertexture` file in
   `Textures/`.
2. Add a camera where the view should come from. In its Camera component,
   set **Target Texture** to the new asset. You can change **Texture Size**
   there too. The size belongs to the asset, so the change applies to
   everything that uses it.
3. Open a material and assign the render texture to a texture slot, the same
   way you would assign an image. Drag and drop works too. For a glowing
   screen, use the **Emission** texture with an emission colour of white.

## How it behaves

- A camera with a target texture renders only into that texture. It is never
  used as the main camera, even if it is marked as main, and it is never
  drawn as an overlay.
- Render-texture cameras draw before the screen cameras each frame, so
  materials show the current frame. They use their own post-processing,
  lighting, sky and tag filters, just like a normal camera.
- A material samples a render texture exactly like a screenshot of the
  camera's view imported as an image. The texture holds the camera's final,
  tone-mapped colour, so slots that read colour (albedo, emission) look right.
  Data slots (metallic, roughness, normal) read the same colour, which is
  rarely useful.
- If a render texture's asset file is missing, its camera stays assigned but
  idle. It never takes over the screen.
- In the editor, render textures update in both the Scene and Game views.

## Limitations

- Render textures have no mipmaps, so a small, distant screen can shimmer.
  Use a size close to the screen's on-screen size.
- When one render texture shows another, or shows itself, it displays the
  previous frame's image for that texture.
- Each render texture has its own renderer, which costs one extra scene render
  per frame plus memory for its own render targets. Render-texture cameras use
  cascaded shadow maps instead of virtual shadow maps to keep memory down.
- Render textures need the RHI renderer (Vulkan, or the editor's RHI preview).
  The legacy OpenGL runtime path leaves them blank.
- Target textures cannot yet be assigned from C# scripts. To pause an
  offscreen camera, set the camera component's `Enabled` property; the
  texture then keeps its last image.
