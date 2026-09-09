// User shader layers on the GPU: background, post, persistent feedback, error fallback, reload.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "params/parameter_set.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/scene.hpp"
#include "shaders/shader_layers.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>

using namespace avgen;

namespace {

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

std::filesystem::path writeShader(const char* name, const std::string& source) {
    const auto path = std::filesystem::temp_directory_path() / (std::string("avgen_layer_") + name + ".wgsl");
    std::ofstream(path) << source;
    return path;
}

const char* kSolid = R"(/*{
  "DESCRIPTION": "solid colour from an input",
  "INPUTS": [{"NAME": "tint", "TYPE": "color", "DEFAULT": [0.2, 0.6, 0.9, 1.0]}]
}*/
fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> {
    return inputs.tint;
}
)";

const char* kInvert = R"(/*{ "DESCRIPTION": "invert the scene" }*/
fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> {
    let c = textureSample(inputImage, linearSampler, uv);
    return vec4<f32>(vec3<f32>(1.0) - clamp(c.rgb, vec3<f32>(0.0), vec3<f32>(1.0)), 1.0);
}
)";

const char* kFeedback = R"(/*{
  "INPUTS": [{"NAME": "add", "TYPE": "float", "DEFAULT": 0.1, "MIN": 0.0, "MAX": 1.0}],
  "PASSES": [{"TARGET": "acc", "PERSISTENT": true, "FLOAT": true}, {}]
}*/
fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> {
    let previous = textureSample(acc, linearSampler, uv);
    if (sys.passIndex < 0.5) {
        return vec4<f32>(previous.rgb + vec3<f32>(inputs.add), 1.0);
    }
    return vec4<f32>(previous.rgb, 1.0);
}
)";

const char* kBroken = R"(/*{ "INPUTS": [] }*/
fn mainImage(uv: vec2<f32>, fragCoord: vec2<f32>) -> vec4<f32> {
    return oops;
}
)";

} // namespace

