// Phase 6.2 of the renderer forensics plan: the water mask, the depths it is made of, and the
// spaces those depths are measured in.
//
// `SYM-WATER-1` -- water standing on dry ground at a shoreline -- was reproduced, root-caused and
// fixed elsewhere: `world::buildChunkWater` emits a quad when any corner is wet, so the sheet
// overhangs the bank by one grid cell on purpose, and a dry corner used to report the depth of the
// level it borrowed from a wet neighbour instead of the depth at itself. Two tests already guard
// that: `[gpu][renderer][water][forensics][shoreline]` (six views over synthetic beds, no water
// pixel on dry land) and `[unit][water][forensics][shoreline]` (the geometry invariant on the real
// generator). None of that is repeated here.
//
// What is here is the rest of 6.2, and its centre of gravity is the *space* audit. A shoreline is a
// comparison between two depths, and every artefact this phase is chasing is what happens when the
// two are not in the same units, on the same axis, or against the same origin. The chain is:
//
//   linear_depth.wgsl   writes max(dot(p - cameraPos, cameraForward), 1e-4) -- along the forward
//                       axis, in metres, from a depth buffer resolved through invViewProj
//   water.wgsl          reads that as `bed` and compares it against its own
//                       viewDepth = dot(worldPos - cameraPos, cameraForward);
//                       thickness = bed - viewDepth
//   the vertex          carries uv.x, the *vertical* bed depth in metres, which drives the shore
//                       fade and caps the ray thickness
//
// Three quantities, two along the view ray and one vertical, and the whole shoreline is their
// difference. So the tests below measure them from outside: the target against camera mathematics
// the CPU does independently, the reconstruction against the terrain field the world was generated
// from, and the water's own opacity against where in the frame the same water landed -- which is
// the measurement that separates "along the forward axis" from "to the eye".
//
// Every quantitative test runs with post and ambient occlusion off and reads the scene-linear HDR
// target rather than the tone-mapped image. Auto-exposure re-meters when a frame's content changes,
// so an absolute threshold on a tone-mapped pixel compares two exposures rather than two surfaces;
// and the AO buffer is temporally jittered, which would put frame-index noise into every difference
// taken here.

#include "app/engine.hpp"
#include "app/viewport_pick.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"
#include "scene/scene.hpp"
#include "world/terrain_query.hpp"
#include "world/world_map.hpp"

#include <fmt/format.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kW = 192;
constexpr std::uint32_t kH = 128;

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

// Post off so nothing exposes, grades or blooms between the two arms of a comparison; ambient
// occlusion off so the only thing varying across frames is what the test is varying.
rendering::SceneRenderer::PassToggles quantitativeToggles() {
    rendering::SceneRenderer::PassToggles toggles;
    toggles.post = false;
    toggles.ao = false;
    return toggles;
}

// The linear-depth target is R32Float and the readback library offers a whole-texture reader only
// for R32Uint. Both are one four-byte channel per texel and a texture-to-buffer copy does not
// interpret them, so the same call reads the float target and the bits come back untouched. Worth
// doing rather than walking a frame through `readTexelR32Float`, which is one submit and one stall
// per pixel.
std::vector<float> readLinearDepth(gpu::Context& ctx, const wgpu::Texture& texture) {
    auto bits = gpu::readTextureR32Uint(ctx, texture, kW, kH);
    REQUIRE(bits.has_value());
    std::vector<float> out(bits->size());
    for (std::size_t i = 0; i < bits->size(); ++i) {
        out[i] = std::bit_cast<float>((*bits)[i]);
    }
    return out;
}

app::PickView pickViewFor(const rendering::RendererDiagnosticFrame& frame) {
    app::PickView view;
    view.invViewProj = glm::inverse(frame.viewProjection);
    view.cameraPosition = frame.cameraPosition;
    // The axis linear_depth.wgsl projects onto: the third column of the inverse view matrix,
    // negated, because a camera looks down -Z in view space.
    view.cameraForward = -glm::normalize(glm::vec3(glm::inverse(frame.view)[2]));
    view.size = {kW, kH};
    return view;
}

// Where a world point lands on screen, or nothing when it is behind the camera or off the frame.
std::optional<glm::uvec2> projectToPixel(const glm::mat4& viewProj, glm::vec3 p) {
    const glm::vec4 clip = viewProj * glm::vec4(p, 1.0f);
    if (clip.w <= 1e-4f) {
        return std::nullopt;
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    const float x = (ndc.x * 0.5f + 0.5f) * static_cast<float>(kW);
    const float y = (0.5f - ndc.y * 0.5f) * static_cast<float>(kH);
    if (!(x >= 0.0f && x < static_cast<float>(kW) && y >= 0.0f && y < static_cast<float>(kH))) {
        return std::nullopt;
    }
    return glm::uvec2(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y));
}

double channelDifference(const float* a, const float* b) {
    return std::fabs(static_cast<double>(a[0]) - b[0]) + std::fabs(static_cast<double>(a[1]) - b[1]) +
           std::fabs(static_cast<double>(a[2]) - b[2]);
}

// Which pixels the water owns, measured rather than declared. Water writes no object identifier --
// its pipeline masks every auxiliary target but colour and emission, deliberately, because a normal
// or an id averaged over a transparency is worse than none -- so the identifier target cannot answer
// "is there water here" and an A/B against the same frame with the water pass off is the only
// instrument that can.
std::vector<float> waterContribution(const gpu::ImageF& wet, const gpu::ImageF& dry) {
    std::vector<float> out(static_cast<std::size_t>(kW) * kH, 0.0f);
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            out[static_cast<std::size_t>(y) * kW + x] =
                static_cast<float>(channelDifference(wet.pixel(x, y), dry.pixel(x, y)));
        }
    }
    return out;
}

// A water pixel is one the water pass changed at all. A float epsilon rather than a brightness: the
// two arms differ only by the water, so any change at all is the water.
constexpr float kCoveredEpsilon = 1e-5f;

std::size_t countCovered(const std::vector<float>& contribution) {
    return static_cast<std::size_t>(
        std::count_if(contribution.begin(), contribution.end(), [](float v) { return v > kCoveredEpsilon; }));
}

