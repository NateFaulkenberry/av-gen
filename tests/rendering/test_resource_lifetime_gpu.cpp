// Renderer forensics Phase 3.4 (frame synchronisation and resource lifetime) and the two open
// items of Phase 3.2 (camera-relative rendering).
//
// The existing lifetime regressions each move one axis: eight target sizes, a scene swap, a
// repeated frame index, twelve post-chain configurations. Each of them proves that *that* axis is
// clean. None of them proves the axes are clean together, and a stale slot or a target that was
// released while a bind group still named it is exactly the kind of fault that needs two things to
// happen in the wrong order. So the stress here interleaves them -- resize, reload, scene swap,
// seek forwards, seek backwards, camera cut, frame-index reuse -- and checks the result against a
// fresh engine and a fresh renderer at checkpoints, rather than only asking whether Dawn complained.
//
// `ctx->errorCount()` is checked everywhere and is not decoration: Dawn's uncaptured-error callback
// is where a destroyed-but-still-referenced texture, a bind group whose layout no longer matches and
// a buffer written past its size all surface. It stays at zero for the whole sequence, which is the
// "validation for resources destroyed, replaced, resized or rebound while still referenced" the plan
// asks for, asserted from outside the renderer.
//
// **Two findings, and both came from crossing the axes rather than from any one of them.**
//
// One. A timeline seek restores every renderer cache except the particle pools. Measured here: on
// the mixed QA scene a renderer that has played forty frames and then seeks back to 0.5 s produces
// a different frame from a fresh renderer taken straight there, and forcing the pools to reset --
// the only way the renderer offers, by handing it a different `scene::Scene` object -- makes the two
// agree exactly. With the particle isolation arm off they agree without any of that, so the pools
// are the whole of the difference: `SceneRenderer::resetTemporalHistory()` does not call
// `ParticleRenderer::resetAll()`, and `resetAll` -- whose own comment says it is "used on
// seek/offline restarts" -- has no caller but the scene-pointer change inside `update()`.
// "a seek restores every renderer cache except the particle pools" below pins both halves.
//
// Two, and it only surfaced because of the first. A checkpoint is blind to the renderer's memory of
// the previous frame unless something in the frame depends on it, so these tests turn motion blur
// on -- at which point the scene with a rig stopped reproducing, at every second of a thirteen-point
// sweep. The cause is not the renderer: `SkinnedRig::previousPalette` is scene state that a seek
// does not reseed, by up to 71 model units on this asset, so the first frame after every scrub
// carries joint motion vectors for a jump nobody made and motion blur draws them. "a seek reseeds
// the current skinning palette but not the previous one" measures that on its own.
//
// Two traps this file is written around, both of which have produced tests that could not fail
// elsewhere in this investigation:
//
//   1. `FixedStepClock::restartAt` every frame reports a frame delta of zero, so anything that
//      integrates does nothing and an A/B comes out equal because neither arm ran. There is one
//      clock per session here, ticked; `seekTo` is the only thing that restarts it, because a seek
//      is a discontinuity and its delta genuinely is zero.
//   2. Writing `scene().camera` does nothing -- a composition re-derives it from `camera/mode`,
//      `camera/position` and `camera/target` on every update -- and `setBase` alone never reaches
//      `applyParameters`, which reads the final value. `cutCamera` drives the parameters, calls
//      `resetFinals`, and REQUIREs the camera took the pose. The Phase 3.2 tests instead drive a
//      *detached copy* of the scene, which no composition update can overwrite, and REQUIRE the
//      renderer's own diagnostic frame reports the pose they asked for.

#include "app/engine.hpp"
#include "assets/asset_registry.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "rendering/scene_renderer.hpp"
#include "scene/composition.hpp"
#include "scene/scene.hpp"

#include <fmt/format.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

constexpr double kFps = 60.0;

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

fs::path qaScene(const char* stem) {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / stem;
}

bool haveAlien() {
    return fs::is_regular_file(fs::path(AVGEN_SOURCE_DIR) / "assets" / "imported" / "alien.gltf");
}

int luma(const std::uint8_t* px) { return px[0] + px[1] + px[2]; }

// "The frame has something in it." Used only where the camera is at its authored pose, because a
// camera cut is allowed to point at nothing and a test that forbade that would be testing the cut.
bool hasContrast(const gpu::Image8& image) {
    int lo = 3 * 255;
    int hi = 0;
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const int l = luma(image.pixel(x, y));
            lo = std::min(lo, l);
            hi = std::max(hi, l);
        }
    }
    return hi - lo > 30;
}

std::size_t channelsDiffering(const gpu::Image8& a, const gpu::Image8& b) {
    if (a.width != b.width || a.height != b.height || a.rgba.size() != b.rgba.size()) {
        return a.rgba.size() + b.rgba.size();
    }
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.rgba.size(); ++i) {
        n += a.rgba[i] == b.rgba[i] ? 0 : 1;
    }
    return n;
}

// ---- one engine, one renderer, one clock -------------------------------------------------------
//
// Kept together so that no test can acquire a second clock and quietly lose its frame deltas, and
// so that a reload puts back everything a reload invalidates (the parameter set is rebuilt from the
// file, so the camera handles and the authored pose have to be re-read).
struct Session {
    gpu::Context& ctx;
    rendering::SceneRenderer renderer;
    app::Engine engine{app::EngineMode::Offline};
    FixedStepClock clock{kFps};
    fs::path file;
    glm::vec3 authoredEye{0.0f};
    glm::vec3 authoredAim{0.0f};
    // The frame time of the most recent tick, so a camera cut can be applied at the moment the
    // session is actually at rather than at an invented one.
    FrameTime now{};

    Session(gpu::Context& context, gpu::ShaderLibrary& shaders) : ctx(context), renderer(context, shaders) {
        REQUIRE(renderer.init().has_value());
    }

    void load(const fs::path& path) {
        file = path;
        REQUIRE(engine.loadComposition(path).has_value());
        const auto* eye = engine.params().findAs<glm::vec3>("camera/position");
        const auto* aim = engine.params().findAs<glm::vec3>("camera/target");
        REQUIRE(eye != nullptr);
        REQUIRE(aim != nullptr);
        authoredEye = eye->base();
        authoredAim = aim->base();
        // Motion blur on, because it is what makes a frame depend on the renderer's memory of the
        // previous one. Without it the QA scenes render the same picture whatever `prevViewProj` and
        // the previous model matrices hold, and a checkpoint comparing a walked renderer against a
        // fresh one is blind to the whole temporal path it is meant to be stressing. Set through the
        // parameter set, because a composition re-derives its post settings from there on every
        // update and a write to `scene().post` would be gone by the next frame.
        //
        // Off for a scene with a rig, and that is not tidiness: `SkinnedRig::previousPalette` is
        // scene state that a seek does not reseed, so the first frame after a jump carries joint
        // motion vectors for a jump that did not happen. "a seek reseeds the current skinning
        // palette but not the previous one" measures that on its own, with the magnitude; leaving
        // blur on here would fold a known animation defect into every resource-lifetime checkpoint
        // and make them report it over and over as though it were the renderer's.
        const bool skinned = !engine.scene().rigs.empty();
        auto* blur = engine.params().findAs<float>("post/motionBlur/amount");
        REQUIRE(blur != nullptr);
        blur->setBase(skinned ? 0.0f : 0.8f);
        engine.params().resetFinals();
        // A load is a discontinuity in everything the renderer remembers about the previous frame.
        renderer.resetTemporalHistory();
    }

