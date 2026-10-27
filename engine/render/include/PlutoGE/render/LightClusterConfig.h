#pragma once
// Clustered local-light grid shared by LightClusterGrid and the lighting shaders. Tiles split the view's NDC
// rectangle; slices split view depth logarithmically between the nearest and farthest light reach.
#define PLUTO_LIGHT_CLUSTER_X 16
#define PLUTO_LIGHT_CLUSTER_Y 9
#define PLUTO_LIGHT_CLUSTER_Z 24
#define PLUTO_LIGHT_CLUSTER_COUNT (PLUTO_LIGHT_CLUSTER_X * PLUTO_LIGHT_CLUSTER_Y * PLUTO_LIGHT_CLUSTER_Z)
// Local lights (points then spots) the frame parameters carry; one mask bit each.
#define PLUTO_MAX_LOCAL_LIGHTS 32
#define PLUTO_LIGHT_CLUSTER_WORDS ((PLUTO_MAX_LOCAL_LIGHTS + 31) / 32)
#define PLUTO_LIGHT_CLUSTER_GROUP_SIZE 64