// The QA water scene, driven through the Engine so the shoreline is a real generated one: the node
// carries no `world` block, which gives `world::defaultWorld()` and its river.
struct Shore {
    std::unique_ptr<gpu::Context> ctx;
    std::unique_ptr<gpu::ShaderLibrary> shaders;
    std::unique_ptr<rendering::SceneRenderer> renderer;
    std::unique_ptr<app::Engine> engine;
    FixedStepClock clock{60.0};
    FrameTime time{};

    static fs::path scenePath() {
        return fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / "renderer-qa-water.scene.json";
    }

    static Shore make() {
        Shore s;
        s.ctx = makeContext();
        s.shaders = std::make_unique<gpu::ShaderLibrary>(*s.ctx, std::vector{fs::path(AVGEN_SHADER_SOURCE_DIR)});
        s.renderer = std::make_unique<rendering::SceneRenderer>(*s.ctx, *s.shaders);
        REQUIRE(s.renderer->init().has_value());
        s.renderer->setPassToggles(quantitativeToggles());
        s.engine = std::make_unique<app::Engine>(app::EngineMode::Offline);
        REQUIRE(s.engine->loadComposition(scenePath()).has_value());
        s.step();
        return s;
    }

    // One clock, ticked. A clock restarted at each second reports a frame delta of zero and
    // everything that integrates then stands still.
    void step() {
        time = engine->tick(clock);
        engine->setViewport(kW, kH);
        engine->update(time);
    }

    // The camera the scene file authors is a grazing view from the bank, which is where
    // `SYM-WATER-1` was reported; tests that need the bed and the surface over the same ground look
    // down at the river instead, and say so where they do it.
    void aim(glm::vec3 position, glm::vec3 target) {
        params::ParameterSet& parameters = engine->params();
        auto* mode = parameters.findAs<int>("camera/mode");
        auto* pos = parameters.findAs<glm::vec3>("camera/position");
        auto* at = parameters.findAs<glm::vec3>("camera/target");
        REQUIRE(mode != nullptr);
        REQUIRE(pos != nullptr);
        REQUIRE(at != nullptr);
        mode->setBase(1); // free
        pos->setBase(position);
        at->setBase(target);
        // `setBase` does not reach `applyParameters`, which reads the *final* value.
        parameters.resetFinals();
        step();
        // The camera is re-derived from these parameters every update, so writing `scene().camera`
        // directly would have done nothing. This is the line that says the pose was actually taken.
        REQUIRE(scene().camera.position == position);
        REQUIRE(scene().camera.target == target);
    }

    // Pins the timeline. Every test that varies the camera calls this after each move, so the
    // ripples are in the same place in every frame of the comparison and the only thing that
    // changed is the thing being changed.
    void hold(double seconds) {
        time.renderTime = seconds;
        time.deltaTime = 1.0 / 60.0;
        time.frameIndex = 0;
        engine->update(time);
    }

    scene::Scene& scene() { return engine->composition()->scene(); }
    world::TerrainQuery query() { return engine->composition()->terrainQuery(); }

    gpu::ImageF render() {
        auto image = renderer->renderToImageFloat(scene(), time, kW, kH);
        REQUIRE(image.has_value());
        return std::move(*image);
    }

    // The same frame with the water pass off, which is the only reference that isolates the water.
    gpu::ImageF renderDry() {
        auto toggles = quantitativeToggles();
        toggles.water = false;
        renderer->setPassToggles(toggles);
        gpu::ImageF image = render();
        renderer->setPassToggles(quantitativeToggles());
        return image;
    }

    // The bed A/B: the ground made emissive, so any pixel that shows some of the bed changes by an
    // amount proportional to how much of it survives the water. Making it emissive rather than
    // moving it leaves every depth in the frame exactly where it was.
    gpu::ImageF renderLitBed() {
        std::vector<float> saved;
        for (scene::Entity& e : scene().entities) {
            saved.push_back(e.material.emissiveIntensity);
            if (e.style != scene::MeshStyle::Water) {
                e.material.emissiveColor = {1.0f, 0.0f, 0.0f};
                e.material.emissiveIntensity = 4.0f;
            }
        }
        gpu::ImageF image = render();
        for (std::size_t i = 0; i < scene().entities.size(); ++i) {
            scene().entities[i].material.emissiveIntensity = saved[i];
        }
        return image;
    }
};

// A single flat quad, for the tests that need geometry whose position the CPU knows exactly.
scene::MeshData quad(float half, float y) {
    scene::MeshData mesh;
    mesh.vertices.push_back({{-half, y, -half}, {0, 1, 0}, {0, 0}});
    mesh.vertices.push_back({{half, y, -half}, {0, 1, 0}, {1, 0}});
    mesh.vertices.push_back({{half, y, half}, {0, 1, 0}, {1, 1}});
    mesh.vertices.push_back({{-half, y, half}, {0, 1, 0}, {0, 1}});
    mesh.indices = {0, 2, 1, 0, 3, 2};
    return mesh;
}

} // namespace

