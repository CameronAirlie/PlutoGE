#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>

class SharedTexturePublisher
{
public:
    struct Frame
    {
        std::uint64_t sequence = 0;
        std::uint64_t generation = 0;
        std::uint32_t slot = 0;
        int width = 0;
        int height = 0;
        std::uintptr_t remoteHandle = 0;
    };

    SharedTexturePublisher();
    ~SharedTexturePublisher();

    SharedTexturePublisher(const SharedTexturePublisher &) = delete;
    SharedTexturePublisher &operator=(const SharedTexturePublisher &) = delete;

    bool Initialize(std::uint32_t electronProcessId, std::string &error);
    bool CanPublish(int width, int height, std::size_t maxFramesInFlight, std::string &error);
    std::optional<Frame> Publish(int width, int height, std::string &error);
    void Release(std::uint64_t generation, std::uint32_t slot);
    void Shutdown();
    [[nodiscard]] bool IsAvailable() const;

private:
    struct Implementation;
    std::unique_ptr<Implementation> m_implementation;
};
