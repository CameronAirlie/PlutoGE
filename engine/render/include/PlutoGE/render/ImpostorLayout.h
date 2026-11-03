#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

// The octahedral impostor layout shared by the baker (assetimport::BakeImpostor) and the renderer
// (BasicLitImpostor.slang mirrors these functions in ImpostorLayout.slang; keep both in step).
//
// An impostor is a grid of framesPerSide x framesPerSide orthographic views of a model from directions over its upper
// hemisphere, packed into one atlas. Frame (i, j) looks from HemiOctahedralDecode((i, j) / (framesPerSide - 1)), so the
// grid's corners and edges hold the horizon and its centre looks straight down. Each frame covers the model's bounding
// sphere: its pixels span [-radius, radius] along the frame's right and up axes.
//
// Two RGBA8 maps share the layout:
// - albedo: colour (sRGB) and coverage in alpha;
// - normalDepth: the surface normal in the frame's own basis (right, up, view direction), encoded n * 0.5 + 0.5, so a
//   billboard whose tangent frame is that basis reconstructs it with an ordinary tangent-space normal map; alpha is the
//   surface's depth toward the viewer, depth / radius * 0.5 + 0.5.
namespace PlutoGE::render::impostor
{
    struct Parameters
    {
        std::uint32_t framesPerSide = 8;
        std::uint32_t frameResolution = 128;
        glm::vec3 boundsCenter{0.0f}; // model space
        float boundsRadius = 0.0f;
        // Instances whose bounding sphere projects smaller than this radius draw as the impostor.
        float maxScreenRadiusPixels = 40.0f;

        [[nodiscard]] std::uint32_t AtlasSize() const { return framesPerSide * frameResolution; }
        [[nodiscard]] bool Valid() const { return framesPerSide >= 2 && frameResolution > 0 && boundsRadius > 0.0f; }
        bool operator==(const Parameters &) const = default;
    };

    // Upper-hemisphere direction (y >= 0) <-> [0, 1]^2, rotated so the square's edges are the horizon.
    inline glm::vec2 HemiOctahedralEncode(glm::vec3 direction)
    {
        direction.y = std::max(direction.y, 0.0f);
        const float sum = std::abs(direction.x) + direction.y + std::abs(direction.z);
        const glm::vec2 octahedral = sum > 0.0f ? glm::vec2(direction.x, direction.z) / sum : glm::vec2(0.0f);
        return glm::vec2(octahedral.x + octahedral.y, octahedral.x - octahedral.y) * 0.5f + 0.5f;
    }

    inline glm::vec3 HemiOctahedralDecode(glm::vec2 coordinates)
    {
        const glm::vec2 rotated = coordinates * 2.0f - 1.0f;
        const glm::vec2 octahedral(0.5f * (rotated.x + rotated.y), 0.5f * (rotated.x - rotated.y));
        const glm::vec3 direction(octahedral.x, 1.0f - std::abs(octahedral.x) - std::abs(octahedral.y), octahedral.y);
        return glm::normalize(direction);
    }

    // The direction frame (i, j) looks from (toward the viewer).
    inline glm::vec3 FrameDirection(glm::uvec2 frame, std::uint32_t framesPerSide)
    {
        return HemiOctahedralDecode(glm::vec2(frame) / static_cast<float>(framesPerSide - 1));
    }

    // The frame nearest a model-space direction toward the viewer.
    inline glm::uvec2 NearestFrame(const glm::vec3 &direction, std::uint32_t framesPerSide)
    {
        const glm::vec2 grid = glm::round(HemiOctahedralEncode(direction) * static_cast<float>(framesPerSide - 1));
        return glm::uvec2(glm::clamp(grid, glm::vec2(0.0f), glm::vec2(static_cast<float>(framesPerSide - 1))));
    }

    // A frame's image axes: right and up, perpendicular to the view direction. Straight down, right stays +x.
    struct FrameBasis
    {
        glm::vec3 right, up, view;
    };
    inline FrameBasis Basis(const glm::vec3 &view)
    {
        const glm::vec3 reference = std::abs(view.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, -1.0f);
        const glm::vec3 right = glm::normalize(glm::cross(reference, view));
        return {right, glm::cross(view, right), view};
    }
}