// ---- the space audit, part one: the target itself -----------------------------------------------
//
// `linear_depth.wgsl` resolves the depth attachment to R32F metres, and everything downstream --
// ambient occlusion, the contact-shadow march, the particle fog coupling, picking and the water's
// own thickness -- reads it as *distance along the camera's forward axis*. The distance to the eye
// is a different number, larger by 1/cos of the angle off that axis; the two agree exactly in the
// middle of the frame and nowhere else. That is what makes a mistake here easy to ship: it looks
// right wherever anyone puts the thing they are checking.
//
// So this checks it where the two differ. One flat quad whose plane the CPU knows exactly, taps
// spread to the corners of the frame, and the expected value computed independently from the
// renderer's own camera matrices.
TEST_CASE("the linear depth target holds distance along the forward axis and not to the eye",
          "[gpu][renderer][forensics][water6_2]") {
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    scene::Scene s;
    s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
    const auto mesh = s.addMesh(quad(300.0f, 0.0f));
    scene::Entity& floor = s.addEntity("floor", mesh);
    floor.material.baseColor = {0.5f, 0.5f, 0.5f};
    s.camera.position = {0.0f, 18.0f, 18.0f};
    s.camera.target = {0.0f, 0.0f, 0.0f};
    s.camera.farPlane = 500.0f;
    REQUIRE(renderer.renderFrame(s, FrameTime{}, kW, kH).has_value());

    const rendering::RendererDiagnosticFrame frame = renderer.diagnosticFrame();
    const app::PickView view = pickViewFor(frame);
    const std::vector<float> depth = readLinearDepth(*ctx, renderer.linearDepthTexture());

    double worstAbsolute = 0.0;
    double worstRelative = 0.0;
    double widestSpread = 0.0; // how far apart the two candidate spaces get across the taps
    std::size_t taps = 0;
    for (std::uint32_t ty = 2; ty < kH; ty += 5) {
        for (std::uint32_t tx = 2; tx < kW; tx += 5) {
            // The CPU's own answer: the ray through this pixel, intersected with the plane the quad
            // lies in. Nothing in these four lines consults the depth buffer.
            const glm::vec3 ray = app::rayThroughPixel(view, {tx, ty});
            if (ray.y >= -1e-4f) {
                continue; // above the horizon: the quad is not under this pixel
            }
            const float t = -view.cameraPosition.y / ray.y;
            const glm::vec3 p = view.cameraPosition + ray * t;
            if (std::fabs(p.x) > 295.0f || std::fabs(p.z) > 295.0f) {
                continue; // past the quad's edge
            }
            const float expected = glm::dot(p - view.cameraPosition, view.cameraForward);
            const float toEye = glm::length(p - view.cameraPosition);
            const float measured = depth[static_cast<std::size_t>(ty) * kW + tx];
            REQUIRE(measured < app::kPickFarDistance); // the quad really is under this tap
            const double error = std::fabs(static_cast<double>(measured) - expected);
            worstAbsolute = std::max(worstAbsolute, error);
            worstRelative = std::max(worstRelative, error / expected);
            widestSpread = std::max<double>(widestSpread, static_cast<double>(toEye) / expected - 1.0);
            ++taps;
        }
    }

    INFO(taps << " taps; worst error " << worstAbsolute << " m (" << worstRelative * 100.0
              << "%), and the two spaces are up to " << widestSpread * 100.0 << "% apart across them");
    REQUIRE(taps > 100);
    // The test's own discriminating power, asserted rather than assumed. If every tap sat near the
    // optical axis the two spaces would agree and the check below would pass for either of them.
    REQUIRE(widestSpread > 0.15);
    CHECK(worstRelative < 0.005);
    CHECK(worstAbsolute < 0.20);
    CHECK(ctx->errorCount() == 0);
}

// ---- the space audit, part two: the shoreline's own pixels ---------------------------------------
//
// The same claim on the geometry the symptom was reported against, and against a source of truth
// that is not the renderer at all: the world field the terrain was generated from. A pixel the water
// covers is reconstructed out of the depth target -- which holds the *bed*, because a blended
// surface is not in the depth prepass -- and the point that comes back has to lie on the terrain the
// CPU can evaluate analytically at that xz.
//
// This is also the plan's capture item: water depth, terrain depth, linear depth, surface height,
// terrain position and camera depth, read at the same shoreline pixels, each in a stated space.
TEST_CASE("a shoreline pixel reconstructs onto the terrain field the world was generated from",
          "[gpu][renderer][forensics][water6_2]") {
    if (!fs::is_regular_file(Shore::scenePath())) {
        SKIP("the water QA scene is not present");
    }
    Shore shore = Shore::make();

    const gpu::ImageF wet = shore.render();
    const rendering::RendererDiagnosticFrame frame = shore.renderer->diagnosticFrame();
    const std::vector<float> depth = readLinearDepth(*shore.ctx, shore.renderer->linearDepthTexture());
    const gpu::ImageF dry = shore.renderDry();
    const std::vector<float> contribution = waterContribution(wet, dry);

    const app::PickView view = pickViewFor(frame);
    const world::TerrainQuery query = shore.query();
    REQUIRE(query.map != nullptr);

    // Water has to be on screen at all, or everything below is a statement about an empty set.
    const std::size_t covered = countCovered(contribution);
    INFO(covered << " water pixels of " << kW * kH);
    REQUIRE(covered > 500);

    // Only the near half of the frame. The terrain node authors three LOD levels over 40 m chunks,
    // so a distant chunk is a decimated mesh and deviates from the analytic field by design -- 2.2 m
    // at 100 m on this scene, measured. That is a level-of-detail property, not a depth-space one,
    // and a tolerance loose enough to accommodate it would no longer be testing anything.
    constexpr float kNearMetres = 50.0f;
    double worstError = 0.0;
    double sumError = 0.0;
    std::size_t checked = 0;
    std::size_t reprojectionFailures = 0;
    std::string capture;
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t p = static_cast<std::size_t>(y) * kW + x;
            if (contribution[p] <= kCoveredEpsilon) {
                continue;
            }
            const float linear = depth[p];
            if (linear >= app::kPickFarDistance || linear > kNearMetres) {
                continue;
            }
            const glm::vec3 bed = app::worldPositionAt(view, {x, y}, linear);
            const glm::vec2 xz(bed.x, bed.z);
            const float terrainHeight = query.heightAt(xz);
            const double error = std::fabs(static_cast<double>(bed.y) - terrainHeight);
            worstError = std::max(worstError, error);
            sumError += error;
            ++checked;

            // The reconstruction has to be self-consistent as well as correct: the point it returns
            // must sit at the depth it was reconstructed from, on the forward axis. A half-texel or
            // a flipped Y would slide the point along the ray and reach the terrain comparison as
            // noise rather than as the sign error it is.
            const float back = glm::dot(bed - view.cameraPosition, view.cameraForward);
            if (std::fabs(back - linear) > 1e-2f) {
                ++reprojectionFailures;
            }

            if (capture.size() < 800 && (x % 37) == 0 && (y % 23) == 0) {
                capture += fmt::format(
                    "px({},{}) linearDepth={:.3f} cameraDepth={:.3f} bed=({:.2f},{:.2f},{:.2f}) "
                    "terrainY={:.2f} waterSurfaceY={:.2f} waterDepth={:.3f} waterPixel={:.4f}\n",
                    x, y, linear, back, bed.x, bed.y, bed.z, terrainHeight,
                    query.map->waterSurface(xz), query.waterDepthAt(xz), contribution[p]);
            }
        }
    }
    INFO("shoreline capture; metres throughout, world space except the two depths, which are along "
         "the camera's forward axis:\n"
         << capture);
    INFO(checked << " water pixels within " << kNearMetres << " m; mean |bed.y - heightAt| "
                 << (checked > 0 ? sumError / static_cast<double>(checked) : 0.0) << " m, worst "
                 << worstError << " m; " << reprojectionFailures << " failed to reproject");
    REQUIRE(checked > 200);
    CHECK(reprojectionFailures == 0);
    // Half a metre against a 1.25 m grid spacing and a river cut into it: this catches an axis, an
    // origin or a unit, and does not pretend to catch a centimetre.
    CHECK(worstError < 0.5);
    CHECK(sumError / static_cast<double>(checked) < 0.15);
    CHECK(shore.ctx->errorCount() == 0);
}

