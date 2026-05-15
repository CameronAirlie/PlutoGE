#pragma once

#include <memory>
#include <string>

namespace PlutoGE::scene
{
    class Scene;

    struct SceneBakeSettings
    {
        int lightmapResolution = 64;
        int lightmapTileSize = 16;
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

    class SceneBakeTask
    {
    public:
        SceneBakeTask(const SceneBakeTask &) = delete;
        SceneBakeTask &operator=(const SceneBakeTask &) = delete;
        SceneBakeTask(SceneBakeTask &&) noexcept;
        SceneBakeTask &operator=(SceneBakeTask &&) noexcept;
        ~SceneBakeTask();

        void Cancel();
        bool IsRunning() const;
        bool IsFinished() const;
        bool IsCancelled() const;
        std::string GetStatusMessage() const;
        SceneBakeResult Finalize(Scene &scene);

    private:
        struct Impl;
        explicit SceneBakeTask(std::unique_ptr<Impl> impl);

        std::unique_ptr<Impl> m_impl;

        friend class SceneBaker;
    };

    class SceneBaker
    {
    public:
        SceneBakeResult Bake(Scene &scene, const SceneBakeSettings &settings) const;
        SceneBakeResult Bake(Scene &scene) const;
        std::unique_ptr<SceneBakeTask> BeginBake(Scene &scene, const SceneBakeSettings &settings, SceneBakeResult *outImmediateResult = nullptr) const;
    };
}