    // One frame of ordinary playback at a given size. The clock is ticked, never restarted.
    gpu::Image8 play(std::uint32_t w, std::uint32_t h) {
        now = engine.tick(clock);
        return draw(now, w, h);
    }

    // A seek: the transport resynchronises and the render clock -- the authority in Offline mode --
    // is placed on the second asked for. The zero frame delta here is the truth, not the trap:
    // nothing is meant to integrate across a jump.
    FrameTime seekTo(double seconds) {
        engine.seekSeconds(seconds);
        clock.restartAt(seconds);
        now = engine.tick(clock);
        REQUIRE(now.renderTime == seconds);
        // What the application does, and the reason this belongs here rather than in each test: a
        // transport discontinuity bumps a revision that both render loops watch, and they drop the
        // renderer's temporal state when it changes. A harness that skipped this would be comparing
        // a seeked frame against a cold one and calling the motion vectors left over from wherever
        // the camera used to be a renderer bug.
        renderer.resetTemporalHistory();
        return now;
    }

    gpu::Image8 draw(const FrameTime& time, std::uint32_t w, std::uint32_t h) {
        engine.setViewport(w, h);
        engine.update(time);
        auto image = renderer.renderToImage(engine.scene(), time, w, h);
        REQUIRE(image.has_value());
        REQUIRE(image->width == w);
        REQUIRE(image->height == h);
        return std::move(*image);
    }

    // The only route to the camera that reaches the scene. `camera/mode` is forced to 1 (free) or
    // the orbit camera overwrites the position on the next update and the cut silently does nothing.
    void cutCamera(glm::vec3 eye, glm::vec3 aim, std::uint32_t w, std::uint32_t h) {
        auto* mode = engine.params().findAs<int>("camera/mode");
        auto* position = engine.params().findAs<glm::vec3>("camera/position");
        auto* target = engine.params().findAs<glm::vec3>("camera/target");
        REQUIRE(mode != nullptr);
        REQUIRE(position != nullptr);
        REQUIRE(target != nullptr);
        mode->setBase(1);
        position->setBase(eye);
        target->setBase(aim);
        engine.params().resetFinals(); // setBase alone never reaches applyParameters
        engine.setViewport(w, h);
        engine.update(now);
        REQUIRE(engine.scene().camera.position == eye);
        REQUIRE(engine.scene().camera.target == aim);
        // A cut is a discontinuity: the application's camera-cut action resets temporal state, and a
        // test that did not would be comparing a cut frame against a fresh one with motion vectors
        // left over from wherever the camera used to be.
        renderer.resetTemporalHistory();
    }

    void restoreAuthoredCamera(std::uint32_t w, std::uint32_t h) { cutCamera(authoredEye, authoredAim, w, h); }
};

// The reference: an engine and a renderer that have been nowhere else. Memoised, because building
// one means loading the composition and initialising a renderer, and the stress asks for the same
// checkpoint many times.
struct Reference {
    gpu::Context& ctx;
    gpu::ShaderLibrary& shaders;
    std::map<std::string, std::uint64_t> cache;

    std::uint64_t hashAt(const fs::path& file, double seconds, std::uint32_t w, std::uint32_t h) {
        const std::string key = fmt::format("{}|{}|{}x{}", file.string(), seconds, w, h);
        if (const auto it = cache.find(key); it != cache.end()) {
            return it->second;
        }
        Session fresh(ctx, shaders);
        fresh.load(file);
        const FrameTime time = fresh.seekTo(seconds);
        const gpu::Image8 image = fresh.draw(time, w, h);
        REQUIRE(hasContrast(image));
        const std::uint64_t hash = gpu::hashImage(image);
        cache.emplace(key, hash);
        return hash;
    }
};

// ---- Phase 3.2: a scene detached from its composition ------------------------------------------
//
// `Composition::scene()` is re-derived from the parameter set on every update, so a test that wants
// to put the camera somewhere and keep it there takes a copy and drives that. The copy is a value:
// meshes, materials and entities included, with no composition behind it to overwrite anything.
scene::Scene detachedScene(const fs::path& file, std::uint32_t w, std::uint32_t h) {
    assets::AssetRegistry registry(file.parent_path());
    auto loaded = scene::Composition::loadFile(file, registry);
    REQUIRE(loaded.has_value());
    scene::Composition& composition = **loaded;
    params::ParameterSet parameters;
    params::Modulator modulator;
    composition.attach(parameters, modulator);
    composition.setViewport(w, h);
    FrameTime time{};
    composition.update(time);
    return composition.scene();
}

// `static-object` is the orange one (base colour 0.85/0.3/0.15) against a near-black background, two
// grey-white spheres and a blue one, so red dominance picks it out and nothing else. This is only
// good enough to *track* one known object across frames; where a test needs to say "these pixels are
// that object" it measures the footprint instead (see `footprint` below), because a hue rule cannot
// tell a grey sphere lit by a blue environment from a blue sphere, and the first version of this
// file got exactly that wrong.
bool isOrange(const std::uint8_t* px) {
    return luma(px) > 60 && px[0] > px[1] + 20 && px[0] > px[2] + 20;
}

struct Blob {
    std::size_t count = 0;
    glm::vec2 centroid{0.0f};
    glm::vec2 lo{0.0f};
    glm::vec2 hi{0.0f};
};

// Where an object's pixels actually are, in image coordinates (top-left origin, y down), weighted by
// how bright they are so a dim rim does not drag the centre around.
Blob findBlob(const gpu::Image8& image, bool (*belongs)(const std::uint8_t*)) {
    Blob blob;
    blob.lo = glm::vec2(std::numeric_limits<float>::max());
    blob.hi = glm::vec2(std::numeric_limits<float>::lowest());
    double sx = 0.0;
    double sy = 0.0;
    double total = 0.0;
    for (std::uint32_t y = 0; y < image.height; ++y) {
        for (std::uint32_t x = 0; x < image.width; ++x) {
            const std::uint8_t* px = image.pixel(x, y);
            if (!belongs(px)) {
                continue;
            }
            const double weight = luma(px);
            sx += weight * (static_cast<double>(x) + 0.5);
            sy += weight * (static_cast<double>(y) + 0.5);
            total += weight;
            ++blob.count;
            const glm::vec2 here(static_cast<float>(x), static_cast<float>(y));
            blob.lo = glm::min(blob.lo, here);
            blob.hi = glm::max(blob.hi, here);
        }
    }
    if (total > 0.0) {
        blob.centroid = glm::vec2(static_cast<float>(sx / total), static_cast<float>(sy / total));
    }
    return blob;
}

