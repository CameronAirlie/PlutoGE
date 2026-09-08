# RHI particles, point lights, and thumbnails

The editor and runtime pass the scene to `RhiSceneRenderer`, which collects enabled point lights and particle systems. Legacy OpenGL rendering remains available, while the RHI path uses portable vertex buffers and Slang shaders.

Particles use the existing CPU simulator, including collisions and sub-emitters. Local-space particles follow emitter transforms. Rendering preserves lifetime color/size/fades, rotation, flipbooks, soft depth intersections, volumetric smoke, directional and local smoke lighting, material emission, and trails. Particle colors are premultiplied for the RHI blend state. The particle pass runs before glass and temporal/display processing, without writing opaque depth or G-buffer attachments. GPU transform-feedback simulation is not used by this path.

Point lighting supports up to 16 lights per render. Up to four shadow-casting point lights receive six 512-pixel faces in a shared shadow atlas. Shadow rendering honors masked materials, instanced geometry, and shadow-caster flags. Point shadows do not depend on a directional light being present. The atlas adds material binding 13 (flattened OpenGL texture slot 21).

Content-browser and material-editor previews share the scene renderer within each cache, copy completed previews into persistent images, and register textures through the editor compositor. ImGui texture IDs remain 64-bit. Preview material changes invalidate cached GPU assets; normal mip uploads complete before rendering a preview. Material-preview resources are released before editor/device shutdown.

Validation: `PlutoGEVulkanParticlePointTests` and `PlutoGEOpenGLParticlePointTests` compare rendered pixels for light color/range, all six shadow faces, masked and instanced casters, shadow toggling, particle blending, soft depth fading, and opaque-depth rejection. The focused OpenGL test omits unrelated virtual-shadow compute pipelines to accommodate drivers with lower storage-binding limits.