// ---- the space audit, part three: the water's own comparison --------------------------------------
//
// `thickness = bed - viewDepth` is the one place in the frame where the depth target and a shader's
// own idea of depth meet, and it is the comparison a space mismatch would corrupt. It cannot be read
// out directly, but it has a signature: if either side were measured to the eye rather than along
// the forward axis, the gap between them would grow with the angle off the optical axis, and the
// same water would report more thickness -- so more opacity -- the further it sat from the middle of
// the frame.
//
// So this renders the same water twice from the same eye, turning the camera so a chosen patch lands
// first near the centre and then out toward a corner. Turning about the eye leaves every world-space
// input identical: the same surface point, the same view ray, the same normal, the same fresnel.
// Only the pixel changes. How much of the bed survives the water is measured with the bed A/B, which
// is a far larger signal than the water's own colour.
TEST_CASE("water thickness does not depend on where in the frame the water landed",
          "[gpu][renderer][forensics][water6_2]") {
    if (!fs::is_regular_file(Shore::scenePath())) {
        SKIP("the water QA scene is not present");
    }
    Shore shore = Shore::make();
    const glm::vec3 eye(40.0f, 26.0f, -86.0f);
    const glm::vec3 centreTarget(12.0f, 0.0f, -112.0f);

    // Where the water is, found from the frame rather than guessed: the surface over the bed under
    // the pixels the water owns when it is in the middle of the frame.
    shore.aim(eye, centreTarget);
    shore.hold(2.0);
    const gpu::ImageF centred = shore.render();
    const rendering::RendererDiagnosticFrame centreFrame = shore.renderer->diagnosticFrame();
    const std::vector<float> centreDepth = readLinearDepth(*shore.ctx, shore.renderer->linearDepthTexture());
    const gpu::ImageF centredDry = shore.renderDry();
    const std::vector<float> centreContribution = waterContribution(centred, centredDry);
    REQUIRE(countCovered(centreContribution) > 500);

    const app::PickView centreView = pickViewFor(centreFrame);
    const world::TerrainQuery query = shore.query();
    REQUIRE(query.map != nullptr);

    std::vector<glm::vec3> patch;
    for (std::uint32_t y = 4; y < kH - 4 && patch.size() < 48; y += 3) {
        for (std::uint32_t x = 4; x < kW - 4 && patch.size() < 48; x += 3) {
            const std::size_t p = static_cast<std::size_t>(y) * kW + x;
            if (centreContribution[p] <= kCoveredEpsilon || centreDepth[p] > 60.0f) {
                continue;
            }
            const glm::vec3 bed = app::worldPositionAt(centreView, {x, y}, centreDepth[p]);
            const glm::vec2 xz(bed.x, bed.z);
            // Water this test can say something about: deep enough that the shore fade is not what
            // is setting its opacity, and over ground the world agrees is submerged.
            if (query.waterDepthAt(xz) < 0.5f) {
                continue;
            }
            patch.emplace_back(bed.x, query.map->waterSurface(xz), bed.z);
        }
    }
    INFO(patch.size() << " water surface points chosen from the centred frame");
    REQUIRE(patch.size() >= 10);

    const gpu::ImageF centredLit = shore.renderLitBed();

    // The same eye, turned. The target swings so the patch moves toward a corner; the eye does not
    // move, so nothing about the water's own shading can change.
    const glm::vec3 axis = centreTarget - eye;
    const glm::vec3 corneredTarget =
        eye + glm::normalize(glm::normalize(axis) + glm::vec3(-0.34f, 0.16f, 0.26f)) * glm::length(axis);
    shore.aim(eye, corneredTarget);
    shore.hold(2.0);
    const gpu::ImageF cornered = shore.render();
    const rendering::RendererDiagnosticFrame cornerFrame = shore.renderer->diagnosticFrame();
    const gpu::ImageF corneredLit = shore.renderLitBed();

    // How far off the optical axis a sample sits, as a fraction of the half-frame. The two candidate
    // spaces diverge with this number, so a comparison between two samples at the same offset would
    // prove nothing and is not made.
    const auto offAxis = [](glm::uvec2 px) {
        const double dx = (static_cast<double>(px.x) + 0.5) / kW * 2.0 - 1.0;
        const double dy = (static_cast<double>(px.y) + 0.5) / kH * 2.0 - 1.0;
        return std::sqrt(dx * dx + dy * dy);
    };

    double worstRatio = 1.0;
    double centreOffsetSum = 0.0;
    double cornerOffsetSum = 0.0;
    std::size_t compared = 0;
    for (const glm::vec3& point : patch) {
        const auto centrePixel = projectToPixel(centreFrame.viewProjection, point);
        const auto cornerPixel = projectToPixel(cornerFrame.viewProjection, point);
        if (!centrePixel || !cornerPixel) {
            continue;
        }
        const double bedThroughCentre = channelDifference(centredLit.pixel(centrePixel->x, centrePixel->y),
                                                          centred.pixel(centrePixel->x, centrePixel->y));
        const double bedThroughCorner = channelDifference(corneredLit.pixel(cornerPixel->x, cornerPixel->y),
                                                          cornered.pixel(cornerPixel->x, cornerPixel->y));
        if (bedThroughCentre < 0.05 && bedThroughCorner < 0.05) {
            continue; // no bed reaches the eye through this point either way; nothing to compare
        }
        centreOffsetSum += offAxis(*centrePixel);
        cornerOffsetSum += offAxis(*cornerPixel);
        worstRatio = std::max(worstRatio, std::max(bedThroughCentre, bedThroughCorner) /
                                              std::max(std::min(bedThroughCentre, bedThroughCorner), 1e-6));
        ++compared;
    }
    INFO(compared << " points compared; mean off-axis offset "
                  << (compared ? centreOffsetSum / static_cast<double>(compared) : 0.0) << " centred against "
                  << (compared ? cornerOffsetSum / static_cast<double>(compared) : 0.0)
                  << " cornered; worst bed-through-water ratio " << worstRatio);
    REQUIRE(compared >= 8);
    // The samples really did move off-axis, or this is a comparison between two views of the middle
    // of the frame and a mismatch would be invisible in both of them.
    REQUIRE(cornerOffsetSum / static_cast<double>(compared) >
            centreOffsetSum / static_cast<double>(compared) + 0.2);
    CHECK(worstRatio < 2.0);
    CHECK(shore.ctx->errorCount() == 0);
}

