#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <string>

namespace PlutoGE::core
{
    // A dedicated thread that runs one frame's job at a time, so that work (a render thread's recording, submission
    // and presentation) overlaps whatever the caller does next (simulating the following frame). At most one job is in
    // flight: Submit first waits for the previous job. Wait blocks until the worker is idle and rethrows an exception
    // the finished job threw, so a failure surfaces on the caller's next synchronization point. The destructor waits
    // for the job in flight (discarding its exception) and stops the thread.
    class FrameWorker
    {
    public:
        explicit FrameWorker(std::string threadName = "PlutoGE frame worker");
        ~FrameWorker();
        FrameWorker(const FrameWorker &) = delete;
        FrameWorker &operator=(const FrameWorker &) = delete;

        void Submit(std::function<void()> job);
        void Wait();
        [[nodiscard]] bool IsBusy() const noexcept;
        // How long the last Wait (or Submit's wait) blocked, in milliseconds.
        [[nodiscard]] float LastWaitMilliseconds() const noexcept;

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}
