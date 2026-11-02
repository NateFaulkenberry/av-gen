// ADR-1934: the slit gated by identity. History is read only where the stored pixel belonged to a gated object, so an
// ungated object can never be smeared, however long the gated slots' trails are.
//
// The fixture is ADR-1931's mover crossing a dark frame, with a static backdrop as entity 0 (identifier 0 is the
// cleared background, so entity 0 is never gated -- the backdrop is where that rule belongs) and the mover as entity 1.

#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"
#include "params/parameter_set.hpp"
#include "shaders/shader_layers.hpp"
#include "support/image_diff.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>

using namespace avgen;

#define CHECK_IDENTICAL(a, b)                                                                      \
    do {                                                                                           \
        const auto d__ = ::avgen::testing::byteDiff((a).rgba, (b).rgba);                           \
        INFO(d__.describe());                                                                      \
        CHECK(d__.identical());                                                                    \
    } while (false)
#define CHECK_DIFFERS(a, b)                                                                        \
    do {                                                                                           \
        const auto d__ = ::avgen::testing::byteDiff((a).rgba, (b).rgba);                           \
        INFO(d__.describe());                                                                      \
        CHECK_FALSE(d__.identical());                                                              \
    } while (false)

namespace {

constexpr std::uint32_t kSize = 160;

std::unique_ptr<gpu::Context> makeContext() {
    static bool logInit = false;
    if (!logInit) {
        log::init(log::Level::Warn);
        logInit = true;
    }
    auto ctx = gpu::Context::create(gpu::ContextDesc{});
    if (!ctx) {
        SKIP("no GPU adapter available: " << ctx.error().message);
    }
    return std::move(*ctx);
}

// entity 0: a dim static backdrop sphere; entity 1: the bright mover, in `moverSlot`
scene::Scene stage(std::uint8_t moverSlot) {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.post.bloomIntensity = 0.0f;
    s.camera.position = {0.0f, 0.0f, 12.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    scene::Entity backdrop;
    backdrop.name = "backdrop";
    backdrop.mesh = s.addMesh(scene::makeIcosphere(1.4f, 2));
    backdrop.material.baseColor = {0.3f, 0.3f, 0.3f};
    backdrop.material.emissiveColor = {0.2f, 0.4f, 0.9f};
    backdrop.material.emissiveIntensity = 1.5f;
    backdrop.transform.position = {2.5f, 2.0f, -1.0f};
    s.entities.push_back(std::move(backdrop));
    scene::Entity e;
    e.name = "mover";
    e.mesh = s.addMesh(scene::makeIcosphere(0.6f, 2));
    e.material.baseColor = {1.0f, 1.0f, 1.0f};
    e.material.emissiveColor = {1.0f, 0.85f, 0.6f};
    e.material.emissiveIntensity = 6.0f;
    e.temporalSlot = moverSlot;
    s.entities.push_back(std::move(e));
    return s;
}

void placeAt(scene::Scene& s, int frame) {
    const float x = -4.5f + 0.3f * static_cast<float>(frame % 30);
    const float y = -2.0f + 0.13f * static_cast<float>(frame % 31);
    s.entities[1].transform.position = {x, y, 0.0f};
}

scene::Scene gated(scene::Scene s, float trail, float fracture = 0.0f) {
    s.temporal.slit.enabled = true;
    s.temporal.slit.amount = 1.0f;
    s.temporal.slit.span = 48.0f;
    s.temporal.slit.gate = 1.0f;
    s.temporal.history.capacity = 48;
    for (auto& slot : s.temporal.slit.slots) { // every slot live: only the identity may decide what is gated
        slot.trail = trail;
        slot.fade = 0.3f;
        slot.fracture = fracture;
        slot.strips = 12.0f;
    }
    return s;
}

struct Shot {
    gpu::Image8 image;
    rendering::TemporalStats stats;
};

Shot playRange(gpu::Context& ctx, gpu::ShaderLibrary& shaders, scene::Scene s, int first, int last,
               shaders::ShaderLayerSet* layers = nullptr) {
    rendering::SceneRenderer renderer(ctx, shaders);
    REQUIRE(renderer.init().has_value());
    FixedStepClock clock(60.0);
    clock.restartAt(static_cast<double>(first) / 60.0);
    Shot out;
    for (int i = first; i < last; ++i) {
        placeAt(s, i);
        FrameTime t = clock.tick();
        t.frameIndex = static_cast<std::uint64_t>(i);
        rendering::ShaderFrameInputs inputs{layers, nullptr};
        if (layers != nullptr) {
            layers->update(t, shaders::StdUniforms{});
        }
        auto img = renderer.renderToImage(s, t, kSize, kSize, layers != nullptr ? &inputs : nullptr);
        REQUIRE(img.has_value());
        out.image = std::move(*img);
    }
    out.stats = renderer.stats().temporal;
    return out;
}

std::filesystem::path writeShader(const char* name, const std::string& source) {
    const auto path = std::filesystem::temp_directory_path() / (std::string("avgen_gate_layer_") + name + ".wgsl");
    std::ofstream(path) << source;
    return path;
}

const char* kFlip = R"(/*{ "DESCRIPTION": "mirror the image left to right" }*/
fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> {
    return textureSample(inputImage, linearSampler, vec2<f32>(1.0 - uv.x, uv.y));
}
)";

gpu::Image8 mirrored(const gpu::Image8& in) {
    gpu::Image8 out = in;
    for (std::uint32_t y = 0; y < kSize; ++y) {
        for (std::uint32_t x = 0; x < kSize; ++x) {
            for (int c = 0; c < 4; ++c) {
                out.rgba[(static_cast<std::size_t>(y) * kSize + x) * 4 + c] =
                    in.rgba[(static_cast<std::size_t>(y) * kSize + (kSize - 1 - x)) * 4 + c];
            }
        }
    }
    return out;
}

} // namespace

