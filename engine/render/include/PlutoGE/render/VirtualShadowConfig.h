#pragma once
// Shared by C++ and Slang. Keep addressing and resource sizes in one place.
// Directional clipmaps: fine levels 0..N-1, each twice the span of the last, then the coarse root at level N. N grows
// with the shadow distance (VirtualShadowParameters::levels.x) so the finest level stays at most
// PLUTO_VSM_MAX_FINEST_SPAN metres across; level slots above the root, up to the spot levels, are unused.
#define PLUTO_VSM_MIN_FINE_LEVELS 4
#define PLUTO_VSM_MAX_FINE_LEVELS 8
#define PLUTO_VSM_MAX_FINEST_SPAN 64.0
#define PLUTO_VSM_DIRECTIONAL_LEVELS (PLUTO_VSM_MAX_FINE_LEVELS + 1)
#define PLUTO_VSM_SPOT_COUNT 4
#define PLUTO_VSM_SPOT_GRID 16
#define PLUTO_VSM_SPOT_ROOT_GRID 2
#define PLUTO_VSM_SPOT_ROOT_BASE (PLUTO_VSM_DIRECTIONAL_LEVELS + PLUTO_VSM_SPOT_COUNT)
#define PLUTO_VSM_LEVELS (PLUTO_VSM_DIRECTIONAL_LEVELS + 2 * PLUTO_VSM_SPOT_COUNT)
// Caster level masks (VirtualShadowCaster::identity.w) hold one bit per level, so there are at most 32 levels.
#define PLUTO_VSM_ALL_LEVELS ((1u << PLUTO_VSM_LEVELS) - 1u)
#define PLUTO_VSM_ROOT_GRID 8
#define PLUTO_VSM_ROOT_PAGES (PLUTO_VSM_ROOT_GRID * PLUTO_VSM_ROOT_GRID)
// Virtual pages per axis of `level` when the directional root is level `root` (spot fine levels scale by resolution).
#define PLUTO_VSM_LEVEL_GRID(level, root) ((level) >= PLUTO_VSM_SPOT_ROOT_BASE ? PLUTO_VSM_SPOT_ROOT_GRID : ((level) >= PLUTO_VSM_DIRECTIONAL_LEVELS ? PLUTO_VSM_SPOT_GRID : ((level) == (root) ? PLUTO_VSM_ROOT_GRID : PLUTO_VSM_GRID)))
#define PLUTO_VSM_GRID 128
#define PLUTO_VSM_PAGE_SIZE 128
#define PLUTO_VSM_ATLAS_TILES 32
#define PLUTO_VSM_DEFAULT_POOL_PAGES 256
#define PLUTO_VSM_STATS_HISTORICAL_AGE 18
#define PLUTO_VSM_STATS_UPDATED_TRIANGLES_BASE 19
#define PLUTO_VSM_STATS_DIRTY_TRIANGLES_BASE (PLUTO_VSM_STATS_UPDATED_TRIANGLES_BASE + PLUTO_VSM_LEVELS)
#define PLUTO_VSM_STATS_MAX_DIRTY_TRIANGLES (PLUTO_VSM_STATS_DIRTY_TRIANGLES_BASE + PLUTO_VSM_LEVELS)
#define PLUTO_VSM_STATS_TRIANGLE_DEFERRED (PLUTO_VSM_STATS_MAX_DIRTY_TRIANGLES + 1)
#define PLUTO_VSM_STATS_PAGE_DEFERRED (PLUTO_VSM_STATS_MAX_DIRTY_TRIANGLES + 2)
// Dirty pages without valid depth (new coverage), and resident pages whose casters changed.
#define PLUTO_VSM_STATS_NEW_DIRTY (PLUTO_VSM_STATS_MAX_DIRTY_TRIANGLES + 3)
#define PLUTO_VSM_STATS_CHANGED_DIRTY (PLUTO_VSM_STATS_MAX_DIRTY_TRIANGLES + 4)
// Dirty pages whose update redraws the static casters (with a static layer; otherwise every dirty page).
#define PLUTO_VSM_STATS_STATIC_DIRTY (PLUTO_VSM_STATS_MAX_DIRTY_TRIANGLES + 5)
#define PLUTO_VSM_COUNTER_COUNT (((PLUTO_VSM_STATS_STATIC_DIRTY + 64) / 64) * 64)
#define PLUTO_VSM_PAGE_METADATA_BYTES 96
#define PLUTO_VSM_CAPACITY (PLUTO_VSM_ATLAS_TILES * PLUTO_VSM_ATLAS_TILES)
#define PLUTO_VSM_RESOLUTION (PLUTO_VSM_GRID * PLUTO_VSM_PAGE_SIZE)
#define PLUTO_VSM_ATLAS_SIZE (PLUTO_VSM_ATLAS_TILES * PLUTO_VSM_PAGE_SIZE)
#define PLUTO_VSM_LEVEL_PAGES (PLUTO_VSM_GRID * PLUTO_VSM_GRID)
#define PLUTO_VSM_UPDATE_LIST_OFFSET (PLUTO_VSM_LEVELS + PLUTO_VSM_LEVELS * PLUTO_VSM_LEVEL_PAGES)
#define PLUTO_VSM_MAX_DRAW_CHUNKS 8192
// Caster chunks of at most this many models are planned per instance: each caster/page membership holds a mask of the
// instances whose bounds reach the page, and the page draws only those. Larger (whole-draw fallback) chunks draw every
// model into every page their bounds reach.
#define PLUTO_VSM_MASKED_INSTANCES 16
// A caster's page list entry: physical page slot, chunk-local instance (masked chunks only) and whether it sways.
#define PLUTO_VSM_ENTRY_SLOT_MASK 0xffffu
#define PLUTO_VSM_ENTRY_INSTANCE_SHIFT 16
#define PLUTO_VSM_ENTRY_INSTANCE_MASK 0x7fffu
#define PLUTO_VSM_ENTRY_SWAY 0x80000000u
// Reserve enough instance indices for every physical page and 64 models.
#define PLUTO_VSM_INSTANCE_STRIDE (PLUTO_VSM_CAPACITY * 64)
