// How much of a voxel's light a ray loses, shared by the voxelizer's consumers (docs/FOLIAGE_TRANSMISSION.md).
//
// Solid surfaces fill their voxels: a fragment adds a full footprint of weight, and the voxel is opaque.
//
// Foliage (materials with diffuse transmission) is a cloud of leaves that covers part of each voxel. Its fragments add
// their alpha at the voxel's mip, so a voxel's weight is the leaf area over its face: an optical depth. Light through
// such a medium falls exponentially, with half the area facing the ray on average for randomly turned leaves.
//
// Opacity stays below one half however dense the leaves. Rays read a value of one half or more as a solid surface
// (DDGIVoxelTrace.slang: a wall thinner than a voxel filters to at least one half, which they make opaque), so without
// this a dense canopy would turn back into a roof.
#ifndef PLUTO_VCT_VOXEL_OPACITY
#define PLUTO_VCT_VOXEL_OPACITY

// Set in a surface record (VCTVoxelize.slang) by solid surfaces, so a voxel holding both records a solid one (records keep
// the largest value). Bit 31 marks a record present.
static const uint VctRecordSolid = 0x40000000u;
static const uint VctRecordPresent = 0x80000000u;
static const float VctFoliageMaximumOpacity = 0.49;
// Alpha below which a leaf fragment adds nothing worth its atomics.
static const float VctFoliageMinimumAlpha = 0.02;

bool vctRecordIsFoliage(uint record)
{
    return (record & (VctRecordPresent | VctRecordSolid)) == VctRecordPresent;
}

float vctVoxelOpacity(uint weight, uint record)
{
    const float coverage = float(weight) / 4095.0;
    return vctRecordIsFoliage(record) ? min(1.0 - exp(-0.5 * coverage), VctFoliageMaximumOpacity) : saturate(coverage);
}

#endif
