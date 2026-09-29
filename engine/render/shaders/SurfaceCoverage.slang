// Shared inline coverage for surface and depth shaders. Keep discard in the
// caller: returning a material struct through a discard-capable helper can
// leave derivative helper lanes undefined on some drivers.
    if (alphaMode == 1 && sampledColor.a < alphaCutoff) discard;
    if (alphaMode == 2 && glassParameters.x < 0.5 && shaderGraphFrame.z < 0.5)
    {
        // Complementary coverage tests keep solid BLEND texels depth-correct,
        // including overlapping triangles inside one draw. Evaluate after the
        // shader graph so animated opacity follows the same rule in both passes.
        // Exported interior atlases can encode solid regions as 253/255 alpha.
        // Treat the final 1% as opaque; retain ordinary fractional coverage.
        const bool opaqueCoverage = sampledColor.a >= 0.99;
        if (transparentPass == opaqueCoverage) discard;
        if (!transparentPass) sampledColor.a = 1.0;
    }