// ---- the bed through the water ------------------------------------------------------------------
//
// The property the whole ADR-099 design exists for: opacity comes from how much water the ray
// crosses, so the same sheet is nearly clear at the bank and solid in the channel with nothing
// authored per pixel to make it so. If the shader ignored the scene's depth -- or read it from the
// wrong place -- the surface would be a flat wash, and this is the measurement that would say so.
//
// Looking down at the river rather than along it, because this is the one test that classifies a
// pixel by the depth of the water at the *bed's* xz: at a grazing angle the bed under a pixel and
// the surface over that pixel are metres apart and the classification stops meaning anything.
TEST_CASE("the bed shows through shallow water and not through deep water",
          "[gpu][renderer][forensics][water6_2]") {
    if (!fs::is_regular_file(Shore::scenePath())) {
        SKIP("the water QA scene is not present");
    }
    Shore shore = Shore::make();
    shore.aim(glm::vec3(20.0f, 34.0f, -96.0f), glm::vec3(14.0f, 0.0f, -110.0f));
    shore.hold(2.0);

    const gpu::ImageF wet = shore.render();
    const rendering::RendererDiagnosticFrame frame = shore.renderer->diagnosticFrame();
    const std::vector<float> depth = readLinearDepth(*shore.ctx, shore.renderer->linearDepthTexture());
    const gpu::ImageF dry = shore.renderDry();
    const std::vector<float> contribution = waterContribution(wet, dry);
    REQUIRE(countCovered(contribution) > 500);
    const gpu::ImageF litBed = shore.renderLitBed();

    const app::PickView view = pickViewFor(frame);
    const world::TerrainQuery query = shore.query();
    REQUIRE(query.map != nullptr);

    // Bands of CPU water depth, all of them past this scene's 0.8 m `edgeFade`, so the shore fade is
    // saturated across every one of them and the only thing that can separate them is the thickness
    // the ray crosses. Below the fade the two effects are confounded -- and a control that replaced
    // the Beer-Lambert term with a constant still passed a version of this test that used a 0.3 m
    // band, because the fade alone reproduced the gradient.
    constexpr std::array<float, 5> kEdges{0.9f, 1.5f, 2.5f, 4.0f, 1.0e9f};
    std::array<double, 4> bandSum{};
    std::array<std::size_t, 4> bandCount{};
    for (std::uint32_t y = 0; y < kH; ++y) {
        for (std::uint32_t x = 0; x < kW; ++x) {
            const std::size_t p = static_cast<std::size_t>(y) * kW + x;
            if (contribution[p] <= kCoveredEpsilon || depth[p] >= app::kPickFarDistance) {
                continue;
            }
            const glm::vec3 bed = app::worldPositionAt(view, {x, y}, depth[p]);
            const float waterDepth = query.waterDepthAt(glm::vec2(bed.x, bed.z));
            for (std::size_t band = 0; band + 1 < kEdges.size(); ++band) {
                if (waterDepth >= kEdges[band] && waterDepth < kEdges[band + 1]) {
                    bandSum[band] += channelDifference(litBed.pixel(x, y), wet.pixel(x, y));
                    ++bandCount[band];
                    break;
                }
            }
        }
    }
    std::array<double, 4> bandMean{};
    std::string profile;
    for (std::size_t band = 0; band < bandMean.size(); ++band) {
        bandMean[band] = bandCount[band] > 0 ? bandSum[band] / static_cast<double>(bandCount[band]) : 0.0;
        profile += band + 2 < kEdges.size()
                       ? fmt::format("{:.1f}-{:.1f} m: {} px passing {:.3f} of the bed\n", kEdges[band],
                                     kEdges[band + 1], bandCount[band], bandMean[band])
                       : fmt::format("{:.1f} m and deeper: {} px passing {:.3f} of the bed\n",
                                     kEdges[band], bandCount[band], bandMean[band]);
    }
    INFO("bed through water, by the depth of the water over it:\n" << profile);
    for (std::size_t band = 0; band < bandMean.size(); ++band) {
        REQUIRE(bandCount[band] > 30);
    }
    // The bed reaches the eye through a metre of water...
    CHECK(bandMean[0] > 0.3);
    // ...and less of it through each deeper band. The deepest two are not separated from each
    // other: past about three metres the transmission has already fallen to nothing and what is
    // left between them is the surface's own shading, not the bed.
    CHECK(bandMean[1] < bandMean[0]);
    CHECK(bandMean[2] < bandMean[1]);
    // The channel is a volume rather than a tint on one.
    CHECK(bandMean[3] < bandMean[0] * 0.4);
    CHECK(shore.ctx->errorCount() == 0);
}

