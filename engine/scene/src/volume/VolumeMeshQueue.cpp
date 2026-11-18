#include "PlutoGE/scene/volume/VolumeMeshQueue.h"

#include <chrono>

namespace PlutoGE::scene::volume
{
    VolumeMeshQueue::VolumeMeshQueue(std::shared_ptr<const VolumeMesher> mesher)
        : m_mesher(mesher ? std::move(mesher) : std::make_shared<SurfaceNetsMesher>())
    {
    }

    VolumeMeshQueue::~VolumeMeshQueue()
    {
        Clear();
    }

    void VolumeMeshQueue::Enqueue(const TerrainVolume &volume, VolumeChunkCoord chunk, Finish finish)
    {
        const auto &grid = volume.GetGrid();
        const auto region = grid.ChunkSamples(chunk);
        auto block = volume.Gather(m_mesher->RequiredSamples(region));
        const std::uint64_t ticket = m_nextTicket++;
        m_latest[chunk] = ticket;
        m_jobs.push_back(Job{
            .chunk = chunk,
            .ticket = ticket,
            .mesh = std::async(std::launch::async,
                               [mesher = m_mesher, block = std::move(block), region, grid, chunk, finish = std::move(finish)] {
                                   auto mesh = mesher->Mesh(block, region, grid, grid.ChunkOrigin(chunk));
                                   if (finish)
                                       finish(chunk, mesh);
                                   return mesh;
                               }),
        });
    }

    std::vector<VolumeMeshQueue::Result> VolumeMeshQueue::Collect(std::size_t maxResults, bool wait)
    {
        std::vector<Result> results;
        for (auto job = m_jobs.begin(); job != m_jobs.end();)
        {
            if (!wait && results.size() >= maxResults)
                break;
            if (!wait && job->mesh.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
            {
                ++job;
                continue;
            }
            auto mesh = job->mesh.get();
            const auto latest = m_latest.find(job->chunk);
            if (latest != m_latest.end() && latest->second == job->ticket)
            {
                m_latest.erase(latest);
                results.push_back({job->chunk, std::move(mesh)});
            }
            job = m_jobs.erase(job);
        }
        return results;
    }

    void VolumeMeshQueue::Clear()
    {
        for (auto &job : m_jobs)
            if (job.mesh.valid())
                job.mesh.wait();
        m_jobs.clear();
        m_latest.clear();
    }
}
