# Building toon shaders

Shader graphs can replace direct lighting using ordinary nodes. Connect a colour to the **Direct Lighting** pin on the Output node; leave it disconnected to use standard PBR. Keep **Unlit surface** disabled. The graph is evaluated for each active directional/point light, and the renderer adds those contributions together. Ambient/environment lighting and emission are added separately.

This runs in the OpenGL and Vulkan **RHI** renderers, including the material editor's live preview. The legacy deferred renderer keeps PBR surface lighting; it does not evaluate the Direct Lighting branch. Glass keeps its physical lighting/transmission model. Shadow casting, voxelization and baking use the surface outputs, not the custom light response.

## Lighting inputs

Add these from the **Lighting** node menu:

| Node | Output |
| --- | --- |
| Light Direction | Normalized world-space direction from the surface towards this light. |
| Light Color | Linear RGB light colour multiplied by its intensity. |
| Light Attenuation | Distance/range falloff; 1 for the directional light. |
| Shadow Attenuation | Filtered visibility: 0 in shadow, 1 when lit. |

Light inputs may only feed **Direct Lighting**, including through subgraphs. Connecting them to Albedo, Emission, Opacity or Vertex Offset is a validation error. The renderer supplies the existing scene shadow result, including point shadows. It does not multiply your result by N·L, colour, falloff or visibility again: your graph controls each of those operations.

**Material Input: Normal** includes the material normal map. **World Normal** is the geometric normal. If you construct a normal in the graph, share that branch between Output Normal and your lighting calculation. Material inputs in the lighting branch are the original surface inputs, so sharing a tinted or textured branch does not apply it twice.

## Build a shader from scratch

1. Create a graph and keep its Albedo, Normal and other surface connections. These still define the surface for ambient lighting, shadows and GI.
2. Connect **Material Input: Normal** and **Light Direction** to a **Dot** node. Connect Dot to **Clamp**, with Min 0 and Max 1.
3. Multiply that facing value by **Shadow Attenuation**. This lets cast shadows enter the same colour bands as surface shading. Alternatively multiply shadow visibility after your ramp for ordinary dark shadows.
4. Make two **Step** nodes with Edge values 0.3 and 0.7, feeding the shadowed facing value to both Value pins.
5. Use the first Step as the T input of a **Lerp** between shadow and midtone Vec3 colours. Use the second Step as T of another Lerp between that result and a highlight Vec3 colour.
6. Use **Material Input: Color** for the material base colour already multiplied by its albedo texture. Split it into RGB with a Color node and the component pins of a Vec3 node, then multiply by an optional Vec3 tint. Feed this shared surface colour into Albedo and multiply the ramp by it, **Light Color**, and **Light Attenuation**. Connect the result to **Direct Lighting**. Scalars broadcast, but vector widths must match: split an RGBA texture into RGB using the component pins of a Vec3 node before multiplying it by Light Color.
7. Enable **Outline pass**, choose its colour, and adjust **Outline width (world units)**. Start around 0.02 for a metre-scale mesh. This is an independent silhouette shell for opaque meshes; see [OutlineShaders.md](OutlineShaders.md).

Replace Step with **Smoothstep** to soften a boundary. Set Min to the threshold and Max to threshold + transition width. Equal edges give an exact step; reversed edges are sorted. **Floor** enables evenly spaced bands. For example, a Code Expression with the facing value connected to A can use `floor(saturate(A) * 2.0 + 0.5) / 2.0` for three brightness levels. Step, Floor and Smoothstep work component-wise and are also available as `step`, `floor` and `smoothstep` in Code Expression nodes.

## Editable example

Assign `project://Materials/Toon.plutomaterial` in the RocketLeg sample. It uses `Shaders/Toon.plutoshadergraph` and a reusable `Shaders/ToonRamp.plutoshadergraph`.

The shader uses the material's base colour and albedo texture for both the surface and toon lighting. **Tint** is an additional multiplier and defaults to white; the sample material's orange colour is set in its ordinary base-colour field. Texture alpha and material opacity retain their usual behaviour. The material also exposes **ShadowThreshold**, **HighlightThreshold** and **Softness**. Open ToonRamp to edit the three Vec3 band colours or change the ramp itself. Its A–D inputs are brightness, shadow threshold, highlight threshold and softness. Set Softness to 0 for hard boundaries. Keep the shadow threshold below the highlight threshold.

The sample deliberately retains a coloured shadow band, so a shadowed light can still supply that stylized colour. For fully black cast shadows, multiply Shadow Attenuation after the ramp instead. Ambient/environment light, GI, multiple overlapping lights and post-processing can introduce extra shades: control those scene settings when you want a strictly limited palette.

Custom lighting shares the existing 64-operation budget with the surface and vertex branches. It re-evaluates the graph for each light, so keep reusable lighting graphs compact. Disconnected Direct Lighting preserves the existing PBR path and old shader assets require no migration.