// ---- a still shoreline --------------------------------------------------------------------------
//
// A waterline that crawls or strobes while nothing is moving is the artefact this item names, and
// the way it gets in is a quantity keyed to the frame counter rather than to the timeline. The water
// shader's flow clock is `time.renderTime` for exactly that reason -- an offline render has to land
// on the same water as the live one -- so this holds the timeline still, advances the frame index,
// and asserts the water's contribution to every pixel is unchanged.
//
// Then it advances the timeline by one frame and requires the picture to move, because a test that
// only shows a still image is equally satisfied by water that is not being drawn.
TEST_CASE("the shoreline holds still while the timeline does", "[gpu][renderer][forensics][water6_2]") {
    if (!fs::is_regular_file(Shore::scenePath())) {
        SKIP("the water QA scene is not present");
    }
    Shore shore = Shore::make();

    const auto contributionAt = [&](double seconds, std::uint64_t frameIndex) {
        shore.time.renderTime = seconds;
        shore.time.deltaTime = 1.0 / 60.0;
        shore.time.frameIndex = frameIndex;
        shore.engine->update(shore.time);
        const gpu::ImageF wet = shore.render();
        const gpu::ImageF dry = shore.renderDry();
        return waterContribution(wet, dry);
    };

    const std::vector<float> reference = contributionAt(3.0, 0);
    const std::size_t covered = countCovered(reference);
    INFO(covered << " water pixels");
    REQUIRE(covered > 500);

    std::size_t movedPixels = 0;
    double worstDrift = 0.0;
    for (std::uint64_t frameIndex = 1; frameIndex <= 12; ++frameIndex) {
        const std::vector<float> held = contributionAt(3.0, frameIndex);
        for (std::size_t p = 0; p < reference.size(); ++p) {
            worstDrift = std::max(worstDrift, std::fabs(static_cast<double>(held[p]) - reference[p]));
            movedPixels += (reference[p] > kCoveredEpsilon) != (held[p] > kCoveredEpsilon) ? 1 : 0;
        }
    }
    INFO("twelve frames at a held timeline: " << movedPixels << " pixels changed hands, worst drift "
                                              << worstDrift);
    CHECK(movedPixels == 0);
    CHECK(worstDrift == 0.0);

    // ...and the same measurement one frame of timeline later does move: the ripples travel and the
    // foam breaks up against them, so the waterline is not where it was.
    const std::vector<float> later = contributionAt(3.0 + 1.0 / 60.0, 13);
    std::size_t changed = 0;
    for (std::size_t p = 0; p < reference.size(); ++p) {
        changed += std::fabs(static_cast<double>(later[p]) - reference[p]) > 1e-4 ? 1 : 0;
    }
    INFO(changed << " pixels differ one frame of timeline later");
    REQUIRE(changed > covered / 20);
    CHECK(shore.ctx->errorCount() == 0);
}

// ---- the surface and its bed do not trade pixels --------------------------------------------------
//
// Z-fighting between a water surface and the bed under it shows as pixels flipping ownership when
// the camera moves a centimetre. Separating that from the waterline legitimately sweeping past is
// the whole difficulty, and the discriminator is *how many times* a pixel flips: the camera walks
// one way in equal steps, so an edge reaches a pixel once, and a pixel that changes hands twice
// changed for a reason that is not the edge.
//
// A first version of this test counted flips whose 5x5 neighbourhood was solid water, on the theory
// that a fight shows up away from the silhouette. It did not fail its control: coincident surfaces
// speckle, a speckled region has no solid neighbourhood anywhere in it, and every flip was
// classified as an edge. Counting transitions per pixel catches the same control eleven times over.
//
// Water is drawn with `depthCompare = Less` and no depth write, so the surface and the bed are held
// apart by real geometry and not by a bias. That is the property under test: a seam here would be a
// reason to look at the geometry, never a reason to add an offset.
TEST_CASE("the water surface and its bed keep their pixels under a small camera move",
          "[gpu][renderer][forensics][water6_2]") {
    if (!fs::is_regular_file(Shore::scenePath())) {
        SKIP("the water QA scene is not present");
    }
    Shore shore = Shore::make();
    const glm::vec3 eye(38.0f, 12.0f, -88.0f);
    const glm::vec3 target(14.0f, 0.0f, -110.0f);

    std::vector<std::vector<char>> masks;
    for (int step = 0; step < 8; ++step) {
        shore.aim(eye + glm::vec3(0.02f * static_cast<float>(step), 0.0f, 0.0f), target);
        shore.hold(2.0);
        const gpu::ImageF wet = shore.render();
        const gpu::ImageF dry = shore.renderDry();
        const std::vector<float> contribution = waterContribution(wet, dry);
        std::vector<char> mask(contribution.size());
        for (std::size_t p = 0; p < contribution.size(); ++p) {
            mask[p] = contribution[p] > kCoveredEpsilon ? 1 : 0;
        }
        REQUIRE(std::count(mask.begin(), mask.end(), 1) > 500);
        masks.push_back(std::move(mask));
    }

    // The camera sweeps one way, in equal steps, so a pixel near the waterline crosses the edge
    // once and never comes back: over fourteen centimetres at twenty to forty metres the image
    // shifts by less than a pixel, and a pixel cannot cross two different shorelines in that. A
    // pixel that changes hands more than once is therefore not the edge arriving -- it is the
    // surface and the bed taking turns, which is what z-fighting looks like from outside.
    std::size_t strobingPixels = 0;
    std::size_t singleFlips = 0;
    std::size_t worstTransitions = 0;
    for (std::size_t p = 0; p < masks.front().size(); ++p) {
        std::size_t transitions = 0;
        for (std::size_t step = 1; step < masks.size(); ++step) {
            transitions += masks[step][p] != masks[step - 1][p] ? 1 : 0;
        }
        worstTransitions = std::max(worstTransitions, transitions);
        if (transitions > 1) {
            ++strobingPixels;
        } else if (transitions == 1) {
            ++singleFlips;
        }
    }
    INFO(strobingPixels << " pixels changed hands more than once over seven 2 cm steps, " << singleFlips
                        << " changed once, worst " << worstTransitions << " changes at one pixel");
    // The waterline did move, or this is seven renders of the same picture and nothing about
    // ownership was put to the test.
    REQUIRE(singleFlips > 10);
    CHECK(strobingPixels == 0);
    CHECK(shore.ctx->errorCount() == 0);
}