TEST_CASE("Background shader layer paints its input colour behind the scene", "[gpu][shaders]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    params::ParameterSet params;
    shaders::ShaderLayerSet layers(params, 0.0);
    const auto path = writeShader("solid", kSolid);
    auto id = layers.add(path, shaders::LayerStage::Background);
    REQUIRE(id.has_value());
    REQUIRE(params.find("shader/avgen_layer_solid/tint") != nullptr);

    scene::Scene empty;
    empty.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    FrameTime time{};
    layers.update(time, shaders::StdUniforms{});
    rendering::ShaderFrameInputs inputs{&layers, nullptr};
    auto image = renderer.renderToImage(empty, time, 32, 32, &inputs);
    REQUIRE(image.has_value());
    CHECK(ctx->errorCount() == 0);
    const auto* px = image->pixel(16, 16);
    // (0.2, 0.6, 0.9) scene-linear -> ACES -> sRGB: blue > green > red, all clearly non-zero.
    CHECK(px[2] > px[1]);
    CHECK(px[1] > px[0]);
    CHECK(px[0] > 40);

    // Change the parameter: the layer follows on the next frame.
    auto* tint = params.findAs<glm::vec4>("shader/avgen_layer_solid/tint");
    REQUIRE(tint != nullptr);
    tint->setBase(glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
    params.resetFinals();
    layers.update(time, shaders::StdUniforms{});
    auto red = renderer.renderToImage(empty, time, 32, 32, &inputs);
    REQUIRE(red.has_value());
    CHECK(red->pixel(16, 16)[0] > red->pixel(16, 16)[2] + 100);
    layers.find(*id)->enabled = false;
    auto off = renderer.renderToImage(empty, time, 32, 32, &inputs);
    REQUIRE(off.has_value());
    CHECK(off->pixel(16, 16)[0] < 40);
    std::filesystem::remove(path);
}

TEST_CASE("Post shader layer inverts the scene image", "[gpu][shaders]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    params::ParameterSet params;
    shaders::ShaderLayerSet layers(params, 0.0);
    scene::Scene empty;
    empty.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    FrameTime time{};
    auto before = renderer.renderToImage(empty, time, 16, 16);
    REQUIRE(before.has_value());
    const int beforeSum = before->pixel(8, 8)[0] + before->pixel(8, 8)[1] + before->pixel(8, 8)[2];

    const auto path = writeShader("invert", kInvert);
    REQUIRE(layers.add(path, shaders::LayerStage::Post).has_value());
    layers.update(time, shaders::StdUniforms{});
    rendering::ShaderFrameInputs inputs{&layers, nullptr};
    auto after = renderer.renderToImage(empty, time, 16, 16, &inputs);
    REQUIRE(after.has_value());
    CHECK(ctx->errorCount() == 0);
    const int afterSum = after->pixel(8, 8)[0] + after->pixel(8, 8)[1] + after->pixel(8, 8)[2];
    CHECK(beforeSum < 120);
    CHECK(afterSum > 600); // black scene -> white
    std::filesystem::remove(path);
}

TEST_CASE("Persistent pass target accumulates across frames and resets with the layer", "[gpu][shaders]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    params::ParameterSet params;
    shaders::ShaderLayerSet layers(params, 0.0);
    const auto path = writeShader("feedback", kFeedback);
    REQUIRE(layers.add(path, shaders::LayerStage::Background).has_value());
    scene::Scene empty;
    empty.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    rendering::ShaderFrameInputs inputs{&layers, nullptr};
    int previous = -1;
    bool increasing = true;
    for (int frame = 0; frame < 6; ++frame) {
        FrameTime time{};
        time.frameIndex = static_cast<std::uint64_t>(frame);
        layers.update(time, shaders::StdUniforms{});
        auto image = renderer.renderToImage(empty, time, 16, 16, &inputs);
        REQUIRE(image.has_value());
        const int value = image->pixel(8, 8)[1];
        if (previous >= 0 && value <= previous) {
            increasing = false;
        }
        previous = value;
    }
    CHECK(increasing);
    CHECK(previous > 100);
    CHECK(ctx->errorCount() == 0);
    std::filesystem::remove(path);
}

TEST_CASE("A broken shader falls back to the error pattern and reports the error; fixing it reloads",
          "[gpu][shaders]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    params::ParameterSet params;
    shaders::ShaderLayerSet layers(params, 0.0);
    const auto path = writeShader("broken", kBroken);
    auto id = layers.add(path, shaders::LayerStage::Background); // parses fine; compile fails on the GPU
    REQUIRE(id.has_value());
    scene::Scene empty;
    empty.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    FrameTime time{};
    layers.update(time, shaders::StdUniforms{});
    rendering::ShaderFrameInputs inputs{&layers, nullptr};
    ctx->clearErrors();
    auto image = renderer.renderToImage(empty, time, 32, 32, &inputs);
    REQUIRE(image.has_value());
    const std::string error = renderer.shaderStack().errorFor(*id);
    CHECK(error.find("oops") != std::string::npos);
    // Magenta stripes: red and blue high, green well below them, somewhere in the image. The test
    // compares green against red rather than an absolute level because the default operator is AgX
    // (ADR-039), which lifts a fully crushed channel instead of clipping it to zero as ACES does.
    bool magenta = false;
    for (std::uint32_t x = 0; x < 32 && !magenta; ++x) {
        const auto* px = image->pixel(x, 16);
        magenta = px[0] > 150 && px[2] > 150 && int(px[1]) < int(px[0]) - 80;
    }
    CHECK(magenta);
    ctx->clearErrors();

    // Fix the file and reload: the error clears and the colour becomes the input colour.
    std::ofstream(path) << kSolid;
    REQUIRE(layers.reload(*id).has_value());
    params.resetFinals();
    layers.update(time, shaders::StdUniforms{});
    auto fixed = renderer.renderToImage(empty, time, 32, 32, &inputs);
    REQUIRE(fixed.has_value());
    CHECK(renderer.shaderStack().errorFor(*id).empty());
    CHECK(fixed->pixel(16, 16)[2] > fixed->pixel(16, 16)[0]);
    CHECK(params.find("shader/avgen_layer_broken/tint") != nullptr);
    std::filesystem::remove(path);
}

TEST_CASE("Engine shaders reload from disk and keep working", "[gpu][shaders]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());
    REQUIRE(renderer.reloadEngineShaders().has_value());
    CHECK(renderer.engineShaderReloads() == 1);
    scene::Scene empty;
    FrameTime time{};
    auto image = renderer.renderToImage(empty, time, 8, 8);
    REQUIRE(image.has_value());
    CHECK(ctx->errorCount() == 0);
}
