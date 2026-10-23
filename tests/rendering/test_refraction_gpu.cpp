// ADR-1603: the refraction phase for procedural materials, and the ladder's levers on it and on the cinematic water.
//
//   - a refractive procedural surface samples the refraction copy: in front of a red backdrop a blue crystal with no
//     opacity shows red, and its absorption darkens what it shows by channel;
//   - with the ladder's refraction off it draws as its opaque material (the fallback keeps its identity: blue);
//   - a scene with no refractive material pays nothing: the same render passes and the same pixels whether the
//     ladder allows refraction or not, and no refractive draw.
//   - ADR-1640: dispersion 0 is the single read, pixel for pixel; above 0 a tilted slab over a sharp white/black edge
//     separates red from blue there (a coloured fringe at the edge only), and the ladder's refractionDispersion off
//     brings back the single read exactly.
#include "core/log.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "rendering/water_renderer.hpp"
#include "scene/scene.hpp"

#include <catch2/catch_test_macros.hpp>
#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <vector>
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

scene::ProceduralGeometry cylinder(const char* name, float radius, float height, glm::vec3 at, glm::vec3 colour,
                                   std::uint64_t hash) {
    scene::ProceduralGeometry g;
    g.name = name;
    g.source.kind = scene::PrimitiveKind::Cylinder;
    g.source.radius = radius;
    g.source.height = height;
    g.source.radialSegments = 24;
    g.source.heightSegments = 2;
    scene::InstanceRecord r{};
    r.position = {at.x, at.y, at.z, 1.0f};
    r.rotation = {0.0f, 0.0f, 0.0f, 1.0f};
    r.scale = {1.0f, 1.0f, 1.0f, 0.0f};
    r.random = {0.5f, 0.5f, 0.5f, 0.5f};
    r.color = {1.0f, 1.0f, 1.0f, 0.0f};
    r.emissive = {1.0f, 1.0f, 1.0f, 0.0f};
    g.instances = {r};
    g.structureVersion = 1;
    g.meshHash = hash;
    g.material.baseColor = colour;
    g.material.unlit = true;
    return g;
}

// A wide red backdrop, and a blue crystal in front of its middle.
scene::Scene crystalScene(bool refractive) {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.camera.position = {0.0f, 1.0f, 8.0f};
    s.camera.target = {0.0f, 1.0f, 0.0f};
    s.procedurals.push_back(cylinder("backdrop", 6.0f, 6.0f, {0.0f, -2.0f, -4.0f}, {0.9f, 0.1f, 0.05f}, 11));
    auto crystal = cylinder("crystal", 0.8f, 3.0f, {0.0f, -0.5f, 2.0f}, {0.05f, 0.1f, 0.9f}, 12);
    if (refractive) {
        crystal.material.refraction.strength = 0.02f;
        crystal.material.refraction.opacity = 0.0f;
        crystal.material.refraction.thickness = 4.0f;
    }
    s.procedurals.push_back(crystal);
    return s;
}

struct Rgb {
    int r = 0, g = 0, b = 0;
};
Rgb centre(const gpu::Image8& img) {
    const std::size_t i = (static_cast<std::size_t>(img.height / 2) * img.width + img.width / 2) * 4;
    return {img.rgba[i], img.rgba[i + 1], img.rgba[i + 2]};
}

struct Rig {
    std::unique_ptr<gpu::Context> ctx = makeContext();
    gpu::ShaderLibrary shaders{*ctx, {std::filesystem::path(AVGEN_SHADER_SOURCE_DIR)}};
    rendering::SceneRenderer renderer{*ctx, shaders};
    Rig() { REQUIRE(renderer.init().has_value()); }
    gpu::Image8 render(const scene::Scene& s, bool refraction) {
        rendering::QualitySettings q = renderer.qualitySettings();
        q.refraction = refraction;
        renderer.setQualitySettings(q);
        auto img = renderer.renderToImage(s, FrameTime{}, 96, 96);
        REQUIRE(img.has_value());
        return *img;
    }
};

