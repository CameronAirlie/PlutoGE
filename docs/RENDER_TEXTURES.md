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
- TAA resolves both colour and geometry coverage. Transparent backgrounds keep
  fractional alpha at silhouette edges when the final texture is published.
- A material samples a render texture exactly like a screenshot of the
  camera's view imported as an image. The texture holds the camera's final,
  tone-mapped colour, so slots that read colour (albedo, emission) look right.
  Data slots (metallic, roughness, normal) read the same colour, which is
  rarely useful.
- If a render texture's asset file is missing, its camera stays assigned but
  idle. It never takes over the screen.
- In the editor, render textures update in both the Scene and Game views.

## Showing a render texture in UI

RML documents can show a render texture wherever they can show an image. Give
the asset's path in `src`, relative to the document like any other image:

```html
<img id="minimap" src="../Textures/Minimap.plutorendertexture"/>
```

```css
#minimap { width: 256px; height: 256px; border-radius: 128px; }
.monitor { decorator: image(../Textures/Monitor.plutorendertexture); }
```

- The image updates every frame, and its colours match how the camera looks
  on screen. It shows nothing until the camera renders its first frame.
- The camera must still have this asset as its **Target Texture**; the UI
  only displays the texture.
- Give the element an explicit size in RCSS. The texture's natural size is
  read when the document loads, so later changes to **Texture Size** do not
  resize elements that rely on it.
- This needs the RHI UI renderer; the legacy OpenGL UI renderer leaves the
  image blank.

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
