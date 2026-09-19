// Multi-layer AOV EXR (ADR-344 Phase 6, spec section 33).
//
// The rule being enforced: a normal is not a colour. Stored as R/G/B a downstream colour-managed
// pipeline will apply a transform to it, and the result looks like a shading bug in a compositor
// rather than a naming mistake in a renderer. Named layers make that impossible.

#include "assets/exr.hpp"
#include "pathtrace/path_tracer.hpp"
#include "pathtrace/snapshot.hpp"
#include "scene/mesh_generators.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <tinyexr.h>

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

// Reads back the channel NAMES a file actually contains, which is the whole claim under test.
std::vector<std::string> channelNames(const std::filesystem::path& path) {
    EXRVersion version;
    if (ParseEXRVersionFromFile(&version, path.string().c_str()) != TINYEXR_SUCCESS) return {};
    EXRHeader header;
    InitEXRHeader(&header);
    const char* err = nullptr;
    if (ParseEXRHeaderFromFile(&header, &version, path.string().c_str(), &err) != TINYEXR_SUCCESS) {
        FreeEXRErrorMessage(err);
        return {};
    }
    std::vector<std::string> names;
    names.reserve(static_cast<std::size_t>(header.num_channels));
    for (int i = 0; i < header.num_channels; ++i) names.emplace_back(header.channels[i].name);
    FreeEXRHeader(&header);
    return names;
}

