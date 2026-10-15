#pragma once
// Shared C++/Slang layout: packed cubemap roughness levels, followed by the
// fourteen diffuse quadrature samples in the last row.
#define PLUTO_SKY_FACE_SIZE 64
#define PLUTO_SKY_MAX_LEVEL 6
#define PLUTO_SKY_ATLAS_WIDTH (2 * PLUTO_SKY_FACE_SIZE)
#define PLUTO_SKY_DIFFUSE_ROW (6 * PLUTO_SKY_FACE_SIZE)
#define PLUTO_SKY_ATLAS_HEIGHT (PLUTO_SKY_DIFFUSE_ROW + 1)