// Where an object's pixels are, measured rather than inferred from its colour: the frame is drawn
// again with that one entity hidden, and the pixels that changed are the ones it was responsible
// for. An occluder's footprint is exactly the region that changes when it goes away, so this is
// independent of material, lighting and of which objects happen to share a hue.
Blob footprint(const gpu::Image8& with, const gpu::Image8& without, int threshold = 12) {
    Blob blob;
    blob.lo = glm::vec2(std::numeric_limits<float>::max());
    blob.hi = glm::vec2(std::numeric_limits<float>::lowest());
    REQUIRE(with.width == without.width);
    REQUIRE(with.height == without.height);
    double sx = 0.0;
    double sy = 0.0;
    for (std::uint32_t y = 0; y < with.height; ++y) {
        for (std::uint32_t x = 0; x < with.width; ++x) {
            const std::uint8_t* a = with.pixel(x, y);
            const std::uint8_t* b = without.pixel(x, y);
            int worst = 0;
            for (int c = 0; c < 3; ++c) {
                worst = std::max(worst, std::abs(int(a[c]) - int(b[c])));
            }
            if (worst < threshold) {
                continue;
            }
            // Unweighted: the claim is about the area the object covers, and weighting by how much
            // each pixel changed would pull the centre towards whatever is brightest behind it.
            sx += static_cast<double>(x) + 0.5;
            sy += static_cast<double>(y) + 0.5;
            ++blob.count;
            const glm::vec2 here(static_cast<float>(x), static_cast<float>(y));
            blob.lo = glm::min(blob.lo, here);
            blob.hi = glm::max(blob.hi, here);
        }
    }
    if (blob.count > 0) {
        blob.centroid = glm::vec2(static_cast<float>(sx / static_cast<double>(blob.count)),
                                  static_cast<float>(sy / static_cast<double>(blob.count)));
    }
    return blob;
}

// Normalised device coordinates to pixels, the mapping the rasteriser uses: x right, y *down* in the
// image, both halves of the viewport included.
glm::vec2 ndcToPixels(glm::vec3 ndc, std::uint32_t w, std::uint32_t h) {
    return {(ndc.x * 0.5f + 0.5f) * static_cast<float>(w), (0.5f - ndc.y * 0.5f) * static_cast<float>(h)};
}

} // namespace