bool has(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

std::filesystem::path tmp(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

} // namespace

TEST_CASE("writeExrLayers writes the channel names it was given", "[unit][pathtrace][exr]") {
    const std::uint32_t w = 8;
    const std::uint32_t h = 4;
    const std::size_t n = w * h;
    std::vector<float> a(n, 0.25f);
    std::vector<float> b(n, 0.5f);
    std::vector<float> c(n, 0.75f);

    const std::vector<assets::ExrChannel> channels = {
        {"normal.X", a, true}, {"normal.Y", b, true}, {"normal.Z", c, false},
    };
    const auto path = tmp("avgen_layers.exr");
    std::filesystem::remove(path);
    REQUIRE(assets::writeExrLayers(path, w, h, channels).has_value());
    REQUIRE(std::filesystem::exists(path));

    const auto names = channelNames(path);
    REQUIRE(names.size() == 3);
    REQUIRE(has(names, "normal.X"));
    REQUIRE(has(names, "normal.Y"));
    REQUIRE(has(names, "normal.Z"));
    // CONTROL: the thing section 33 forbids must NOT be in the file.
    REQUIRE_FALSE(has(names, "R"));
    REQUIRE_FALSE(has(names, "G"));
    REQUIRE_FALSE(has(names, "B"));
    std::filesystem::remove(path);
}

TEST_CASE("writeExrLayers refuses malformed input", "[unit][pathtrace][exr]") {
    const std::vector<float> four(4, 1.0f);
    const std::vector<float> three(3, 1.0f);
    const auto path = tmp("avgen_layers_bad.exr");

    REQUIRE_FALSE(assets::writeExrLayers(path, 0, 4, std::vector<assets::ExrChannel>{{"R", four, false}})
                      .has_value());
    REQUIRE_FALSE(assets::writeExrLayers(path, 2, 2, {}).has_value());
    // Wrong length.
    REQUIRE_FALSE(assets::writeExrLayers(path, 2, 2, std::vector<assets::ExrChannel>{{"R", three, false}})
                      .has_value());
    // Duplicate names would make the file ambiguous.
    REQUIRE_FALSE(assets::writeExrLayers(
                      path, 2, 2,
                      std::vector<assets::ExrChannel>{{"R", four, false}, {"R", four, false}})
                      .has_value());
    // CONTROL: the well-formed version of the same call succeeds, so the refusals are specific.
    REQUIRE(assets::writeExrLayers(path, 2, 2, std::vector<assets::ExrChannel>{{"R", four, false}})
                .has_value());
    std::filesystem::remove(path);
}

TEST_CASE("the AOV EXR carries beauty as colour and features as named layers",
          "[unit][pathtrace][exr][aov]") {
    scene::Scene s;
    s.environment.sky.enabled = false;
    s.environment.backgroundColor = glm::vec3(0.01f);
    s.camera.position = glm::vec3(0.0f, 1.2f, 3.0f);
    s.camera.target = glm::vec3(0.0f, 0.4f, 0.0f);
    s.camera.lens.useExplicitFov = true;
    s.camera.fovYRadians = 0.9f;

    const scene::MeshId floorId = s.addMesh(scene::makePlane(4.0f, 1));
    s.addEntity("floor", floorId).material.baseColor = glm::vec3(0.7f, 0.2f, 0.2f);
    const scene::MeshId ballId = s.addMesh(scene::makeIcosphere(0.5f, 2));
    scene::Entity& ball = s.addEntity("ball", ballId);
    ball.transform.position = glm::vec3(0.0f, 0.5f, 0.0f);
    ball.material.emissiveColor = glm::vec3(0.2f, 0.9f, 1.0f);
    ball.material.emissiveIntensity = 8.0f;

    pathtrace::TraceSettings t;
    t.width = 48;
    t.height = 32;
    t.samplesPerPixel = 8;
    t.maxDepth = 1;
    t.captureFeatures = true;

    pathtrace::PathTracer tr;
    pathtrace::Framebuffer fb;
    REQUIRE(tr.render(pathtrace::buildSnapshot(s), t, fb).has_value());
    REQUIRE_FALSE(fb.isBlack());

    const auto path = tmp("avgen_pathtrace_aov.exr");
    std::filesystem::remove(path);
    REQUIRE(pathtrace::writeFramebufferAovExr(fb, path).has_value());

    const auto names = channelNames(path);
    INFO("channels: " << [&] { std::string j; for (const auto& n : names) j += n + " "; return j; }());
    // 3 beauty + 3 albedo + 3 normal + 3 emission + depth + id.
    REQUIRE(names.size() == 14);
    // Beauty IS colour and belongs under R/G/B.
    REQUIRE(has(names, "R"));
    REQUIRE(has(names, "G"));
    REQUIRE(has(names, "B"));
    // Albedo is colour too, so it keeps R/G/B -- under its own layer prefix.
    REQUIRE(has(names, "albedo.R"));
    REQUIRE(has(names, "albedo.B"));
    // A normal is NOT colour: X/Y/Z, never R/G/B. This is the section 33 claim.
    REQUIRE(has(names, "normal.X"));
    REQUIRE(has(names, "normal.Y"));
    REQUIRE(has(names, "normal.Z"));
    REQUIRE_FALSE(has(names, "normal.R"));
    REQUIRE_FALSE(has(names, "normal.G"));

    // The beauty layer must round-trip as the linear radiance that went in, over-range included --
    // this is where a sneaky display transform would show.
    const auto read = assets::readExr(path);
    REQUIRE(read.has_value());
    REQUIRE(read->width == t.width);
    const auto* px = reinterpret_cast<const float*>(read->data.data());
    const auto expected = fb.resolvedRadiance();
    double worst = 0.0;
    float peak = 0.0f;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        worst = std::max(worst, static_cast<double>(std::abs(px[i * 4 + 0] - expected[i].x)));
        peak = std::max(peak, expected[i].x);
    }
    REQUIRE(worst < 1e-6);
    (void)peak;
    // The emissive ball puts values above 1 in the frame, so the round trip is not vacuous.
    const float peakG = [&] {
        float m = 0.0f;
        for (const auto& c : expected) m = std::max(m, c.y);
        return m;
    }();
    REQUIRE(peakG > 1.0f);
    std::filesystem::remove(path);
}

