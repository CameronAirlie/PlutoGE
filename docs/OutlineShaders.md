# Outline shader assets

Assign `project://Shaders/Outline.plutoshadergraph` in the material editor, or use the supplied `Materials/Outline.plutomaterial`. The roads project and RocketLeg sample both include these assets.

In the shader editor, enable **Outline pass**, choose **Outline colour**, set **Outline width (world units)**, then Save. Cached materials update when the graph is saved. The regular graph still defines the surface; the outline is an additional unlit, depth-tested back-face shell. Width zero disables it. Existing shader assets default to no outline.

Supported for opaque standard mesh materials in the legacy geometry renderer and the OpenGL/Vulkan RHI renderer, including instanced and deformed meshes. Shells do not cast shadows or contribute to voxel GI. Transparent, glass and alpha-masked materials skip the shell. Width is world-space, so apparent thickness decreases with distance. Use closed meshes with smooth vertex normals and outward winding; split hard normals can leave gaps, and mirrored winding requires correction in the mesh. This is a silhouette outline, not a screen-space crease or wireframe effect. Geometry just outside the camera frustum is still culled using the original mesh bounds.

Surface graphs now execute in both RHI backends. See CustomShaderAudit.md for supported nodes and the remaining pass limitations.
