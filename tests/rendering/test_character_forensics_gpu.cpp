// Renderer forensics Phase 5.3: one character, on its own, taken apart.
//
// The isolation scene (`examples/qa/renderer-qa-character.scene.json`) is a camera, a ground grid,
// one skinned alien and one key light. Nothing else -- no particles, no water, no terrain, no
// second rig, no procedural scatter -- so when a pose jumps, a limb disappears or a frame will not
// reproduce, there is exactly one place it can have come from. That is the whole reason the scene
// exists; every assertion below is worth less on a scene with more in it.
//
// What the plan asks for, and where it is:
//   * the progression animation off -> on -> culling -> shadows -> transparency -> post:
//     "the subsystem progression ..." below, which prints the table it records.
//   * character id, skeleton id, clip, animation time and delta, pose version, bone count,
//     skinning buffer and GPU index: "the character isolation scene ...".
//   * NaN/Inf matrices, invalid quaternions and scales, invalid influences, with the exact clip and
//     second when one is found: "no bone matrix ...".
//   * bind pose, idle, walk, run, loop, pause, resume, seek, scrub, reverse, near/far camera and
//     reload: "the character's pose is a pure function of the timeline".
//
// Three traps this file is written around, all of which have produced a test that could not fail
// somewhere else in this investigation:
//
//   1. `FixedStepClock::restartAt` per frame reports a frame delta of zero. Anything that
//      integrates then does nothing and the two arms of an A/B come out identical because neither
//      ran. There is one clock here, ticked; `jumpTo` is the only thing that restarts it, because a
//      seek is a discontinuity and its delta genuinely is zero.
//   2. Writing `scene().camera` does nothing -- it is re-derived from `camera/*` every update -- and
//      `setBase` alone does not reach `applyParameters`, which reads the *final* value. `aimCamera`
//      drives the parameters, calls `resetFinals`, and REQUIREs the camera actually moved.
//   3. The renderer's diagnostic frame is built from `Scene::entities`. A `procedural` node would
//      not appear in it at all; this scene has none, and the inventory test asserts that rather
//      than assuming it.

#include "app/engine.hpp"
#include "app/transport.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "gpu/context.hpp"
#include "gpu/readback.hpp"
#include "gpu/shader_library.hpp"
#include "params/parameter_set.hpp"
#include "params/timeline.hpp"
#include "rendering/scene_renderer.hpp"
#include "rendering/skinning.hpp"
#include "scene/animation.hpp"
#include "scene/composition.hpp"
#include "scene/scene.hpp"
#include "scene/skeleton.hpp"

#include <fmt/format.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Approx;
namespace fs = std::filesystem;

namespace {

constexpr std::uint32_t kW = 240;
constexpr std::uint32_t kH = 150;
constexpr double kFps = 60.0;
// Long enough that a seek can land anywhere the tests ask for, and long enough that the transport
// has a stated length rather than none.
//
// It is *not* needed to make seeking work, though an earlier version of this comment said so.
// `Transport::clampToRange` returns the second unchanged in Offline mode -- "the fixed-step clock is
// the authority; a render range is not a play range" -- and a duration of zero is treated as
// unbounded rather than as a zero-length piece, deliberately (ADR-102). Measured: an Offline engine
// over this scene reports `durationSeconds() == 0` and `seekSeconds(7.5)` lands on exactly 7.5.
// The length is here so the piece has one, which is closer to how the scene is played in the
// application, and so the loop and scrub sections have a stated range to work within.
constexpr double kPieceSeconds = 30.0;

fs::path sceneFile() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "qa" / "renderer-qa-character.scene.json";
}

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

// Gives the piece a length without changing a pixel: two keys holding a parameter at the value it
// already has. See `kPieceSeconds` for why this is a convenience rather than a necessity -- an
// Offline transport does not clamp a seek at all.
constexpr const char* kHeldTarget = "nodes/alien/emissiveBoost";

void givePieceALength(app::Engine& engine) {
    auto* held = engine.params().findAs<float>(kHeldTarget);
    REQUIRE(held != nullptr);
    // Idempotent, because `loadComposition` rebuilds the parameters and leaves the timeline alone:
    // a reload that re-added this would stack a second identical track on the same target, and the
    // engine would rightly warn about it every time.
    if (engine.timeline().findTrack(kHeldTarget) == nullptr) {
        params::Track track;
        track.target = kHeldTarget;
        const float value = held->base();
        track.addKey({.time = 0.0, .value = {value}, .interp = params::KeyInterp::Linear});
        track.addKey({.time = kPieceSeconds, .value = {value}, .interp = params::KeyInterp::Linear});
        engine.timeline().addTrack(track);
    }
    engine.timeline().enabled = true;
    engine.rebind();
    engine.refreshTransport();
    REQUIRE(engine.durationSeconds() >= kPieceSeconds);
}

// Where the character is, found rather than spelled: the entity name an importer produces is a
// function of the asset's own node names, and a hard-coded one turns a renamed mesh into a passing
// test over an empty scene.
struct Character {
    std::string entity;
    std::size_t entityIndex = 0;
    scene::RigId rig = scene::kInvalidRig;
    scene::MeshId mesh = scene::kInvalidMesh;
};

Character findCharacter(const scene::Scene& scene) {
    Character found;
    std::size_t rigged = 0;
    for (std::size_t i = 0; i < scene.entities.size(); ++i) {
        if (scene.entities[i].rig == scene::kInvalidRig) {
            continue;
        }
        ++rigged;
        found.entity = scene.entities[i].name;
        found.entityIndex = i;
        found.rig = scene.entities[i].rig;
        found.mesh = scene.entities[i].mesh;
    }
    REQUIRE(rigged == 1); // the isolation scene's whole premise
    REQUIRE(found.rig < scene.rigs.size());
    return found;
}

// The engine, the renderer and the one clock, kept together so no test can accidentally acquire a
// second clock and lose its frame deltas.
struct Harness {
    std::unique_ptr<gpu::Context> ctx;
    std::unique_ptr<gpu::ShaderLibrary> shaders;
    std::unique_ptr<rendering::SceneRenderer> renderer;
    app::Engine engine{app::EngineMode::Offline};
    FixedStepClock clock{kFps};
    Character character;

