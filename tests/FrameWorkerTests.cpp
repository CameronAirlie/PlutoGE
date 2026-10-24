#include "PlutoGE/core/FrameWorker.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using PlutoGE::core::FrameWorker;

namespace
{
    void Require(bool condition, const std::string &message)
    {
        if (!condition) throw std::runtime_error(message);
    }

    // Jobs run one at a time, in submission order, on a thread other than the caller's.
    void RunsJobsInOrderOnItsThread()
    {
        FrameWorker worker;
        std::vector<int> order;
        std::atomic<bool> otherThread{true};
        const auto caller = std::this_thread::get_id();
        for (int index = 0; index < 50; ++index)
            worker.Submit([&, index]
            {
                order.push_back(index);
                if (std::this_thread::get_id() == caller) otherThread = false;
            });
        worker.Wait();
        Require(otherThread, "a job ran on the caller's thread");
        Require(order.size() == 50, "a job did not run");
        for (int index = 0; index < 50; ++index)
            Require(order[index] == index, "jobs ran out of order");
        Require(!worker.IsBusy(), "worker busy after Wait");
    }

    // Submit returns while the job runs, so the caller's next frame overlaps it.
    void JobOverlapsTheCaller()
    {
        FrameWorker worker;
        std::atomic<bool> release{false}, started{false};
        worker.Submit([&]
        {
            started = true;
            while (!release) std::this_thread::yield();
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!started && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
        Require(started && worker.IsBusy(), "the job did not run concurrently with the caller");
        release = true;
        worker.Wait();
    }

    // A failed job reports at the next synchronization point and leaves the worker usable.
    void FailuresSurfaceOnce()
    {
        FrameWorker worker;
        worker.Submit([] { throw std::runtime_error("frame failed"); });
        bool caught = false;
        try { worker.Wait(); }
        catch (const std::runtime_error &error) { caught = std::string(error.what()) == "frame failed"; }
        Require(caught, "Wait did not rethrow the job's exception");
        worker.Wait();   // Reported once.
        std::atomic<int> ran{0};
        worker.Submit([&] { ++ran; });
        worker.Wait();
        Require(ran == 1, "the worker did not recover after a failed job");
        worker.Submit([] { throw std::runtime_error("unobserved"); });
        caught = false;
        try { worker.Submit([] {}); }
        catch (const std::runtime_error &) { caught = true; }
        Require(caught, "Submit did not report the previous job's failure");
    }

    void DestructorWaitsForTheJob()
    {
        std::atomic<bool> finished{false};
        {
            FrameWorker worker;
            worker.Submit([&]
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                finished = true;
            });
        }
        Require(finished, "the destructor did not wait for the job in flight");
    }
}

int main()
{
    try
    {
        RunsJobsInOrderOnItsThread();
        JobOverlapsTheCaller();
        FailuresSurfaceOnce();
        DestructorWaitsForTheJob();
        std::cout << "Frame worker tests passed\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
