#include "gpu/pipeline_compile.hpp"

#include "core/log.hpp"
#include "gpu/resource_stats.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace avgen::gpu {

namespace {

std::atomic<std::uint64_t> gStarted{0};
std::atomic<std::uint64_t> gLanded{0};
int gBatchDepth = 0; // PipelineBatch scopes alive; main-thread only, like the started set

struct HookState {
    std::function<void(const PipelineCompileProgress&)> hook;
    std::thread::id thread;
    bool inside = false;
    std::string waitingFor;
};
HookState& hookState() {
    static HookState state;
    return state;
}

std::string labelOf(wgpu::StringView label) {
    if (label.data == nullptr) {
        return "(unlabelled)";
    }
    return std::string(label.data, label.length == WGPU_STRLEN ? std::strlen(label.data) : label.length);
}

bool timingAll() {
    static const bool on = [] {
        const char* v = std::getenv("AVGEN_PIPELINE_TIMING");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return on;
}

double msSince(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// Calls the hook if this is its thread and it is not already running.
void tick() {
    HookState& s = hookState();
    if (!s.hook || s.inside || std::this_thread::get_id() != s.thread) {
        return;
    }
    s.inside = true;
    s.hook(pipelineCompileProgress());
    s.inside = false;
}

// AVGEN_SYNC_PIPELINES=1: every creation on the calling thread, as before ADR-1562 (an A/B switch).
bool forceSync() {
    static const bool on = [] {
        const char* v = std::getenv("AVGEN_SYNC_PIPELINES");
        return v != nullptr && v[0] != '\0' && v[0] != '0';
    }();
    return on;
}

wgpu::Instance instanceOf(const wgpu::Device& device) { return device.GetAdapter().GetInstance(); }

constexpr std::uint64_t kSliceNs = 30'000'000ull; // the hook's period while waiting

// Waits for `future`, ticking the hook between slices.
void waitTicking(const wgpu::Device& device, wgpu::Future future, const std::string& label) {
    const wgpu::Instance instance = instanceOf(device);
    HookState& s = hookState();
    const std::string previous = s.waitingFor;
    s.waitingFor = label;
    while (instance.WaitAny(future, kSliceNs) == wgpu::WaitStatus::TimedOut) {
        tick();
    }
    s.waitingFor = previous;
}

// The started pipelines not yet landed, and the failures among those landed.
struct Started {
    std::uint64_t outstanding = 0;
    std::vector<std::string> failures;
};
Started& started() {
    static Started s;
    return s;
}

} // namespace

PipelineCompileProgress pipelineCompileProgress() {
    PipelineCompileProgress p;
    p.started = gStarted.load(std::memory_order_relaxed);
    p.landed = gLanded.load(std::memory_order_relaxed);
    p.waitingFor = hookState().waitingFor;
    return p;
}

void setPipelineWaitHook(std::function<void(const PipelineCompileProgress&)> hook) {
    HookState& s = hookState();
    s.hook = std::move(hook);
    s.thread = std::this_thread::get_id();
}

void notePipelineTime(const char* kind, const std::string& label, double ms) {
    if (timingAll()) {
        log::info("pipeline-timing {} '{}' {:.1f} ms", kind, label, ms);
    }
}

wgpu::RenderPipeline compileRenderPipeline(const wgpu::Device& device, const wgpu::RenderPipelineDescriptor* desc) {
    const std::string label = labelOf(desc->label);
    const auto start = std::chrono::steady_clock::now();
    if (forceSync()) {
        wgpu::RenderPipeline p = device.CreateRenderPipeline(desc);
        notePipelineTime("render", label, msSince(start));
        return p;
    }
    gStarted.fetch_add(1, std::memory_order_relaxed);
    wgpu::RenderPipeline result;
    bool ok = false;
    const wgpu::Future future = device.CreateRenderPipelineAsync(
        desc, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::CreatePipelineAsyncStatus status, wgpu::RenderPipeline pipeline, wgpu::StringView) {
            ok = status == wgpu::CreatePipelineAsyncStatus::Success && pipeline;
            result = std::move(pipeline);
        });
    waitTicking(device, future, label);
    gLanded.fetch_add(1, std::memory_order_relaxed);
    if (!ok) {
        // The synchronous path, for what it always produced on a failure: the error object, and the error raised
        // into whatever error scope the caller pushed.
        result = device.CreateRenderPipeline(desc);
    }
    notePipelineTime("render", label, msSince(start));
    return result;
}

wgpu::ComputePipeline compileComputePipeline(const wgpu::Device& device,
                                             const wgpu::ComputePipelineDescriptor* desc) {
    const std::string label = labelOf(desc->label);
    const auto start = std::chrono::steady_clock::now();
    if (forceSync()) {
        wgpu::ComputePipeline p = device.CreateComputePipeline(desc);
        notePipelineTime("compute", label, msSince(start));
        return p;
    }
    gStarted.fetch_add(1, std::memory_order_relaxed);
    wgpu::ComputePipeline result;
    bool ok = false;
    const wgpu::Future future = device.CreateComputePipelineAsync(
        desc, wgpu::CallbackMode::WaitAnyOnly,
        [&](wgpu::CreatePipelineAsyncStatus status, wgpu::ComputePipeline pipeline, wgpu::StringView) {
            ok = status == wgpu::CreatePipelineAsyncStatus::Success && pipeline;
            result = std::move(pipeline);
        });
    waitTicking(device, future, label);
    gLanded.fetch_add(1, std::memory_order_relaxed);
    if (!ok) {
        result = device.CreateComputePipeline(desc);
    }
    notePipelineTime("compute", label, msSince(start));
    return result;
}

void startRenderPipeline(const wgpu::Device& device, const wgpu::RenderPipelineDescriptor* desc,
                         wgpu::RenderPipeline* out) {
    if (gBatchDepth == 0 || forceSync()) {
        static_cast<void>(pipelineCountersNoteAsync());
        *out = compileRenderPipeline(device, desc);
        return;
    }
    const std::string label = labelOf(desc->label);
    const auto start = std::chrono::steady_clock::now();
    gStarted.fetch_add(1, std::memory_order_relaxed);
    static_cast<void>(pipelineCountersNoteAsync());
    ++started().outstanding;
    *out = nullptr;
    device.CreateRenderPipelineAsync(
        desc, wgpu::CallbackMode::AllowProcessEvents,
        [out, label, start](wgpu::CreatePipelineAsyncStatus status, wgpu::RenderPipeline pipeline,
                            wgpu::StringView message) {
            gLanded.fetch_add(1, std::memory_order_relaxed);
            --started().outstanding;
            if (status == wgpu::CreatePipelineAsyncStatus::Success && pipeline) {
                *out = std::move(pipeline);
            } else {
                started().failures.push_back(fmt::format("'{}': {}", label, labelOf(message)));
            }
            notePipelineTime("render", label, msSince(start));
        });
}

void startComputePipeline(const wgpu::Device& device, const wgpu::ComputePipelineDescriptor* desc,
                          wgpu::ComputePipeline* out) {
    if (gBatchDepth == 0 || forceSync()) {
        static_cast<void>(pipelineCountersNoteAsyncCompute());
        *out = compileComputePipeline(device, desc);
        return;
    }
    const std::string label = labelOf(desc->label);
    const auto start = std::chrono::steady_clock::now();
    gStarted.fetch_add(1, std::memory_order_relaxed);
    static_cast<void>(pipelineCountersNoteAsyncCompute());
    ++started().outstanding;
    *out = nullptr;
    device.CreateComputePipelineAsync(
        desc, wgpu::CallbackMode::AllowProcessEvents,
        [out, label, start](wgpu::CreatePipelineAsyncStatus status, wgpu::ComputePipeline pipeline,
                            wgpu::StringView message) {
            gLanded.fetch_add(1, std::memory_order_relaxed);
            --started().outstanding;
            if (status == wgpu::CreatePipelineAsyncStatus::Success && pipeline) {
                *out = std::move(pipeline);
            } else {
                started().failures.push_back(fmt::format("'{}': {}", label, labelOf(message)));
            }
            notePipelineTime("compute", label, msSince(start));
        });
}

namespace {
Result<void> finishPipelines(const wgpu::Device& device) {
    const wgpu::Instance instance = instanceOf(device);
    HookState& s = hookState();
    const std::string previous = s.waitingFor;
    s.waitingFor = "the parallel compiles";
    auto lastTick = std::chrono::steady_clock::now();
    while (started().outstanding > 0) {
        instance.ProcessEvents();
        if (started().outstanding == 0) {
            break;
        }
        if (msSince(lastTick) >= 30.0) {
            tick();
            lastTick = std::chrono::steady_clock::now();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    s.waitingFor = previous;
    if (!started().failures.empty()) {
        std::string all;
        for (const std::string& f : started().failures) {
            all += (all.empty() ? "" : "; ") + f;
        }
        started().failures.clear();
        return fail("pipeline creation failed: {}", all);
    }
    return {};
}
} // namespace

PipelineBatch::PipelineBatch(const wgpu::Device& device) : device_(device) { ++gBatchDepth; }

PipelineBatch::~PipelineBatch() {
    if (started().outstanding > 0) {
        static_cast<void>(finish());
    }
    --gBatchDepth;
}

Result<void> PipelineBatch::finish() { return finishPipelines(device_); }

std::size_t pipelinesInFlight() { return static_cast<std::size_t>(started().outstanding); }

} // namespace avgen::gpu