    // Built in the constructor rather than handed back from a factory: Engine is neither copyable
    // nor movable, so a `return h;` here would not compile and a test that worked around it with a
    // pointer would be one more thing to get wrong.
    //
    // `graphics = false` builds the same engine with no device behind it. The pose is scene-side
    // arithmetic, so the reference engines the purity tests compare against need no GPU -- and a
    // reference that opened its own device would make those tests several times slower for nothing.
    explicit Harness(bool graphics = true) {
        if (graphics) {
            ctx = makeContext();
            shaders = std::make_unique<gpu::ShaderLibrary>(*ctx,
                                                          std::vector{fs::path(AVGEN_SHADER_SOURCE_DIR)});
            renderer = std::make_unique<rendering::SceneRenderer>(*ctx, *shaders);
            REQUIRE(renderer->init().has_value());
        }
        REQUIRE(engine.loadComposition(sceneFile()).has_value());
        givePieceALength(engine);
        step();
        character = findCharacter(engine.scene());
        if (renderer) {
            renderer->setDiagnosticEntity(character.entity);
        }
    }

    [[nodiscard]] std::string node() const { return character.entity.substr(0, character.entity.find('/')); }

    // One frame of ordinary playback. The clock is ticked, never restarted.
    FrameTime step() {
        const FrameTime time = engine.tick(clock);
        engine.setViewport(kW, kH);
        engine.update(time);
        return time;
    }

    // A scene reload, everything it invalidates put back: the parameter set is rebuilt from the
    // file, so the length has to be re-established, and the entity indices are only guaranteed to
    // be what they were because they are looked up again.
    void reload() {
        REQUIRE(engine.loadComposition(sceneFile()).has_value());
        givePieceALength(engine);
        step();
        character = findCharacter(engine.scene());
        if (renderer) {
            renderer->setDiagnosticEntity(character.entity);
            renderer->resetTemporalHistory();
        }
    }

    void steps(int n) {
        for (int i = 0; i < n; ++i) {
            static_cast<void>(step());
        }
    }

    // A seek: the transport resynchronises and the render clock -- which is the authority in
    // EngineMode::Offline -- is placed on the second asked for. This is the one place a zero frame
    // delta is the truth rather than the trap, because nothing is meant to integrate across a jump.
    FrameTime jumpTo(double seconds) {
        engine.seekSeconds(seconds);
        clock.restartAt(seconds);
        const FrameTime time = step();
        REQUIRE(time.renderTime == seconds);
        return time;
    }

    gpu::Image8 shot(const FrameTime& time) {
        renderer->resetTemporalHistory(); // every comparison here is between cold frames
        auto image = renderer->renderToImage(engine.scene(), time, kW, kH);
        REQUIRE(image.has_value());
        return std::move(*image);
    }

    [[nodiscard]] const scene::SkinnedRig& rig() const { return engine.scene().rigs[character.rig]; }
    [[nodiscard]] const scene::Entity& entity() const {
        return engine.scene().entities[character.entityIndex];
    }
    [[nodiscard]] const rendering::RenderObjectDiagnostic& diagnostic() const {
        const rendering::RenderObjectDiagnostic* d = renderer->diagnosticObject(character.entity);
        REQUIRE(d != nullptr);
        return *d;
    }
};

// The camera, driven the only way that reaches the scene, and checked. `camera/mode` must be 1
// (free) or the orbit camera overwrites the position on the next update and every "looking away"
// test in this file silently looks at the character instead.
void aimCamera(Harness& h, glm::vec3 eye, glm::vec3 target) {
    auto* mode = h.engine.params().findAs<int>("camera/mode");
    auto* position = h.engine.params().findAs<glm::vec3>("camera/position");
    auto* aim = h.engine.params().findAs<glm::vec3>("camera/target");
    REQUIRE(mode != nullptr);
    REQUIRE(position != nullptr);
    REQUIRE(aim != nullptr);
    mode->setBase(1);
    position->setBase(eye);
    aim->setBase(target);
    h.engine.params().resetFinals(); // setBase alone never reaches applyParameters
    h.step();
    REQUIRE(h.engine.scene().camera.position == eye);
}

// ---- pose validity ------------------------------------------------------------------------------

// What the plan calls "invalid pose data", with the frame that produced it attached. Returned as a
// message rather than asserted in place so the caller can name the clip and the second: "joint 31 of
// the palette is not finite" is a bug report only once it says which clip, at which time.
std::string paletteFault(const std::vector<glm::mat4>& palette) {
    for (std::size_t k = 0; k < palette.size(); ++k) {
        const glm::mat4& m = palette[k];
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                if (!std::isfinite(m[c][r])) {
                    return fmt::format("palette[{}][{}][{}] is {}", k, c, r, m[c][r]);
                }
            }
        }
        // A joint matrix that has collapsed to zero volume turns the skin it drives inside out or
        // sucks it to a point; a mirrored one (negative determinant) flips its winding. Neither can
        // come out of a translation/rotation/scale chain with positive scales, so either means the
        // arithmetic above it went wrong rather than the artist doing something unusual.
        const float determinant = glm::determinant(glm::mat3(m));
        if (!std::isfinite(determinant) || determinant <= 1e-9f) {
            return fmt::format("palette[{}] has determinant {}", k, determinant);
        }
        // Model space for this asset is roughly a hundred units across; a joint a kilometre away is
        // a runaway, not a pose.
        const glm::vec3 translation(m[3]);
        if (glm::length(translation) > 1.0e4f) {
            return fmt::format("palette[{}] translation is {} units from the origin", k,
                               glm::length(translation));
        }
    }
    return {};
}

std::string poseFault(const scene::SkinnedRig& rig) {
    for (std::size_t j = 0; j < rig.pose.size(); ++j) {
        const scene::Transform& t = rig.pose.local[j];
        if (!std::isfinite(t.position.x) || !std::isfinite(t.position.y) || !std::isfinite(t.position.z)) {
            return fmt::format("joint {} position is not finite", j);
        }
        const float length = glm::length(t.rotation);
        if (!std::isfinite(length) || std::abs(length - 1.0f) > 1e-3f) {
            return fmt::format("joint {} rotation has length {}", j, length);
        }
        if (!std::isfinite(t.scale.x) || !std::isfinite(t.scale.y) || !std::isfinite(t.scale.z) ||
            std::min({t.scale.x, t.scale.y, t.scale.z}) <= 1e-6f) {
            return fmt::format("joint {} scale is ({}, {}, {})", j, t.scale.x, t.scale.y, t.scale.z);
        }
    }
    return {};
}