// ---- Phase 3.2: the projected-position chain ---------------------------------------------------
//
// The plan asks for "projected-position checks for world, camera-relative, clip and screen
// coordinates". There is no camera-relative origin in this renderer -- the audit established that --
// so the camera-relative stage is not a separate matrix to read back but a claim to test: the view
// matrix must be exactly one rigid transform that subtracts the eye once. This walks the chain a
// stage at a time from the renderer's own diagnostic matrices, and finishes at the pixels, because
// every stage agreeing with the next is still consistent with the whole chain being applied to the
// wrong viewport.
TEST_CASE("the projected position chain agrees from world through clip to the pixels",
          "[gpu][renderer][forensics][lifetime3_4]") {
    const fs::path file = qaScene("renderer-qa-minimal.scene.json");
    if (!fs::is_regular_file(file)) {
        SKIP("the minimal QA scene is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    constexpr std::uint32_t kW = 400;
    constexpr std::uint32_t kH = 250;
    scene::Scene work = detachedScene(file, kW, kH);
    renderer.setDiagnosticEntity("static-object");

    // The authored pose, which frames the object well off the optical axis on purpose: an object in
    // the middle of the frame projects correctly under a view matrix with its x and y confused.
    FrameTime time{};
    time.renderTime = 0.5;
    time.frameIndex = 30;
    // Cold, because the footprint measurement below differences two renders and temporal history
    // left over from one of them would show up outside the object as difference that is not the
    // object.
    const auto coldShot = [&] {
        renderer.resetTemporalHistory();
        auto shot = renderer.renderToImage(work, time, kW, kH);
        REQUIRE(shot.has_value());
        return std::move(*shot);
    };
    const gpu::Image8 full = coldShot();

    const rendering::RendererDiagnosticFrame& frame = renderer.diagnosticFrame();
    const rendering::RenderObjectDiagnostic* object = renderer.diagnosticObject("static-object");
    REQUIRE(object != nullptr);
    REQUIRE(object->finite);
    REQUIRE(object->visible);
    REQUIRE_FALSE(object->cameraCulled);

    // 1. World. The diagnostic's world position is the translation column of its world matrix, and
    //    both are the authored (10, 2, -20) -- the transform has not been re-derived on the way.
    const glm::vec3 world = object->worldPosition;
    CHECK(world == glm::vec3(10.0f, 2.0f, -20.0f));
    CHECK(glm::vec3(object->worldMatrix[3]) == world);
    CHECK(object->worldMatrix[3][3] == 1.0f);

    // 2. Camera-relative. The view matrix must be a rotation about the eye and nothing else: its
    //    upper 3x3 orthonormal with determinant +1, and the eye landing exactly on the view-space
    //    origin. A second subtraction anywhere would put the eye at -R*eye instead of at zero, which
    //    is the arithmetic signature of the double-subtraction bug this phase is named after.
    const glm::mat3 basis(frame.view);
    const glm::mat3 shouldBeIdentity = glm::transpose(basis) * basis;
    for (int c = 0; c < 3; ++c) {
        for (int r = 0; r < 3; ++r) {
            CHECK(shouldBeIdentity[c][r] == Approx(c == r ? 1.0f : 0.0f).margin(1e-5));
        }
    }
    CHECK(glm::determinant(basis) == Approx(1.0f).margin(1e-5));
    const glm::vec4 eyeInView = frame.view * glm::vec4(frame.cameraPosition, 1.0f);
    CHECK(glm::length(glm::vec3(eyeInView)) < 1e-3f);

    //    And the camera-relative stage itself: rotating (world - eye) by the view basis is the same
    //    vector the view matrix produces. Equal here means the eye was subtracted exactly once.
    const glm::vec3 cameraRelative = world - frame.cameraPosition;
    const glm::vec3 viaBasis = basis * cameraRelative;
    const glm::vec3 viaView = glm::vec3(frame.view * glm::vec4(world, 1.0f));
    CHECK(glm::length(viaBasis - viaView) < 1e-3f);
    CHECK(viaView.z < 0.0f); // right-handed view space: in front of the camera is -z

    // 3. Clip. Projecting the view-space point and multiplying by the composed view-projection are
    //    the same operation; if they are not, something between them is not the camera.
    const glm::vec4 clipViaStages = frame.projection * glm::vec4(viaView, 1.0f);
    const glm::vec4 clipViaComposed = frame.viewProjection * glm::vec4(world, 1.0f);
    CHECK(glm::length(glm::vec3(clipViaStages - clipViaComposed)) < 1e-2f);
    CHECK(clipViaStages.w == Approx(clipViaComposed.w).margin(1e-3));
    REQUIRE(clipViaComposed.w > 1e-4f);

    // 4. Normalised device coordinates. WebGPU depth is 0..1 and the object is inside the frustum,
    //    and it is off-centre horizontally, which is what makes stage 5 a real measurement.
    const glm::vec3 ndc = glm::vec3(clipViaComposed) / clipViaComposed.w;
    CHECK(std::abs(ndc.x) < 1.0f);
    CHECK(std::abs(ndc.y) < 1.0f);
    CHECK(ndc.z > 0.0f);
    CHECK(ndc.z < 1.0f);
    CHECK(std::abs(ndc.x) > 0.2f);

    // 5. Screen, against the pixels themselves. Everything above is arithmetic that agrees with
    //    itself, which is consistent with the whole chain being applied to the wrong viewport.
    //
    //    Each object's pixels are found by hiding it and taking what changed, not by its colour: the
    //    first version of this test classified by hue and read the grey sphere -- lit by a blue-ish
    //    environment -- as the blue one, which put the "measured" centroid 28 pixels from the
    //    predicted one and looked like a projection bug.
    const auto setVisible = [&](std::string_view entity, bool visible) {
        bool found = false;
        for (scene::Entity& e : work.entities) {
            if (e.name == entity) {
                e.visible = visible;
                found = true;
            }
        }
        REQUIRE(found);
    };

    struct Subject {
        const char* name;
        glm::vec3 world{0.0f};
        glm::vec3 boundsMin{0.0f};
        glm::vec3 boundsMax{0.0f};
    };
    std::array<Subject, 3> subjects = {Subject{"static-object"}, Subject{"near-object"},
                                       Subject{"far-object"}};
    for (Subject& subject : subjects) {
        const rendering::RenderObjectDiagnostic* d = renderer.diagnosticObject(subject.name);
        REQUIRE(d != nullptr);
        REQUIRE_FALSE(d->cameraCulled);
        subject.world = d->worldPosition;
        subject.boundsMin = d->worldBoundsMin;
        subject.boundsMax = d->worldBoundsMax;
    }
    const glm::mat4 vp = frame.viewProjection;

    std::array<glm::vec2, 3> measured{};
    std::array<glm::vec2, 3> predicted{};
    for (std::size_t i = 0; i < subjects.size(); ++i) {
        const Subject& subject = subjects[i];
        INFO("object " << subject.name);
        const glm::vec4 clip = vp * glm::vec4(subject.world, 1.0f);
        REQUIRE(clip.w > 1e-4f);
        predicted[i] = ndcToPixels(glm::vec3(clip) / clip.w, kW, kH);

        setVisible(subject.name, false);
        const gpu::Image8 without = coldShot();
        setVisible(subject.name, true);
        const Blob blob = footprint(full, without);
        measured[i] = blob.centroid;
        INFO("footprint " << blob.count << " px, centroid (" << blob.centroid.x << ", "
                          << blob.centroid.y << "), predicted (" << predicted[i].x << ", "
                          << predicted[i].y << ")");
        REQUIRE(blob.count > 80); // the object draws something, before anything is claimed about where

        // The screen position the chain predicts is where the object's pixels are. The tolerance is
        // a few pixels because the centroid of a projected ellipsoid is not exactly the projection
        // of its centre under perspective, not because the chain is approximate.
        CHECK(std::abs(blob.centroid.x - predicted[i].x) < 6.0f);
        CHECK(std::abs(blob.centroid.y - predicted[i].y) < 6.0f);

        // And every pixel of it is inside the screen-space box its own world bounds project to. The
        // centroid alone would survive a scale error that left the centre where it was; this would
        // not.
        glm::vec2 boundsLo(std::numeric_limits<float>::max());
        glm::vec2 boundsHi(std::numeric_limits<float>::lowest());
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 point((corner & 1) ? subject.boundsMax.x : subject.boundsMin.x,
                                  (corner & 2) ? subject.boundsMax.y : subject.boundsMin.y,
                                  (corner & 4) ? subject.boundsMax.z : subject.boundsMin.z);
            const glm::vec4 cornerClip = vp * glm::vec4(point, 1.0f);
            REQUIRE(cornerClip.w > 1e-4f);
            const glm::vec2 px = ndcToPixels(glm::vec3(cornerClip) / cornerClip.w, kW, kH);
            boundsLo = glm::min(boundsLo, px);
            boundsHi = glm::max(boundsHi, px);
        }
        INFO("projected bounds x " << boundsLo.x << ".." << boundsHi.x << ", y " << boundsLo.y << ".."
                                   << boundsHi.y);
        CHECK(blob.lo.x >= boundsLo.x - 2.0f);
        CHECK(blob.hi.x <= boundsHi.x + 2.0f);
        CHECK(blob.lo.y >= boundsLo.y - 2.0f);
        CHECK(blob.hi.y <= boundsHi.y + 2.0f);
    }

    // Three objects, three distinct places: a chain that collapsed every transform onto the same
    // screen position would satisfy every per-object check above.
    CHECK(glm::length(measured[0] - measured[1]) > 40.0f);
    CHECK(glm::length(measured[0] - measured[2]) > 40.0f);
    CHECK(glm::length(measured[1] - measured[2]) > 40.0f);

    // Depth orders the way the geometry does: near, far, static, at 9, 15 and 22 metres out under a
    // 0..1 depth convention with no reversal.
    const auto depthOf = [&](const Subject& subject) {
        const glm::vec4 clip = vp * glm::vec4(subject.world, 1.0f);
        return clip.z / clip.w;
    };
    CHECK(depthOf(subjects[1]) < depthOf(subjects[2]));
    CHECK(depthOf(subjects[2]) < depthOf(subjects[0]));

    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 3.2: no drift across consecutive frames ---------------------------------------------
//
// The plan asks for "a regression that detects double subtraction across consecutive frames". With
// no camera-relative origin to subtract, the honest form of that claim is that the projection is a
// pure function of the camera pose and the transform: whatever the renderer has been doing for the
// last few hundred frames, arriving at a pose must give the same answer as arriving at it the first
// time. An origin that accumulated -- one subtraction per frame, or one that was never reset -- would
// pass every single-frame check above and fail here on lap two.
//
// Time is frozen deliberately. The minimal scene animates nothing, so advancing the clock would add
// no information and would leave "something moved" available as an explanation for a difference
// between laps. The frame index still advances, so the renderer's per-frame bookkeeping runs.
TEST_CASE("a static object's projection repeats exactly on every lap of a camera loop",
          "[gpu][renderer][forensics][lifetime3_4]") {
    const fs::path file = qaScene("renderer-qa-minimal.scene.json");
    if (!fs::is_regular_file(file)) {
        SKIP("the minimal QA scene is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    rendering::SceneRenderer renderer(*ctx, shaders);
    REQUIRE(renderer.init().has_value());

    constexpr std::uint32_t kW = 224;
    constexpr std::uint32_t kH = 140;
    constexpr int kLapFrames = 60;
    constexpr int kLaps = 6;
    scene::Scene work = detachedScene(file, kW, kH);
    renderer.setDiagnosticEntity("static-object");
    const glm::vec3 authored(10.0f, 2.0f, -20.0f);
    const glm::mat4 authoredMatrix = [&] {
        for (const scene::Entity& e : work.entities) {
            if (e.name == "static-object") {
                return e.transform.matrix();
            }
        }
        FAIL("the minimal QA scene has no static-object");
        return glm::mat4(1.0f);
    }();

    // A closed loop that goes a long way from the object and comes back to exactly where it started,
    // so lap n and lap n+1 are the same poses in the same order and any difference is the renderer's.
    //
    // The eye orbits the object *and* the aim pans off it by up to twenty-six degrees, which is the
    // part that matters: an earlier version kept the object centred by aiming straight at it, and its
    // "the projection repeats" claim was true of a projection that never changed -- measured, the
    // object's ndc.x moved by 1e-7 over the whole lap. Now it sweeps right across the frame and off
    // both edges, so repeating it means something.
    const auto poseAt = [&](int step) {
        const float t = static_cast<float>(step) / static_cast<float>(kLapFrames);
        const float a = t * 2.0f * 3.14159265358979f;
        const glm::vec3 eye(authored.x + 34.0f * std::sin(a), 3.0f + 9.0f * (1.0f - std::cos(a)),
                            authored.z + 34.0f * std::cos(a));
        const glm::vec3 toObject = glm::normalize(authored - eye);
        const float pan = 0.45f * std::sin(a * 3.0f);
        const glm::vec3 aimDirection =
            glm::vec3(glm::rotate(glm::mat4(1.0f), pan, glm::vec3(0.0f, 1.0f, 0.0f)) *
                      glm::vec4(toObject, 0.0f));
        return std::pair{eye, eye + aimDirection * 20.0f};
    };

    std::uint64_t frameIndex = 0;
    const auto renderPose = [&](glm::vec3 eye, glm::vec3 aim) {
        work.camera.position = eye;
        work.camera.target = aim;
        FrameTime time{};
        time.renderTime = 2.0; // frozen; see the note above
        time.deltaTime = 0.0;
        time.frameIndex = frameIndex++;
        auto image = renderer.renderToImage(work, time, kW, kH);
        REQUIRE(image.has_value());
        // The pose the renderer used is the pose that was asked for. Without this the whole test
        // could be comparing a camera that never moved against itself.
        REQUIRE(renderer.diagnosticFrame().cameraPosition == eye);
        return std::move(*image);
    };

    struct Sample {
        glm::vec3 ndc{0.0f};
        glm::mat4 view{1.0f};
        glm::mat4 viewProjection{1.0f};
        glm::vec2 centroid{0.0f};
        std::size_t pixels = 0;
    };
    std::array<std::vector<Sample>, kLaps> laps;

    for (int lap = 0; lap < kLaps; ++lap) {
        // Between laps two and three the camera is thrown ten kilometres away and brought back. An
        // origin that accumulates, or one captured once and never refreshed, has its largest chance
        // here: the excursion is three orders of magnitude outside the scene.
        if (lap == 3) {
            for (int i = 0; i < 20; ++i) {
                const float t = static_cast<float>(i) / 19.0f;
                static_cast<void>(renderPose(authored + glm::vec3(0.0f, 0.0f, 1.0e4f * t + 40.0f), authored));
            }
        }
        laps[lap].reserve(kLapFrames);
        for (int step = 0; step < kLapFrames; ++step) {
            const auto [eye, aim] = poseAt(step);
            const gpu::Image8 image = renderPose(eye, aim);
            const rendering::RendererDiagnosticFrame& frame = renderer.diagnosticFrame();
            const rendering::RenderObjectDiagnostic* object = renderer.diagnosticObject("static-object");
            REQUIRE(object != nullptr);
            // The transform itself never moves, at either of the two places it exists.
            REQUIRE(object->worldPosition == authored);
            REQUIRE(object->worldMatrix == authoredMatrix);

            Sample sample;
            const glm::vec4 clip = frame.viewProjection * glm::vec4(authored, 1.0f);
            REQUIRE(clip.w > 1e-4f); // this loop never puts the object behind the camera
            sample.ndc = glm::vec3(clip) / clip.w;
            sample.view = frame.view;
            sample.viewProjection = frame.viewProjection;
            const Blob orange = findBlob(image, isOrange);
            sample.centroid = orange.centroid;
            sample.pixels = orange.count;
            laps[lap].push_back(sample);
        }
    }

    // The loop is a loop: the poses really do sweep the object across the frame, so "identical on
    // every lap" is a claim about repetition and not about a camera that never moved.
    float ndcLo = std::numeric_limits<float>::max();
    float ndcHi = std::numeric_limits<float>::lowest();
    std::size_t onScreen = 0;
    for (const Sample& s : laps[0]) {
        ndcLo = std::min(ndcLo, s.ndc.x);
        ndcHi = std::max(ndcHi, s.ndc.x);
        onScreen += s.pixels > 50 ? 1 : 0;
    }
    const float ndcSpan = ndcHi - ndcLo;
    INFO("ndc.x sweeps " << ndcSpan << " across the lap; the object is visible on " << onScreen
                         << " of " << kLapFrames << " frames");
    CHECK(ndcSpan > 1.0f);
    CHECK(onScreen > kLapFrames / 3);

    std::size_t ndcMismatches = 0;
    std::size_t matrixMismatches = 0;
    float worstNdc = 0.0f;
    float worstCentroid = 0.0f;
    for (int lap = 1; lap < kLaps; ++lap) {
        for (int step = 0; step < kLapFrames; ++step) {
            const Sample& first = laps[0][static_cast<std::size_t>(step)];
            const Sample& again = laps[lap][static_cast<std::size_t>(step)];
            // Exact equality, not a tolerance: the inputs are bit-identical, so the outputs are too
            // unless something carried state across the laps.
            ndcMismatches += first.ndc == again.ndc ? 0 : 1;
            matrixMismatches += (first.view == again.view && first.viewProjection == again.viewProjection) ? 0 : 1;
            worstNdc = std::max(worstNdc, glm::length(first.ndc - again.ndc));
            if (first.pixels > 50 && again.pixels > 50) {
                worstCentroid = std::max(worstCentroid, glm::length(first.centroid - again.centroid));
            }
        }
    }
    INFO("worst ndc drift " << worstNdc << ", worst centroid drift " << worstCentroid << " px");
    CHECK(ndcMismatches == 0);
    CHECK(matrixMismatches == 0);
    // The pixels, not only the matrices: a projection that repeats while the drawn object does not
    // would mean the shader is reading a different transform from the one the diagnostics report.
    //
    // Not exact, and the residual is named rather than absorbed: lap zero starts with a cold ambient
    // occlusion history and the later laps do not, which moves the object's measured centre by about
    // fifteen thousandths of a pixel. A tenth of a pixel is three orders of magnitude below anything
    // an accumulating origin could do and still two orders above the temporal residual.
    CHECK(worstCentroid < 0.1f);
    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 3.4: the interleaved stress ---------------------------------------------------------
//
// Resize, reload, scene swap, seek forwards, seek backwards, camera cut and frame-index reuse, all
// crossed rather than taken one at a time, over enough rounds that a target released while a bind
// group still named it, or a cache keyed by something that repeats, would have to show.
//
// The mixed QA scene is in the rotation but not in the checkpoints. Its particle pools are the one
// piece of renderer state a seek does not restore -- see the dedicated test below -- so comparing a
// walked renderer against a fresh one on that scene measures the particle sim, not resource reuse.
// The other four scenes between them cover plain meshes, procedural instancing, a skinned rig with
// its palette uploads, blended transparency, and terrain with water targets.
TEST_CASE("an interleaved resize reload seek and cut sequence matches fresh references",
          "[gpu][renderer][forensics][lifetime3_4]") {
    std::vector<fs::path> rotation;
    std::vector<bool> checkpointed;
    const auto consider = [&](const char* stem, bool compare) {
        const fs::path path = qaScene(stem);
        if (fs::is_regular_file(path)) {
            rotation.push_back(path);
            checkpointed.push_back(compare);
        }
    };
    consider("renderer-qa-minimal.scene.json", true);
    consider("renderer-qa-transparency.scene.json", true);
    // Checkpointed again since `SYM-TERRAIN-1` was fixed. It was excluded while that stood, because
    // a fresh-reference comparison on a scene that does not render the same frame twice reports the
    // open defect rather than anything about resource reuse.
    consider("renderer-qa-water.scene.json", true);
    if (haveAlien()) {
        consider("renderer-qa-character.scene.json", true);
        consider("renderer-qa.scene.json", false); // in the sequence, not in the checkpoints
    }
    if (rotation.size() < 3) {
        SKIP("the QA scene set is not present");
    }

    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    Reference reference{*ctx, shaders, {}};
    Session walked(*ctx, shaders);
    walked.load(rotation.front());
    std::size_t current = 0;

    // Deliberately awkward: an odd width, a width narrower than its height, a very small frame and a
    // wide one, so a target that is reused when it should be replaced is the wrong shape rather than
    // merely the wrong contents.
    const std::array<std::pair<std::uint32_t, std::uint32_t>, 7> sizes = {
        std::pair{256u, 160u}, {97u, 61u}, {320u, 200u}, {48u, 176u},
        std::pair{193u, 121u}, {512u, 128u}, {64u, 64u}};
    const std::array<double, 4> checkpointSeconds = {0.75, 1.5, 0.25, 2.0};
    const std::array<glm::vec3, 4> cuts = {glm::vec3{0.0f, 12.0f, 24.0f}, glm::vec3{-18.0f, 2.0f, 4.0f},
                                           glm::vec3{6.0f, 1.0f, -14.0f}, glm::vec3{0.0f, 60.0f, 0.1f}};

    constexpr int kRounds = 18;
    std::size_t checkpointsTaken = 0;
    std::size_t transitions = 0;
    for (int round = 0; round < kRounds; ++round) {
        INFO("round " << round << " on " << rotation[current].filename().string());
        const auto [w, h] = sizes[static_cast<std::size_t>(round) % sizes.size()];

        // Ordinary playback at this size, with the clock ticking, so the frame deltas are real.
        for (int i = 0; i < 3; ++i) {
            static_cast<void>(walked.play(w, h));
            ++transitions;
        }

        // A camera cut, then a second one at a different size before the first has been anywhere.
        walked.cutCamera(cuts[static_cast<std::size_t>(round) % cuts.size()], glm::vec3(0.0f, 1.0f, -4.0f), w, h);
        static_cast<void>(walked.play(w, h));
        const auto [w2, h2] = sizes[static_cast<std::size_t>(round + 3) % sizes.size()];
        walked.cutCamera(cuts[static_cast<std::size_t>(round + 1) % cuts.size()], glm::vec3(2.0f, 0.0f, -9.0f), w2,
                         h2);
        static_cast<void>(walked.play(w2, h2));
        transitions += 4;

        // A seek, alternating direction round by round, and the same frame drawn twice from the same
        // FrameTime: a frame index that does not advance is its own hazard, because the renderer
        // drops AO history on one and a pass that assumed monotonic indices would not.
        const double seekTo = (round % 2 == 0) ? 2.5 - 0.1 * round : 0.2 + 0.1 * round;
        const FrameTime held = walked.seekTo(std::max(0.0, seekTo));
        const gpu::Image8 once = walked.draw(held, w2, h2);
        const gpu::Image8 twice = walked.draw(held, w2, h2);

        INFO("repeated frame index " << held.frameIndex << " at " << held.renderTime << " s");
        // Exact, on every scene including terrain and water. This was reported rather than asserted
        // while `SYM-TERRAIN-1` stood: ambient occlusion's history was valid on the first render of
        // a frame and dropped on the second, so re-rendering a frame produced a different picture
        // from the one it was re-rendering. Fixed in `AoRenderer::render`, and this is the
        // assertion that says so.
        CHECK(channelsDiffering(once, twice) == 0);
        transitions += 2;

        // Every third round the composition is replaced: the same file back (a reload) or the next
        // scene in the rotation (a swap). Both invalidate meshes, textures, rigs and every cache
        // keyed on them; only the swap changes what those caches should contain.
        if (round % 3 == 2) {
            const bool swap = (round % 6 == 5);
            if (swap) {
                current = (current + 1) % rotation.size();
            }
            walked.load(rotation[current]);
            ++transitions;
        }

        // The checkpoint: the authored camera, a canonical size and a canonical second, against an
        // engine and a renderer that have been nowhere else.
        if (checkpointed[current]) {
            const double seconds = checkpointSeconds[static_cast<std::size_t>(round) % checkpointSeconds.size()];
            const std::uint32_t cw = (round % 2 == 0) ? 192u : 288u;
            const std::uint32_t ch = (round % 2 == 0) ? 120u : 180u;
            walked.restoreAuthoredCamera(cw, ch);
            const FrameTime time = walked.seekTo(seconds);
            const gpu::Image8 image = walked.draw(time, cw, ch);
            INFO("checkpoint " << rotation[current].filename().string() << " at " << seconds << " s, " << cw
                               << "x" << ch);
            CHECK(hasContrast(image));
            CHECK(gpu::hashImage(image) == reference.hashAt(rotation[current], seconds, cw, ch));
            ++checkpointsTaken;
        }
        REQUIRE(ctx->errorCount() == 0);
    }

    INFO(transitions << " transitions, " << checkpointsTaken << " checkpoints");
    CHECK(transitions > 150);
    CHECK(checkpointsTaken >= 10);
    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 3.4: reverse seek sweep and frame-index reuse ---------------------------------------
//
// The plan lists these two as the remaining shapes. A descending sweep is not the ascending one read
// backwards: every cache that is filled on the way up is asked for something older on the way down,
// and anything that treats "the time went backwards" as "no time passed" holds the previous frame.
TEST_CASE("a reverse timeline sweep and a repeated frame index match fresh renders",
          "[gpu][renderer][forensics][lifetime3_4]") {
    const fs::path file = qaScene("renderer-qa-character.scene.json");
    if (!fs::is_regular_file(file) || !haveAlien()) {
        SKIP("the character QA scene or its alien asset is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    Reference reference{*ctx, shaders, {}};
    constexpr std::uint32_t kW = 208;
    constexpr std::uint32_t kH = 130;

    Session walked(*ctx, shaders);
    walked.load(file);
    // A warm-up that leaves the renderer somewhere: forty playback frames at changing sizes, so the
    // sweep below starts from a renderer full of other frames' resources rather than a cold one.
    for (int i = 0; i < 40; ++i) {
        static_cast<void>(walked.play(128u + static_cast<std::uint32_t>(i % 5) * 41u,
                                      80u + static_cast<std::uint32_t>(i % 3) * 33u));
    }

    std::vector<double> seconds;
    for (int i = 12; i >= 0; --i) {
        seconds.push_back(0.25 * i);
    }

    // Descending. Each second is compared against a fresh engine and a fresh renderer taken straight
    // there, which is the only reference that cannot have inherited the walk.
    std::vector<std::uint64_t> descending;
    for (const double at : seconds) {
        INFO("descending to " << at << " s");
        const FrameTime time = walked.seekTo(at);
        const gpu::Image8 image = walked.draw(time, kW, kH);
        CHECK(hasContrast(image));
        const std::uint64_t hash = gpu::hashImage(image);
        CHECK(hash == reference.hashAt(file, at, kW, kH));
        descending.push_back(hash);
    }
    // The scene genuinely animates across the sweep, or the agreement above would be the agreement
    // of thirteen identical frames.
    CHECK(std::set<std::uint64_t>(descending.begin(), descending.end()).size() > 8);

    // Ascending over the same seconds through the same walked session: the same frames, whichever
    // direction they were reached from.
    for (std::size_t i = seconds.size(); i-- > 0;) {
        INFO("ascending to " << seconds[i] << " s");
        const FrameTime time = walked.seekTo(seconds[i]);
        const gpu::Image8 image = walked.draw(time, kW, kH);
        CHECK(gpu::hashImage(image) == descending[i]);
    }

    // Frame-index reuse: the same FrameTime drawn eight times running, at two sizes, must give the
    // same frame every time and the same frame a fresh renderer gives.
    const FrameTime held = walked.seekTo(1.25);
    const std::uint64_t expected = reference.hashAt(file, 1.25, kW, kH);
    for (int i = 0; i < 8; ++i) {
        INFO("repeat " << i);
        CHECK(gpu::hashImage(walked.draw(held, kW, kH)) == expected);
        // A differently sized draw in between, so the repetition is not simply a target that was
        // never touched: the frame index still does not advance.
        static_cast<void>(walked.draw(held, 96u, 72u));
    }
    CHECK(ctx->errorCount() == 0);
}

// ---- Phase 3.4: what a seek does and does not restore ------------------------------------------
//
// The interleaved stress above leaves the mixed QA scene out of its checkpoints, and this is why.
// Both halves are asserted so the boundary is on record rather than in a comment: with the particle
// arm off, a walked renderer seeked back to a second reproduces a fresh one exactly, so nothing else
// in the frame carries state across a seek; with particles on it does not, and forcing the pools to
// reset -- by handing the renderer a different `scene::Scene` object, which is the only trigger
// `ParticleRenderer::update` has -- restores the agreement.
//
// Both claims survive the fix. When `resetTemporalHistory()` learns to call `resetAll()`, the
// particle-arm-off comparison still holds and the after-reset comparison still holds; only the
// reported divergence in between goes to zero.
TEST_CASE("a seek restores every renderer cache, including the particle pools",
          "[gpu][renderer][forensics][lifetime3_4]") {
    const fs::path file = qaScene("renderer-qa.scene.json");
    if (!fs::is_regular_file(file) || !haveAlien()) {
        SKIP("the mixed QA scene or its alien asset is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    constexpr std::uint32_t kW = 200;
    constexpr std::uint32_t kH = 128;
    constexpr double kCheckpoint = 0.5;

    const auto walkAndSeek = [&](bool particles) {
        Session session(*ctx, shaders);
        rendering::SceneRenderer::PassToggles toggles;
        toggles.particles = particles;
        session.renderer.setPassToggles(toggles);
        session.load(file);
        for (int i = 0; i < 40; ++i) {
            static_cast<void>(session.play(128u + static_cast<std::uint32_t>(i % 5) * 37u,
                                           80u + static_cast<std::uint32_t>(i % 3) * 29u));
        }
        const FrameTime time = session.seekTo(kCheckpoint);
        return session.draw(time, kW, kH);
    };
    const auto freshAt = [&](bool particles) {
        Session session(*ctx, shaders);
        rendering::SceneRenderer::PassToggles toggles;
        toggles.particles = particles;
        session.renderer.setPassToggles(toggles);
        session.load(file);
        const FrameTime time = session.seekTo(kCheckpoint);
        return session.draw(time, kW, kH);
    };

    // Half one: with the particle simulation removed, a seek is a complete restoration. Meshes,
    // textures, the skinned palette, the transparent pass, the procedural instance sets and every
    // render target the forty warm-up frames resized are all back where a fresh renderer has them.
    const gpu::Image8 walkedNoParticles = walkAndSeek(false);
    const gpu::Image8 freshNoParticles = freshAt(false);
    REQUIRE(hasContrast(freshNoParticles));
    CHECK(channelsDiffering(walkedNoParticles, freshNoParticles) == 0);

    // Half two: with particles on, the pools used to be whatever the warm-up left them, because
    // nothing reset them on a seek -- `ParticleRenderer::resetAll` had said in its own comment since
    // it was written that it is "used on seek/offline restarts", and the only caller was the
    // scene-pointer change, so a seek that kept the same scene kept the simulation. Sixty-three
    // channels of a hundred thousand: the size of difference that reads as noise and is not.
    //
    // `SceneRenderer::resetTemporalHistory` now resets them, so this is an assertion rather than the
    // report it was when the defect was found. Half one above is what makes it meaningful: it says
    // the particles were the *whole* of the difference and not one contributor to it.
    const gpu::Image8 walkedParticles = walkAndSeek(true);
    const gpu::Image8 freshParticles = freshAt(true);
    REQUIRE(hasContrast(freshParticles));
    const std::size_t carried = channelsDiffering(walkedParticles, freshParticles);
    INFO("after a seek, " << carried << " of " << freshParticles.rgba.size()
                          << " channels still carry the particle state the walk left behind");
    CHECK(carried == 0);

    // Half three: the pools reset when the renderer is handed a different scene object, and once
    // they have, the walked renderer and the fresh one agree exactly. That is what makes the
    // particle pools -- and not some other cache the warm-up touched -- the whole of the difference.
    {
        Session session(*ctx, shaders);
        session.load(file);
        for (int i = 0; i < 40; ++i) {
            static_cast<void>(session.play(128u + static_cast<std::uint32_t>(i % 5) * 37u,
                                           80u + static_cast<std::uint32_t>(i % 3) * 29u));
        }
        {
            // A detached copy of the same scene, drawn once. `ParticleRenderer::update` resets every
            // pool when the scene it is given is not the one it saw last, so this frame and the next
            // both arrive with empty pools.
            scene::Scene decoy = session.engine.scene();
            FrameTime discard{};
            REQUIRE(session.renderer.renderToImage(decoy, discard, kW, kH).has_value());
        }
        const FrameTime time = session.seekTo(kCheckpoint);
        const gpu::Image8 afterReset = session.draw(time, kW, kH);
        CHECK(channelsDiffering(afterReset, freshParticles) == 0);
    }

    CHECK(ctx->errorCount() == 0);
}


// ---- Phase 3.4: what a seek does and does not reseed, on the animation side --------------------
//
// Found by turning motion blur on for the resource-lifetime checkpoints above: with it on, and only
// on the scene that has a rig, a renderer that has played forty frames and then seeks no longer
// reproduces a fresh one -- at every second of a thirteen-point sweep, not at one of them.
//
// It is not a renderer fault, which is why it is pinned separately rather than left to fail a
// lifetime test. `SkinnedRig::previousPalette` is *scene* state: `Engine::seekSeconds` reseeds
// modulation, sources, cues and the entity world, the animation player puts the current palette
// exactly where a fresh engine puts it -- and the previous palette is left holding the pose from
// before the jump. The skinned pass builds each vertex's previous clip position from it
// (`pbr_skinned.wgsl` blends the palette a second time at `base = count`), so the first frame after
// every scrub carries joint motion vectors for a jump nobody made, and motion blur draws them.
//
// The two positive claims below both survive the fix; the divergence in between is reported rather
// than asserted, so closing it cannot fail this test.
TEST_CASE("a seek reseeds the current skinning palette but not the previous one",
          "[gpu][renderer][forensics][lifetime3_4]") {
    const fs::path file = qaScene("renderer-qa-character.scene.json");
    if (!fs::is_regular_file(file) || !haveAlien()) {
        SKIP("the character QA scene or its alien asset is not present");
    }
    auto ctx = makeContext();
    gpu::ShaderLibrary shaders(*ctx, {fs::path(AVGEN_SHADER_SOURCE_DIR)});
    constexpr std::uint32_t kW = 208;
    constexpr std::uint32_t kH = 130;
    constexpr double kCheckpoint = 1.0;

    const auto arrive = [&](Session& session, bool walk, float blurAmount) {
        session.load(file);
        auto* blur = session.engine.params().findAs<float>("post/motionBlur/amount");
        REQUIRE(blur != nullptr);
        blur->setBase(blurAmount);
        session.engine.params().resetFinals();
        if (walk) {
            for (int i = 0; i < 40; ++i) {
                static_cast<void>(session.play(160u, 100u));
            }
        }
        const FrameTime time = session.seekTo(kCheckpoint);
        return session.draw(time, kW, kH);
    };

    // Half one: with blur off, a seek is a complete restoration. Whatever forty frames of playback
    // left behind, the frame at one second is the frame a fresh engine and a fresh renderer draw.
    {
        Session walked(*ctx, shaders);
        Session fresh(*ctx, shaders);
        const gpu::Image8 a = arrive(walked, true, 0.0f);
        const gpu::Image8 b = arrive(fresh, false, 0.0f);
        REQUIRE(hasContrast(b));
        CHECK(channelsDiffering(a, b) == 0);

        // And the pose itself agrees exactly, joint for joint, which is what says the animation
        // player seeked correctly and localises what follows to the *previous* palette alone.
        const std::vector<scene::SkinnedRig>& walkedRigs = walked.engine.scene().rigs;
        const std::vector<scene::SkinnedRig>& freshRigs = fresh.engine.scene().rigs;
        REQUIRE(walkedRigs.size() == 1);
        REQUIRE(freshRigs.size() == 1);
        REQUIRE(walkedRigs[0].palette.size() > 8);
        REQUIRE(walkedRigs[0].palette.size() == freshRigs[0].palette.size());
        CHECK(walkedRigs[0].paletteTime == freshRigs[0].paletteTime);
        std::size_t currentDiffering = 0;
        for (std::size_t j = 0; j < walkedRigs[0].palette.size(); ++j) {
            currentDiffering += walkedRigs[0].palette[j] == freshRigs[0].palette[j] ? 0 : 1;
        }
        CHECK(currentDiffering == 0);

        // Half two, reported: the previous palette is the pose from before the jump. In model units,
        // because "49 of 49 joints differ" does not say whether it matters and "by 71 units" does --
        // this asset is about a hundred units tall.
        REQUIRE(walkedRigs[0].previousPalette.size() == freshRigs[0].previousPalette.size());
        std::size_t previousDiffering = 0;
        float worst = 0.0f;
        for (std::size_t j = 0; j < walkedRigs[0].previousPalette.size(); ++j) {
            const glm::mat4& x = walkedRigs[0].previousPalette[j];
            const glm::mat4& y = freshRigs[0].previousPalette[j];
            previousDiffering += x == y ? 0 : 1;
            for (int c = 0; c < 4; ++c) {
                for (int r = 0; r < 4; ++r) {
                    worst = std::max(worst, std::abs(x[c][r] - y[c][r]));
                }
            }
        }
        WARN("after a seek, " << previousDiffering << " of " << walkedRigs[0].previousPalette.size()
                              << " joints of previousPalette still hold the pose from before the jump, "
                                 "worst element "
                              << worst
                              << " model units (Engine::seekSeconds reseeds the current palette and "
                                 "not the previous one)");
    }

    // Half three: the divergence reaches the picture only through the temporal path. With blur on,
    // the same two sessions are compared again and the difference is measured rather than asserted;
    // what *is* asserted is that blur is the gate -- the blur-off arm above was exact.
    {
        Session walked(*ctx, shaders);
        Session fresh(*ctx, shaders);
        const gpu::Image8 a = arrive(walked, true, 0.8f);
        const gpu::Image8 b = arrive(fresh, false, 0.8f);
        REQUIRE(hasContrast(b));
        const std::size_t smeared = channelsDiffering(a, b);
        WARN("with motion blur on, the first frame after the seek differs from a fresh one over "
             << smeared << " of " << b.rgba.size() << " channels");
        // A fresh session is a fresh session either way: this is the control that says the
        // comparison above is between two renderers and not between two accidents.
        Session second(*ctx, shaders);
        const gpu::Image8 c = arrive(second, false, 0.8f);
        CHECK(channelsDiffering(b, c) == 0);
    }

    CHECK(ctx->errorCount() == 0);
}