// ADR-1640: a white backdrop whose right edge is a sharp vertical line at x = `edgeX` (black beyond it), seen through
// a clear slab tilted about the vertical so its face normal has a constant sideways component: the whole slab reads the
// copy at one offset, and a dispersive slab reads red and blue at two others.
scene::Scene dispersionScene(float dispersion, float edgeX = 0.3f) {
    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    s.environment.showSkybox = false;
    s.camera.position = {0.0f, 1.0f, 8.0f};
    s.camera.target = {0.0f, 1.0f, 0.0f};
    auto backdrop = cylinder("backdrop", 1.0f, 1.0f, {edgeX - 4.0f, -2.0f, -4.0f}, {0.9f, 0.9f, 0.9f}, 21);
    backdrop.source.kind = scene::PrimitiveKind::Box;
    backdrop.source.size = {8.0f, 6.0f, 0.2f};
    backdrop.instances[0].position.y = 1.0f;
    s.procedurals.push_back(backdrop);
    auto slab = cylinder("slab", 1.0f, 1.0f, {0.0f, 1.0f, 2.0f}, {0.05f, 0.1f, 0.9f}, 22);
    slab.source.kind = scene::PrimitiveKind::Box;
    slab.source.size = {3.0f, 3.0f, 0.3f};
    const float half = glm::radians(35.0f) * 0.5f;
    slab.instances[0].rotation = {0.0f, std::sin(half), 0.0f, std::cos(half)};
    slab.material.refraction.strength = 0.1f;
    slab.material.refraction.opacity = 0.0f;
    slab.material.refraction.thickness = 4.0f;
    slab.material.refraction.dispersion = dispersion;
    s.procedurals.push_back(slab);
    return s;
}

// The largest |R - B| along the image's middle row, between 25% and 75% of its width (inside the slab), and the sign
// of red minus blue at that pixel.
struct Separation {
    int largest = 0;
    int redOverBlue = 0; // pixels in the window where R - B > 24
    int blueOverRed = 0; // ...and where B - R > 24
};
Separation separation(const gpu::Image8& img) {
    Separation out;
    const std::size_t row = img.height / 2;
    for (std::uint32_t x = img.width / 4; x < img.width * 3 / 4; ++x) {
        const std::size_t i = (row * img.width + x) * 4;
        const int d = static_cast<int>(img.rgba[i]) - static_cast<int>(img.rgba[i + 2]);
        out.largest = std::max(out.largest, std::abs(d));
        out.redOverBlue += d > 24 ? 1 : 0;
        out.blueOverRed += d < -24 ? 1 : 0;
    }
    return out;
}

} // namespace