// The static half: the influences the GPU reads. A joint index past the end of the palette reads
// another rig's matrices or uninitialised memory, and weights that do not sum to one scale a vertex
// towards or away from the origin -- which is exactly what "the character stretches to infinity"
// looks like.
std::string influenceFault(const scene::MeshData& mesh, std::size_t paletteSize) {
    if (!mesh.skinned()) {
        return fmt::format("the mesh has {} vertices and {} influences", mesh.vertices.size(),
                           mesh.skin.size());
    }
    for (std::size_t v = 0; v < mesh.skin.size(); ++v) {
        const scene::SkinInfluence& s = mesh.skin[v];
        float sum = 0.0f;
        float largest = 0.0f;
        for (int i = 0; i < 4; ++i) {
            if (s.joints[i] >= paletteSize) {
                return fmt::format("vertex {} influence {} names joint {} of {}", v, i, s.joints[i],
                                   paletteSize);
            }
            const float w = s.weights[i];
            if (!std::isfinite(w) || w < 0.0f || w > 1.0f) {
                return fmt::format("vertex {} influence {} has weight {}", v, i, w);
            }
            sum += w;
            largest = std::max(largest, w);
        }
        if (std::abs(sum - 1.0f) > 1e-3f) {
            return fmt::format("vertex {} weights sum to {}", v, sum);
        }
        if (largest <= 0.0f) {
            return fmt::format("vertex {} is influenced by nothing", v);
        }
    }
    return {};
}

// How far two palettes are apart, in model units. Used instead of exact equality wherever the two
// sides reached the same second by different arithmetic (a loop wrap, a rate grid); exact equality
// is used wherever the claim is that they ran the *same* arithmetic.
float paletteDistance(const std::vector<glm::mat4>& a, const std::vector<glm::mat4>& b) {
    if (a.size() != b.size()) {
        return std::numeric_limits<float>::infinity();
    }
    float worst = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                worst = std::max(worst, std::abs(a[i][c][r] - b[i][c][r]));
            }
        }
    }
    return worst;
}

std::size_t jointsDiffering(const std::vector<glm::mat4>& a, const std::vector<glm::mat4>& b) {
    if (a.size() != b.size()) {
        return a.size() + b.size();
    }
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        n += a[i] == b[i] ? 0 : 1;
    }
    return n;
}

// Asks the composition for a clip and checks the rig took it. `setNodeAnimation` returns true when
// the *node* exists, which it always does here -- the state landing is a separate question, and a
// misspelled clip name only warns.
void playClip(Harness& h, const char* state, double origin) {
    REQUIRE(h.engine.composition() != nullptr);
    REQUIRE(h.engine.composition()->setNodeAnimation(h.node(), state, origin, 0.0f, 1.0f, true));
    h.jumpTo(origin);
    REQUIRE(h.rig().player.currentState() == std::string_view(state));
}

} // namespace

// ---- the instrument ----------------------------------------------------------------------------
//
// Everything after this assumes the scene is what it claims to be and that the diagnostic reports
// the character rather than something next to it. Both are asserted here, along with the plan's
// list of things to instrument, because a forensic test whose subject is missing passes.
TEST_CASE("the character isolation scene holds one rig one ground and one light",
          "[gpu][composition][forensics][character5_3]") {
    if (!fs::is_regular_file(sceneFile())) {
        SKIP("the character isolation scene is not present");
    }
    Harness h;
    const scene::Scene& scene = h.engine.scene();

    // The inventory. `procedurals` is checked at zero because the renderer's diagnostic frame is
    // built from `entities` alone: a character that arrived as a procedural would be invisible to
    // every assertion below and the test would report nothing wrong.
    CHECK(scene.entities.size() == 2); // the ground and the character
    CHECK(scene.procedurals.empty());
    CHECK(scene.particles.empty());
    CHECK(scene.waters.empty());
    CHECK(scene.lights.size() == 1);
    REQUIRE(scene.rigs.size() == 1);

    // Character id and skeleton id.
    const scene::SkinnedRig& rig = h.rig();
    INFO("character '" << h.character.entity << "' on rig '" << rig.name << "'");
    CHECK(h.character.rig == 0);
    CHECK_FALSE(rig.name.empty());
    CHECK(rig.skeleton.valid());
    CHECK(rig.skeleton.jointCount() > 20); // a real skeleton, not an empty one that poses to nothing
    CHECK(rig.palette.size() == rig.skeleton.paletteSize());
    CHECK(rig.palette.size() > 20);

    // The clips, and the one the scene authored.
    CHECK(rig.clips.size() == 3);
    for (const char* wanted : {"Idle", "Walk", "Run"}) {
        INFO("clip " << wanted);
        CHECK(rig.findClip(wanted) >= 0);
        CHECK(rig.player.findState(wanted) != nullptr);
    }
    CHECK(rig.player.currentState() == std::string_view("Walk")); // what the scene file says

    // The renderer's view of the same character.
    const FrameTime time = h.step();
    const gpu::Image8 image = h.shot(time);
    CHECK(gpu::hashImage(image) != 0);
    const rendering::RenderObjectDiagnostic& diagnostic = h.diagnostic();
    CHECK(diagnostic.name == h.character.entity);
    CHECK(diagnostic.entityIndex == h.character.entityIndex);
    CHECK(diagnostic.rigIndex == h.character.rig);
    CHECK(diagnostic.jointCount == rig.palette.size()); // bone count, as the frame sees it
    CHECK(diagnostic.finite);
    CHECK(diagnostic.visible);
    CHECK_FALSE(diagnostic.cameraCulled);
    CHECK(diagnostic.submitted);
    CHECK(diagnostic.cullReason == "submitted");
    CHECK(diagnostic.objectSlot != std::numeric_limits<std::uint32_t>::max());
    // The character's box has to have volume, or every culling verdict below is meaningless.
    CHECK(glm::all(glm::greaterThan(diagnostic.worldBoundsMax, diagnostic.worldBoundsMin)));
    // On screen means every frustum plane reports the box on its inside.
    for (std::size_t plane = 0; plane < diagnostic.frustumMargins.size(); ++plane) {
        INFO("frustum plane " << plane);
        CHECK(diagnostic.frustumMargins[plane] > 0.0f);
    }

    // The skinning buffer and the GPU index the draw binds. One rig, so it is the first slice; the
    // offset is a dynamic offset into a uniform buffer and has to be 256-byte aligned or the draw
    // is a validation error rather than a wrong picture.
    const rendering::SkinningRenderer::Slice slice = h.renderer->skinning().slice(h.character.rig);
    CHECK(slice.valid());
    CHECK(slice.jointCount == rig.palette.size());
    CHECK(slice.offset % 256 == 0);
    const rendering::SkinningStats& skinning = h.renderer->skinning().stats();
    CHECK(skinning.rigs == 1);
    CHECK(skinning.draws > 0);
    // Each slice holds this frame's palette and the one it was drawn with last frame, for motion
    // vectors: a buffer sized for one of them is the defect that makes a skinned character's motion
    // blur read from whatever was next to it.
    CHECK(skinning.uploadBytes >= 2 * rig.palette.size() * sizeof(glm::mat4));

    // Animation time, delta and pose version, over frames the clock actually advanced. At this
    // distance the rig has no rate limit, so the pose time is the frame time and the version moves
    // once per frame; a version that stands still while the clip runs is a pose that is not being
    // recomputed, and one that moves twice is a rig being posed by two callers.
    double previousTime = h.rig().paletteTime;
    std::uint64_t previousVersion = h.rig().paletteVersion;
    for (int frame = 0; frame < 12; ++frame) {
        const FrameTime now = h.step();
        INFO("frame " << frame << " at " << now.renderTime << " s");
        CHECK(now.deltaTime > 0.0); // the clock is ticking, not being restarted
        CHECK(h.rig().paletteTime == now.renderTime);
        CHECK(h.rig().paletteTime - previousTime == Catch::Approx(now.deltaTime).margin(1e-9));
        CHECK(h.rig().paletteVersion == previousVersion + 1);
        previousTime = h.rig().paletteTime;
        previousVersion = h.rig().paletteVersion;
    }

    CHECK(h.ctx->errorCount() == 0);
}

