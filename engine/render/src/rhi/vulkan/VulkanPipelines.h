#pragma once

#include <volk.h>

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <thread>
#include <vector>

// How the Vulkan device turns pipeline descriptions into driver pipelines without making its callers wait for each
// driver compile in turn. A large uber-shader takes the driver many seconds to compile when its own shader cache is
// cold (a game's first launch, or after a driver update), so compiling serially at start-up costs the sum of them all.
namespace PlutoGE::render::rhi::vulkan
{
    // Compiles pipelines on worker threads. A pipeline is only needed once something binds it: Resolve waits for it,
    // or compiles it on the calling thread when no worker has started it yet, so a small pipeline needed now never
    // waits behind large ones queued earlier. Thread-safe.
    class VulkanPipelineCompiler
    {
    public:
        // The driver call: fills the pipeline (and does any clean-up of what it used), returning its result.
        using Compile = std::function<VkResult(VkPipeline &pipeline)>;

        // One pipeline's compile. Immutable once done; any number of threads may resolve it.
        class Job
        {
        public:
            explicit Job(Compile compile) : m_compile(std::move(compile)) {}
            // The compiled pipeline, compiling it here if no worker has started; VK_NULL_HANDLE when compilation
            // failed (`result` then says why).
            VkPipeline Resolve(VkResult *result = nullptr);
            // Compiles unless a worker or a resolver already did or is doing so. Returns at once in that case.
            void RunIfQueued();
            // Whether the compile finished (successfully or not), without waiting.
            [[nodiscard]] bool IsDone() const noexcept { return m_finished.load(std::memory_order_acquire); }

        private:
            enum class State { Queued, Running, Done };
            std::mutex m_mutex;
            std::condition_variable m_done;
            State m_state = State::Queued;
            Compile m_compile;
            VkResult m_result = VK_SUCCESS;
            std::atomic<VkPipeline> m_pipeline{VK_NULL_HANDLE};
            std::atomic<bool> m_finished{false};
        };

        // `workers` 0 compiles each pipeline as it is submitted, on the submitting thread (a serial device).
        explicit VulkanPipelineCompiler(unsigned workers);
        // Finishes every submitted compile first: their pipelines and clean-up belong to the device.
        ~VulkanPipelineCompiler();
        VulkanPipelineCompiler(const VulkanPipelineCompiler &) = delete;
        VulkanPipelineCompiler &operator=(const VulkanPipelineCompiler &) = delete;

        [[nodiscard]] std::shared_ptr<Job> Submit(Compile compile);
        [[nodiscard]] bool IsSerial() const noexcept { return m_workers.empty(); }
        // Called (on the thread that finished last) whenever every submitted compile has finished after at least one
        // new one did: a moment to persist what was compiled.
        void SetIdleCallback(std::function<void()> callback);
        // Workers for a machine: half its hardware threads (the game and render threads keep theirs), 2 to 8;
        // PLUTO_PIPELINE_COMPILE_THREADS overrides it (0 for serial compiles, for comparisons).
        [[nodiscard]] static unsigned DefaultWorkerCount() noexcept;

    private:
        void Work();
        // Fires the idle callback when nothing is queued or compiling and something compiled since it last fired.
        void NotifyIfIdle();

        std::mutex m_mutex;
        std::condition_variable m_wake;
        std::deque<std::shared_ptr<Job>> m_queue;
        bool m_stopping = false;
        std::size_t m_compiling = 0, m_compiledSinceIdle = 0;
        std::function<void()> m_idle;
        std::vector<std::thread> m_workers;
    };

    // A VkPipelineCache kept on disk between runs, so a warm start does not depend on the driver's own cache (which
    // drivers key by executable path, and may evict). Data from another device or driver version is discarded.
    class VulkanPipelineCacheFile
    {
    public:
        // The cache file for this executable under the user's local cache directory; empty where there is none, or
        // when PLUTO_PIPELINE_CACHE=0.
        [[nodiscard]] static std::filesystem::path DefaultPath();

        // Creates the cache from `path` when its data suits `properties`' device and driver, else empty.
        VulkanPipelineCacheFile(VkDevice device, const VkPhysicalDeviceProperties &properties, std::filesystem::path path);
        ~VulkanPipelineCacheFile();
        VulkanPipelineCacheFile(const VulkanPipelineCacheFile &) = delete;
        VulkanPipelineCacheFile &operator=(const VulkanPipelineCacheFile &) = delete;

        [[nodiscard]] VkPipelineCache Get() const noexcept { return m_cache; }
        // Held by each pipeline compile using the cache, so Save never reads it while one adds to it.
        [[nodiscard]] std::shared_lock<std::shared_mutex> Use() const { return std::shared_lock(m_access); }
        // Writes the cache's current data (atomically) when it changed since the last write. Any thread; call
        // before the device is destroyed.
        bool Save(std::string *error = nullptr) const;

    private:
        VkDevice m_device = VK_NULL_HANDLE;
        VkPipelineCache m_cache = VK_NULL_HANDLE;
        std::filesystem::path m_path;
        mutable std::shared_mutex m_access;
        mutable std::mutex m_saving;
        mutable std::size_t m_savedSize = 0;
    };
}
