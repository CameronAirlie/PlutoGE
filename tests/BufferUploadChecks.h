#pragma once
#include "PlutoGE/render/rhi/Resource.h"
#include "PlutoGE/render/rhi/RenderDevice.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

// Multiple copies of mutable CPU input, partial destination updates, arena
// rollover and more submissions than in-flight slots. Every GPU snapshot must
// retain the values from its own submission rather than a later CPU write.
template<class Device>
void CheckLargeBufferUploads(Device &device)
{
    using namespace PlutoGE::render::rhi;
    constexpr std::size_t partSize = 9 * 1024 * 1024;
    constexpr int frames = 7;
    Buffer destination(device, device.CreateBuffer({partSize * 2, BufferUsage::Storage, "Upload regression"}));
    std::vector<std::byte> input(partSize);
    auto &context = device.GetImmediateContext();
    int completed = 0;
    double recordingMs = 0;
    for (int frame = 0; frame < frames; ++frame)
    {
        context.BeginFrame();
        const auto start = std::chrono::steady_clock::now();
        for (unsigned part = 0; part < 2; ++part)
        {
            std::fill(input.begin(), input.end(), std::byte(frame * 2 + part + 1));
            device.UpdateBuffer(destination.Get(), part * partSize, input);
        }
        std::fill(input.begin(), input.end(), std::byte{255});
        recordingMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (!context.QueueBufferReadback(destination.Get(), partSize * 2, [&, frame](std::span<const std::byte> snapshot) {
            if (snapshot.size() != partSize * 2) throw std::runtime_error("Upload readback size changed");
            for (unsigned part = 0; part < 2; ++part)
                if (!std::all_of(snapshot.begin() + part * partSize, snapshot.begin() + (part + 1) * partSize,
                                [=](std::byte value) { return value == std::byte(frame * 2 + part + 1); }))
                    throw std::runtime_error("In-flight staging upload was overwritten or copied to the wrong offset");
            ++completed;
        })) throw std::runtime_error("Upload readback could not be queued");
        context.Submit();
    }
    for (int frame = 0; frame < 3; ++frame) { context.BeginFrame(); context.Submit(); }
    if (completed != frames) throw std::runtime_error("Upload callbacks did not complete");
    std::cout << "PASS: staged uploads, offsets, CPU source lifetime, arena rollover and in-flight reuse; "
              << recordingMs / frames << " ms CPU recording per 18 MiB\n";
}

// Consecutive recorded updates share one barrier pair; an update overlapping a queued one starts a new batch, so
// later writes still win. Inline and staged updates mix in either order.
template<class Device>
void CheckBatchedBufferUploads(Device &device)
{
    using namespace PlutoGE::render::rhi;
    constexpr std::size_t smallSize = 256, largeSize = 128 * 1024;
    Buffer small(device, device.CreateBuffer({smallSize, BufferUsage::Storage, "Batched upload small"}));
    Buffer large(device, device.CreateBuffer({largeSize, BufferUsage::Storage, "Batched upload large"}));
    const auto fill = [](std::size_t size, int value) { return std::vector<std::byte>(size, std::byte(value)); };
    auto &context = device.GetImmediateContext();
    std::vector<std::byte> smallResult, largeResult;
    context.BeginFrame();
    device.UpdateBuffer(small.Get(), 0, fill(64, 1));
    device.UpdateBuffer(small.Get(), 64, fill(64, 2));
    device.UpdateBuffer(large.Get(), 0, fill(largeSize, 5));  // staged
    device.UpdateBuffer(small.Get(), 0, fill(64, 3));         // overlaps the first: a second batch
    device.UpdateBuffer(large.Get(), 0, fill(64, 6));         // inline after the staged copy
    const auto readback = [&](BufferHandle buffer, std::vector<std::byte> &result)
    {
        if (!context.QueueBufferReadback(buffer, 128, [&](std::span<const std::byte> bytes) { result.assign(bytes.begin(), bytes.end()); }))
            throw std::runtime_error("Batched upload readback could not be queued");
    };
    readback(small.Get(), smallResult);
    readback(large.Get(), largeResult);
    context.Submit();
    const auto stats = device.GetTimingStats();
    for (int frame = 0; frame < 3; ++frame) { context.BeginFrame(); context.Submit(); }
    const auto expect = [](const std::vector<std::byte> &bytes, int first, int second, const char *message)
    {
        if (bytes.size() != 128 ||
            !std::all_of(bytes.begin(), bytes.begin() + 64, [=](std::byte value) { return value == std::byte(first); }) ||
            !std::all_of(bytes.begin() + 64, bytes.end(), [=](std::byte value) { return value == std::byte(second); }))
            throw std::runtime_error(message);
    };
    expect(smallResult, 3, 2, "A batched upload lost the order of overlapping writes");
    expect(largeResult, 6, 5, "An inline upload did not follow the staged copy it overlaps");
    if (stats.bufferUploads != 5 || stats.bufferUploadBatches != 2)
        throw std::runtime_error("Recorded buffer uploads were not batched (" + std::to_string(stats.bufferUploads) +
                                 " uploads in " + std::to_string(stats.bufferUploadBatches) + " batches)");
    std::cout << "PASS: 5 buffer uploads recorded in 2 batches, overlapping writes ordered\n";
}