// ---- the progression ---------------------------------------------------------------------------
//
// The plan's order: animation off, animation on, culling, shadows, transparency, post. Its value is
// entirely in doing them one at a time -- a frame that misbehaves at the shadow rung with the
// animation rung clean names the subsystem, which is the only thing a whole-renderer A/B can never
// do.
//
// Two things make this honest rather than decorative. First, every rung but culling renders **the
// same scene state**: the engine is stepped once and then the same `FrameTime` is drawn six times
// with six different toggle sets, so a hash that changes changed because of the toggle and not
// because the clip moved on a frame. Second, every rung is asserted to change something *countable*
// -- a draw, a shadow draw, a skinned draw, a byte of joint buffer -- and not merely to change the
// pixels, because "the pixels differ" is where the last investigation started and it is not
// evidence about a subsystem.
//
// The transparency rung is the interesting one and it is reported as two rungs, because on this
// scene the authored character is opaque: switching transparency off must then remove *nothing*,
// and the arm only has teeth once something in the frame is actually blended. Both are here, and
// the first would fail if the control started taking opaque geometry with it.
TEST_CASE("the subsystem progression over one character changes exactly what it names",
          "[gpu][composition][forensics][character5_3]") {
    if (!fs::is_regular_file(sceneFile())) {
        SKIP("the character isolation scene is not present");
    }
    Harness h;
    using Toggles = rendering::SceneRenderer::PassToggles;

    struct Rung {
        std::string name;
        std::uint64_t hash = 0;
        std::uint32_t draws = 0;
        std::uint32_t shadowDraws = 0;
        std::uint32_t skinnedDraws = 0;
        std::uint32_t skinnedRigs = 0;
        std::uint32_t uploadBytes = 0;
        bool submitted = false;
        std::string cullReason;
    };
    std::vector<Rung> table;

    // Draws the scene exactly as it stands, twice: a rung whose own frame will not reproduce is a
    // finding in itself, and checking it here costs one render.
    const auto arm = [&](std::string name, const Toggles& toggles, const FrameTime& time) {
        h.renderer->setPassToggles(toggles);
        const gpu::Image8 first = h.shot(time);
        const Rung rung{std::move(name),
                        gpu::hashImage(first),
                        h.renderer->stats().drawCalls,
                        h.renderer->stats().shadowDraws,
                        h.renderer->skinning().stats().draws,
                        h.renderer->skinning().stats().rigs,
                        h.renderer->skinning().stats().uploadBytes,
                        h.diagnostic().submitted,
                        h.diagnostic().cullReason};
        const gpu::Image8 again = h.shot(time);
        INFO("rung " << rung.name << " did not reproduce its own frame");
        CHECK(gpu::hashImage(again) == rung.hash);
        table.push_back(rung);
        return table.back();
    };

    Toggles bare;
    bare.shadows = false;
    bare.ao = false;
    bare.volume = false;
    bare.post = false;
    bare.transparency = false;
    bare.particles = false;
    bare.water = false;
    bare.animation = false;

    // ---- culling, first, because it is the one rung that needs its own camera -------------------
    //
    // Running it here and reporting it in the plan's order keeps every other rung on one frozen
    // frame. The claim is not that culling changes the picture -- a character that is off screen
    // looks the same whether or not it was submitted -- but that the *verdict* and the draw follow
    // the frustum, and that the character comes back when the camera does. A cull that never
    // released is SYM-ANIM-2.
    Toggles animationOn = bare;
    animationOn.animation = true;
    constexpr double kCullSecond = 2.5;
    aimCamera(h, glm::vec3(0.0f, 1.6f, 4.5f), glm::vec3(0.0f, 1.0f, 0.0f));
    FrameTime at = h.jumpTo(kCullSecond);
    const Rung onScreen = arm("on screen", animationOn, at);
    REQUIRE(onScreen.submitted);

    // Ninety degrees away and well past the character: nothing of it is in the frustum.
    aimCamera(h, glm::vec3(0.0f, 1.6f, 4.5f), glm::vec3(40.0f, 1.0f, 40.0f));
    at = h.step();
    REQUIRE(h.entity().cameraCulled); // the composition's cull actually fired, or this proves nothing
    const Rung culled = arm("culling on  (looking away)", animationOn, at);
    Toggles noCulling = animationOn;
    noCulling.culling = false;
    const Rung uncut = arm("culling off (looking away)", noCulling, at);

    INFO("on screen " << onScreen.draws << " draws, culled " << culled.draws << ", cull off " << uncut.draws);
    CHECK_FALSE(culled.submitted);
    CHECK(culled.cullReason == "camera-frustum");
    // A culled rig is not skinned either -- but only because this rung runs with shadows off. A
    // character behind the camera still casts into the frame, so with the cascade on it is skinned
    // for the shadow pass while contributing no camera draw, which is correct and is a different
    // claim from this one.
    CHECK(culled.skinnedDraws == 0);
    CHECK(uncut.submitted);                // the toggle overrides the verdict...
    CHECK(uncut.cullReason == "submitted");
    CHECK(uncut.draws > culled.draws);     // ...and the draw comes back with it
    CHECK(uncut.skinnedDraws > 0);
    // The pose is not frozen by being culled: `updateRigs` deliberately ignores the camera frustum,
    // because a character whose timeline stops while it is off screen walks out from behind the
    // camera in the wrong phase.
    CHECK(h.rig().paletteTime == at.renderTime);

    // Back to the character, at the second it was drawn at before. This is the recovery half: the
    // frame must be the one it was, not merely a frame.
    aimCamera(h, glm::vec3(0.0f, 1.6f, 4.5f), glm::vec3(0.0f, 1.0f, 0.0f));
    at = h.jumpTo(kCullSecond);
    const Rung restored = arm("on screen again", animationOn, at);
    CHECK(restored.submitted);
    CHECK(restored.hash == onScreen.hash);

    // ---- the ladder, every rung on one frozen frame ---------------------------------------------
    at = h.jumpTo(4.0);   // far enough into the walk cycle that a pose is not a bind pose
    const Rung bindPose = arm("animation off", bare, at);
    const Rung posed = arm("+ animation", animationOn, at);
    Toggles shadows = animationOn;
    shadows.shadows = true;
    const Rung shadowed = arm("+ shadows", shadows, at);
    Toggles transparency = shadows;
    transparency.transparency = true;
    const Rung blendPass = arm("+ transparency", transparency, at);
    Toggles post = transparency;
    post.post = true;
    const Rung posted = arm("+ post", post, at);

    // Animation. The skinned path is the whole of the difference: the same entity, the same draw
    // count, drawn from its rest vertices instead of a palette.
    CHECK(bindPose.submitted);              // "bind pose", not "gone"
    CHECK(bindPose.skinnedDraws == 0);
    // Nothing is uploaded for a pose nothing reads: with the control off the joint buffer is not
    // even refreshed. `skinnedRigs` is deliberately *not* asserted at zero here -- it counts rigs
    // with a palette resident on the GPU, and the control skips the refresh rather than evicting
    // anything, so it holds whatever the last posed frame left. The zeros further down the table
    // are the same idempotence: a palette already uploaded at this version is not uploaded again.
    CHECK(bindPose.uploadBytes == 0);
    CHECK(posed.skinnedDraws > 0);
    CHECK(posed.skinnedRigs == 1);
    CHECK(posed.uploadBytes > 0);           // this frame's palette is new: the rung before skipped it
    CHECK(posed.draws == bindPose.draws);   // a pipeline choice, not an object count
    CHECK(posed.hash != bindPose.hash);

    // Shadows. Countable: the character and the ground are drawn again into the cascade.
    CHECK(bindPose.shadowDraws == 0);
    CHECK(posed.shadowDraws == 0);
    CHECK(shadowed.shadowDraws > 0);
    CHECK(shadowed.draws == posed.draws);   // shadow draws are not camera draws
    CHECK(shadowed.hash != posed.hash);

    // Transparency, with the authored material: the character is opaque, so this rung must be a
    // null arm. It is reported as one rather than quietly skipped -- a control that took opaque
    // geometry away would show up right here.
    CHECK(h.entity().material.alphaMode == scene::AlphaMode::Opaque);
    CHECK(blendPass.draws == shadowed.draws);
    CHECK(blendPass.hash == shadowed.hash);

    // Post. Bloom, grading and the rest are full-screen work: the picture changes and the geometry
    // does not.
    CHECK(posted.draws == blendPass.draws);
    CHECK(posted.shadowDraws == blendPass.shadowDraws);
    CHECK(posted.hash != blendPass.hash);

    // The transparency arm with something to remove. The scene authors no blended material, so one
    // is made here: with the character blended, switching transparency off has to take it out of
    // the frame entirely, and switching it back on has to put it back exactly as it was.
    scene::Composition* composition = h.engine.composition();
    REQUIRE(composition != nullptr);
    composition->scene().entities[h.character.entityIndex].material.alphaMode = scene::AlphaMode::Blend;
    composition->scene().entities[h.character.entityIndex].material.opacity = 0.5f;
    at = h.step();
    REQUIRE(h.entity().material.alphaMode == scene::AlphaMode::Blend); // the update did not undo it
    const Rung blended = arm("blended, transparency on", transparency, at);
    Toggles noTransparency = transparency;
    noTransparency.transparency = false;
    const Rung blendedOff = arm("blended, transparency off", noTransparency, at);
    INFO("blended " << blended.draws << " draws, transparency off " << blendedOff.draws);
    CHECK(blended.skinnedDraws > 0);
    CHECK(blendedOff.skinnedDraws == 0);      // the character is not drawn at all
    CHECK(blendedOff.draws < blended.draws);
    CHECK(blendedOff.hash != blended.hash);
    const Rung blendedBack = arm("blended, transparency on again", transparency, at);
    CHECK(blendedBack.hash == blended.hash);  // and the control releases

    std::string report = "Phase 5.3 progression, one character alone:\n";
    report += fmt::format("{:<30} {:>18} {:>6} {:>7} {:>7} {:>6} {:>8}  {}\n", "rung", "hash", "draws",
                          "shadow", "skinned", "rigs", "bytes", "verdict");
    for (const Rung& rung : table) {
        report += fmt::format("{:<30} {:>18} {:>6} {:>7} {:>7} {:>6} {:>8}  {}\n", rung.name, rung.hash,
                              rung.draws, rung.shadowDraws, rung.skinnedDraws, rung.skinnedRigs,
                              rung.uploadBytes, rung.cullReason);
    }
    WARN(report);

    CHECK(h.ctx->errorCount() == 0);
}

