// HIST (Effect Library Wave 1): per-owner transform history on the simulation's step instants, and
// the Trail that reads it through RIBBON.
//
// The claim that matters is the ADR-700 one: a scrub to second N holds exactly the history a play to
// N recorded. It is only true if the bank is (a) recorded by the seek replay at every step, as a play
// records it at every frame, and (b) carried in the checkpoint the replay restores from. So the
// play/scrub arms below compare EVERY sample of the ring, bitwise, after a scrub that restored from a
// checkpoint (not only after a replay from zero, which would pass with (b) missing). The Glowmere arm
// is the roadmap's: the `visitor` saucer, which the director flies, at 150 s -- past the old ninety-
// second window.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "organism/mushroom.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/procedural.hpp"
#include "scene/tree_generated.hpp"
#include "signals/signal_bus.hpp"
#include "world/effects/effect_registry.hpp"
#include "world/effects/effect_stack.hpp"
#include "world/effects/history_bank.hpp"
#include "world/effects/ribbon_frame.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
namespace fs = std::filesystem;

namespace {

constexpr double kStep = 1.0 / 60.0;

void record(world::HistoryBank& bank, std::size_t ring, double t, glm::vec3 p) {
    bank.record(ring, t, p, glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
}

// Every sample of every ring, bit for bit: the digest a play and a scrub must share.
std::string digest(const world::HistoryBank& bank) {
    std::ostringstream o;
    o << std::hexfloat;
    for (std::size_t r = 0; r < bank.ringCount(); ++r) {
        o << bank.ringNode(r) << ':' << bank.sampleCount(r) << '\n';
        for (std::size_t i = 0; i < bank.sampleCount(r); ++i) {
            const world::HistorySample& s = bank.sample(r, i);
            o << s.t << ' ' << s.position.x << ',' << s.position.y << ',' << s.position.z << ' ' << s.rotation.w
              << ',' << s.rotation.x << ',' << s.rotation.y << ',' << s.rotation.z << ' ' << s.scale.x << '\n';
        }
    }
    return o.str();
}

} // namespace

TEST_CASE("a history ring keeps its depth and one sample past it, and reads between samples",
          "[hist][effects]") {
    world::HistoryBank bank;
    const world::HistorySubscription subs[] = {{"craft", 0.5f}};
    REQUIRE(bank.subscribe(subs));
    CHECK_FALSE(bank.subscribe(subs)); // the same set is not a change (and does not drop checkpoints)
    REQUIRE(bank.ringCount() == 1);

    // A body moving at 6 m/s along +X, sampled on the grid for two seconds.
    for (int k = 0; k <= 120; ++k) {
        record(bank, 0, k * kStep, glm::vec3(6.0f * static_cast<float>(k * kStep), 1.0f, 0.0f));
    }
    // Half a second is thirty steps; the ring keeps exactly one sample at or before the cut.
    const std::size_t n = bank.sampleCount(0);
    CHECK(n == 31);
    CHECK(bank.sample(0, 0).t <= 2.0 - 0.5 + 1e-9);
    CHECK(bank.sample(0, 1).t > 2.0 - 0.5);

    world::HistorySample s;
    REQUIRE(bank.sampleAt("craft", 1.75 + kStep * 0.5, s));
    CHECK_THAT(s.position.x, WithinAbs(6.0 * (1.75 + kStep * 0.5), 1e-4));
    CHECK_FALSE(bank.sampleAt("craft", 1.0, s)); // older than the ring holds: refused, not clamped
    CHECK_FALSE(bank.sampleAt("craft", 2.5, s)); // newer than the newest sample

    glm::vec3 v(0.0f);
    REQUIRE(bank.velocity("craft", v));
    CHECK_THAT(v.x, WithinRel(6.0f, 1e-4f));
    glm::vec3 a(1.0f);
    REQUIRE(bank.acceleration(0, a));
    CHECK_THAT(glm::length(a), WithinAbs(0.0, 1e-2));

    SECTION("the same instant replaces, an earlier one starts again") {
        record(bank, 0, 2.0, glm::vec3(99.0f));
        CHECK(bank.sampleCount(0) == n);
        CHECK(bank.sample(0, n - 1).position.x == 99.0f);
        record(bank, 0, 0.25, glm::vec3(1.0f));
        CHECK(bank.sampleCount(0) == 1);
    }
    SECTION("a snapshot restores into a cleared bank exactly") {
        const std::string before = digest(bank);
        const world::HistoryBank::Snapshot snap = bank.snapshot();
        bank.clear();
        CHECK(bank.sampleCount(0) == 0);
        bank.restore(snap);
        CHECK(digest(bank) == before);
    }
    SECTION("the key moves with the subscription, so stale checkpoints drop") {
        const std::uint64_t k0 = bank.key();
        const world::HistorySubscription deeper[] = {{"craft", 2.0f}};
        REQUIRE(bank.subscribe(deeper));
        CHECK(bank.key() != k0);
        // A re-subscription keeps what it had.
        CHECK(bank.sampleCount(0) == n);
    }
}

namespace {

// ---- a scripted scene: one craft orbiting (a simulation integrator) with a drift on top -------------

constexpr const char* kScene = R"({ "format": "avgen-scene", "version": 1, "name": "hist-fixture",
  "nodes": [ { "kind": "orb", "name": "craft", "position": [0, 6, 0] },
             { "kind": "orb", "name": "marker", "position": [30, 0, 0] } ],
  "entities": [ { "name": "craft", "node": "craft", "seed": 7,
                  "behaviors": [ { "kind": "orbit", "radius": 15.0, "rate": 20.0, "authority": "simulation" },
                                 { "kind": "drift", "radius": 1.5, "rate": 0.4 } ] } ] })";

