// Load-time machinery: the load profile (core/LoadProfile.h), persisted imported-file checks
// (assets::ImportedFileCheckCache) and background Vulkan pipeline compilation (rhi/vulkan/VulkanPipelines.h).
#include "PlutoGE/assets/AssetDatabase.h"
#include "PlutoGE/core/CpuTrace.h"
#include "PlutoGE/core/LoadProfile.h"
#include "PlutoGE/platform/ContentDigest.h"
#include "rhi/vulkan/VulkanPipelines.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

using namespace PlutoGE;

namespace
{
    void Require(bool value, const char *message)
    {
        if (!value) throw std::runtime_error(message);
    }

    struct Scratch
    {
        std::filesystem::path root = std::filesystem::temp_directory_path() /
            ("PlutoGE-load-time-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Scratch() { std::filesystem::create_directories(root); }
        ~Scratch() { std::error_code error; std::filesystem::remove_all(root, error); }
    };

    void WriteFile(const std::filesystem::path &path, const std::string &text)
    {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream(path, std::ios::binary | std::ios::trunc) << text;
    }

    std::string ReadFile(const std::filesystem::path &path)
    {
        std::ifstream input(path, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), {}};
    }

    void Profile(const Scratch &scratch)
    {
        auto &profile = core::LoadProfile::Get();
        // Disabled: nothing is recorded.
        {
            const core::LoadProfile::Phase phase("Ignored", true);
            const core::CpuScope scope("Ignored scope");
        }
        Require(profile.Milestones().empty() && profile.Scopes().empty(), "A disabled profile records nothing");

        const auto report = scratch.root / "profile.txt";
        profile.Enable(report);
        profile.Mark("Start");
        {
            const core::LoadProfile::Phase phase("Loading", true);
            {
                const core::CpuScope outer("Outer");
                const core::CpuScope inner("Inner");
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            {
                // A phase inside a phase is a scope of it; recursion counts its time once.
                const core::LoadProfile::Phase nested("Nested");
                const core::CpuScope again("Outer");
                const core::CpuScope deeper("Outer");
            }
        }
        const auto milestones = profile.Milestones();
        Require(milestones.size() == 2 && milestones[0].name == "Start" && milestones[1].name == "Loading" &&
                    milestones[1].ms >= milestones[0].ms,
                "Milestones are marked in order, a marking phase at its end");
        const auto scopes = profile.Scopes();
        const auto find = [&](const char *name) {
            const auto found = std::find_if(scopes.begin(), scopes.end(), [&](const auto &scope) { return scope.name == name; });
            if (found == scopes.end()) throw std::runtime_error(std::string("Missing scope ") + name);
            return *found;
        };
        const auto loading = find("Loading"), outer = find("Outer"), inner = find("Inner"), nested = find("Nested");
        Require(loading.calls == 1 && nested.calls == 1 && inner.calls == 1, "Each scope is counted per call");
        Require(outer.calls == 3 && outer.totalMs <= loading.totalMs + 0.5, "Recursive scopes count their time once");
        Require(inner.selfMs >= 4.0 && outer.selfMs < inner.selfMs, "Self time excludes nested scopes");
        Require(profile.Write() && ReadFile(report).find("Loading") != std::string::npos, "The report names its scopes");
    }

    void FileChecks(const Scratch &scratch)
    {
        const auto library = scratch.root / "Project" / "Library";
        const auto artifact = library / "Artifacts" / "abc" / "mesh.bin";
        WriteFile(artifact, "original bytes");
        content::ContentDigest digest{};
        Require(content::HashFileContent(artifact, digest), "The fixture hashes");
        const auto checksFile = library / std::string(assets::kImportedFileChecksFileName);

        assets::ImportedFileCheckCache first;
        Require(first.Verify(artifact, digest), "A matching file verifies");
        // Resolved directories can end in a separator; files below still belong to them.
        Require(first.Save(checksFile, library / ""), "Checks save");
        Require(ReadFile(checksFile).find("mesh.bin") != std::string::npos, "Saved checks name the Library's files");

        // Same size and modification time: a later session trusts the check without hashing again.
        const auto modified = std::filesystem::last_write_time(artifact);
        WriteFile(artifact, "tampered bytes");
        std::filesystem::last_write_time(artifact, modified);
        assets::ImportedFileCheckCache second;
        second.Load(checksFile, library);
        Require(second.Verify(artifact, digest), "Loaded checks are trusted for unchanged files");
        // Unchanged since it was loaded: nothing to write.
        const auto before = std::filesystem::last_write_time(checksFile);
        Require(second.Save(checksFile, library) && std::filesystem::last_write_time(checksFile) == before,
                "An unchanged cache does not rewrite its file");
        // A changed file is hashed again (and fails).
        std::filesystem::last_write_time(artifact, modified + std::chrono::seconds(5));
        Require(!second.Verify(artifact, digest), "A changed file is verified again");

        // Damaged files add nothing: the tampered file (with its old time) is hashed, not trusted.
        std::filesystem::last_write_time(artifact, modified);
        WriteFile(checksFile, "PLUTOFILECHECKS\t1\nArtifacts/abc/mesh.bin\tnot-a-size\t0\tzz\n");
        assets::ImportedFileCheckCache third;
        third.Load(checksFile, library);
        Require(!third.Verify(artifact, digest), "A damaged checks file is ignored, not trusted");
    }

    VkPipeline FakePipeline(std::uintptr_t value)
    {
#if VK_USE_64_BIT_PTR_DEFINES
        return reinterpret_cast<VkPipeline>(value);
#else
        return static_cast<VkPipeline>(value);
#endif
    }

    void PipelineCompiles()
    {
        using render::rhi::vulkan::VulkanPipelineCompiler;
        // Serial: compiled as submitted.
        {
            VulkanPipelineCompiler serial(0);
            const auto job = serial.Submit([](VkPipeline &pipeline) { pipeline = FakePipeline(1); return VK_SUCCESS; });
            Require(serial.IsSerial() && job->IsDone() && job->Resolve() == FakePipeline(1), "A serial compiler compiles at once");
        }
        // Background: a resolver takes a compile no worker has started, rather than waiting behind a long one.
        {
            std::mutex mutex;
            std::condition_variable release;
            bool released = false;
            std::atomic<int> idleCalls{0};
            VulkanPipelineCompiler compiler(1);
            compiler.SetIdleCallback([&] { ++idleCalls; });
            const auto slow = compiler.Submit([&](VkPipeline &pipeline) {
                std::unique_lock lock(mutex);
                release.wait(lock, [&] { return released; });
                pipeline = FakePipeline(2);
                return VK_SUCCESS;
            });
            const auto callerThread = std::this_thread::get_id();
            std::thread::id compiledOn;
            const auto quick = compiler.Submit([&](VkPipeline &pipeline) {
                compiledOn = std::this_thread::get_id();
                pipeline = FakePipeline(3);
                return VK_SUCCESS;
            });
            const auto failing = compiler.Submit([](VkPipeline &) { return VK_ERROR_OUT_OF_HOST_MEMORY; });
            Require(quick->Resolve() == FakePipeline(3) && compiledOn == callerThread, "A queued compile runs on its resolver");
            VkResult result = VK_SUCCESS;
            Require(!failing->Resolve(&result) && result == VK_ERROR_OUT_OF_HOST_MEMORY, "A failed compile reports its result");
            Require(!slow->IsDone(), "The long compile is still running");
            {
                std::lock_guard lock(mutex);
                released = true;
            }
            release.notify_all();
            Require(slow->Resolve() == FakePipeline(2), "Resolving waits for a running compile");
            for (int wait = 0; wait < 200 && idleCalls == 0; ++wait)
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            Require(idleCalls >= 1, "The compiler reports when it goes idle");
        }
        // Destruction finishes queued compiles.
        {
            std::atomic<int> compiled{0};
            {
                VulkanPipelineCompiler compiler(2);
                for (int i = 0; i < 16; ++i)
                    (void)compiler.Submit([&](VkPipeline &pipeline) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                        ++compiled;
                        pipeline = FakePipeline(4);
                        return VK_SUCCESS;
                    });
            }
            Require(compiled == 16, "Every submitted compile runs before the compiler is gone");
        }
    }
}

int main()
try
{
    Scratch scratch;
    Profile(scratch);
    FileChecks(scratch);
    PipelineCompiles();
    std::cout << "Load time tests passed\n";
    return 0;
}
catch (const std::exception &error)
{
    std::cerr << error.what() << '\n';
    return 1;
}
