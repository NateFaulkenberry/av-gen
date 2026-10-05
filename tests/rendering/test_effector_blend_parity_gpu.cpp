// Every effector op the GPU runs, with every blend, against the CPU reference (ADR-025, ADR-1121).
//
// The existing parity test (test_points_gpu.cpp) gave Scale a Mix blend and everything else Add, so
// the GPU's reading of every other (op, blend) pair was never compared with anything. This one runs
// each pair as the only effector on its own object and compares the live records the draw reads with
// spatial::applyEffectorsToRecords on the same records.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/procedural_renderer.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/procedural.hpp"
#include "scene/scene.hpp"
#include "spatial/effector.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <memory>
#include <string>

using namespace avgen;

namespace {

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Error);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

float maxAbs(const glm::vec4& v) {
    return std::max(std::max(std::abs(v.x), std::abs(v.y)), std::max(std::abs(v.z), std::abs(v.w)));
}

} // namespace

TEST_CASE("Every GPU effector op agrees with the CPU reference under every blend", "[gpu][points][effectors]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene s;
    s.camera.position = {0.0f, 4.0f, 14.0f};
    spatial::FieldSpec bulge;
    bulge.name = "bulge";
    bulge.kind = spatial::FieldKind::Radial;
    bulge.radius = 6.0f;
    bulge.strength = 0.8f;
    s.fields.fields.push_back(bulge);
    spatial::FieldSpec swirl;
    swirl.name = "swirl";
    swirl.kind = spatial::FieldKind::Vortex;
    swirl.falloff.kind = spatial::FalloffKind::Linear;
    swirl.falloff.outer = 8.0f;
    s.fields.fields.push_back(swirl);
    spatial::FieldSpec tint;
    tint.name = "tint";
    tint.kind = spatial::FieldKind::RadialGradient;
    tint.radius = 5.0f;
    tint.colorA = {1.0f, 0.2f, 0.1f, 1.0f};
    tint.colorB = {0.1f, 0.3f, 1.0f, 1.0f};
    s.fields.fields.push_back(tint);

    const spatial::EffectorOp ops[] = {spatial::EffectorOp::PositionOffset, spatial::EffectorOp::Scale,
                                       spatial::EffectorOp::Rotation,       spatial::EffectorOp::Color,
                                       spatial::EffectorOp::Emission,       spatial::EffectorOp::Density};
    const spatial::EffectorBlend blends[] = {spatial::EffectorBlend::Add, spatial::EffectorBlend::Multiply,
                                             spatial::EffectorBlend::Replace, spatial::EffectorBlend::Min,
                                             spatial::EffectorBlend::Max, spatial::EffectorBlend::Mix};
    for (const auto op : ops) {
        for (const auto blend : blends) {
            scene::ProceduralGeometry object;
            object.name = std::string(spatial::effectorOpName(op)) + "-" + spatial::effectorBlendName(blend);
            object.source.kind = scene::PrimitiveKind::Box;
            object.distribution.kind = scene::DistributionKind::Grid;
            object.distribution.gridCount = {8, 2, 8};
            object.distribution.gridSpacing = {1.1f, 1.3f, 1.1f};
            object.variation.randomScale = glm::vec3(0.4f);
            object.variation.randomRotation = glm::vec3(0.5f);
            spatial::Effector e;
            e.op = op;
            e.blend = blend;
            e.field = op == spatial::EffectorOp::Color ? "tint" : op == spatial::EffectorOp::Rotation ? "swirl" : "bulge";
            e.strength = 0.6f;
            e.weight = 0.35f;
            e.axis = {0.2f, 1.0f, 0.1f};
            e.scaleAxis = {1.0f, 0.5f, 1.0f};
            object.effectors.push_back(e);
            REQUIRE(object.rebuild());
            s.procedurals.push_back(object);
        }
    }
    const double time = 1.3;
    REQUIRE(renderer.renderFrame(s, FrameTime{time, 1.0 / 60.0, 0}, 96, 96).has_value());
    int disagreeing = 0;
    for (const scene::ProceduralGeometry& object : s.procedurals) {
        auto gpu = renderer.procedurals().readInstanceRecords(object.name);
        REQUIRE(gpu.has_value());
        REQUIRE(gpu->size() == object.instances.size());
        std::vector<scene::InstanceRecord> cpu = object.instances;
        spatial::applyEffectorsToRecords(cpu, object.effectors, s.fields, time);
        float worst = 0.0f;
        for (std::size_t i = 0; i < cpu.size(); ++i) {
            const auto& a = cpu[i];
            const auto& b = (*gpu)[i];
            worst = std::max({worst, maxAbs(a.position - b.position), maxAbs(a.scale - b.scale),
                              maxAbs(a.color - b.color), maxAbs(a.emissive - b.emissive)});
            // A quaternion and its negation are the same rotation.
            worst = std::max(worst, std::min(maxAbs(a.rotation - b.rotation), maxAbs(a.rotation + b.rotation)));
        }
        if (worst > 1e-4f) {
            ++disagreeing;
        }
        INFO(object.name << ": worst |CPU - GPU| " << worst);
        CHECK(worst <= 1e-4f);
    }
    INFO(disagreeing << " of " << s.procedurals.size() << " (op, blend) pairs disagree");
    CHECK(disagreeing == 0);
    CHECK(ctx->errorCount() == 0);
}