fs::path writeScene() {
    const fs::path dir = fs::temp_directory_path() / ("avgen_hist_" + std::to_string(getpid()));
    fs::create_directories(dir);
    std::ofstream(dir / "hist.json") << kScene;
    return dir;
}

struct Rig {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    std::unique_ptr<scene::Composition> comp;
    world::HistoryBank bank;
    long long frame = 0;

    Rig(const fs::path& dir, const char* file, std::vector<world::HistorySubscription> subs) : registry(dir) {
        auto loaded = scene::Composition::loadFile(file, registry);
        REQUIRE(loaded.has_value());
        comp = std::move(*loaded);
        for (const char* name : {"audio.bass", "audio.mid", "audio.rms", "audio.treble"}) {
            bus.declare(name, 0.0f, 1.0f);
        }
        bus.declare("audio.onset", 0.0f, 1.0f, true);
        comp->attach(params, modulator);
        comp->setViewport(640, 360);
        comp->scene().detailLimits.entityDistanceCull = false;
        REQUIRE(bank.subscribe(subs));
        comp->setHistoryBank(&bank);
    }
    void tick() {
        FrameTime time;
        time.renderTime = static_cast<double>(frame) / 60.0;
        time.deltaTime = frame == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(frame);
        params.resetFinals();
        comp->updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp->updateBehaviour(time, bus);
        comp->update(time);
        comp->recordHistory(bank, time.renderTime); // what Engine::update does after the controller
        ++frame;
    }
    void playTo(double seconds) {
        const auto last = static_cast<long long>(std::llround(seconds * 60.0));
        while (frame <= last) {
            tick();
        }
    }
    void seekTo(double seconds) {
        comp->seekWithDirector(seconds, params,
                               entity::SeekBudget{.maxSeconds = 90.0,
                                                  .maxBodySteps = entity::SeekBudget::kEditorBodySteps,
                                                  .mode = entity::SeekMode::Checkpointed},
                               1.0 / 60.0);
        frame = static_cast<long long>(std::llround(seconds * 60.0)) + 1;
    }
};

} // namespace

TEST_CASE("a scrubbed transform history is the played one, sample for sample, from a checkpoint too",
          "[hist][seek][determinism][adr700]") {
    const fs::path dir = writeScene();
    const std::vector<world::HistorySubscription> subs{{"craft", 1.5f}, {"marker", 0.25f}};
    constexpr double kLate = 37.5;

    Rig played(dir, "hist.json", subs);
    played.playTo(kLate);
    const std::string play = digest(played.bank);
    // The control that the digest is about something: the craft moved, at the orbit's speed.
    glm::vec3 v(0.0f);
    REQUIRE(played.bank.velocity("craft", v));
    INFO("played velocity " << v.x << "," << v.y << "," << v.z);
    CHECK(glm::length(v) > 3.0f);
    REQUIRE(played.bank.sampleCount(0) > 80); // 1.5 s at 60 Hz

    SECTION("a scrub replayed from zero") {
        Rig scrubbed(dir, "hist.json", subs);
        scrubbed.seekTo(kLate);
        CHECK(scrubbed.comp->entityWorld().lastSeekWork().exact);
        CHECK(scrubbed.comp->entityWorld().lastSeekWork().restoredFrom < 0.0);
        CHECK(digest(scrubbed.bank) == play);
    }
    SECTION("a scrub restored from a checkpoint after being somewhere else") {
        Rig scrubbed(dir, "hist.json", subs);
        scrubbed.seekTo(60.0);    // records a checkpoint every second, and leaves the bank at 60 s
        scrubbed.playTo(61.0);    // and ordinary frames on top
        scrubbed.seekTo(kLate);
        const auto work = scrubbed.comp->entityWorld().lastSeekWork();
        INFO("restored from " << work.restoredFrom << " s");
        REQUIRE(work.restoredFrom >= kLate - 1.0);
        CHECK(work.exact);
        CHECK(digest(scrubbed.bank) == play);
        // And the frames after it agree too: the history keeps growing identically.
        played.playTo(kLate + 0.5);
        scrubbed.playTo(kLate + 0.5);
        CHECK(digest(scrubbed.bank) == digest(played.bank));
    }
    SECTION("control: the digest tells one step from the next") {
        Rig later(dir, "hist.json", subs);
        later.seekTo(kLate + kStep);
        CHECK(digest(later.bank) != play);
    }
    fs::remove_all(dir);
}