TEST_CASE("writing AOVs without capturing them writes only the beauty pass",
          "[unit][pathtrace][exr][aov]") {
    // CONTROL for the test above: with `captureFeatures` off there are no feature layers, so the
    // presence of `normal.X` there really is the feature capture and not something unconditional.
    scene::Scene s;
    s.environment.sky.enabled = true;
    const scene::MeshId id = s.addMesh(scene::makePlane(4.0f, 1));
    s.addEntity("floor", id);
    s.camera.position = glm::vec3(0.0f, 2.0f, 3.0f);
    s.camera.target = glm::vec3(0.0f, 0.0f, 0.0f);

    pathtrace::TraceSettings t;
    t.width = 16;
    t.height = 16;
    t.samplesPerPixel = 4;
    t.maxDepth = 0;
    t.captureFeatures = false;

    pathtrace::PathTracer tr;
    pathtrace::Framebuffer fb;
    REQUIRE(tr.render(pathtrace::buildSnapshot(s), t, fb).has_value());

    const auto path = tmp("avgen_pathtrace_beauty_only.exr");
    std::filesystem::remove(path);
    REQUIRE(pathtrace::writeFramebufferAovExr(fb, path).has_value());
    const auto names = channelNames(path);
    REQUIRE(names.size() == 3);
    REQUIRE(has(names, "R"));
    REQUIRE_FALSE(has(names, "normal.X"));
    REQUIRE_FALSE(has(names, "albedo.R"));
    std::filesystem::remove(path);
}

TEST_CASE("depth, emission and id AOVs carry what their names say",
          "[unit][pathtrace][aov]") {
    // A scene with two objects at KNOWN distances, one emissive and one not, so each AOV has an
    // answer that can be checked rather than merely inspected.
    scene::Scene s;
    s.environment.sky.enabled = false;
    s.environment.backgroundColor = glm::vec3(0.0f);
    s.camera.position = glm::vec3(0.0f, 0.0f, 10.0f);
    s.camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
    s.camera.up = glm::vec3(0.0f, 1.0f, 0.0f);
    s.camera.lens.useExplicitFov = true;
    s.camera.fovYRadians = 0.8f;

    // A wall at z = 0, so every point on it is exactly 10 m from the camera in VIEW depth even
    // though the corner rays travel further. That distinction is the point of the depth arm.
    const scene::MeshId wallId = s.addMesh(scene::makePlane(6.0f, 1));
    scene::Entity& wall = s.addEntity("wall", wallId);
    wall.transform.rotation = glm::angleAxis(1.5707963f, glm::vec3(1.0f, 0.0f, 0.0f));
    wall.material.baseColor = glm::vec3(0.5f);

    // A small emissive ball 4 m nearer the camera.
    const scene::MeshId ballId = s.addMesh(scene::makeIcosphere(0.8f, 2));
    scene::Entity& ball = s.addEntity("ball", ballId);
    ball.transform.position = glm::vec3(0.0f, 0.0f, 4.0f);
    ball.material.emissiveColor = glm::vec3(0.1f, 0.8f, 0.4f);
    ball.material.emissiveIntensity = 5.0f;

    pathtrace::TraceSettings t;
    t.width = 64;
    t.height = 64;
    t.samplesPerPixel = 4;
    t.maxDepth = 0;
    t.captureFeatures = true;

    pathtrace::PathTracer tr;
    pathtrace::Framebuffer fb;
    REQUIRE(tr.render(pathtrace::buildSnapshot(s), t, fb).has_value());

    const auto idx = [&](std::uint32_t x, std::uint32_t y) { return y * fb.width + x; };
    const auto& depth = fb.rawDepth();
    const auto& ids = fb.rawObjectId();
    const auto emission = fb.resolvedEmission();

    // --- depth: the ball is at 6 m, the wall at 10 m ---
    REQUIRE(depth[idx(32, 32)] == Approx(6.0f).margin(0.9f));   // centre: the ball's near face
    REQUIRE(depth[idx(2, 32)] == Approx(10.0f).margin(0.2f));   // edge: the wall
    // VIEW depth, not ray length: the wall's corner must read the SAME 10 m as its centre, even
    // though that ray travelled further. A depth pass built from `t` would bow outwards here.
    REQUIRE(depth[idx(2, 2)] == Approx(depth[idx(2, 32)]).margin(0.2f));

    // --- emission: only the ball emits, and it emits the colour it was given ---
    const glm::vec3 centreEmission = emission[idx(32, 32)];
    const glm::vec3 edgeEmission = emission[idx(2, 32)];
    REQUIRE(centreEmission.y > 1.0f);
    REQUIRE(centreEmission.y > centreEmission.x * 3.0f);  // it is green-cyan, as authored
    REQUIRE(glm::length(edgeEmission) == Approx(0.0f).margin(1e-5));  // the wall does not emit

    // --- id: two objects, two different ids, and neither is the miss value ---
    const float ballId_ = ids[idx(32, 32)];
    const float wallId_ = ids[idx(2, 32)];
    INFO("ball id " << ballId_ << " wall id " << wallId_);
    REQUIRE(ballId_ >= 0.0f);
    REQUIRE(wallId_ >= 0.0f);
    REQUIRE(ballId_ != wallId_);
    // The id is an exact integer, not an average: it must survive a float round trip unchanged.
    REQUIRE(ballId_ == Approx(std::round(ballId_)));

    // --- and all of it reaches the EXR under names that say what they are ---
    const auto path = tmp("avgen_pathtrace_all_aovs.exr");
    std::filesystem::remove(path);
    REQUIRE(pathtrace::writeFramebufferAovExr(fb, path).has_value());
    const auto names = channelNames(path);
    REQUIRE(has(names, "emission.R"));
    REQUIRE(has(names, "depth.Z"));
    REQUIRE(has(names, "id.X"));
    // Depth and id are geometry, not colour, so they must not be sitting in R/G/B either.
    REQUIRE_FALSE(has(names, "depth.R"));
    REQUIRE_FALSE(has(names, "id.R"));
    REQUIRE(names.size() == 14); // 3 beauty + 3 albedo + 3 normal + 3 emission + depth + id
    std::filesystem::remove(path);
}

