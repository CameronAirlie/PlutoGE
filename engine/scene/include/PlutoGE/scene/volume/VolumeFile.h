#pragma once

#include "PlutoGE/scene/volume/VolumeChunk.h"
#include "PlutoGE/scene/volume/VolumeGrid.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace PlutoGE::scene::volume
{
    class TerrainVolume;

    // A volume's stored chunks in one file (a world tile's tile_x_z_volume.plutovolume, a save game's edits).
    //
    // Version 1, little endian: "PLVO", version (u32), voxel size (f32), chunk samples per side (i32), origin (3 x f32),
    // chunk count (u32), then per chunk its coordinate (3 x i32), payload size (u32) and payload: the densities, then
    // the materials, each as runs of (length u8 1..255, value u8). Chunks are written in coordinate order.
    namespace VolumeFile
    {
        inline constexpr std::string_view Extension = ".plutovolume";
        inline constexpr std::uint32_t Version = 1;

        struct Contents
        {
            VolumeGrid grid;
            std::vector<std::pair<VolumeChunkCoord, VolumeChunk>> chunks;
        };

        [[nodiscard]] std::vector<std::uint8_t> Encode(const VolumeGrid &grid, std::span<const std::pair<VolumeChunkCoord, VolumeChunk>> chunks);
        // Every stored chunk of a volume.
        [[nodiscard]] std::vector<std::uint8_t> Encode(const TerrainVolume &volume);
        // nullopt (with a reason) when the bytes are not a volume file, are damaged, or come from a newer version.
        [[nodiscard]] std::optional<Contents> Decode(std::span<const std::uint8_t> bytes, std::string *error = nullptr);
        // Decodes into a volume, replacing chunks it holds. False (and the volume untouched) when the file is damaged or
        // its grid differs from the volume's.
        bool Load(std::span<const std::uint8_t> bytes, TerrainVolume &volume, std::string *error = nullptr);
    }
}