TEST_CASE("a scene with nothing subscribed replays exactly as it did before HIST",
          "[hist][seek][checkpoint][adr700]") {
    // The gate's CPU half: with no bank, or an empty one, `seekWithDirector` builds the hooks it
    // always built, so the checkpoint key -- and so every existing checkpoint -- is unchanged.
    const fs::path dir = writeScene();
    Rig with(dir, "hist.json", {{"craft", 1.0f}});
    with.comp->setHistoryBank(nullptr);
    with.seekTo(5.0);
    const std::uint64_t keyNone = with.comp->entityWorld().checkpointStats().count;
    world::HistoryBank empty;
    with.comp->setHistoryBank(&empty);
    with.seekTo(4.5);
    // Same key: the second seek restored from the first one's checkpoints rather than dropping them.
    CHECK_FALSE(with.comp->entityWorld().lastSeekWork().invalidated);
    CHECK(with.comp->entityWorld().lastSeekWork().restoredFrom == 4.0);
    CHECK(keyNone == 5);
    // ...and a real subscription does drop them: its history is in none of them.
    world::HistoryBank real;
    const world::HistorySubscription subs[] = {{"craft", 1.0f}};
    REQUIRE(real.subscribe(subs));
    with.comp->setHistoryBank(&real);
    with.seekTo(4.5);
    CHECK(with.comp->entityWorld().lastSeekWork().invalidated);
    fs::remove_all(dir);
}

// ---- the roadmap's arm: the Glowmere saucer at 150 s -------------------------------------------------

namespace {

fs::path film() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.scene.json";
}
bool filmAssetsPresent() {
    return fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "farm" / "cow.glb") &&
           fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb");
}

} // namespace

TEST_CASE("Glowmere: the visitor's history at 150 s is the same played and scrubbed",
          "[hist][seek][determinism][glowmere][adr700]") {
    if (!filmAssetsPresent()) {
        SKIP("farm or alien assets missing");
    }
    organism::registerMushroomGenerator();
    scene::registerTreeGenerator();
    const std::vector<world::HistorySubscription> subs{{"visitor", 2.5f}};
    const fs::path dir = film().parent_path();
    const std::string file = film().filename().string();

    Rig played(dir, file.c_str(), subs);
    played.playTo(150.0);
    const std::string play = digest(played.bank);
    // The craft is a moving owner at this second (the director flies it), or this proves nothing.
    const std::size_t ring = played.bank.find("visitor");
    REQUIRE(ring < played.bank.ringCount());
    REQUIRE(played.bank.sampleCount(ring) > 100);
    const glm::vec3 head = played.bank.sample(ring, played.bank.sampleCount(ring) - 1).position;
    const glm::vec3 tail = played.bank.sample(ring, 0).position;
    INFO("the visitor moved " << glm::length(head - tail) << " m over the ring");

    Rig scrubbed(dir, file.c_str(), subs);
    scrubbed.seekTo(150.0);
    CHECK(scrubbed.comp->entityWorld().lastSeekWork().exact);
    CHECK(digest(scrubbed.bank) == play);

    // And through the director's checkpoint: somewhere later first, then back, so the history
    // comes out of the checkpoint at 149 s rather than out of a replay from zero.
    scrubbed.seekTo(156.0);
    scrubbed.seekTo(150.0);
    const auto work = scrubbed.comp->entityWorld().lastSeekWork();
    INFO("restored from " << work.restoredFrom << " s");
    REQUIRE(work.restoredFrom >= 149.0);
    CHECK(work.exact);
    CHECK(digest(scrubbed.bank) == play);
}

