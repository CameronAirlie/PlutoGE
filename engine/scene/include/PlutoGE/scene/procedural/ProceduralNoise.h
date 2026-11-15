#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string_view>

namespace PlutoGE::scene::procedural
{
    // Deterministic hashing and noise for procedural terrain. Everything is integer hashing and plain float arithmetic
    // in a fixed order, so the same inputs give the same output on every platform and in every run (no std::rand, no
    // distribution objects whose results differ between standard libraries).

    [[nodiscard]] std::uint32_t Hash(std::uint32_t a, std::uint32_t b = 0, std::uint32_t c = 0, std::uint32_t d = 0);
    [[nodiscard]] std::uint64_t Hash64(std::uint64_t a, std::uint64_t b = 0, std::uint64_t c = 0, std::uint64_t d = 0);
    // A stable hash of a name (FNV-1a), for seeding from asset and layer names.
    [[nodiscard]] std::uint32_t HashName(std::string_view name);
    // [0, 1) from a hash.
    [[nodiscard]] inline float UnitFloat(std::uint32_t hash) { return static_cast<float>(hash >> 8) * (1.0f / 16777216.0f); }

    enum class NoiseType : std::uint8_t
    {
        Value,  // one octave of smooth value noise
        Fbm,    // fractal sum of octaves: rolling variation
        Ridged, // folded octaves: sharp ridges and veins
    };

    struct NoiseSettings
    {
        NoiseType type = NoiseType::Fbm;
        float scale = 50.0f; // world units per feature of the first octave
        int octaves = 4;     // 1..8
        float lacunarity = 2.0f;
        float gain = 0.5f;
        std::uint32_t seed = 0;
        bool operator==(const NoiseSettings &) const = default;
    };

    // Noise at a world position (x, z), 0..1.
    [[nodiscard]] float EvaluateNoise(const NoiseSettings &settings, glm::vec2 position, std::uint32_t globalSeed = 0);
}
