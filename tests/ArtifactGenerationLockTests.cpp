#include <PlutoGE/assets/ArtifactGenerationLock.h>
#include <PlutoGE/assets/AssetStorageMap.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace
{
    using namespace PlutoGE::assets;
    void Require(bool value, const std::string &message) { if (!value) throw std::runtime_error(message); }
    struct Scratch
    {
        std::filesystem::path parent = std::filesystem::canonical(std::filesystem::temp_directory_path());
        std::filesystem::path root = parent / ("PlutoGE-generation-lock-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        Scratch() { std::filesystem::create_directories(root / "Library/Artifacts"); }
        ~Scratch()
        {
            if (root.parent_path() == parent && root.filename().string().starts_with("PlutoGE-generation-lock-"))
            { std::error_code ignored; std::filesystem::remove_all(root, ignored); }
        }
    };
}
int main(int argc, char **argv) try
{
    using namespace PlutoGE::assets;
    PlutoGE::content::ContentDigest key{}; key[0] = 1;
    if (argc == 4 && std::string_view(argv[1]) == "--probe")
    {
        ArtifactGenerationLock probe;
        const auto mode = std::string_view(argv[3]) == "reader" ? ArtifactGenerationLockMode::SharedReader : ArtifactGenerationLockMode::ExclusiveCollector;
        return probe.TryAcquire(argv[2], key, mode) ? 0 : 3;
    }
    Scratch scratch;
#ifdef _WIN32
    const auto probeProcess = [&](bool reader, DWORD expected)
    {
        const auto executable = std::filesystem::absolute(argv[0]).wstring();
        auto command = L"\"" + executable + L"\" --probe \"" + scratch.root.wstring() + L"\" " + (reader ? L"reader" : L"collector");
        STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
        Require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process), "Cannot launch independent lock probe");
        CloseHandle(process.hThread);
        const auto wait = WaitForSingleObject(process.hProcess, 10000);
        DWORD status = 1; const bool read = GetExitCodeProcess(process.hProcess, &status);
        if (wait != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1);
        CloseHandle(process.hProcess);
        Require(wait == WAIT_OBJECT_0 && read && status == expected, "Independent process violated generation ownership");
    };
#endif
    const auto generation = scratch.root / "Library/Artifacts" / PlutoGE::content::DigestToHex(key);
    std::filesystem::create_directory(generation);
    std::string error;
    Require(!ArtifactGenerationLock{}.TryAcquire(scratch.root, {}, ArtifactGenerationLockMode::SharedReader, &error), "Empty key was leased");
    {
        auto first = std::make_shared<ArtifactGenerationLock>();
        Require(first->TryAcquire(scratch.root, key, ArtifactGenerationLockMode::SharedReader, &error) && first->IsHeld(), error);
        ArtifactGenerationLock second;
        Require(second.TryAcquire(scratch.root, key, ArtifactGenerationLockMode::SharedReader, &error), error);
        Require(!ArtifactGenerationLock{}.TryAcquire(scratch.root, key, ArtifactGenerationLockMode::ExclusiveCollector, &error),
            "Collector acquired a generation with live readers");
        Require(!first->TryAcquire(scratch.root, key, ArtifactGenerationLockMode::SharedReader, &error), "Lease owner reacquired its lock");
        #ifdef _WIN32
        probeProcess(true, 0); probeProcess(false, 3);
        #endif
        // Immutable storage copies retain the lease after the original owner exits.
        ImportedAssetStorage storage{"project://Mesh.plutomesh", generation / "Files/Mesh.plutomesh", {}, true, first};
        auto copy = storage; storage.generationLease.reset(); first.reset();
        Require(!ArtifactGenerationLock{}.TryAcquire(scratch.root, key, ArtifactGenerationLockMode::ExclusiveCollector, &error),
            "Copied storage entry did not retain generation ownership");
        copy.generationLease.reset();
    }
    {
        ArtifactGenerationLock collector;
        Require(collector.TryAcquire(scratch.root, key, ArtifactGenerationLockMode::ExclusiveCollector, &error), error);
        Require(!ArtifactGenerationLock{}.TryAcquire(scratch.root, key, ArtifactGenerationLockMode::SharedReader, &error),
            "Reader acquired a generation being collected");
        Require(!ArtifactGenerationLock{}.TryAcquire(scratch.root, key, ArtifactGenerationLockMode::ExclusiveCollector, &error),
            "Two collectors owned the same generation");
        #ifdef _WIN32
        probeProcess(true, 3); probeProcess(false, 3);
        #endif
        std::filesystem::remove(generation);
    }
    Require(!ArtifactGenerationLock{}.TryAcquire(scratch.root, key, ArtifactGenerationLockMode::SharedReader, &error),
        "Removed generation was leased");
    std::filesystem::create_directory(generation);
    Require(ArtifactGenerationLock{}.TryAcquire(scratch.root, key, ArtifactGenerationLockMode::SharedReader, &error),
        "Stable lock file could not be reused after generation recreation");
    std::filesystem::remove(generation);
    { std::ofstream file(generation); file << "not a directory"; }
    Require(!ArtifactGenerationLock{}.TryAcquire(scratch.root, key, ArtifactGenerationLockMode::SharedReader, &error),
        "Non-directory generation was leased");
    std::cout << "Generation lease tests passed\n";
    return 0;
}
catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
