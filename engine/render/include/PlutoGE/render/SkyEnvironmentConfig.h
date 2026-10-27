#pragma once
// Shared C++/Slang layout: packed cubemap roughness levels, followed by the
// fourteen diffuse quadrature samples in the last row.
#define PLUTO_SKY_FACE_SIZE 64
#define PLUTO_SKY_MAX_LEVEL 6
// Each face of each level is stored with a one-texel gutter holding the
// neighbouring faces' texels, so one hardware bilinear read per level filters
// across cube edges. Levels pack left to right; faces stack top to bottom.
#define PLUTO_SKY_GUTTER 1
#define PLUTO_SKY_LEVEL_SIZE(level) (PLUTO_SKY_FACE_SIZE >> (level))
#define PLUTO_SKY_TILE_SIZE(level) (PLUTO_SKY_LEVEL_SIZE(level) + 2 * PLUTO_SKY_GUTTER)
#define PLUTO_SKY_LEVEL_X(level) (2 * PLUTO_SKY_FACE_SIZE - 2 * PLUTO_SKY_LEVEL_SIZE(level) + 2 * PLUTO_SKY_GUTTER * (level))
#define PLUTO_SKY_ATLAS_WIDTH (PLUTO_SKY_LEVEL_X(PLUTO_SKY_MAX_LEVEL) + PLUTO_SKY_TILE_SIZE(PLUTO_SKY_MAX_LEVEL))
#define PLUTO_SKY_DIFFUSE_ROW (6 * PLUTO_SKY_TILE_SIZE(0))
#define PLUTO_SKY_ATLAS_HEIGHT (PLUTO_SKY_DIFFUSE_ROW + 1)