TEST_CASE("the gate never smears an ungated object, and leaves a gated one's trail", "[gpu][temporal][adr1934]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const int last = 70;
    const Shot off = playRange(*ctx, shaders, stage(0), 0, last);
    // Every slot has a long trail, but nothing is in a slot: the picture is the one without the effect.
    const Shot ungated = playRange(*ctx, shaders, gated(stage(0), 40.0f), 0, last);
    CHECK(ungated.stats.gated);
    CHECK(ungated.stats.slotRingBytes == 80ull * 80ull * 48ull); // R8Uint, the colour ring's size and depth
    CHECK_IDENTICAL(off.image, ungated.image);
    // The mover in slot 1 leaves its trail.
    const Shot trailed = playRange(*ctx, shaders, gated(stage(1), 40.0f), 0, last);
    CHECK_DIFFERS(off.image, trailed.image);
    // Live slots with no term (trail 0, fracture 0) change nothing even for a gated object.
    const Shot idle = playRange(*ctx, shaders, gated(stage(1), 0.0f), 0, last);
    CHECK_IDENTICAL(off.image, idle.image);
    // Fracture is a different picture from the trail alone.
    const Shot fractured = playRange(*ctx, shaders, gated(stage(1), 40.0f, 0.8f), 0, last);
    CHECK_DIFFERS(trailed.image, fractured.image);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("a gated trail is FIR: a renderer that starts a span early lands on the same frame", "[gpu][temporal][adr1934]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const int last = 90;
    const scene::Scene s = gated(stage(1), 40.0f, 0.5f);
    const Shot played = playRange(*ctx, shaders, s, 0, last);
    const Shot late = playRange(*ctx, shaders, s, last - 48, last);
    CHECK_IDENTICAL(played.image, late.image);
    const Shot short_ = playRange(*ctx, shaders, s, last - 10, last);
    CHECK_DIFFERS(played.image, short_.image);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("the gate off releases the slot ring", "[gpu][temporal][adr1934]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    scene::Scene s = gated(stage(1), 20.0f);
    s.temporal.slit.gate = 0.0f;
    const Shot shot = playRange(*ctx, shaders, s, 0, 30);
    CHECK_FALSE(shot.stats.gated);
    CHECK(shot.stats.slotRingBytes == 0u);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("RGB temporal displacement pulls a gated object's colours apart in time, and only its", "[gpu][temporal][adr1936]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const int last = 70;
    const Shot off = playRange(*ctx, shaders, stage(0), 0, last);
    auto withRgb = [](scene::Scene s, float rgb) {
        for (auto& slot : s.temporal.slit.slots) {
            slot.rgb = rgb;
        }
        return s;
    };
    // nothing in a slot: the split has nothing to read
    CHECK_IDENTICAL(off.image, playRange(*ctx, shaders, withRgb(gated(stage(0), 0.0f), 6.0f), 0, last).image);
    // the gated mover, split with no trail and no fracture, is a different picture from the mover unsplit. Two frames:
    // the mover crosses its own width in about four, so further back it was never over the same pixels to read
    const Shot split = playRange(*ctx, shaders, withRgb(gated(stage(1), 0.0f), 2.0f), 0, last);
    CHECK_DIFFERS(off.image, split.image);
    // ...and FIR: a renderer that starts a span early lands on the same frame
    const Shot splitLate = playRange(*ctx, shaders, withRgb(gated(stage(1), 0.0f), 2.0f), last - 48, last);
    CHECK_IDENTICAL(split.image, splitLate.image);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("a late shader layer runs on the temporal stage's output; a post layer runs before it", "[gpu][temporal][adr1935]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    const int last = 60;
    const scene::Scene s = gated(stage(1), 40.0f);
    const Shot plain = playRange(*ctx, shaders, s, 0, last);
    const auto path = writeShader("flip", kFlip);
    // Late: the mirror of the gated frame, exactly -- the warp bends the trails with everything else
    {
        params::ParameterSet params;
        shaders::ShaderLayerSet layers(params, 0.0);
        REQUIRE(layers.add(path, shaders::LayerStage::Late).has_value());
        const Shot late = playRange(*ctx, shaders, s, 0, last, &layers);
        CHECK_IDENTICAL(mirrored(plain.image), late.image);
    }
    // Post: mirrored before the capture, against identities that are not -- not the same picture
    {
        params::ParameterSet params;
        shaders::ShaderLayerSet layers(params, 0.0);
        REQUIRE(layers.add(path, shaders::LayerStage::Post).has_value());
        const Shot post = playRange(*ctx, shaders, s, 0, last, &layers);
        CHECK_DIFFERS(mirrored(plain.image), post.image);
    }
    std::filesystem::remove(path);
    CHECK(ctx->errorCount() == 0);
}

TEST_CASE("feedback zooms about its centre: the default is the frame's, a moved one is a different picture", "[gpu][temporal][adr1937]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    auto withFeedback = [](scene::Scene s, float cx, float cy) {
        s.temporal.feedback.enabled = true;
        s.temporal.feedback.frames = 10;
        s.temporal.feedback.amount = 0.8f;
        s.temporal.feedback.zoom = 0.96f;
        s.temporal.feedback.rotate = 2.0f;
        s.temporal.feedback.centerX = cx;
        s.temporal.feedback.centerY = cy;
        return s;
    };
    const int last = 40;
    const Shot centred = playRange(*ctx, shaders, withFeedback(stage(0), 0.5f, 0.5f), 0, last);
    scene::Scene plain = stage(0);
    plain.temporal.feedback.enabled = true;
    plain.temporal.feedback.frames = 10;
    plain.temporal.feedback.amount = 0.8f;
    plain.temporal.feedback.zoom = 0.96f;
    plain.temporal.feedback.rotate = 2.0f; // the centre left at its default
    CHECK_IDENTICAL(centred.image, playRange(*ctx, shaders, plain, 0, last).image);
    const Shot moved = playRange(*ctx, shaders, withFeedback(stage(0), 0.2f, 0.7f), 0, last);
    CHECK_DIFFERS(centred.image, moved.image);
    // FIR, like the rest of the family. The lead is 20 frames for a 10-tap feedback: a fresh renderer's first frames carry
    // the 1-LSB pipeline-warm-up effect (ADR-1932) into the ring, and a 12-frame lead read one of them (105 bytes, delta 1)
    CHECK_IDENTICAL(moved.image, playRange(*ctx, shaders, withFeedback(stage(0), 0.2f, 0.7f), last - 20, last).image);
    CHECK(ctx->errorCount() == 0);
}