// ---- Trail through RIBBON, on the CPU ----------------------------------------------------------------

namespace {

// A scene query that answers for one node, standing where the ring's newest sample is.
class OneNode final : public world::EffectSceneQuery {
public:
    explicit OneNode(glm::vec3 p) : p_(p) {}
    [[nodiscard]] bool nodePosition(std::string_view, glm::vec3& out) const override {
        out = p_;
        return true;
    }
    [[nodiscard]] bool nodeView(std::string_view, world::NodeView& out) const override {
        out = world::NodeView{};
        out.world = glm::mat4(1.0f);
        out.world[3] = glm::vec4(p_, 1.0f);
        return true;
    }

private:
    glm::vec3 p_;
};

world::EffectInstance trailOn(const std::string& owner) {
    world::EffectInstance e = world::makeEffect(world::EffectKind::Trail, "Trail");
    e.owner = world::EffectOwner::entity(owner);
    e.timing = world::Timing{};
    e.timing.fadeIn = 0.0;
    e.timing.fadeOut = 0.0;
    return e;
}

} // namespace

TEST_CASE("a Trail builds one strip through its owner's history, and says why when it does not",
          "[trail][ribbon][effects]") {
    world::HistoryBank bank;
    const world::HistorySubscription subs[] = {{"craft", 3.0f}};
    REQUIRE(bank.subscribe(subs));
    const auto path = [](double t) { return glm::vec3(10.0f * std::cos(static_cast<float>(t)), 4.0f,
                                                      10.0f * std::sin(static_cast<float>(t))); };
    for (int k = 0; k <= 180; ++k) {
        record(bank, 0, k * kStep, path(k * kStep));
    }
    const OneNode scene(path(3.0));
    world::EffectContext ctx;
    ctx.seconds = 3.0;
    ctx.scene = &scene;

    std::vector<world::EffectInstance> effects{trailOn("craft")};
    std::vector<world::EffectStatus> status(1, world::EffectStatus::Dormant);
    std::vector<std::string> reasons(1);
    world::RibbonFrame frame;
    world::buildRibbonFrame(effects, ctx, bank, frame, {}, status, reasons);
    REQUIRE(frame.strips.size() == 1);
    CHECK(status[0] == world::EffectStatus::Drawn);
    CHECK(frame.strips[0].vertexCount >= 20);
    CHECK(frame.strips[0].vertexCount % 2 == 0);
    // The head is the owner, now; the tail is `length` seconds back along the path.
    const glm::vec3 head(frame.vertices.front().positionSide);
    const glm::vec3 tail(frame.vertices.back().positionSide);
    CHECK(glm::length(head - path(3.0)) < 1e-4f);
    const float length = effects[0].values.getFloat("trail/length", 0.0f);
    REQUIRE(length > 0.0f);
    CHECK(glm::length(tail - path(3.0 - static_cast<double>(length))) < 1e-3f);
    // Tapered and faded: the tail is narrower and more transparent than the head.
    CHECK(frame.vertices.back().tangentWidth.w < frame.vertices.front().tangentWidth.w);
    CHECK(frame.vertices.back().color.a < frame.vertices.front().color.a);
    // The records hook -- what the conformance probe asks -- agrees with the builder.
    const world::EffectSchema* schema = world::effectSchema(world::EffectKind::Trail);
    REQUIRE(schema != nullptr);
    REQUIRE(schema->resolve.records != nullptr);
    CHECK(schema->resolve.records(effects[0], ctx) == 1);

    SECTION("the same world points from one frame to the next: nothing swims") {
        // A frame later the interior control points are the same instants, so every strip vertex
        // that is not the head, the first interior point or the tail cut is where it was.
        world::RibbonFrame next;
        record(bank, 0, 3.0 + kStep, path(3.0 + kStep));
        const OneNode moved(path(3.0 + kStep));
        world::EffectContext later = ctx;
        later.seconds = 3.0 + kStep;
        later.scene = &moved;
        world::buildRibbonFrame(effects, later, bank, next, {}, status, reasons);
        REQUIRE(next.strips.size() == 1);
        std::size_t shared = 0;
        for (const world::RibbonVertex& a : frame.vertices) {
            for (const world::RibbonVertex& b : next.vertices) {
                if (glm::length(glm::vec3(a.positionSide) - glm::vec3(b.positionSide)) < 1e-5f &&
                    a.positionSide.w == b.positionSide.w) {
                    ++shared;
                    break;
                }
            }
        }
        INFO(shared << " of " << frame.vertices.size() << " vertices kept their place");
        CHECK(shared * 10 >= frame.vertices.size() * 7);
    }
    SECTION("disabled, outside its window, or with an owner that never moved") {
        effects[0].enabled = false;
        world::buildRibbonFrame(effects, ctx, bank, frame, {}, status, reasons);
        CHECK(frame.strips.empty());
        CHECK(status[0] == world::EffectStatus::Disabled);

        effects[0].enabled = true;
        effects[0].activation = world::Activation::Window;
        effects[0].timing.windowStart = 10.0;
        effects[0].timing.windowSeconds = 2.0;
        world::buildRibbonFrame(effects, ctx, bank, frame, {}, status, reasons);
        CHECK(frame.strips.empty());
        CHECK(status[0] == world::EffectStatus::Dormant);
        CHECK(schema->resolve.records(effects[0], ctx) == 0);

        effects[0].activation = world::Activation::Always;
        world::HistoryBank still;
        REQUIRE(still.subscribe(subs));
        for (int k = 0; k <= 180; ++k) {
            record(still, 0, k * kStep, glm::vec3(1.0f, 2.0f, 3.0f));
        }
        const OneNode parked(glm::vec3(1.0f, 2.0f, 3.0f));
        world::EffectContext parkedCtx = ctx;
        parkedCtx.scene = &parked;
        world::buildRibbonFrame(effects, parkedCtx, still, frame, {}, status, reasons);
        CHECK(frame.strips.empty());
        CHECK(status[0] == world::EffectStatus::Dormant);
    }
    SECTION("an activation that opens late grows the trail out of its owner") {
        effects[0].activation = world::Activation::Window;
        effects[0].timing.windowStart = 2.9;
        effects[0].timing.windowSeconds = 5.0;
        world::buildRibbonFrame(effects, ctx, bank, frame, {}, status, reasons);
        REQUIRE(frame.strips.size() == 1);
        const glm::vec3 cut(frame.vertices.back().positionSide);
        CHECK(glm::length(cut - path(2.9)) < 1e-3f);
    }
}