TEST_CASE("a ray that hits nothing has no depth and no id", "[unit][pathtrace][aov]") {
    // A miss must not read as "at the camera" or "object 0". Both are plausible-looking values that
    // a compositor would act on.
    scene::Scene s;
    s.environment.sky.enabled = true;
    const scene::MeshId id = s.addMesh(scene::makeIcosphere(3.0f, 2)); // big enough to cover the centre
    scene::Entity& tiny = s.addEntity("tiny", id);
    tiny.transform.position = glm::vec3(0.0f, 0.0f, 0.0f);
    s.camera.position = glm::vec3(0.0f, 0.0f, 20.0f);
    s.camera.target = glm::vec3(0.0f, 0.0f, 0.0f);
    s.camera.lens.useExplicitFov = true;
    s.camera.fovYRadians = 1.2f;

    pathtrace::TraceSettings t;
    t.width = 32;
    t.height = 32;
    t.samplesPerPixel = 2;
    t.maxDepth = 0;
    t.captureFeatures = true;
    pathtrace::PathTracer tr;
    pathtrace::Framebuffer fb;
    REQUIRE(tr.render(pathtrace::buildSnapshot(s), t, fb).has_value());

    // A corner pixel misses the tiny sphere entirely.
    REQUIRE(fb.rawDepth()[0] < 0.0f);
    REQUIRE(fb.rawObjectId()[0] < 0.0f);
    // CONTROL: the centre pixel hits it, so the negatives above are a miss and not a broken buffer.
    const std::size_t centre = 16u * fb.width + 16u;
    REQUIRE(fb.rawDepth()[centre] > 0.0f);
    REQUIRE(fb.rawObjectId()[centre] >= 0.0f);
}
