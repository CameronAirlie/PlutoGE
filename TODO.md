1. [x] - Discrete mesh, material and animation assets
    a. [x] - Imported gltf/fbx creates meshes, animations, materials, textures
    b. [x] - each mesh has its own lod settings (source of truth for mesh lod)
    c. [x] - convert all systems to accept new asset types instead of gltf/fbx models
    d. [x] - Be able to drag and drop gltf/fbx directly into content browser to import
    e. [x] - Be able to drag and drop from content browser into scene hierarchy
2. [x] - Improve prefab system
    a. [x] - fix references when adding them to a scene
3. [x] - Edit individual foliage assets (position, scale, rotation)
4. [x] - Ability to copy/paste/delete/duplicate objects from viewport and hierarchy + add keybinds
5. [x] - Fix issue causing terrain to lose material on reload
6. [x] - Improve post processing stack ui to be able to drag to arrange effects
7. [x] - Fix brightness levels. when gamma is set to 2.2 the scene is way too bright.
8. [x] - Fix humanoid mesh animation. Legs are stretched and sticking out sideways unanimated. may be fixed by #1
9. [x] - Extract post processing effects into separate asset type. have editor camera and camera components select post processing effedt asset preset.
10. [x] - Integrate engine with visual studio so visual studio is able to attach to the engine process. Similar to unity, expose an sdk or package to be able to work on scripts using a packaged engine executable for production.
11. [x] - Implement sound system
        [x] - Add support for 3D spatial audio
        [x] - Add sound emitter and sound listener components.
        [x] - Come up with how to deal with walls that muffle/block sound
12. [x] - Spline system
        [x] - Collision (for roads/racetracks etc.)
13. [ ] - Setup project distribution. No dependency execution on new machine
        [x] - Can build executable. then copy to another machine and run with minimal dependencies
        [x] - Decide whether exported builds should bundle .NET/hostfxr or require a target-machine runtime install
14. [x] - Fix issue where when selecting a spline point, it shows a duplicate of the whole track offset at the point position
15. [x] - Implement ocean component for water 'simulation'.
        - [x] - Handle areas that should not have ocean using spline-like area tool. this can be inverted for lakes.
16. [ ] - Implement fast cloth simulation used in hanging cloths and capes etc.
17. [x] - Implement texture painting. For use in decal placement and landscape texture painting.
