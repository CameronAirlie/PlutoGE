// Shared by the Slang voxelizer and the embedded legacy GLSL voxelizer.
// Clip the original triangle to a unit raster cell, not its conservative bounds.
// Working relative to the cell keeps the area calculation stable on large grids.
float vctCross(float2 a, float2 b) { return a.x * b.y - a.y * b.x; }
float2 vctProject(float3 p, int axis)
{
    return axis == 0 ? p.zy : axis == 1 ? p.xz : p.xy;
}
float vctTriangleCoverage(float2 a, float2 b, float2 c, float2 cell,
                          out float3 barycentric)
{
    a -= cell; b -= cell; c -= cell;
    float2 polygon[8];
    polygon[0] = a; polygon[1] = b; polygon[2] = c;
    int count = 3;
    for (int plane = 0; plane < 4; ++plane)
    {
        float2 clipped[8];
        int nextCount = 0;
        int component = plane / 2;
        float boundary = float(plane % 2);
        float sign = plane % 2 == 0 ? 1.0 : -1.0;
        for (int i = 0; i < count; ++i)
        {
            float2 start = polygon[i], end = polygon[(i + 1) % count];
            float ds = (start[component] - boundary) * sign;
            float de = (end[component] - boundary) * sign;
            if (ds >= 0.0) clipped[nextCount++] = start;
            if ((ds > 0.0 && de < 0.0) || (ds < 0.0 && de > 0.0))
                clipped[nextCount++] = lerp(start, end, ds / (ds - de));
        }
        count = nextCount;
        for (int i = 0; i < count; ++i) polygon[i] = clipped[i];
    }
    barycentric = float3(1.0, 0.0, 0.0);
    if (count < 3) return 0.0;
    float twiceArea = 0.0;
    float2 center = float2(0.0);
    for (int i = 0; i < count; ++i)
    {
        twiceArea += vctCross(polygon[i], polygon[(i + 1) % count]);
        center += polygon[i];
    }
    // A convex combination stays on the original triangle, including at edges.
    center /= float(count);
    float determinant = vctCross(b - a, c - a);
    if (abs(determinant) < 1e-12) return 0.0;
    barycentric.y = vctCross(center - a, c - a) / determinant;
    barycentric.z = vctCross(b - a, center - a) / determinant;
    barycentric.x = 1.0 - barycentric.y - barycentric.z;
    return clamp(abs(twiceArea) * 0.5, 0.0, 1.0);
}
