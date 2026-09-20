# Orthographic game cameras

Select a Camera component and choose **Projection > Orthographic**. Set
**OrthographicHeight** to the full vertical span in world units. Horizontal
coverage follows viewport aspect ratio; camera distance does not change apparent
object size. FOV remains stored for switching back to perspective.

The component saves these settings in scenes and prefabs, using the normal
Inspector property and undo workflows. Older camera records default to perspective.
The editor's selected-camera outline uses parallel sides for orthographic cameras.
The editor viewport's independent orthographic controls remain available.

```csharp
var camera = GameObject.GetComponent<CameraComponent>();
if (camera is not null)
{
    camera.Projection = CameraProjection.Orthographic;
    camera.OrthographicHeight = 18.0f;
}
```

Use Camera Rig to follow a character and change OrthographicHeight for zoom.
Near/far clipping retains the engine's reversed-depth convention. Nonpositive
heights clamp to a small positive span; nonfinite values use a safe default.
Zero-size viewport queries produce finite projection matrices.

RHI directional shadow coverage accounts for parallel camera rays. DLSS/FSR
temporal upscalers currently require perspective camera metadata, so orthographic
cameras render at native output resolution and report that fallback in upscaler
status. Native post-processing remains available. Individual experimental effects
still need scene-specific visual checks; the DungeonCrawler prototype uses FXAA,
bloom and tone mapping.

Rebuild both the native host and ScriptCore after updating this feature. Projection
uses a separate managed bridge registration, preserving the existing camera
component registration signature.
