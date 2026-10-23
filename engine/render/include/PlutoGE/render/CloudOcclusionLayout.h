#pragma once
// Shared C++/Slang layout of the cloud occlusion block carried by the frame
// lighting parameters. CloudOcclusion.cpp packs it and CloudOcclusion.slang
// reads it. Volumes beyond this count do not occlude lighting outside the
// cloud pass; the nearest volumes are kept.
#define PLUTO_CLOUD_OCCLUSION_VOLUMES 4
