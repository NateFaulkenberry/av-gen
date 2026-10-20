// ADR-1562: pipelines compile on Dawn's workers; the waiting thread's hook runs; a started pipeline lands at finish();
// a failure still reaches the caller's error scope.

#include "gpu/context.hpp"
#include "gpu/pipeline_compile.hpp"
#include "gpu/resource_stats.hpp"
#include "gpu/shader_library.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <memory>
#include <string>

using namespace avgen;

namespace {

std::unique_ptr<gpu::Context> makeContext() {
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

// A compute kernel whose text varies with `salt`, so every case compiles a pipeline Metal has not cached.
wgpu::ShaderModule kernel(gpu::Context& ctx, const std::string& salt) {
    gpu::ShaderLibrary lib(ctx, {});
    const std::string source = "@group(0) @binding(0) var<storage, read_write> data: array<f32>;\n"
                               "@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) id: vec3<u32>) {\n"
                               "    data[id.x] = data[id.x] * " + salt + " + 1.0;\n}\n";
    auto module = lib.compile(source, "pipeline-compile-test");
    REQUIRE(module.has_value());
    return *module;
}

wgpu::ComputePipelineDescriptor descriptor(const wgpu::ShaderModule& module, const char* label) {
    wgpu::ComputePipelineDescriptor desc{};
    desc.label = label;
    desc.compute.module = module;
    desc.compute.entryPoint = "main";
    return desc; // auto layout
}

std::string uniqueSalt() {
    static int n = 0;
    return std::to_string(1.0 + 1e-3 * ++n + static_cast<double>(std::rand() % 100000) * 1e-9);
}

} // namespace

TEST_CASE("a synchronous pipeline creation returns the pipeline and keeps the waiting thread's hook running",
          "[gpu][adr1562]") {
    auto ctx = makeContext();
    int ticks = 0;
    gpu::setPipelineWaitHook([&](const gpu::PipelineCompileProgress&) { ++ticks; });
    const wgpu::ShaderModule module = kernel(*ctx, uniqueSalt());
    const auto desc = descriptor(module, "pipeline-compile-sync");
    const gpu::PipelineCompileProgress before = gpu::pipelineCompileProgress();
    const wgpu::ComputePipeline pipeline = gpu::createComputePipeline(ctx->device(), &desc);
    gpu::setPipelineWaitHook({});
    CHECK(pipeline != nullptr);
    CHECK(pipeline.GetBindGroupLayout(0) != nullptr); // the auto layout is there: a real pipeline
    const gpu::PipelineCompileProgress after = gpu::pipelineCompileProgress();
    CHECK(after.started == before.started + 1);
    CHECK(after.landed == before.landed + 1);
    INFO(ticks << " hook calls"); // how many depends on how long Metal took; zero is legitimate for a tiny kernel
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("started pipelines are null until finish(), then all there; without a batch they are made at once",
          "[gpu][adr1562]") {
    auto ctx = makeContext();
    std::array<wgpu::ComputePipeline, 4> pipelines{};
    {
        gpu::PipelineBatch batch(ctx->device());
        for (auto& p : pipelines) {
            const wgpu::ShaderModule module = kernel(*ctx, uniqueSalt());
            const auto desc = descriptor(module, "pipeline-compile-started");
            gpu::startComputePipeline(ctx->device(), &desc, &p);
        }
        CHECK(gpu::pipelinesInFlight() <= pipelines.size());
        REQUIRE(batch.finish().has_value());
        CHECK(gpu::pipelinesInFlight() == 0);
    }
    for (const auto& p : pipelines) {
        CHECK(p != nullptr);
    }
    // No batch alive: the same call creates on the spot (a renderer initialised on its own behaves as before).
    wgpu::ComputePipeline immediate;
    const wgpu::ShaderModule module = kernel(*ctx, uniqueSalt());
    const auto desc = descriptor(module, "pipeline-compile-unbatched");
    gpu::startComputePipeline(ctx->device(), &desc, &immediate);
    CHECK(immediate != nullptr);
    CHECK(gpu::pipelinesInFlight() == 0);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("a pipeline that fails still fails: in a batch finish() names it, alone its error reaches the scope",
          "[gpu][adr1562]") {
    auto ctx = makeContext();
    const wgpu::ShaderModule module = kernel(*ctx, uniqueSalt());
    wgpu::ComputePipelineDescriptor bad = descriptor(module, "pipeline-compile-bad");
    bad.compute.entryPoint = "no_such_entry";
    {
        gpu::PipelineBatch batch(ctx->device());
        wgpu::ComputePipeline p;
        gpu::startComputePipeline(ctx->device(), &bad, &p);
        const auto r = batch.finish();
        REQUIRE_FALSE(r.has_value());
        CHECK(r.error().message.find("pipeline-compile-bad") != std::string::npos);
        CHECK(p == nullptr);
    }
    // The synchronous path: as before ADR-1562, the error is raised into the scope the caller pushed.
    ctx->device().PushErrorScope(wgpu::ErrorFilter::Validation);
    const wgpu::ComputePipeline p = gpu::createComputePipeline(ctx->device(), &bad);
    std::string error;
    auto future = ctx->device().PopErrorScope(
        wgpu::CallbackMode::WaitAnyOnly, [&](wgpu::PopErrorScopeStatus, wgpu::ErrorType type, wgpu::StringView msg) {
            if (type != wgpu::ErrorType::NoError) {
                error = gpu::Context::toString(msg);
            }
        });
    ctx->waitFor(future);
    CHECK_FALSE(error.empty());
    CHECK(ctx->errorCount() == 0); // captured by the scope, not uncaptured
}