TEST_CASE("Refraction dispersion separates red from blue at an edge; 0 is the single read (ADR-1640)",
          "[gpu][procedural][refraction][adr1640]") {
    Rig rig;
    const auto render = [&](const scene::Scene& s, bool dispersionAllowed) {
        rendering::QualitySettings q = rig.renderer.qualitySettings();
        q.refraction = true;
        q.refractionDispersion = dispersionAllowed;
        rig.renderer.setQualitySettings(q);
        auto img = rig.renderer.renderToImage(s, FrameTime{}, 160, 160);
        REQUIRE(img.has_value());
        return *img;
    };
    const auto none = render(dispersionScene(0.0f), true);
    const Separation flat = separation(none);
    INFO("dispersion 0: largest |R-B| " << flat.largest);
    CHECK(rig.renderer.stats().procedural.refractiveDraws >= 1);
    CHECK(flat.largest <= 4); // a grey edge stays grey through the single read

    // Not a blank frame: the edge is inside the window, so the single read shows both white and black there.
    {
        int bright = 0, dark = 0;
        const std::size_t row = none.height / 2;
        for (std::uint32_t x = none.width / 4; x < none.width * 3 / 4; ++x) {
            const int g = none.rgba[(row * none.width + x) * 4 + 1];
            bright += g > 150 ? 1 : 0;
            dark += g < 30 ? 1 : 0;
        }
        INFO("window: " << bright << " bright, " << dark << " dark");
        CHECK(bright >= 8);
        CHECK(dark >= 8);
    }

    const auto dispersed = render(dispersionScene(0.6f), true);
    const Separation split = separation(dispersed);
    INFO("dispersion 0.6: largest |R-B| " << split.largest << ", " << split.redOverBlue << " red-over-blue and "
                                          << split.blueOverRed << " blue-over-red pixels");
    CHECK(split.largest >= 80);
    // a fringe on each side of the edge: one channel has crossed it, the other not yet
    CHECK(split.redOverBlue + split.blueOverRed >= 3);
    // only near the edge: the rest of the slab stays grey
    CHECK(split.redOverBlue + split.blueOverRed <= 30);

    // The ladder's refractionDispersion off: the single read again, pixel for pixel the dispersion-0 frame.
    const auto ladderOff = render(dispersionScene(0.6f), false);
    CHECK(gpu::hashImage(ladderOff) == gpu::hashImage(none));
    // ...and dispersion 0 with the lever on or off is the same frame.
    CHECK(gpu::hashImage(render(dispersionScene(0.0f), false)) == gpu::hashImage(none));
    CHECK(rig.ctx->errorCount() == 0);

    // Evidence for review (ADR-1640): AVGEN_DISPERSION_DUMP=<dir> writes the three frames as PPM.
    if (const char* dir = std::getenv("AVGEN_DISPERSION_DUMP"); dir != nullptr && *dir != '\0') {
        std::filesystem::create_directories(dir);
        REQUIRE(gpu::writePpm(none, std::filesystem::path(dir) / "dispersion-0.ppm").has_value());
        REQUIRE(gpu::writePpm(dispersed, std::filesystem::path(dir) / "dispersion-0.6.ppm").has_value());
        REQUIRE(gpu::writePpm(ladderOff, std::filesystem::path(dir) / "dispersion-0.6-ladder-off.ppm").has_value());
    }
}

// ADR-1640: dispersion 0 is the frame from before ADR-1640. Hidden ([.]): it needs the shaders of a commit before
// ADR-1640 in a directory, AVGEN_BASELINE_SHADER_DIR (`git archive 0b0e2634 shaders | tar -x -C <dir>`, then point it
// at <dir>/shaders). The object uniforms of a material without dispersion are the same bytes either side (lane0.w is
// 0, a unit test checks the packing), so the shader is the only difference; both frames are rendered here.
TEST_CASE("Refraction with dispersion 0 matches the shaders from before ADR-1640 (baseline)",
          "[.][gpu][procedural][refraction][adr1640][baseline]") {
    const char* baselineDir = std::getenv("AVGEN_BASELINE_SHADER_DIR");
    if (baselineDir == nullptr || *baselineDir == '\0') {
        SKIP("AVGEN_BASELINE_SHADER_DIR is not set");
    }
    const auto renderWith = [](const std::filesystem::path& shaderDir, const scene::Scene& s) {
        auto ctx = makeContext();
        gpu::ShaderLibrary shaders{*ctx, {shaderDir}};
        rendering::SceneRenderer renderer{*ctx, shaders};
        REQUIRE(renderer.init().has_value());
        auto img = renderer.renderToImage(s, FrameTime{}, 160, 160);
        REQUIRE(img.has_value());
        CHECK(renderer.stats().procedural.refractiveDraws >= 1);
        CHECK(ctx->errorCount() == 0);
        return gpu::hashImage(*img);
    };
    for (const scene::Scene& s : {dispersionScene(0.0f), crystalScene(true)}) {
        const auto now = renderWith(AVGEN_SHADER_SOURCE_DIR, s);
        const auto before = renderWith(baselineDir, s);
        CHECK(now == before);
    }
}

