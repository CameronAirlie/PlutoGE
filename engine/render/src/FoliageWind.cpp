#include "PlutoGE/render/FoliageWind.h"
#include "PlutoGE/render/FoliageWindLayout.h"
#include <glm/gtc/constants.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>

// The CPU half of the wind model. FoliageWind.slang mirrors SceneWindGust and FoliageWindOffset line for line;
// FoliageWindTests checks the properties both rely on (calm is zero, bounds hold, roots stay put).
namespace PlutoGE::render
{
    namespace
    {
        constexpr float Tau = glm::two_pi<float>();

        glm::vec2 SafeDirection(glm::vec2 direction)
        {
            const float length = glm::length(direction);
            return std::isfinite(length) && length > 1.0e-6f ? direction / length : glm::vec2(1.0f, 0.0f);
        }

        // Gust level in [0, 1] from the packed field: the shader's foliageWindGust.
        float GustLevel(glm::vec2 direction, float inverseGustSize, float travelSpeed, glm::vec2 positionXZ, float time)
        {
            const glm::vec2 across(-direction.y, direction.x);
            const float wave = (glm::dot(positionXZ, direction) - time * travelSpeed) * inverseGustSize;
            return 0.5f + float(PLUTO_WIND_GUST_PRIMARY) * std::sin(Tau * wave) +
                   float(PLUTO_WIND_GUST_SECONDARY) *
                       std::sin(Tau * (wave * float(PLUTO_WIND_GUST_SECONDARY_SCALE) +
                                       glm::dot(positionXZ, across) * inverseGustSize * float(PLUTO_WIND_GUST_CROSS)) +
                                float(PLUTO_WIND_GUST_SECONDARY_PHASE));
        }

