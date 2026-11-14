#pragma once
// Shared C++/Slang constants of detail grass (docs/DETAIL_GRASS.md). DetailGrass.cpp lays fields out and packs their
// generation parameters with them; DetailGrassGenerate.slang and BasicLitGrass.slang read them. Change both sides
// together.

// Distance rings a field may use. Ring k places clumps on the lattice of every 2^k-th candidate, so each ring
// dispatches about as many candidates as the first however far it reaches.
#define PLUTO_DETAIL_GRASS_MAX_RINGS 6
// Clump mesh LODs (DetailGrassClumpShape) and the indirect draws a field issues, one per LOD.
#define PLUTO_DETAIL_GRASS_LODS 3
// Compute workgroup side: candidates are dispatched as 8x8 tiles of a ring's lattice.
#define PLUTO_DETAIL_GRASS_GROUP_SIZE 8
// A thinned clump shrinks into the ground as the kept share falls through this fraction of its rank, instead of
// vanishing at once (DetailGrassLattice::Fade).
#define PLUTO_DETAIL_GRASS_RANK_FADE 0.35
// Share of the draw distance over which the whole field shrinks away.
#define PLUTO_DETAIL_GRASS_DISTANCE_FADE 0.2
// Clumps shrunk below this height (metres) are not emitted.
#define PLUTO_DETAIL_GRASS_MIN_HEIGHT 0.01
// Integer hash salts: one per independent random value a candidate draws.
#define PLUTO_DETAIL_GRASS_SALT_RANK 0x1u
#define PLUTO_DETAIL_GRASS_SALT_JITTER_X 0x2u
#define PLUTO_DETAIL_GRASS_SALT_JITTER_Z 0x3u
#define PLUTO_DETAIL_GRASS_SALT_HEIGHT 0x4u
#define PLUTO_DETAIL_GRASS_SALT_WIDTH 0x5u
#define PLUTO_DETAIL_GRASS_SALT_YAW 0x6u
#define PLUTO_DETAIL_GRASS_SALT_COLOUR 0x7u
// Patch noise for dry and healthy colour regions: lattice salt.
#define PLUTO_DETAIL_GRASS_SALT_PATCH 0x8u
