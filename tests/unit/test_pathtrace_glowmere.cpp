// Glowmere Valley through the path tracer (ADR-351 Phase 7).
//
// This is the phase where the section 55 capability report stops being theoretical. Glowmere has
// everything the tracer cannot do: GPU-only particles, water whose entire look lives in a shader,
// scattered ecology, skinned characters. The deliverable here is NOT "everything works" -- it is an
// honest, complete, tested account of what does and what does not, produced BEFORE the render
// rather than discovered after somebody looks at the frame.
//
// Every assertion is conditional on the project loading, because the assets are gitignored and a
// worktree without them must skip rather than fail. That is the rule main landed in
// `test_treeisland_example.cpp` after a half-linked worktree produced a confusing failure -- and
// after this branch reported that failure as somebody else's twice.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "pathtrace/path_tracer.hpp"
#include "pathtrace/snapshot.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <cmath>
#include <cstdio>
#include <string>

using namespace avgen;
using Catch::Approx;

namespace {

std::filesystem::path projectPath(const char* name) {
    return std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" / name;
}

// Loads a project and evaluates it at `seconds`. Returns null if the project or its assets are
// absent, so a worktree without them skips instead of failing.
std::unique_ptr<app::Engine> loadAt(const char* name, double seconds) {
    const auto path = projectPath(name);
    if (!std::filesystem::exists(path)) return nullptr;
    auto engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
    if (!engine->loadProject(path)) return nullptr;
    // One warm-up at dt 0 then the real step, which is what app::RenderJob does: some systems need
    // a frame to settle and a snapshot of frame zero is not a snapshot of the scene.
    engine->update(FrameTime{seconds, 0.0, 0});
    engine->update(FrameTime{seconds, 1.0 / 60.0, 1});
    return engine;
}

const pathtrace::Capability* find(const pathtrace::Snapshot& s, std::string_view feature) {
    for (const auto& c : s.capabilities.entries) {
        if (c.feature == feature) return &c;
    }
    return nullptr;
}

float luminance(const glm::vec3& c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

} // namespace

TEST_CASE("Glowmere Valley reaches the path tracer as geometry", "[unit][pathtrace][glowmere]") {
    auto engine = loadAt("glowmere-valley-2-multicam.json", 12.0);
    if (engine == nullptr) {
        SUCCEED("glowmere-valley-2-multicam.json or its assets are not in this worktree");
        return;
    }

    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(engine->scene());
    pathtrace::logCapabilities(snap);

    INFO("meshes " << snap.meshes.size() << " triangles " << snap.triangleCount() << " lights "
                   << snap.lights.size() << " emissive tris " << snap.emissiveTriangles.size());

    // A real world, not a toy. If this collapses to a handful of triangles the snapshot is dropping
    // something silently, which is the failure mode section 87 exists to prevent.
    REQUIRE(snap.meshes.size() > 20);
    REQUIRE(snap.triangleCount() > 50000);

    // Every triangle must be finite. One NaN vertex poisons the BVH and the failure surfaces later
    // as an unexplained black region rather than as a bad mesh.
    for (const auto& m : snap.meshes) {
        for (const auto& p : m.positions) {
            REQUIRE(std::isfinite(p.x));
            REQUIRE(std::isfinite(p.y));
            REQUIRE(std::isfinite(p.z));
        }
        for (const auto& n : m.normals) {
            REQUIRE(glm::length(n) == Approx(1.0f).margin(1e-3));
        }
    }
}

TEST_CASE("the capability report accounts for what Glowmere contains",
          "[unit][pathtrace][glowmere][capability]") {
    auto engine = loadAt("glowmere-valley-2-multicam.json", 12.0);
    if (engine == nullptr) {
        SUCCEED("project or assets absent");
        return;
    }
    const scene::Scene& scene = engine->scene();
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(scene);

    // The report must not be empty for a world this complicated. An empty report on Glowmere would
    // mean the tracer believes it rendered everything, which is the one thing that cannot be true.
    REQUIRE_FALSE(snap.capabilities.entries.empty());
    INFO("report:\n" << snap.capabilities.format());

    // Each claim below is conditional on the scene ACTUALLY containing the thing, so the test says
    // something real on whatever Glowmere happens to hold rather than encoding today's content.
    if (!scene.particles.empty()) {
        const auto* c = find(snap, "particle system");
        REQUIRE(c != nullptr);
        REQUIRE(c->support == pathtrace::Support::Unsupported);
        REQUIRE(c->count == static_cast<int>(scene.particles.size()));
        // And it must say WHY, not merely that it failed.
        REQUIRE(c->detail.find("compute shader") != std::string::npos);
    }
    if (!scene.procedurals.empty()) {
        const auto* c = find(snap, "procedural instance");
        const auto* failed = find(snap, "procedural source");
        REQUIRE((c != nullptr || failed != nullptr));
        if (c != nullptr) REQUIRE(c->count > 0);
    }

    const bool hasWater = std::any_of(scene.entities.begin(), scene.entities.end(),
                                      [](const scene::Entity& e) { return e.style == scene::MeshStyle::Water; });
    if (hasWater) {
        const auto* c = find(snap, "water surface");
        REQUIRE(c != nullptr);
        REQUIRE(c->support == pathtrace::Support::Degraded);
    }

    // Anything reported as Unsupported or Degraded must carry a non-empty explanation. A report row
    // that says "unsupported" and nothing else is not a report, it is a shrug.
    for (const auto& c : snap.capabilities.entries) {
        INFO("feature " << c.feature);
        REQUIRE_FALSE(c.detail.empty());
        REQUIRE(c.count >= 0);
    }
}

TEST_CASE("a Glowmere frame renders and is a picture rather than a colour",
          "[unit][pathtrace][glowmere]") {
    auto engine = loadAt("glowmere-valley-2-multicam.json", 12.0);
    if (engine == nullptr) {
        SUCCEED("project or assets absent");
        return;
    }
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(engine->scene());

    pathtrace::TraceSettings t;
    t.width = 192;
    t.height = 108;
    t.samplesPerPixel = 8;
    t.maxDepth = 2;
    t.russianRouletteDepth = 2;
    t.captureFeatures = true;
    t.debugCheckNonFinite = true;

    pathtrace::PathTracer tr;
    pathtrace::Framebuffer fb;
    const auto ok = tr.render(snap, t, fb);
    REQUIRE(ok.has_value());

    REQUIRE_FALSE(fb.isBlack());
    REQUIRE(tr.stats().nonFiniteSamples == 0);
    REQUIRE(tr.stats().negativeSamples == 0);

    // Not a flat fill. A renderer that painted every pixel the background colour would pass
    // "not black", and this project has shipped exactly that bug before in a different renderer:
    // a skybox whose delivered frame was the empty sky's own hash, bit for bit.
    const auto pixels = fb.resolvedRadiance();
    float lo = 1e30f;
    float hi = -1e30f;
    for (const auto& c : pixels) {
        const float l = luminance(c);
        lo = std::min(lo, l);
        hi = std::max(hi, l);
    }
    INFO("luminance range " << lo << " .. " << hi << " mean " << fb.meanLuminance());
    REQUIRE(hi > lo * 4.0f);   // there is real structure, not one colour
    REQUIRE(hi > 0.0f);

    // Count distinct-ish luminance buckets: a gradient would pass the range check above, a real
    // frame has content spread across many levels.
    int buckets[32] = {};
    for (const auto& c : pixels) {
        const float l = luminance(c);
        const int b = std::clamp(static_cast<int>(31.0f * l / std::max(hi, 1e-6f)), 0, 31);
        buckets[b]++;
    }
    const int occupied = static_cast<int>(std::count_if(std::begin(buckets), std::end(buckets),
                                                        [](int n) { return n > 0; }));
    INFO("occupied luminance buckets " << occupied << " of 32");
    REQUIRE(occupied >= 8);

    // The depth AOV must agree with the picture: a world scene has depth at a range of distances,
    // not one plane and not all misses.
    const auto& depth = fb.rawDepth();
    const int hits = static_cast<int>(std::count_if(depth.begin(), depth.end(),
                                                    [](float d) { return d > 0.0f; }));
    REQUIRE(hits > static_cast<int>(depth.size()) / 10);
    float dLo = 1e30f;
    float dHi = 0.0f;
    for (float d : depth) {
        if (d <= 0.0f) continue;
        dLo = std::min(dLo, d);
        dHi = std::max(dHi, d);
    }
    INFO("depth range " << dLo << " .. " << dHi << " over " << hits << " hits");
    REQUIRE(dHi > dLo * 2.0f);
}

TEST_CASE("preview: render Glowmere to /tmp", "[.pathtrace-glowmere-preview]") {
    auto engine = loadAt("glowmere-valley-2-multicam.json", 12.0);
    if (engine == nullptr) {
        WARN("project or assets absent");
        return;
    }
    const pathtrace::Snapshot snap = pathtrace::buildSnapshot(engine->scene());
    pathtrace::logCapabilities(snap);

    pathtrace::TraceSettings t;
    t.width = 480;
    t.height = 270;
    t.samplesPerPixel = 32;
    t.maxDepth = 3;
    t.russianRouletteDepth = 2;
    t.captureFeatures = true;

    pathtrace::PathTracer tr;
    pathtrace::Framebuffer fb;
    REQUIRE(tr.render(snap, t, fb).has_value());

    const auto path = std::filesystem::temp_directory_path() / "avgen_glowmere.ppm";
    FILE* f = std::fopen(path.c_str(), "wb");
    REQUIRE(f != nullptr);
    std::fprintf(f, "P6\n%u %u\n255\n", fb.width, fb.height);
    for (std::uint32_t y = 0; y < fb.height; ++y) {
        for (std::uint32_t x = 0; x < fb.width; ++x) {
            const glm::vec3 c = fb.pixel(x, y);
            for (int k = 0; k < 3; ++k) {
                const float v = std::pow(std::clamp(c[k] * 4.0f, 0.0f, 1.0f), 1.0f / 2.2f);
                const auto b = static_cast<unsigned char>(v * 255.0f + 0.5f);
                std::fwrite(&b, 1, 1, f);
            }
        }
    }
    std::fclose(f);
    WARN("wrote " << path.string() << "  mean " << fb.meanLuminance() << "  triangles "
                  << snap.triangleCount() << "  bvh " << tr.stats().buildSeconds * 1000.0 << " ms"
                  << "  render " << tr.stats().renderSeconds * 1000.0 << " ms");
}