// ---- invalid pose data --------------------------------------------------------------------------
//
// The plan asks for two things here and they are different jobs. The first is to *reject* invalid
// pose data -- NaN and Inf joint matrices, non-unit quaternions, collapsed scales, influences naming
// joints that do not exist or carrying weights that do not sum to one. The second is to capture the
// exact animation, frame and state when one is found, because "a bone matrix was NaN somewhere in a
// thirty-second walk" is not a bug report.
//
// So the sweep runs all three clips through the seconds where a pose is most likely to be wrong --
// the start, a wrap, a cross-fade in flight -- and every failure carries the clip, the state, the
// second, the frame index and the joint. The static influence check runs once, because the
// influences are an import product and do not change per frame.
TEST_CASE("no bone matrix weight or influence on the character is invalid",
          "[gpu][composition][forensics][character5_3]") {
    if (!fs::is_regular_file(sceneFile())) {
        SKIP("the character isolation scene is not present");
    }
    Harness h;
    const scene::SkinnedRig& rig = h.rig();

    // The influences the GPU reads, once. A joint index one past the palette is the classic import
    // defect and it does not announce itself: the shader reads a neighbouring rig's matrix and the
    // character grows an arm out of its head at some other character's pose.
    REQUIRE(h.character.mesh < h.engine.scene().meshes.size());
    const scene::MeshData& mesh = h.engine.scene().meshes[h.character.mesh];
    INFO("mesh '" << mesh.name << "', " << mesh.vertices.size() << " vertices");
    REQUIRE(mesh.skinned());
    REQUIRE(mesh.vertices.size() > 100); // a mesh with a handful of vertices proves nothing
    const std::string influences = influenceFault(mesh, rig.skeleton.paletteSize());
    INFO("influences: " << (influences.empty() ? "ok" : influences));
    CHECK(influences.empty());

    // The bind pose, evaluated the way the rig would evaluate it if no clip were playing. This is
    // the floor: a skeleton whose *rest* palette is already invalid cannot produce a valid animated
    // one, and separating the two is what says whether an asset or the sampler is at fault.
    {
        scene::Pose rest = scene::restPose(rig.skeleton);
        std::vector<glm::mat4> scratch;
        std::vector<glm::mat4> bind;
        scene::skinningPalette(rig.skeleton, rest, scratch, bind);
        REQUIRE(bind.size() == rig.palette.size());
        const std::string fault = paletteFault(bind);
        INFO("bind pose: " << (fault.empty() ? "ok" : fault));
        CHECK(fault.empty());
    }

    // The sweep. Each clip is entered with its phase origin at zero and its own duration is walked
    // past twice, so a wrap is inside the window rather than just after it.
    std::size_t checked = 0;
    std::size_t faults = 0;
    for (const char* state : {"Idle", "Walk", "Run"}) {
        playClip(h, state, 0.0);
        const int clipIndex = rig.findClip(state);
        REQUIRE(clipIndex >= 0);
        const float duration = rig.clips[static_cast<std::size_t>(clipIndex)].duration;
        REQUIRE(duration > 0.0f);
        const int frames = static_cast<int>(std::ceil(2.0 * duration * kFps));
        for (int frame = 0; frame < frames; ++frame) {
            const FrameTime time = h.step();
            // The context every fault report needs, attached before the checks so a failure prints
            // it rather than leaving somebody to guess which of the three clips was running.
            INFO("clip '" << state << "' (" << duration << " s), state '" << rig.player.currentState()
                          << "', frame " << frame << " at " << time.renderTime << " s, clip time "
                          << rig.player.stateTime(rig.clips, time.renderTime));
            const std::string pose = poseFault(rig);
            const std::string palette = paletteFault(rig.palette);
            INFO("pose: " << (pose.empty() ? "ok" : pose) << "; palette: "
                          << (palette.empty() ? "ok" : palette));
            CHECK(pose.empty());
            CHECK(palette.empty());
            faults += pose.empty() && palette.empty() ? 0 : 1;
            ++checked;
        }
    }

    // A cross-fade in flight, which the sweep above never sees: every clip there was entered with a
    // blend of zero. A blend is where a non-unit quaternion comes from -- two rotations averaged
    // componentwise instead of slerped shorten towards the origin -- so the pose is checked on
    // every frame while the player says it is still blending, and the test asserts it really was.
    playClip(h, "Idle", 0.0);
    REQUIRE(h.engine.composition()->setNodeAnimation(h.node(), "Run", h.rig().paletteTime, 0.75f, 1.0f,
                                                     true));
    std::size_t blendingFrames = 0;
    for (int frame = 0; frame < 60; ++frame) {
        const FrameTime time = h.step();
        INFO("cross-fade frame " << frame << " at " << time.renderTime << " s, from '"
                                 << rig.player.fadingState() << "' to '" << rig.player.currentState()
                                 << "' at weight " << rig.player.blendWeight(time.renderTime));
        const std::string pose = poseFault(rig);
        const std::string palette = paletteFault(rig.palette);
        INFO("pose: " << (pose.empty() ? "ok" : pose) << "; palette: " << (palette.empty() ? "ok" : palette));
        CHECK(pose.empty());
        CHECK(palette.empty());
        blendingFrames += rig.player.blending(time.renderTime) ? 1 : 0;
        ++checked;
    }
    INFO(checked << " frames checked over three clips and one cross-fade");
    CHECK(blendingFrames > 20); // the cross-fade was actually in flight for most of that window
    CHECK(faults == 0);
    CHECK(checked > 400);

    // And the renderer's own verdict on the same frames: it refuses a non-finite model matrix, and
    // the diagnostic reports per-object finiteness separately from that.
    const FrameTime time = h.step();
    static_cast<void>(h.shot(time));
    CHECK(h.diagnostic().finite);
    CHECK(h.ctx->errorCount() == 0);
}