// ---- one second, two ways -------------------------------------------------------------------------
//
// The shoreline is a function of the timeline second and nothing else: the flow clock is
// `time.renderTime`, the mesh is generated once, and the depth it is compared against belongs to
// terrain that does not animate. So a second arrived at by a seek and the same second arrived at
// after visiting two others have to produce the same waterline, to the bit.
//
// `Engine::seekSeconds` rather than repeated updates, because a jump integrated through
// `Composition::update` is playback across the gap and not a seek, and a forensic test that skips
// the seek is testing an API nobody uses.
TEST_CASE("the same second reached two ways gives the same shoreline",
          "[gpu][renderer][forensics][water6_2]") {
    if (!fs::is_regular_file(Shore::scenePath())) {
        SKIP("the water QA scene is not present");
    }
    Shore shore = Shore::make();

    const auto shorelineAt = [](Shore& s, std::initializer_list<double> route) {
        for (const double seconds : route) {
            s.engine->seekSeconds(seconds);
            FixedStepClock clock(60.0);
            clock.restartAt(seconds);
            s.time = s.engine->tick(clock);
            s.engine->setViewport(kW, kH);
            s.engine->update(s.time);
            s.renderer->resetTemporalHistory(); // a seek is a cut, not motion
        }
        const gpu::ImageF wet = s.render();
        const gpu::ImageF dry = s.renderDry();
        return waterContribution(wet, dry);
    };

    const std::vector<float> direct = shorelineAt(shore, {4.0});
    const std::size_t covered = countCovered(direct);
    INFO(covered << " water pixels");
    REQUIRE(covered > 500);

    const std::vector<float> viaElsewhere = shorelineAt(shore, {1.5, 9.25, 4.0});
    CHECK(viaElsewhere == direct);

    // A fresh engine and a fresh renderer that have been nowhere, which is what says the two routes
    // agree on the *right* answer rather than on a shared piece of stale state.
    Shore fresh = Shore::make();
    CHECK(shorelineAt(fresh, {4.0}) == direct);

    // And the measurement is live: a different second is a different waterline, so the equalities
    // above are not two empty masks agreeing with each other.
    const std::vector<float> otherSecond = shorelineAt(shore, {4.5});
    std::size_t changed = 0;
    for (std::size_t p = 0; p < direct.size(); ++p) {
        changed += std::fabs(static_cast<double>(otherSecond[p]) - direct[p]) > 1e-4 ? 1 : 0;
    }
    INFO(changed << " pixels differ half a second later");
    REQUIRE(changed > covered / 20);
    CHECK(shore.ctx->errorCount() == 0);
}

