#pragma once

#include "PlutoGE/scene/volume/TerrainVolume.h"
#include "PlutoGE/scene/volume/VolumeMesher.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <vector>

namespace PlutoGE::scene::volume
{
    // Meshes a volume's chunks off the calling thread. Enqueue gathers a chunk's samples on the caller's thread (the
    // volume is not thread-safe), then a worker meshes them and runs the caller's finishing step. A chunk enqueued again
    // before its earlier job is collected yields only the newer mesh, so edits in quick succession never land out of
    // order.
    class VolumeMeshQueue
    {
    public:
        // Runs on the worker after meshing, with the chunk and its mesh: fitting to a terrain, building a collider.
        // It must only read data it owns or immutable snapshots.
        using Finish = std::function<void(VolumeChunkCoord, VolumeMesh &)>;

        struct Result
        {
            VolumeChunkCoord chunk;
            VolumeMesh mesh;
        };

        explicit VolumeMeshQueue(std::shared_ptr<const VolumeMesher> mesher);
        // Waits for running jobs (their results are dropped).
        ~VolumeMeshQueue();
        VolumeMeshQueue(const VolumeMeshQueue &) = delete;
        VolumeMeshQueue &operator=(const VolumeMeshQueue &) = delete;

        [[nodiscard]] const VolumeMesher &GetMesher() const { return *m_mesher; }
        void Enqueue(const TerrainVolume &volume, VolumeChunkCoord chunk, Finish finish = {});
        // Finished meshes, oldest first, at most `maxResults` (superseded ones are dropped and not counted). With
        // `wait`, blocks until every job has finished, and returns them all.
        std::vector<Result> Collect(std::size_t maxResults, bool wait = false);
        // Jobs whose results have not been collected (including superseded ones still running).
        [[nodiscard]] std::size_t GetPendingCount() const { return m_jobs.size(); }
        [[nodiscard]] bool Idle() const { return m_jobs.empty(); }
        // Drops every job; running ones finish in the background and are waited for here.
        void Clear();

    private:
        struct Job
        {
            VolumeChunkCoord chunk;
            std::uint64_t ticket = 0;
            std::future<VolumeMesh> mesh;
        };

        std::shared_ptr<const VolumeMesher> m_mesher;
        std::deque<Job> m_jobs;
        std::map<VolumeChunkCoord, std::uint64_t> m_latest; // the ticket whose result counts, per chunk
        std::uint64_t m_nextTicket = 1;
    };
}