// Hidden ([.]): ADR-1640's banding measure. A wide fringe (dispersion 1.5, 480 px) across the white/black edge: how many
// pixels it covers, how many distinct colours it passes through, and the largest step one channel takes between
// neighbouring pixels inside it (a three-read fringe is flat bands with full-height steps between them; a smoother
// spectrum has more colours and smaller steps; seven taps: 13 colours against three reads' 6). It WARNs the numbers
// and asserts only that a fringe was found.
TEST_CASE("Refraction dispersion fringe banding (measure)", "[.][gpu][refraction][measure][adr1640]") {
    Rig rig;
    rendering::QualitySettings q = rig.renderer.qualitySettings();
    q.refraction = true;
    q.refractionDispersion = true;
    rig.renderer.setQualitySettings(q);
    auto img = rig.renderer.renderToImage(dispersionScene(1.5f), FrameTime{}, 480, 480);
    REQUIRE(img.has_value());
    const std::size_t row = img->height / 2;
    const auto at = [&](std::uint32_t x, int c) { return static_cast<int>(img->rgba[(row * img->width + x) * 4 + c]); };
    int fringe = 0, largestStep = 0;
    std::vector<std::uint32_t> colours;
    for (std::uint32_t x = img->width / 4; x < img->width * 3 / 4; ++x) {
        if (std::abs(at(x, 0) - at(x, 2)) <= 8) {
            continue;
        }
        ++fringe;
        colours.push_back(static_cast<std::uint32_t>(at(x, 0) / 4) << 16 | static_cast<std::uint32_t>(at(x, 1) / 4) << 8 |
                          static_cast<std::uint32_t>(at(x, 2) / 4));
        for (int c = 0; c < 3; ++c) {
            largestStep = std::max({largestStep, std::abs(at(x, c) - at(x - 1, c)), std::abs(at(x + 1, c) - at(x, c))});
        }
    }
    std::sort(colours.begin(), colours.end());
    const auto distinct = static_cast<int>(std::unique(colours.begin(), colours.end()) - colours.begin());
    WARN("dispersion 1.5 at 480 px: a fringe of " << fringe << " px, " << distinct
                                                 << " distinct colours (6 bits a channel), largest channel step "
                                                 << largestStep);
    if (const char* dir = std::getenv("AVGEN_DISPERSION_DUMP"); dir != nullptr && *dir != '\0') {
        std::filesystem::create_directories(dir);
        REQUIRE(gpu::writePpm(*img, std::filesystem::path(dir) / "banding-1.5.ppm").has_value());
    }
    CHECK(fringe > 0);
}

// Hidden ([.]): ADR-1640's cost on a small fixture, 1080p, unquantised timestamps (ADR-1600). The dispersion scene's
// slab enlarged to cover about 40% of the frame; dispersion 0 (the single read) against 0.5, interleaved over eight reps
// of 120 frames. It WARNs the medians and asserts only that it measured.
TEST_CASE("Refraction dispersion cost on a small fixture (measure)", "[.][gpu][refraction][measure][adr1640]") {
    Rig rig;
    if (!rig.renderer.timeline().available()) {
        SKIP("timestamp queries unavailable");
    }
    const auto measure = [&](const scene::Scene& s) {
        std::vector<double> gpu, sceneMs;
        for (int i = 0; i < 140; ++i) {
            FrameTime t{};
            t.renderTime = i / 30.0;
            REQUIRE(rig.renderer.renderFrame(s, t, 3840, 2160).has_value());
            if (i >= 20) {
                gpu.push_back(rig.renderer.stats().gpuFrameMs);
                sceneMs.push_back(rig.renderer.timeline().msFor("scene"));
            }
        }
        std::sort(gpu.begin(), gpu.end());
        std::sort(sceneMs.begin(), sceneMs.end());
        return std::pair{gpu[gpu.size() / 2], sceneMs[sceneMs.size() / 2]};
    };
    const auto big = [](float dispersion) {
        scene::Scene s = dispersionScene(dispersion);
        s.procedurals[1].source.size = {5.0f, 4.0f, 0.3f};
        s.procedurals[1].meshHash = 23;
        return s;
    };
    const scene::Scene one = big(0.0f);
    const scene::Scene three = big(0.5f);
    static_cast<void>(measure(three)); // the clocks up and the pipelines built
    std::vector<double> gpuOne, gpuThree, sceneOne, sceneThree;
    for (int rep = 0; rep < 8; ++rep) { // interleaved, so a clock drift lands on both arms
        const auto a = measure(one);
        const auto b = measure(three);
        gpuOne.push_back(a.first);
        sceneOne.push_back(a.second);
        gpuThree.push_back(b.first);
        sceneThree.push_back(b.second);
    }
    const auto list = [](const std::vector<double>& v) {
        std::string out;
        for (const double x : v) {
            out += fmt::format(" {:.3f}", x);
        }
        return out;
    };
    std::vector<double> delta;
    for (std::size_t i = 0; i < sceneOne.size(); ++i) {
        delta.push_back(sceneThree[i] - sceneOne[i]);
    }
    std::vector<double> sortedDelta = delta;
    std::sort(sortedDelta.begin(), sortedDelta.end());
    WARN("4K, a slab over about 40% of the frame, 8 interleaved reps of 120 frames (medians, ms): GPU dispersion 0"
         << list(gpuOne) << " | 0.5" << list(gpuThree) << "; the scene label 0" << list(sceneOne) << " | 0.5"
         << list(sceneThree) << "; scene-label delta per rep" << list(delta) << " (median of reps "
         << fmt::format("{:.3f}", (sortedDelta[3] + sortedDelta[4]) * 0.5) << ")");
    CHECK(gpuThree[0] > 0.0);
}

