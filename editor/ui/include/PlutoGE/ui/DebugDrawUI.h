#pragma once
namespace PlutoGE::ui
{
    struct AmbientProbeDebugSettings {
        bool diffuse = false, reflections = false, rejected = false;
        float diffuseRadius = 0.15f, reflectionRadius = 0.5f, reflectionRoughness = 0.02f;
        int maxDiffuseSpheres = 512;
    };
    AmbientProbeDebugSettings &GetAmbientProbeDebugSettings();
    void OpenDebugDrawControls();
    void RenderDebugDrawControls();
}