TEST_CASE("the ribbon budget reduces detail before it refuses, and says which",
          "[trail][ribbon][effects]") {
    world::HistoryBank bank;
    const world::HistorySubscription subs[] = {{"craft", 8.5f}};
    REQUIRE(bank.subscribe(subs));
    for (int k = 0; k <= 600; ++k) {
        const double t = k * kStep;
        record(bank, 0, t, glm::vec3(20.0f * std::cos(static_cast<float>(t * 3.0)), 0.0f,
                                     20.0f * std::sin(static_cast<float>(t * 2.0))));
    }
    world::EffectInstance e = trailOn("craft");
    e.values.setFloat("trail/length", 8.0f);
    e.values.setFloat("trail/smoothing", 8.0f);
    const OneNode scene(bank.sample(0, bank.sampleCount(0) - 1).position);
    world::EffectContext ctx;
    ctx.seconds = 10.0;
    ctx.scene = &scene;

    // Enough trails on one owner to overrun 65,536 vertices at full detail.
    std::vector<world::EffectInstance> effects;
    for (int i = 0; i < 120; ++i) {
        REQUIRE(world::insertEffect(effects, e).has_value());
    }
    std::vector<world::EffectStatus> status(effects.size(), world::EffectStatus::Dormant);
    std::vector<std::string> reasons(effects.size());
    world::RibbonFrame frame;
    world::buildRibbonFrame(effects, ctx, bank, frame, {}, status, reasons);
    CHECK(frame.vertices.size() <= world::kRibbonVertexBudget);
    std::size_t drawn = 0;
    std::size_t partial = 0;
    std::size_t dropped = 0;
    for (std::size_t i = 0; i < effects.size(); ++i) {
        drawn += status[i] == world::EffectStatus::Drawn ? 1u : 0u;
        partial += status[i] == world::EffectStatus::Partial ? 1u : 0u;
        dropped += status[i] == world::EffectStatus::Dropped ? 1u : 0u;
        if (status[i] == world::EffectStatus::Partial || status[i] == world::EffectStatus::Dropped) {
            CHECK_FALSE(reasons[i].empty());
        }
    }
    INFO(drawn << " drawn, " << partial << " reduced, " << dropped << " dropped, " << frame.vertices.size()
               << " vertices");
    CHECK(drawn > 0);
    CHECK(partial > 0);
    CHECK(dropped > 0);
    CHECK(frame.dropped == dropped);
    CHECK(drawn + partial + dropped == effects.size());
}