TEST_CASE("A refractive procedural surface shows the refraction copy, absorbed by channel (ADR-1603)",
          "[gpu][procedural][refraction][adr1603]") {
    Rig rig;
    scene::Scene s = crystalScene(true);

    // Clear, no opacity: the crystal shows the red backdrop behind it.
    const Rgb clear = centre(rig.render(s, true));
    INFO("clear crystal centre " << clear.r << " " << clear.g << " " << clear.b);
    CHECK(clear.r > 100);
    CHECK(clear.r > clear.b + 60);
    CHECK(rig.renderer.stats().procedural.refractiveObjects == 1);
    CHECK(rig.renderer.stats().procedural.refractiveDraws >= 1);

    // Red absorbed over the path behind it: what it shows darkens in red.
    s.procedurals[1].material.refraction.absorption = {1.5f, 0.0f, 0.0f};
    const Rgb absorbing = centre(rig.render(s, true));
    INFO("absorbing crystal centre " << absorbing.r << " " << absorbing.g << " " << absorbing.b);
    CHECK(absorbing.r < clear.r - 30);

    // Full opacity: its own shaded colour again, through the refraction pipeline.
    s.procedurals[1].material.refraction.absorption = {0.0f, 0.0f, 0.0f};
    s.procedurals[1].material.refraction.opacity = 1.0f;
    const Rgb opaque = centre(rig.render(s, true));
    CHECK(opaque.b > opaque.r + 60);

    // The ladder's refraction off: the opaque material, its identity kept, and no refractive draw.
    s.procedurals[1].material.refraction.opacity = 0.0f;
    const Rgb fallback = centre(rig.render(s, false));
    INFO("fallback centre " << fallback.r << " " << fallback.g << " " << fallback.b);
    CHECK(fallback.b > fallback.r + 60);
    CHECK(rig.renderer.stats().procedural.refractiveDraws == 0);
    CHECK(rig.ctx->errorCount() == 0);
}

TEST_CASE("A scene with no refractive surface pays nothing for the refraction phase (ADR-1603)",
          "[gpu][procedural][refraction][adr1603]") {
    Rig rig;
    const scene::Scene s = crystalScene(false);
    const auto on = rig.render(s, true);
    const auto passesOn = rig.renderer.stats().state.renderPasses;
    CHECK(rig.renderer.stats().procedural.refractiveObjects == 0);
    CHECK(rig.renderer.stats().procedural.refractiveDraws == 0);
    const auto off = rig.render(s, false);
    CHECK(rig.renderer.stats().state.renderPasses == passesOn);
    CHECK(gpu::hashImage(on) == gpu::hashImage(off));

    // ...and the refractive scene's phase is one more render pass (the split after the copy), not more.
    const scene::Scene r = crystalScene(true);
    static_cast<void>(rig.render(r, true));
    CHECK(rig.renderer.stats().state.renderPasses == passesOn + 1);
    CHECK(rig.ctx->errorCount() == 0);
}

