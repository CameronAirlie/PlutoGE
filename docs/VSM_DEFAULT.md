# Default directional shadows

New directional lights and low-level renderer lighting use virtual shadow maps
(VSM) by default. Existing serialized shadow-method values remain compatible:
an explicit legacy Cascaded selection remains Cascaded. Lights without a saved
method inherit the new VSM default.

Selecting VSM never automatically enables cascaded shadow maps. Active VSM skips
cascade camera preparation, allocation, rendering, and receiver sampling. The
Profiler panel and copied capture report `RHI directional shadows` with the active
method or the reason VSM is unavailable.

Opaque fragment-only shader graphs are supported: their shading cannot change
the depth or coverage used by VSM. They no longer force the entire scene to use
cascades, and changes to graph shading/time do not invalidate shadow depth.
Standard masked materials retain texture-based alpha testing.

Vertex-deforming graphs and masked fragment graphs still require graph-aware VSM
depth shaders. If either affects VSM receivers or casters, directional shadows
are disabled with an explicit profiler status; the renderer does not substitute
cascades or render incorrect undeformed/unmasked VSM depth. Missing backend
support, missing shaders, or draw-chunk capacity overflow are also reported.
Legacy cascades remain an explicit compatibility option for these scenes.

Regression coverage checks VSM defaults, opaque graph shadow coverage, unsupported
graph reporting without cascade allocation, switching between explicit legacy
cascades and VSM, page pressure, fog-only receivers, and shadow disablement.