// ---- overlapping surfaces, and the transform every chunk shares -----------------------------------
//
// Water chunks tile a plane and mostly do not overlap on screen, which is why a sort error here
// hides: it needs a grazing view across two of them, and then it is a seam that appears from one
// place and nowhere else. The renderer sorts the water list by the *chunk's own* view depth --
// `makeItem`'s depth is the node origin's and is therefore the same number for every chunk of a
// terrain, so it sorts nothing -- and draws far first.
//
// Two claims, each failing differently. On a synthetic pair of sheets whose colours are nothing
// alike, the *near* one must dominate the composite, and raising the other one above it must swap
// which that is. On the real shoreline, at the grazing camera the scene file authors, the order the
// chunks happen to sit in the entity list must not reach the picture.
TEST_CASE("overlapping water surfaces composite by view depth and not by list order",
          "[gpu][renderer][forensics][water6_2]") {
    if (!fs::is_regular_file(Shore::scenePath())) {
        SKIP("the water QA scene is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});

    // ---- the synthetic pair ----
    {
        rendering::SceneRenderer renderer(*ctx, shaders);
        REQUIRE(renderer.init().has_value());
        renderer.setPassToggles(quantitativeToggles());

        const auto build = [](float redHeight, float blueHeight) {
            scene::Scene s;
            s.environment.backgroundColor = {0.0f, 0.0f, 0.0f};
            s.camera.position = {0.0f, 7.0f, 26.0f};
            s.camera.target = {0.0f, 0.0f, 0.0f};
            const auto bedMesh = s.addMesh(quad(60.0f, -6.0f));
            scene::Entity& bed = s.addEntity("bed", bedMesh);
            bed.material.baseColor = {0.02f, 0.02f, 0.02f};
            const std::array<std::tuple<const char*, float, glm::vec3>, 2> sheets{
                std::tuple{"red", redHeight, glm::vec3(1.0f, 0.0f, 0.0f)},
                std::tuple{"blue", blueHeight, glm::vec3(0.0f, 0.0f, 1.0f)}};
            for (const auto& [name, height, colour] : sheets) {
                scene::WaterSettings settings;
                settings.shallowColor = colour;
                settings.deepColor = colour;
                settings.emissiveColor = colour;
                // Emissive and nearly opaque, so what the composite shows is which sheet was applied
                // last rather than how two dim colours happened to add up.
                settings.emissiveIntensity = 3.0f;
                settings.clarity = 0.05f;
                settings.maxOpacity = 0.98f;
                settings.edgeFade = 0.01f;
                settings.foam = 0.0f;
                settings.ripple = 0.0f;
                settings.fresnel = 0.0f;
                settings.reflection = 0.0f;
                settings.specular = 0.0f;
                settings.refraction = 0.0f;
                s.waters.push_back({std::string(name), settings, 0.0f});
                // uv.x is the vertical bed depth the shore fade and the thickness cap read; five
                // metres of it puts these sheets clear of both.
                scene::MeshData sheet = quad(30.0f, height);
                for (scene::Vertex& v : sheet.vertices) {
                    v.uv = {5.0f, 1.0f};
                }
                scene::Entity& e = s.addEntity(std::string(name), s.addMesh(std::move(sheet)));
                e.style = scene::MeshStyle::Water;
                e.material.program = std::string(name);
                e.material.doubleSided = true;
            }
            return s;
        };

        const auto composite = [&](scene::Scene& s) {
            renderer.resetTemporalHistory();
            auto image = renderer.renderToImageFloat(s, FrameTime{}, kW, kH);
            REQUIRE(image.has_value());
            double red = 0.0;
            double blue = 0.0;
            for (std::uint32_t y = 0; y < kH; ++y) {
                for (std::uint32_t x = 0; x < kW; ++x) {
                    red += image->pixel(x, y)[0];
                    blue += image->pixel(x, y)[2];
                }
            }
            return std::pair{red, blue};
        };

        scene::Scene redOnTop = build(1.0f, 0.0f);
        scene::Scene blueOnTop = build(0.0f, 1.0f);
        const auto [redHighRed, redHighBlue] = composite(redOnTop);
        const auto [blueHighRed, blueHighBlue] = composite(blueOnTop);
        INFO("red sheet above: red " << redHighRed << " blue " << redHighBlue << "; blue sheet above: red "
                                     << blueHighRed << " blue " << blueHighBlue);
        // Both sheets draw either way, or "the nearer one dominates" is a statement about one
        // surface and an empty screen.
        REQUIRE(redHighRed > 0.0);
        REQUIRE(redHighBlue > 0.0);
        REQUIRE(blueHighRed > 0.0);
        REQUIRE(blueHighBlue > 0.0);
        // The near surface is applied last and wins the composite, and which surface that is comes
        // from the geometry rather than from the order the two entities were added. These two lines
        // are each other's control: `red` is added to the scene first in both arrangements, so a
        // renderer compositing in list order would put blue last in both and could not satisfy both.
        CHECK(redHighRed > redHighBlue * 1.5);
        CHECK(blueHighBlue > blueHighRed * 1.5);

        // The list order carries nothing of its own. The two entities are swapped in place, so the
        // near/far relationship is untouched and only the order the renderer receives them changes.
        scene::Scene swapped = build(1.0f, 0.0f);
        std::swap(swapped.entities[1], swapped.entities[2]);
        const auto [swappedRed, swappedBlue] = composite(swapped);
        INFO("after swapping the two water entities: red " << swappedRed << " blue " << swappedBlue);
        CHECK(swappedRed == redHighRed);
        CHECK(swappedBlue == redHighBlue);
    }

    // ---- the real chunks ----
    Shore shore = Shore::make();

    std::vector<std::size_t> waterEntities;
    std::vector<std::size_t> groundEntities;
    for (std::size_t i = 0; i < shore.scene().entities.size(); ++i) {
        (shore.scene().entities[i].style == scene::MeshStyle::Water ? waterEntities : groundEntities)
            .push_back(i);
    }
    INFO(waterEntities.size() << " water chunks and " << groundEntities.size() << " ground chunks");
    REQUIRE(waterEntities.size() > 4);

    // Every chunk of a terrain node is flattened from the same node, so the transform is one
    // transform. A water chunk that had picked up a different one would put its sheet where the bank
    // is not, and the chunk-to-world mapping is what says a sheet is over its own bed.
    const glm::mat4 nodeTransform = shore.scene().entities[groundEntities.front()].transform.matrix();
    for (const scene::Entity& e : shore.scene().entities) {
        INFO("entity '" << e.name << "'");
        REQUIRE(e.transform.matrix() == nodeTransform);
    }

    // ...and each water chunk covers the ground chunk it was named for. The names carry the chunk
    // index, which is the one place the two halves of a chunk can be matched up from outside. The
    // test is not "within some radius" but "nearer than every other ground chunk": a radius is
    // satisfied by any sheet roughly in the right district, and chunks are only forty metres apart.
    const auto centreOf = [&](std::size_t entity) {
        const auto& [lo, hi] = shore.scene().meshBounds(shore.scene().entities[entity].mesh);
        return glm::vec2((lo.x + hi.x) * 0.5f, (lo.z + hi.z) * 0.5f);
    };
    std::size_t pairsChecked = 0;
    for (const std::size_t w : waterEntities) {
        const std::string& name = shore.scene().entities[w].name;
        const std::string suffix = name.substr(name.rfind("water") + 5);
        std::size_t named = shore.scene().entities.size();
        std::size_t nearest = shore.scene().entities.size();
        float nearestDistance = std::numeric_limits<float>::max();
        for (const std::size_t g : groundEntities) {
            const std::string& groundName = shore.scene().entities[g].name;
            if (groundName.substr(groundName.rfind("chunk") + 5) == suffix) {
                named = g;
            }
            const float distance = glm::length(centreOf(w) - centreOf(g));
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = g;
            }
        }
        INFO("'" << name << "' names chunk " << suffix << "; the nearest ground chunk is '"
                 << shore.scene().entities[nearest].name << "' at " << nearestDistance << " m");
        REQUIRE(named < shore.scene().entities.size());
        CHECK(nearest == named);
        ++pairsChecked;
    }
    INFO(pairsChecked << " water chunks matched to their ground chunk");
    CHECK(pairsChecked == waterEntities.size());

    // The shuffle. The authored camera is the grazing view from the bank, which is the case where
    // chunks genuinely overlap on screen, so a list-order dependence would show here.
    const gpu::ImageF ordered = shore.render();
    std::vector<scene::Entity> reversed;
    reversed.reserve(waterEntities.size());
    for (const std::size_t i : waterEntities) {
        reversed.push_back(shore.scene().entities[i]);
    }
    std::reverse(reversed.begin(), reversed.end());
    for (std::size_t k = 0; k < waterEntities.size(); ++k) {
        shore.scene().entities[waterEntities[k]] = reversed[k];
    }
    shore.renderer->resetTemporalHistory();
    const gpu::ImageF reordered = shore.render();
    CHECK(gpu::hashImage(reordered) == gpu::hashImage(ordered));
    CHECK(shore.ctx->errorCount() == 0);
}