        // A plant's phase in [0, 1) from its root cell: the shader's foliageWindSeed.
        float Seed(glm::vec2 positionXZ)
        {
            const auto cellX = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(std::floor(positionXZ.x * float(PLUTO_WIND_SEED_CELLS_PER_METRE))));
            const auto cellZ = static_cast<std::uint32_t>(
                static_cast<std::int32_t>(std::floor(positionXZ.y * float(PLUTO_WIND_SEED_CELLS_PER_METRE))));
            std::uint32_t hash = cellX * 0x8da6b343u ^ cellZ * 0xd8163841u;
            hash ^= hash >> 16u;
            hash *= 0x7feb352du;
            hash ^= hash >> 15u;
            hash *= 0x846ca68bu;
            hash ^= hash >> 16u;
            return static_cast<float>(hash & 0xffffu) / 65536.0f;
        }
    }

    glm::vec2 SceneWindDirectionFromDegrees(float degrees)
    {
        const float radians = glm::radians(degrees);
        return {std::cos(radians), std::sin(radians)};
    }

    float SceneWindStrength(const SceneWindSettings &wind)
    {
        if (wind.Calm() || !std::isfinite(wind.speed))
            return 0.0f;
        return std::clamp(wind.speed / float(PLUTO_WIND_REFERENCE_SPEED), 0.0f, float(PLUTO_WIND_MAX_STRENGTH));
    }

    float SceneWindGust(const SceneWindSettings &wind, glm::vec2 positionXZ, float time)
    {
        const float gustiness = std::clamp(wind.gustiness, 0.0f, 1.0f);
        const float level = GustLevel(SafeDirection(wind.direction), 1.0f / std::max(wind.gustSize, 1.0f),
                                      std::max(wind.speed, 0.0f), positionXZ, time);
        return 1.0f + gustiness * (2.0f * level - 1.0f);
    }

    glm::vec3 SceneWindVelocity(const SceneWindSettings &wind, const glm::vec3 &position, float time)
    {
        if (wind.Calm())
            return glm::vec3(0.0f);
        const glm::vec2 direction = SafeDirection(wind.direction);
        const float speed = wind.speed * SceneWindGust(wind, {position.x, position.z}, time);
        return glm::vec3(direction.x, 0.0f, direction.y) * speed;
    }

    FoliageWindResponse FoliageWindResponse::Sanitized() const
    {
        const auto finiteOr = [](float value, float fallback) { return std::isfinite(value) ? value : fallback; };
        FoliageWindResponse result = *this;
        result.stiffness = std::clamp(finiteOr(stiffness, 0.5f), 0.0f, 1.0f);
        result.flutter = std::clamp(finiteOr(flutter, 0.0f), 0.0f, 1.0f);
        result.bendHeight = std::max(finiteOr(bendHeight, 1.0f), 0.01f);
        result.pivotHeight = finiteOr(pivotHeight, 0.0f);
        return result;
    }

    FoliageWindResponse FoliageWindPresetResponse(FoliageWindPreset preset, float height)
    {
        const float plant = std::isfinite(height) && height > 0.01f ? height : 1.0f;
        FoliageWindResponse response;
        response.enabled = true;
        response.bendHeight = plant;
        switch (preset)
        {
        case FoliageWindPreset::Grass: response.stiffness = 0.15f; response.flutter = 0.6f; break;
        case FoliageWindPreset::Flowers: response.stiffness = 0.3f; response.flutter = 0.45f; break;
        case FoliageWindPreset::Shrub: response.stiffness = 0.55f; response.flutter = 0.5f; break;
        case FoliageWindPreset::Tree:
            response.stiffness = 0.85f;
            response.flutter = 0.35f;
            response.pivotHeight = 0.2f * plant;
            break;
        }
        return response;
    }

    bool FoliageWindAnimates(const SceneWindSettings &wind, const FoliageWindResponse &response)
    {
        if (!response.enabled || SceneWindStrength(wind) <= 0.0f)
            return false;
        const auto sanitized = response.Sanitized();
        return sanitized.stiffness < 1.0f || sanitized.flutter > 0.0f;
    }

    FoliageWindParameters PackFoliageWind(const SceneWindSettings &wind, const FoliageWindResponse &response,
                                          float time, float previousTime)
    {
        if (!FoliageWindAnimates(wind, response))
            return {};
        const auto plant = response.Sanitized();
        const glm::vec2 direction = SafeDirection(wind.direction);
        const float softness = 1.0f - plant.stiffness;
        const float swayFrequency =
            glm::mix(float(PLUTO_WIND_SWAY_FREQUENCY_SOFT), float(PLUTO_WIND_SWAY_FREQUENCY_STIFF), plant.stiffness) /
            std::sqrt(std::max(plant.bendHeight, 0.05f));
        FoliageWindParameters parameters;
        parameters.field = {direction.x, direction.y, SceneWindStrength(wind), std::clamp(wind.gustiness, 0.0f, 1.0f)};
        parameters.timing = {time, previousTime, 1.0f / std::max(wind.gustSize, 1.0f), std::max(wind.speed, 0.0f)};
        parameters.response = {float(PLUTO_WIND_MAX_BEND) * softness * std::sqrt(softness), swayFrequency,
                               plant.flutter * float(PLUTO_WIND_FLUTTER_AMPLITUDE), float(PLUTO_WIND_FLUTTER_FREQUENCY)};
        parameters.shape = {plant.pivotHeight, 1.0f / plant.bendHeight, plant.bendHeight, 1.0f};
        return parameters;
    }

    namespace
    {
        // foliageWindOffset's largest displacement per world unit of vertical scale, for a wind of at most `strength`
        // whose gust factor peaks at `maxGust`, at vertices at most `relative` bend heights above the pivot.
        float WindDisplacementBound(const FoliageWindResponse &plant, float strength, float maxGust, float relative)
        {
            const float softness = 1.0f - plant.stiffness;
            const float sway = float(PLUTO_WIND_MAX_BEND) * softness * std::sqrt(softness) * strength *
                               (maxGust * float(PLUTO_WIND_LEAN_SHARE) + float(PLUTO_WIND_OSCILLATION_SHARE)) *
                               relative * relative;
            // The flutter direction's length is at most sqrt(1 + 0.25 + 1) = 1.5.
            const float flutter = plant.flutter * float(PLUTO_WIND_FLUTTER_AMPLITUDE) * strength * maxGust * relative * 1.5f;
            // Horizontally at most sway + flutter; vertically flutter plus the drop that keeps stems from stretching,
            // which never exceeds the horizontal offset.
            return std::sqrt(2.0f) * (sway + 2.0f * flutter) * plant.bendHeight;
        }
    }

    float FoliageWindMaxDisplacement(const FoliageWindResponse &response)
    {
        if (!response.enabled)
            return 0.0f;
        // Gustiness 1 peaks at twice the mean strength.
        return WindDisplacementBound(response.Sanitized(), float(PLUTO_WIND_MAX_STRENGTH), 2.0f,
                                     float(PLUTO_WIND_MAX_RELATIVE_HEIGHT));
    }

    float FoliageWindMaxDisplacement(const SceneWindSettings &wind, const FoliageWindResponse &response, float localTop)
    {
        if (!FoliageWindAnimates(wind, response))
            return 0.0f;
        const auto plant = response.Sanitized();
        // foliageWindGust lies in [0, 1], so the gust factor peaks at 1 + gustiness.
        const float maxGust = 1.0f + std::clamp(wind.gustiness, 0.0f, 1.0f);
        const float relative = std::isfinite(localTop)
            ? std::clamp((localTop - plant.pivotHeight) / plant.bendHeight, 0.0f, float(PLUTO_WIND_MAX_RELATIVE_HEIGHT))
            : float(PLUTO_WIND_MAX_RELATIVE_HEIGHT);
        return WindDisplacementBound(plant, SceneWindStrength(wind), maxGust, relative);
    }

    glm::vec3 FoliageWindOffset(const FoliageWindParameters &wind, const glm::mat4 &model,
                                const glm::vec3 &localPosition, float time)
    {
        if (!wind.Active())
            return glm::vec3(0.0f);
        const glm::vec3 origin(model * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        const float upScale = glm::length(glm::mat3(model) * glm::vec3(0.0f, 1.0f, 0.0f));
        const float above = localPosition.y - wind.shape.x;
        const float relative = std::clamp(above * wind.shape.y, 0.0f, float(PLUTO_WIND_MAX_RELATIVE_HEIGHT));
        const float bendWorld = wind.shape.z * upScale;
        const glm::vec2 direction(wind.field.x, wind.field.y);
        const float strength = wind.field.z;
        const glm::vec2 root(origin.x, origin.z);

        const float gust = 1.0f + wind.field.w * (2.0f * GustLevel(direction, wind.timing.z, wind.timing.w, root, time) - 1.0f);
        const float seed = Seed(root);
        const float swayPhase = time * wind.response.y;
        const float oscillation = 0.7f * std::sin(Tau * (swayPhase + seed)) +
                                  0.3f * std::sin(Tau * (swayPhase * 2.13f + seed * 1.93f));
        const float lean = wind.response.x * strength *
                           (gust * float(PLUTO_WIND_LEAN_SHARE) + float(PLUTO_WIND_OSCILLATION_SHARE) * oscillation);
        glm::vec3 offset = glm::vec3(direction.x, 0.0f, direction.y) * (lean * relative * relative * bendWorld);

        const float vertexPhase = glm::dot(localPosition, glm::vec3(3.17f, 1.73f, 2.41f)) + seed;
        const float flutterPhase = time * wind.response.w;
        const glm::vec3 flutterDirection(std::sin(Tau * (flutterPhase + vertexPhase)),
                                         0.5f * std::sin(Tau * (flutterPhase * 1.31f + vertexPhase * 1.7f)),
                                         std::cos(Tau * (flutterPhase * 0.87f + vertexPhase * 1.3f)));
        offset += flutterDirection * (wind.response.z * strength * gust * relative * bendWorld);

        // Lower leaning geometry so stems keep roughly their length instead of stretching sideways.
        const float height = std::max(above * upScale, 1.0e-3f);
        const float horizontal = glm::length(glm::vec2(offset.x, offset.z));
        offset.y -= std::min(horizontal * horizontal / (2.0f * height), horizontal);
        return offset;
    }
}