TEST_CASE("The water SSR lever scales the cinematic steps and rays, and turns the SSR off at 0 (ADR-1603)",
          "[gpu][water][refraction][adr1603]") {
    rendering::WaterUniforms u{};
    u.cineA.x = 1.0f;  // ssr
    u.cineB.z = 48.0f; // steps
    u.cineC.y = 4.0f;  // rays
    rendering::WaterUniforms same = u;
    rendering::applyWaterSsrScale(same, 1.0f);
    CHECK(same.cineB.z == 48.0f);
    CHECK(same.cineC.y == 4.0f);
    rendering::WaterUniforms half = u;
    rendering::applyWaterSsrScale(half, 0.5f);
    CHECK(half.cineB.z == 24.0f);
    CHECK(half.cineC.y == 2.0f);
    CHECK(half.cineA.x == 1.0f);
    rendering::WaterUniforms tiny = u;
    rendering::applyWaterSsrScale(tiny, 0.05f);
    CHECK(tiny.cineB.z == 8.0f); // the shader's own floor
    CHECK(tiny.cineC.y == 1.0f);
    rendering::WaterUniforms off = u;
    rendering::applyWaterSsrScale(off, 0.0f);
    CHECK(off.cineA.x == 0.0f);
}

// Hidden ([.]): the small-fixture cost of the refraction phase at 1080p with unquantised timestamps (ADR-1600). Run it
// by name; it measures and WARNs, asserting only that it measured.
TEST_CASE("Refraction phase cost on a small fixture (measure)", "[.][gpu][refraction][measure][adr1603]") {
    Rig rig;
    if (!rig.renderer.timeline().available()) {
        SKIP("timestamp queries unavailable");
    }
    const auto measure = [&](const scene::Scene& s, bool refraction) {
        std::vector<double> gpu, sceneMs;
        rendering::QualitySettings q = rig.renderer.qualitySettings();
        q.refraction = refraction;
        rig.renderer.setQualitySettings(q);
        for (int i = 0; i < 140; ++i) {
            FrameTime t{};
            t.renderTime = i / 30.0;
            REQUIRE(rig.renderer.renderFrame(s, t, 1920, 1080).has_value());
            if (i >= 20) {
                gpu.push_back(rig.renderer.stats().gpuFrameMs);
                sceneMs.push_back(rig.renderer.timeline().msFor("scene"));
            }
        }
        std::sort(gpu.begin(), gpu.end());
        std::sort(sceneMs.begin(), sceneMs.end());
        return std::pair{gpu[gpu.size() / 2], sceneMs[sceneMs.size() / 2]};
    };
    // the crystal enlarged to cover about a third of the frame
    scene::Scene s = crystalScene(true);
    s.procedurals[1].source.radius = 2.5f;
    s.procedurals[1].meshHash = 13;
    s.procedurals[1].material.refraction.absorption = {0.4f, 0.8f, 0.2f};
    s.procedurals[1].material.refraction.opacity = 0.25f;
    static_cast<void>(measure(s, true)); // the clocks up and the pipelines built
    std::vector<double> gpuOff, gpuOn, sceneOff, sceneOn;
    for (int rep = 0; rep < 4; ++rep) { // interleaved, so a clock drift lands on both arms
        const auto a = measure(s, false);
        const auto b = measure(s, true);
        gpuOff.push_back(a.first);
        sceneOff.push_back(a.second);
        gpuOn.push_back(b.first);
        sceneOn.push_back(b.second);
    }
    const auto list = [](const std::vector<double>& v) {
        std::string out;
        for (const double x : v) {
            out += fmt::format(" {:.2f}", x);
        }
        return out;
    };
    WARN("1080p, a crystal over about a third of the frame, 4 interleaved reps of 120 frames (medians, ms): GPU off"
         << list(gpuOff) << " | on" << list(gpuOn) << "; the scene label off" << list(sceneOff) << " | on"
         << list(sceneOn));
    const auto on = std::pair{gpuOn[0], sceneOn[0]};
    CHECK(on.first > 0.0);
}
