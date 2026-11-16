#pragma once

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace PlutoGE::assets
{
    enum class ParticleSimulationSpace
    {
        Local = 0,
        World = 1,
        // A field of particles that surrounds every camera (dust motes, pollen, snow): particles spawn throughout
        // ParticleCameraVolumeSettings::size, move in world space, and each view draws them wrapped into that box
        // centred on itself, so the field never runs out and stays put as the camera moves. The emission shape and
        // the emitter's rotation and scale are ignored. See docs/AIRBORNE_PARTICLES.md.
        CameraVolume = 2,
    };

    // How light reaches a particle.
    enum class ParticleLighting
    {
        // The particle's colour as authored (sparks, glows, rain).
        Unlit = 0,
        // A lit puff: a sphere-like normal lit by the sun and up to four local lights (ParticleSystemAsset::smoke*).
        Smoke = 1,
        // A speck in the air (dust, pollen, spores, ash) lit as the volumetric fog's medium is: sunlight through a
        // phase function, the sun's shadow maps, cloud shadows and terrain occlusion, plus sky light. A speck in a
        // sunbeam glows and the same speck in shade almost vanishes (ParticleScatteringSettings).
        Scattering = 2,
    };

    struct ParticleScatteringSettings
    {
        // Henyey-Greenstein anisotropy, -0.9 to 0.9. Positive scatters forward: specks shine brightest looking toward
        // the sun. Dust about 0.6, pollen about 0.4, 0 scatters evenly.
        float anisotropy = 0.6f;
        // Scales the sun's light. The phase is normalised so even scattering (anisotropy 0) is 1.
        float sunStrength = 1.0f;
        // Scales the sky's light, under the cloud cover.
        float ambientStrength = 1.0f;
        // Darken specks the sun cannot reach: shadow maps (virtual or cascaded) and terrain beyond them.
        bool sunShadows = true;

        bool operator==(const ParticleScatteringSettings &) const = default;
    };

    // The box a CameraVolume system fills and every camera carries with it.
    struct ParticleCameraVolumeSettings
    {
        // World-space extent in metres. Particles crossing a face reappear at the opposite face.
        glm::vec3 size{30.0f, 16.0f, 30.0f};
        // Fraction of the half-extent over which particles fade out toward the box's faces, so wrapping is never
        // seen. 0 keeps the box hard-edged; 1 fades from the camera outward.
        float edgeFade = 0.35f;

        bool operator==(const ParticleCameraVolumeSettings &) const = default;
    };

    // How particles adapt to the camera drawing them; works in every simulation space.
    struct ParticleViewFadeSettings
    {
        // Particles nearer the camera than this (metres) fade out, gone at half of it. Keeps specks from blooming into
        // blurry discs against the lens. 0 off.
        float nearDistance = 0.0f;
        // Smallest drawn width in pixels. A particle smaller on screen is drawn this wide with its opacity lowered
        // to keep the coverage it had, so sub-pixel specks stay steady instead of shimmering. 0 off.
        float minScreenSize = 0.0f;

        bool operator==(const ParticleViewFadeSettings &) const = default;
    };

    enum class ParticleShape
    {
        Point = 0,
        Sphere = 1,
        Box = 2,
        Cone = 3,
    };

    enum class ParticleRenderShape
    {
        Circle = 0,
        Quad = 1,
    };

    enum class ParticleRenderMode
    {
        Billboard = 0,
        Volumetric = 1,
    };

    enum class ParticleCollisionMode
    {
        Kill = 0,
        Bounce = 1,
        Stop = 2,
    };

    struct ParticleSystemAsset
    {
        bool looping = true;
        bool playOnAwake = true;
        // A looping system starts as if it had already been playing for one particle lifetime, so an ambient field
        // is full on the first frame instead of filling up over its lifetime.
        bool prewarm = false;
        float duration = 5.0f;
        int maxParticles = 1000;
        float startLifetime = 5.0f;
        float lifetimeVariation = 0.0f;
        float startSpeed = 2.0f;
        float speedVariation = 0.0f;
        float startSize = 0.25f;
        float sizeVariation = 0.0f;
        glm::vec4 startColor{1.0f};
        bool colorOverLifetimeEnabled = false;
        glm::vec4 endColor{1.0f, 1.0f, 1.0f, 0.0f};
        bool sizeOverLifetimeEnabled = false;
        float endSize = 0.0f;
        float gravityModifier = 0.0f;
        float drag = 0.0f;
        float buoyancy = 0.0f;
        glm::vec3 windVelocity{0.0f};
        // How fast particles take up the scene's wind (WindComponent), per second: each step their velocity relaxes
        // toward the air's at this rate, gusts included. 0 ignores the scene's wind; rain ~0.5, leaves and snow ~2.
        float sceneWindDrag = 0.0f;
        // The share of the scene's wind velocity particles take up, gusts included (1 the full wind). Below 1 for
        // specks that should drift with the wind's direction and gusts but slower than it, like motes in sheltered air.
        float sceneWindScale = 1.0f;
        // Seconds of motion each particle is drawn stretched along: a streak from where it was this long ago to where
        // it is, as wide as the particle (rain ~0.05, sparks ~0.03). 0 draws ordinary billboards.
        float velocityStretch = 0.0f;
        float turbulenceStrength = 0.0f;
        float turbulenceFrequency = 0.5f;
        float rotationSpeed = 0.0f;
        float rotationSpeedVariation = 0.0f;
        float startRotation = 0.0f;
        float startRotationVariation = 180.0f;
        float fadeInFraction = 0.0f;
        float fadeOutFraction = 0.25f;
        float emissionRateOverTime = 10.0f;
        float burstTime = 0.0f;
        int burstCount = 0;
        ParticleSimulationSpace simulationSpace = ParticleSimulationSpace::Local;
        ParticleShape shape = ParticleShape::Point;
        glm::vec3 shapeSize{1.0f};
        float shapeRadius = 1.0f;
        float coneAngle = 25.0f;
        ParticleRenderShape renderShape = ParticleRenderShape::Circle;
        ParticleRenderMode renderMode = ParticleRenderMode::Billboard;
        std::string materialAssetReference;
        int flipbookColumns = 1;
        int flipbookRows = 1;
        float flipbookFramesPerSecond = 0.0f;
        bool flipbookLooping = true;
        bool flipbookRandomStart = true;
        bool softParticlesEnabled = false;
        float softParticleDistance = 0.5f;
        ParticleLighting lighting = ParticleLighting::Unlit;
        // Smoke lighting.
        float smokeLightingStrength = 0.65f;
        float smokeAmbient = 0.35f;
        ParticleScatteringSettings scattering;
        ParticleCameraVolumeSettings cameraVolume;
        ParticleViewFadeSettings viewFade;
        // How much the scene's weather (WeatherComponent) thins the particles, 0 to 1: at 1 they are gone in a
        // downpour or on soaked ground, scaled by the larger of precipitation and wetness. Rain washes dust and pollen
        // out of the air and damp ground raises none. 0 ignores the weather.
        float wetWeatherFade = 0.0f;
        float volumeDensity = 2.0f;
        float volumeNoiseStrength = 0.45f;
        float volumeNoiseFrequency = 3.0f;
        float volumeEdgeSoftness = 1.5f;
        float volumeSelfShadow = 0.75f;
        // Thin each volume as it grows so its smoke mass stays constant.
        bool volumeConserveMass = false;

        bool collisionEnabled = false;
        ParticleCollisionMode collisionMode = ParticleCollisionMode::Kill;
        float collisionDampening = 0.0f;
        float collisionBounce = 0.5f;
        float collisionLifetimeLoss = 0.0f;
        float collisionRadius = 0.05f;
        int collisionMaxChecksPerFrame = 256;

        bool trailsEnabled = false;
        float trailLifetime = 0.5f;
        float trailWidth = 0.08f;
        bool trailInheritParticleColor = true;
        std::string trailMaterialAssetReference;

        std::string collisionSubEmitterAssetReference;
        int collisionSubEmitterCount = 0;
        std::string deathSubEmitterAssetReference;
        int deathSubEmitterCount = 0;
    };

    ParticleSystemAsset CreateDefaultParticleSystemAsset();

    // The settings clamped to their valid ranges (non-finite values fall back to the defaults). The loader, the
    // component and the editor all go through these, so the ranges live in one place.
    ParticleScatteringSettings Sanitized(ParticleScatteringSettings settings);
    ParticleCameraVolumeSettings Sanitized(ParticleCameraVolumeSettings settings);
    ParticleViewFadeSettings Sanitized(ParticleViewFadeSettings settings);
    float SanitizedSceneWindScale(float scale);
    float SanitizedWetWeatherFade(float fade);

    // Particle systems built into the engine (no file): referenced as engine://builtin/particles/<name>, listed by
    // Project::GetBuiltinAssetReferences and loaded by AssetManager::LoadParticleSystemAsset.
    inline constexpr std::string_view kBuiltinParticlesPrefix = "engine://builtin/particles/";
    // White droplets thrown up and out of the water, streaked by their motion, falling back under gravity. The default
    // splash of every new Water Effects component (docs/WATER.md).
    inline constexpr std::string_view kBuiltinWaterSplashParticlesReference = "engine://builtin/particles/water-splash";
    ParticleSystemAsset CreateWaterSplashParticleSystemAsset();
    // The built-in system a reference names, or nothing for any other reference.
    std::optional<ParticleSystemAsset> FindBuiltinParticleSystemAsset(std::string_view reference);
}
