// The CPU/GPU bridge on this machine, measured (docs/research/gpu-world-productionization.md, Phase 1).
// Hidden ([.perf]): run it alone, under tools/gpu-lock.sh, and read the WARN lines.
//
//   * the adapter's limits that bound a population, a window and a checkpoint;
//   * what an upload costs (queue.WriteBuffer, CPU time to return, and to completion);
//   * what a GPU-to-GPU copy costs (a checkpoint save or restore);
//   * what a blocking readback costs end to end (what must never be in the frame loop);
//   * how often the queue can be round-tripped (the latency an async readback hides).
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <vector>

using namespace avgen;

namespace {

std::unique_ptr<gpu::Context> makeContext() {
    log::init(log::Level::Warn);
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

double msSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

wgpu::Buffer makeBuffer(gpu::Context& ctx, std::uint64_t bytes, wgpu::BufferUsage usage) {
    wgpu::BufferDescriptor d{};
    d.size = bytes;
    d.usage = usage;
    return ctx.device().CreateBuffer(&d);
}

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

} // namespace

TEST_CASE("What the CPU/GPU bridge costs on this machine", "[.perf][gpu-bridge]") {
    auto ctx = makeContext();
    const auto& caps = ctx->capabilities();
    const wgpu::Limits& l = caps.limits;
    WARN("adapter " << caps.adapterName << " (" << caps.backendName << "), timestamps " << caps.timestampQuery);
    WARN("maxStorageBufferBindingSize " << l.maxStorageBufferBindingSize << " B, maxBufferSize " << l.maxBufferSize
                                        << " B, maxStorageBuffersPerShaderStage " << l.maxStorageBuffersPerShaderStage
                                        << ", maxComputeWorkgroupsPerDimension " << l.maxComputeWorkgroupsPerDimension
                                        << ", maxComputeInvocationsPerWorkgroup "
                                        << l.maxComputeInvocationsPerWorkgroup << ", maxUniformBufferBindingSize "
                                        << l.maxUniformBufferBindingSize << ", maxComputeWorkgroupStorageSize "
                                        << l.maxComputeWorkgroupStorageSize);
    const auto storage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst | wgpu::BufferUsage::CopySrc;

    // Uploads: WriteBuffer returns after copying into Dawn's staging; completion is the queue's.
    for (const std::uint64_t mb : {1ull, 16ull, 64ull}) {
        const std::uint64_t bytes = mb << 20;
        wgpu::Buffer dst = makeBuffer(*ctx, bytes, storage);
        std::vector<std::uint8_t> src(bytes, 7);
        std::vector<double> call;
        std::vector<double> done;
        for (int rep = 0; rep < 7; ++rep) {
            const auto t0 = std::chrono::steady_clock::now();
            ctx->queue().WriteBuffer(dst, 0, src.data(), bytes);
            call.push_back(msSince(t0));
            ctx->waitForQueue();
            done.push_back(msSince(t0));
        }
        WARN("upload " << mb << " MB: WriteBuffer returns in " << median(call) << " ms, complete in " << median(done)
                       << " ms (" << static_cast<double>(mb) / (median(done) / 1000.0) / 1024.0 << " GB/s)");
    }

    // GPU-to-GPU copies (a checkpoint save or restore), 8 in one submit, timed to completion.
    for (const std::uint64_t mb : {1ull, 8ull, 46ull, 128ull}) {
        const std::uint64_t bytes = mb << 20;
        wgpu::Buffer a = makeBuffer(*ctx, bytes, storage);
        wgpu::Buffer b = makeBuffer(*ctx, bytes, storage);
        std::vector<double> per;
        for (int rep = 0; rep < 5; ++rep) {
            wgpu::CommandEncoder enc = ctx->device().CreateCommandEncoder();
            for (int k = 0; k < 8; ++k) {
                enc.CopyBufferToBuffer(k % 2 == 0 ? a : b, 0, k % 2 == 0 ? b : a, 0, bytes);
            }
            wgpu::CommandBuffer cb = enc.Finish();
            const auto t0 = std::chrono::steady_clock::now();
            ctx->queue().Submit(1, &cb);
            ctx->waitForQueue();
            per.push_back(msSince(t0) / 8.0);
        }
        WARN("GPU copy " << mb << " MB: " << median(per) << " ms per copy ("
                         << static_cast<double>(mb) / (median(per) / 1000.0) / 1024.0 << " GB/s)");
    }

    // Blocking readbacks: copy to a MAP_READ staging buffer, map, wait. What the frame loop must not do.
    for (const std::uint64_t bytes : {16ull, 1ull << 20, 46ull << 20}) {
        wgpu::Buffer src = makeBuffer(*ctx, bytes, storage);
        std::vector<double> per;
        for (int rep = 0; rep < 7; ++rep) {
            const auto t0 = std::chrono::steady_clock::now();
            auto data = gpu::readBuffer(*ctx, src, 0, bytes);
            REQUIRE(data.has_value());
            per.push_back(msSince(t0));
        }
        WARN("blocking readback " << bytes << " B: " << median(per) << " ms end to end");
    }

    // An empty round trip: submit nothing, wait for the queue. The floor under any sync point.
    std::vector<double> idle;
    for (int rep = 0; rep < 21; ++rep) {
        wgpu::CommandEncoder enc = ctx->device().CreateCommandEncoder();
        wgpu::CommandBuffer cb = enc.Finish();
        const auto t0 = std::chrono::steady_clock::now();
        ctx->queue().Submit(1, &cb);
        ctx->waitForQueue();
        idle.push_back(msSince(t0));
    }
    WARN("empty submit + wait: " << median(idle) << " ms");
    CHECK(ctx->errorCount() == 0);
}
