#pragma once

#include "PlutoGE/render/ImpostorLayout.h"
#include "PlutoGE/render/Mesh.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace PlutoGE::assetimport
{
    struct ImportedMeshSourceAsset;

    // An RGBA8 image, rows top first.
    struct ImpostorImage
    {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        std::vector<std::uint8_t> pixels;

        [[nodiscard]] bool Empty() const { return width == 0 || height == 0 || pixels.size() < std::size_t(width) * height * 4; }
    };

    // A material as the baker sees it: its colour, optionally textured, and how it masks and faces.
    struct ImpostorMaterial
    {
        glm::vec4 baseColor{1.0f};
        const ImpostorImage *baseColorTexture = nullptr;
        bool alphaTested = false;
        float alphaCutoff = 0.5f;
        // Two-sided surfaces (leaves) face whichever side the frame sees.
        bool twoSided = false;
    };

    // Part of the artifact settings of models baking an impostor: bump whenever BakeImpostor's output changes.
    inline constexpr std::uint32_t ImpostorBakeVersion = 1;

    struct ImpostorBakeSettings
    {
        std::uint32_t framesPerSide = 8;
        std::uint32_t frameResolution = 128;
        // Samples per pixel along each axis: coverage and colour are averaged over them.
        std::uint32_t supersampling = 2;
        float maxScreenRadiusPixels = 40.0f;
    };

    struct ImpostorAtlas
    {
        render::impostor::Parameters parameters;
        ImpostorImage albedo;
        ImpostorImage normalDepth;
    };

    // Renders the submeshes' finest LOD (their own index ranges) into an octahedral impostor atlas
    // (render/ImpostorLayout.h), on the CPU so imports need no GPU. Each submesh uses materials[materialIndex], or an
    // untextured white material when it has none. Returns an atlas with invalid parameters when the mesh is empty.
    ImpostorAtlas BakeImpostor(const render::MeshData &mesh, std::span<const render::Submesh> submeshes,
                               std::span<const ImpostorMaterial> materials, const ImpostorBakeSettings &settings = {});

    // BakeImpostor over an imported model: its materials' base colour, alpha mask and two-sidedness, with their base
    // colour textures decoded from the import (embedded) or read from their source files.
    ImpostorAtlas BakeImportedImpostor(const ImportedMeshSourceAsset &asset, const ImpostorBakeSettings &settings = {});

    // Decodes an image file to RGBA8; empty when it cannot be read.
    ImpostorImage LoadImpostorImage(const std::filesystem::path &path);
}
