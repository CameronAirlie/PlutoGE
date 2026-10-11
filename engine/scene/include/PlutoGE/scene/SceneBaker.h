#pragma once

#include <memory>
#include <optional>
#include <cstdint>
#include <string>
#include <vector>
#include <glm/glm.hpp>

namespace PlutoGE::scene
{
    class Scene;
    struct BakedProbeVolume;
    struct ReflectionProbePlacement { glm::vec3 position{0}, size{1}; };

    struct SceneBakeSettings
    {
        int lightmapResolution = 64;
        int lightmapTileSize = 16;
        int directShadowSampleCount = 8;
        int probeDirectionCount = 64;
        float probeSpacing = 2.0f;
        float probeSurfaceClearance = 0.02f;
        std::uint32_t ambientVolumeEntity = 0; // Zero bakes all enabled ambient volume components.
        std::optional<glm::vec3> probeVolumeOrigin, probeVolumeSize; // Snapshot of component bounds.
        std::optional<glm::mat3> probeGridBasis;
        int maxProbeCount = 4096;
        int indirectBounceSampleCount = 6;
        int indirectBounceCount = 2;
        int indirectDenoisePassCount = 2;
        bool bakeProbeVolume = true;
        bool probesOnly = false;
        bool logProbeDiagnostics = false; // Read-only source attribution for bake troubleshooting.
        bool bakeIndirectBounce = true;
        bool useGpu = false;
        float probeBounceStrength = 0.65f;
        float lightmapBounceStrength = 0.75f;

        static SceneBakeSettings FastPreview();
        static SceneBakeSettings BalancedPreview();
        static SceneBakeSettings Final();
        static SceneBakeSettings HighQuality();
        static SceneBakeSettings Ultra();
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
        static glm::ivec3 CalculateProbeGridResolution(glm::vec3 size, float spacing, int budget);
        static std::vector<ReflectionProbePlacement> GenerateReflectionProbeLayout(const BakedProbeVolume &volume, int count = 4);
        SceneBakeResult Bake(Scene &scene, const SceneBakeSettings &settings) const;
        SceneBakeResult Bake(Scene &scene) const;
        std::unique_ptr<SceneBakeTask> BeginBake(Scene &scene, const SceneBakeSettings &settings, SceneBakeResult *outImmediateResult = nullptr) const;
    };
}
