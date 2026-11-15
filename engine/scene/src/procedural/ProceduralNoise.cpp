#include "PlutoGE/scene/procedural/ProceduralNoise.h"

#include <algorithm>
#include <cmath>

namespace PlutoGE::scene::procedural
{
    namespace
    {
        // lowbias32 (Chris Wellons): a well-mixed 32-bit integer hash.
        std::uint32_t Mix(std::uint32_t x)
        {
            x ^= x >> 16;
            x *= 0x7feb352du;
            x ^= x >> 15;
            x *= 0x846ca68bu;
            x ^= x >> 16;
            return x;
        }

        std::uint64_t Mix64(std::uint64_t x)
        {
            // splitmix64 finalizer
            x ^= x >> 30;
            x *= 0xbf58476d1ce4e5b9ull;
            x ^= x >> 27;
            x *= 0x94d049bb133111ebull;
            x ^= x >> 31;
            return x;
        }

        float Quintic(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

        // Smooth value noise in 0..1 on an integer lattice.
        float ValueNoise(glm::vec2 position, std::uint32_t seed)
        {
            const float fx = std::floor(position.x);
            const float fz = std::floor(position.y);
            // The lattice wraps every 2^24 cells, far beyond any terrain.
            const auto x = static_cast<std::uint32_t>(static_cast<std::int64_t>(fx) & 0xffffff);
            const auto z = static_cast<std::uint32_t>(static_cast<std::int64_t>(fz) & 0xffffff);
            const float tx = Quintic(position.x - fx);
            const float tz = Quintic(position.y - fz);
            const auto corner = [&](std::uint32_t dx, std::uint32_t dz) { return UnitFloat(Hash(seed, (x + dx) & 0xffffff, (z + dz) & 0xffffff)); };
            const float a = corner(0, 0), b = corner(1, 0), c = corner(0, 1), d = corner(1, 1);
            return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * tz;
        }
    }

    std::uint32_t Hash(std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t d)
    {
        std::uint32_t h = Mix(a + 0x9e3779b9u);
        h = Mix(h ^ (b + 0x85ebca6bu));
        h = Mix(h ^ (c + 0xc2b2ae35u));
        return Mix(h ^ (d + 0x27d4eb2fu));
    }

    std::uint64_t Hash64(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d)
    {
        std::uint64_t h = Mix64(a + 0x9e3779b97f4a7c15ull);
        h = Mix64(h ^ (b + 0xd1b54a32d192ed03ull));
        h = Mix64(h ^ (c + 0x8cb92ba72f3d8dd7ull));
        return Mix64(h ^ (d + 0x2545f4914f6cdd1dull));
    }

    std::uint32_t HashName(std::string_view name)
    {
        std::uint32_t hash = 2166136261u;
        for (const char character : name)
        {
            hash ^= static_cast<std::uint8_t>(character);
            hash *= 16777619u;
        }
        return hash;
    }

    float EvaluateNoise(const NoiseSettings &settings, glm::vec2 position, std::uint32_t globalSeed)
    {
        const float scale = std::max(settings.scale, 0.001f);
        const int octaves = std::clamp(settings.octaves, 1, 8);
        const std::uint32_t seed = Hash(globalSeed, settings.seed);
        glm::vec2 p = position / scale;

        if (settings.type == NoiseType::Value)
            return ValueNoise(p, seed);

        float sum = 0.0f;
        float amplitude = 1.0f;
        float total = 0.0f;
        for (int octave = 0; octave < octaves; ++octave)
        {
            float value = ValueNoise(p, Hash(seed, static_cast<std::uint32_t>(octave)));
            if (settings.type == NoiseType::Ridged)
            {
                value = 1.0f - std::abs(value * 2.0f - 1.0f);
                value *= value;
            }
            sum += value * amplitude;
            total += amplitude;
            amplitude *= settings.gain;
            // Rotate each octave a little so lattice rows of different octaves do not line up.
            p = glm::vec2(p.x * 0.8f - p.y * 0.6f, p.x * 0.6f + p.y * 0.8f) * settings.lacunarity + glm::vec2(17.13f, 31.71f);
        }
        return total > 0.0f ? std::clamp(sum / total, 0.0f, 1.0f) : 0.0f;
    }
}