// ---- the pose is a property of the timeline -----------------------------------------------------
//
// This is the property the animation phase-origin repair established, re-asserted on the isolation
// scene and extended across the transport operations Phase 5.3 lists: bind pose, idle, walk, run,
// loop, pause, resume, seek, scrub, reverse, a near and a far camera, and a reload.
//
// The claim, in one line: **the pose at a second is a function of the piece, not of how the playhead
// got there.** Everything below is one route to a second compared against an engine that has been
// nowhere else. A character whose walk cycle depends on playback history is a character that flicks
// to a different pose when somebody scrubs, which is exactly what the original report described.
//
// Palettes are compared rather than images throughout. "The pixels differ" is where the last
// investigation started; "n of 49 joint matrices differ and no entity transform does" is where it
// became a fix.
TEST_CASE("the character's pose is a pure function of the timeline",
          "[gpu][composition][forensics][character5_3]") {
    if (!fs::is_regular_file(sceneFile())) {
        SKIP("the character isolation scene is not present");
    }
    Harness h;

    // An engine that has been nowhere: loaded, given the clip, seeked straight to the second asked
    // for. No device behind it, because a palette is arithmetic.
    const auto fresh = [](const char* state, double seconds) {
        Harness reference(false);
        playClip(reference, state, 0.0);
        reference.jumpTo(seconds);
        return reference.rig().palette;
    };

    SECTION("the bind pose is the same picture at every second") {
        // What `PassToggles::animation = false` draws is the mesh's own vertices, which do not know
        // what time it is. Two seconds far apart must therefore give the identical frame -- and the
        // same two seconds with animation on must not, or the scene is not animating and the claim
        // is about nothing.
        rendering::SceneRenderer::PassToggles toggles;
        toggles.animation = false;
        h.renderer->setPassToggles(toggles);
        const std::uint64_t early = gpu::hashImage(h.shot(h.jumpTo(1.0)));
        const std::uint64_t late = gpu::hashImage(h.shot(h.jumpTo(5.0)));
        CHECK(early == late);

        toggles.animation = true;
        h.renderer->setPassToggles(toggles);
        const std::uint64_t posedEarly = gpu::hashImage(h.shot(h.jumpTo(1.0)));
        const std::uint64_t posedLate = gpu::hashImage(h.shot(h.jumpTo(5.0)));
        CHECK(posedEarly != posedLate);
        CHECK(posedEarly != early);
        CHECK(h.ctx->errorCount() == 0);
    }

    SECTION("idle walk and run each land the same pose played to or seeked to") {
        for (const char* state : {"Idle", "Walk", "Run"}) {
            INFO("clip " << state);
            playClip(h, state, 0.0);
            REQUIRE(h.rig().paletteTime == 0.0);

            // Played there, one ticked frame at a time.
            h.steps(150);
            REQUIRE(h.rig().paletteTime == Approx(2.5).margin(1e-9));
            const std::vector<glm::mat4> played = h.rig().palette;
            const std::vector<glm::mat4> reference = fresh(state, 2.5);
            REQUIRE(reference.size() > 20);
            INFO("played to 2.5 s: " << jointsDiffering(played, reference) << " of " << reference.size()
                                     << " joints differ from a fresh seek");
            CHECK(jointsDiffering(played, reference) == 0);

            // Seeked there, from somewhere else entirely.
            h.jumpTo(7.5);
            INFO("seeked to 7.5 s: " << jointsDiffering(h.rig().palette, fresh(state, 7.5))
                                     << " joints differ");
            CHECK(jointsDiffering(h.rig().palette, fresh(state, 7.5)) == 0);

            // The clip really is doing something between those two seconds, or both comparisons
            // above would hold for a rig that never moved.
            CHECK(jointsDiffering(played, h.rig().palette) > 10);
        }
    }

    SECTION("a looping clip comes back to the same pose one period later") {
        for (const char* state : {"Idle", "Walk", "Run"}) {
            const int index = h.rig().findClip(state);
            REQUIRE(index >= 0);
            const double period = h.rig().clips[static_cast<std::size_t>(index)].duration;
            REQUIRE(period > 0.0);
            REQUIRE(h.rig().player.findState(state)->loop);
            playClip(h, state, 0.0);

            h.jumpTo(3.0);
            const std::vector<glm::mat4> here = h.rig().palette;
            h.jumpTo(3.0 + period);
            const float aPeriodLater = paletteDistance(here, h.rig().palette);
            h.jumpTo(3.0 + 0.5 * period);
            const float halfAPeriodLater = paletteDistance(here, h.rig().palette);
            // Not exact equality: the two sides reach the phase by different arithmetic (one wraps
            // through the clip's duration, the other does not) and the clip's own keys are floats.
            // Model space for this asset is about a hundred units across, so a thousandth of a unit
            // is far below anything a frame could show.
            INFO(state << ": " << aPeriodLater << " apart one period later, " << halfAPeriodLater
                       << " apart half a period later");
            CHECK(aPeriodLater < 1e-3f);
            CHECK(halfAPeriodLater > 1e-2f); // ...and half a period is a genuinely different pose
        }
    }

    SECTION("parking the playhead does not move the pose and resuming does not re-phase it") {
        // The transport really does change state, so a pause that quietly restarted a clip would be
        // visible here. Offline the fixed-step clock is the authority over the render time, which is
        // what poses a rig, so "paused" for this test means what it means to a host that has stopped
        // asking for new frame times: the same second, over and over.
        REQUIRE(h.engine.play().has_value());
        CHECK(h.engine.transport().isPlaying());
        h.steps(60);
        h.engine.pause();
        CHECK_FALSE(h.engine.transport().isPlaying());

        h.jumpTo(5.0);
        const std::vector<glm::mat4> parked = h.rig().palette;
        const std::uint64_t parkedVersion = h.rig().paletteVersion;
        for (int repeat = 0; repeat < 8; ++repeat) {
            h.jumpTo(5.0);
            INFO("held at 5 s, repeat " << repeat);
            CHECK(jointsDiffering(parked, h.rig().palette) == 0);
        }
        // A pose that is *re-evaluated* every frame while the playhead is parked is still correct;
        // one that is *integrated* would drift. What is needed here is a witness that the rig is
        // live rather than frozen, so the equality above is a real result and not a rig nobody
        // touched.
        //
        // `paletteVersion` used to be that witness, and it stopped being one when a genuine defect
        // was fixed. `hold()` bumps the version only when `previousPalette != palette` -- its job is
        // to push the now-equal previous palette to the GPU once. Before the repair, a seek left the
        // previous palette holding the pre-jump pose, so every parked frame found them unequal and
        // bumped. Now a seek reseeds it, they are equal on arrival, and there is correctly nothing
        // to re-upload. The old check was reading the defect as liveness.
        //
        // The witness that does not depend on it: the rig is sampled at the second it was asked
        // for, and moving that second moves the pose and the version. A frozen rig fails both.
        CHECK(h.rig().paletteTime == Approx(5.0).margin(1e-9));
        {
            const std::uint64_t heldVersion = h.rig().paletteVersion;
            const std::vector<glm::mat4> heldPose = h.rig().palette;
            h.jumpTo(5.5);
            CHECK(h.rig().paletteVersion > heldVersion);
            CHECK(jointsDiffering(heldPose, h.rig().palette) > 0);
            h.jumpTo(5.0);
            CHECK(jointsDiffering(parked, h.rig().palette) == 0);
        }
        static_cast<void>(parkedVersion);
        CHECK(jointsDiffering(parked, fresh("Walk", 5.0)) == 0);

        REQUIRE(h.engine.play().has_value());
        CHECK(h.engine.transport().isPlaying());
        h.steps(60); // resumed: one second of ordinary playback from where it was parked
        CHECK(h.rig().paletteTime == Approx(6.0).margin(1e-9));
        CHECK(jointsDiffering(h.rig().palette, fresh("Walk", 6.0)) == 0);
    }

    SECTION("scrubbing back and forth lands the pose the second owns") {
        double at = 3.0;
        for (int i = 0; i < 30; ++i) {
            at = std::clamp(at + (i % 4 == 0 ? -0.37 : 0.19), 0.0, kPieceSeconds);
            h.jumpTo(at);
        }
        for (const double seconds : {1.0, 4.25, 9.0, 0.0}) {
            h.jumpTo(seconds);
            INFO("after a scrub, at " << seconds << " s: "
                                      << jointsDiffering(h.rig().palette, fresh("Walk", seconds))
                                      << " joints differ");
            CHECK(jointsDiffering(h.rig().palette, fresh("Walk", seconds)) == 0);
        }
    }

    SECTION("run backwards second by second and then forwards over the same seconds") {
        // Reverse, as an editor performs it: the playhead is dragged the other way. Recording the
        // descent and replaying the ascent over the identical seconds catches a direction-dependent
        // pose with one engine, which matters because a fresh reference per second would mean
        // thirty-odd glTF loads to answer one question.
        std::vector<std::pair<double, std::vector<glm::mat4>>> descending;
        for (double seconds = 8.0; seconds >= 0.0; seconds -= 0.25) {
            h.jumpTo(seconds);
            descending.emplace_back(seconds, h.rig().palette);
        }
        REQUIRE(descending.size() > 30);
        std::size_t compared = 0;
        for (auto entry = descending.rbegin(); entry != descending.rend(); ++entry) {
            h.jumpTo(entry->first);
            INFO("at " << entry->first << " s, reached backwards then forwards");
            CHECK(jointsDiffering(entry->second, h.rig().palette) == 0);
            ++compared;
        }
        CHECK(compared == descending.size());
        // Anchored against engines that have been nowhere, so a self-consistent-but-wrong sweep
        // cannot pass: the loop above would hold for a rig frozen at one pose.
        for (const double seconds : {0.0, 2.0, 8.0}) {
            INFO("anchor at " << seconds << " s");
            h.jumpTo(seconds);
            CHECK(jointsDiffering(h.rig().palette, fresh("Walk", seconds)) == 0);
        }
        CHECK(jointsDiffering(descending.front().second, descending.back().second) > 10); // it moved
    }

    SECTION("a near and a far camera pose the same character on the same grid") {
        // The rig's distance policy (`nearDistance`, `farHz`, `cullDistance`) is the one thing that
        // makes the pose depend on where the camera is, and it is built so that it *still* does not
        // depend on when the frame is: the sample time is floor(t * hz) / hz, a grid on the
        // timeline rather than a frame counter. So the far camera changes which seconds are sampled
        // and never which pose a sampled second has.
        const scene::SkinnedRig& rig = h.rig();
        REQUIRE(rig.rateFor(4.0f) == 0.0f);              // near: every frame
        REQUIRE(rig.rateFor(40.0f) == rig.farHz);        // between: the reduced rate
        REQUIRE(rig.rateFor(300.0f) < 0.0f);             // beyond cullDistance: not posed at all
        REQUIRE(rig.farHz > 0.0f);

        aimCamera(h, glm::vec3(0.0f, 1.6f, 4.5f), glm::vec3(0.0f, 1.0f, 0.0f));
        for (int frame = 0; frame < 10; ++frame) {
            const FrameTime time = h.step();
            INFO("near, frame " << frame);
            CHECK(h.rig().paletteTime == time.renderTime); // no grid at all up close
            CHECK_FALSE(h.entity().cameraCulled);
        }

        aimCamera(h, glm::vec3(0.0f, 1.6f, 40.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        std::size_t distinctSampleTimes = 0;
        double previous = -1.0;
        for (int frame = 0; frame < 20; ++frame) {
            const FrameTime time = h.step();
            INFO("far, frame " << frame << " at " << time.renderTime << " s");
            CHECK(h.rig().paletteTime ==
                  Approx(scene::SkinnedRig::sampleTime(time.renderTime, rig.farHz)).margin(1e-9));
            CHECK(h.rig().paletteTime <= time.renderTime); // a grid samples the past, never the future
            distinctSampleTimes += h.rig().paletteTime == previous ? 0 : 1;
            previous = h.rig().paletteTime;
            const gpu::Image8 image = h.shot(time);
            CHECK(gpu::hashImage(image) != 0);
            CHECK_FALSE(h.entity().cameraCulled); // forty metres away and still on screen
            CHECK(h.diagnostic().submitted);
        }
        // At 60 fps on a 20 Hz grid, twenty frames hold about seven distinct sample times. The
        // check is that there are several and not twenty: one per frame would mean the rate limit
        // never engaged and the whole section tested the near case twice.
        INFO(distinctSampleTimes << " distinct sample times over 20 frames at " << rig.farHz << " Hz");
        CHECK(distinctSampleTimes > 2);
        CHECK(distinctSampleTimes < 20);

        // Past the cull distance the rig holds: its palette is left exactly as it is, which is a
        // deliberate policy and not a stall. The interesting half is that it *releases*.
        aimCamera(h, glm::vec3(0.0f, 1.6f, 300.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        h.step();
        const std::vector<glm::mat4> held = h.rig().palette;
        const std::uint64_t heldVersion = h.rig().paletteVersion;
        const double heldTime = h.rig().paletteTime;
        for (int frame = 0; frame < 10; ++frame) {
            h.step();
            INFO("beyond the cull distance, frame " << frame);
            CHECK(h.rig().paletteVersion == heldVersion);
            CHECK(h.rig().paletteTime == heldTime);
            CHECK(jointsDiffering(held, h.rig().palette) == 0);
        }

        aimCamera(h, glm::vec3(0.0f, 1.6f, 4.5f), glm::vec3(0.0f, 1.0f, 0.0f));
        h.jumpTo(9.0);
        CHECK(h.rig().paletteTime == 9.0);
        CHECK(jointsDiffering(h.rig().palette, fresh("Walk", 9.0)) == 0);
    }

    SECTION("a reload returns the same pose and the same frame") {
        h.jumpTo(6.25);
        const std::vector<glm::mat4> before = h.rig().palette;
        const std::uint64_t frameBefore = gpu::hashImage(h.shot(h.jumpTo(6.25)));

        for (int attempt = 0; attempt < 2; ++attempt) {
            INFO("reload " << attempt);
            h.reload();
            h.jumpTo(6.25);
            CHECK(jointsDiffering(before, h.rig().palette) == 0);
            CHECK(jointsDiffering(h.rig().palette, fresh("Walk", 6.25)) == 0);
            CHECK(gpu::hashImage(h.shot(h.jumpTo(6.25))) == frameBefore);
        }
        CHECK(h.ctx->errorCount() == 0);
    }
}
