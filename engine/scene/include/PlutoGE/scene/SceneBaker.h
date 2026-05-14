#pragma once

#include <string>

namespace PlutoGE::scene
{
    class Scene;

    struct SceneBakeSettings
    {
        int lightmapResolution = 64;
        int probeDirectionCount = 8;
        int indirectBounceSampleCount = 6;
        bool bakeProbeVolume = true;
        bool bakeIndirectBounce = true;
        float probeBounceStrength = 0.65f;
        float lightmapBounceStrength = 0.75f;

        static SceneBakeSettings FastPreview();
        static SceneBakeSettings BalancedPreview();
        static SceneBakeSettings Final();
    };

    struct SceneBakeResult
    {
        bool succeeded = false;
        int bakedLightmapCount = 0;
        int bakedProbeCount = 0;
        std::string message;
    };

    class SceneBaker
    {
    public:
        SceneBakeResult Bake(Scene &scene, const SceneBakeSettings &settings) const;
        SceneBakeResult Bake(Scene &scene) const;
    };
}