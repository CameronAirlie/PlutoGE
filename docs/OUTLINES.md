# Outlines

`OutlineComponent` draws a screen-space outline around everything its entity renders and, by default, everything its
children render. Use it to show the player what they are about to interact with, mark an objective, or show a target
they have picked. Built games and both editor viewports draw it. Scripts add it, restyle it and switch it on and off
while the game runs.

## Properties

| Property | Meaning |
| --- | --- |
| `Color` | Linear RGB and opacity. |
| `Width` | Pixels on a 1080-line output, 0.5 to 6. Other output heights scale it (4 px at 2160 lines), so an outline looks the same at any resolution. The pass samples at most 6 pixels out. |
| `Hidden Opacity` | Opacity multiplier for the parts of the outline that other geometry hides. 0 never shows the object through walls; the editor's selection uses 0.3. |
| `Include Children` | On by default: also outlines the entity's active descendants (a pickup's `Visual` child, a character's model). |

Add it in the inspector under **Rendering/Outline**, or from a script:

```csharp
var outline = target.GetComponent<OutlineComponent>() ?? target.AddComponent<OutlineComponent>();
outline.Color = new Vector4(1.0f, 0.82f, 0.36f, 0.9f);
outline.Width = 2.0f;
outline.Enabled = true;   // later: outline.Enabled = false;
```

Switching an existing outline on and off with `Enabled` is cheaper than adding and removing the component. The scene
caches its outline components, and adding or removing one rebuilds that cache on the next frame.

## How it is drawn

Every outline keeps its own style. Each frame, `render::BuildSceneOutline` collects the entities whose enabled outline
sits on an active entity, plus their active descendants, into **layers**: one `render::OutlineLayer` per distinct style
(colour, scaled width, hidden opacity), in hierarchy order. Outlines with the same style share a layer. `RhiRenderService`
(built games) and `EditorSceneRenderService` (editor views) pass the layers to `RhiSceneRenderer::SetOutlines`.

`OutlinePass` (`engine/render/src/OutlinePass.*`, shader `SelectionOutline.slang`) draws them. Each layer's commands
are rasterized depth-only, without temporal jitter, into that layer's mask. After tone mapping and upscaling, every layer
is composited onto the final image in order, so a later layer draws over an earlier one where their lines meet. It works
with skinned, instanced and alpha-tested meshes. A layer that covers nothing on screen costs nothing.

In the editor, the selection is one more layer, drawn last: it sits on top of the scene's outlines instead of replacing
them. Debug views draw no outline.

### Extending it

- **A new source of outlines** (a quest marker system, a debug tool) builds `OutlineLayer`s and appends them before
  `SetOutlines`, as the editor does with its selection. Nothing in the renderer changes.
- **A new look** (pulsing, dashed, a fill) belongs in `OutlineStyle` and `SelectionOutline.slang`'s parameters, written
  by `OutlinePass::Composite`. Callers that do not set it keep the current look.

## Limits

- **Four styles per frame** (`render::MaxOutlineLayers`). Each distinct style costs a depth-only pass over its own
  geometry, a depth mask at render resolution and a full-screen composite. Past the limit, further outlines take the
  last layer's style. In the editor the selection uses one of the four.
- Outlines follow render commands. An entity that renders nothing, such as an empty root, has nothing to outline
  unless `Include Children` reaches its meshes.
- Camera-stack overlay layers (weapon cameras) are not outlined.

## Tests

- `PlutoGESceneOutlineTests` checks value clamping and round trips, children and inactive entities, disabled and
  removed outlines, one layer per style, shared layers for equal styles, the layer budget, and width scaling.
- `PlutoGEVulkanScreenOutlineTests` / `PlutoGEOpenGLScreenOutlineTests` render the pass: a visible outline at hidden
  opacity 0, none through a wall, and two styles in one frame.
- `PlutoGEVulkanSceneOutlineRenderingTests` / `PlutoGEOpenGLSceneOutlineRenderingTests` render two entities in two
  styles through `RhiSceneRenderer`, with temporal upscaling, occlusion culling and GPU instance culling in every
  combination, and check that an empty set turns outlines off.
- `PlutoGEComponentRegistryTests` checks the type is registered.
