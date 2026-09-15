// Is the animal inside the beam, and does the beam start where the craft is? (ADR-218)
//
// Two reports, both about the same five seconds of film, and both answered here as numbers rather
// than as impressions:
//
//   1. "make sure the animal being abducted is fully in the visitor beam when it happens - right now
//      its misaligned". *Fully in* is made arithmetic: every world-space corner of every mesh the
//      animal draws, on every frame of the lift, inside the beam's own emitter radius -- the corners
//      themselves rather than the axis-aligned box round them, because the box of a spinning body is
//      up to its own diagonal wider than the body. The animal is a **scaled** node (ADR-213 put the
//      farm at 3.6x), so its world extent is 3.6 times its GLB's and has to be measured in the world
//      rather than read off the asset.
//
//   2. "the particle beam will start dropping from its old position briefly before updating to new
//      position". Two candidates were offered and they are not exclusive -- a one-frame lag between
//      the director moving the craft and the emitter's world position being recomputed, and
//      particles already emitted persisting after the craft moves. The first is measured directly
//      and is *not* what is happening (see the probe); the second is, in a sharper form than
//      "persisting": `ParticleRenderer::update` returns early for a system that is not enabled, so
//      hiding the beam **freezes** its pool rather than clearing it, and showing it again up to two
//      hundred metres away resumes every particle it was holding, unaged, where it was emitted.
//
// GPU-free: the composition, the entity layer and the director are all CPU, and `Composition::
// update` is the same call the engine makes every frame -- which is what puts the beam's emitter
// into world space, so it is the call this file needs and the abduction POC does not make.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "signals/signal_bus.hpp"
#include "stage/staging.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path sceneFile() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2.scene.json";
}

bool farmAssetsPresent() {
    return fs::is_regular_file(fs::path(AVGEN_SOURCE_DIR) / "assets" / "farm" / "cow.glb");
}

const scene::ParticleSystem* beamOf(const scene::Scene& s) {
    for (const auto& ps : s.particles) {
        if (ps.name.find("visitor-beam") != std::string::npos) {
            return &ps;
        }
    }
    return nullptr;
}

// What one abduction did, measured over every frame of its lift.
struct Lift {
    std::string animal;
    double startedAt = 0.0;
    int frames = 0;
    float fromY = 0.0f;        // where the box centre started and ended, so the lift is a number
    float toY = 0.0f;
    float beamRadius = 0.0f;   // the emitter disc's world radius, which does not change
    float worstCorner = 0.0f;  // farthest horizontal reach of the body from the beam axis
    float worstOrigin = 0.0f;  // and of the node's own origin, which is the alignment half alone
    float bodyReach = 0.0f;    // the body's own reach from its origin, which is the size half
};

// One run of the shipped scene with everything the engine does per frame that the director, the
// entity layer and the emitter's world transform can see.
struct Run {
    assets::AssetRegistry registry;
    std::unique_ptr<scene::Composition> comp;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;

    std::vector<Lift> lifts;
    // ---- report 2 ----
    // How long the beam spent *enabled* since its spawn rate last fell to nothing, measured at each
    // moment it was hidden->shown. A particle only ages while its system is enabled, so this is the
    // question "was the pool empty when the beam came back on" with nothing left to guess at.
    std::vector<double> drainedForAtShow;
    std::vector<float> emitterMovedWhileOff;
    std::vector<bool> hadEmittedBeforeShow;
    // ---- the lag arm ----
    double sameFrame = 0.0;
    double oneFrameLagged = 0.0;
    float worstCraftToNode = 0.0f; // the gap between the simulation's craft and the drawn one
    float worstNodeToEmitter = 0.0f;
    float beamRadius = 0.0f;
    float beamLifetimeMax = 0.0f;

    // How far each animal reaches from the point the director puts on the beam's axis, **as
    // authored** -- before a frame has run.
    //
    // The timing is the whole of the measurement. The farm carries `{"kind": "ground",
    // "slopeAlign": 0.55}`, so an animal standing on a hillside is pitched into it, and a pitched
    // 6.3 m-tall horse swings its box corners out by metres: sampled mid-run the same horse reads
    // 4.91 m on the flat and 7.22 m on a slope. That is a fact about standing on a hill and not
    // about hanging in a beam -- measured, a lifted animal's reach matches its authored one to
    // within a few centimetres, because a director's hold is what it is doing instead of standing.
    // As authored, every animal's rotation is yaw only, and reach from the origin is yaw-invariant.
    std::vector<std::pair<std::string, float>> cast;
    [[nodiscard]] std::pair<std::string, float> widestAnimal() const {
        std::pair<std::string, float> worst{"", 0.0f};
        for (const auto& [name, reach] : cast) {
            if (reach > worst.second) {
                worst = {name, reach};
            }
        }
        return worst;
    }
    void measureCast() {
        cast.clear();
        for (const auto& e : comp->entityWorld().entities()) {
            const auto& tags = e->desc().tags;
            if (std::find(tags.begin(), tags.end(), "animal") == tags.end()) {
                continue;
            }
            const glm::vec3 origin = e->visualPosition();
            float reach = 0.0f;
            for (const glm::vec3& p : comp->nodeCorners(e->name())) {
                reach = std::max(reach, glm::length(glm::vec2(p.x - origin.x, p.z - origin.z)));
            }
            cast.emplace_back(e->name(), reach);
        }
    }

    explicit Run(const fs::path& file) : registry(file.parent_path()) {
        auto loaded = scene::Composition::loadFile(file, registry);
        REQUIRE(loaded.has_value());
        comp = std::move(*loaded);
        comp->attach(params, modulator);
        comp->setViewport(1920, 1080);
        // ADR-186's offline setting: every entity every frame, however far from the view. The camera
        // is nowhere near the farm and a culled animal is not simulated at all.
        comp->scene().detailLimits.entityDistanceCull = false;
        // Before the first frame: see the note on `cast`.
        measureCast();
    }

    void play(double seconds, double hz = 60.0) {
        const double step = 1.0 / hz;
        const auto frames = static_cast<int>(std::round(seconds * hz));
        const params::IParameter* beamVisible = params.find("nodes/visitor-beam/visible");
        REQUIRE(beamVisible != nullptr);

        glm::vec3 lastEmitter(0.0f);
        glm::vec3 lastCraftNode(0.0f);
        float prevCraftStep = 0.0f;
        bool havePrev = false;
        bool wasVisible = false;
        bool everEmitted = false;  // a pool that has never emitted is empty by construction
        double enabledSince = 0.0; // seconds the system has been enabled since it last emitted
        glm::vec3 emitterWhenHidden(0.0f);

        std::string liftAnimal;
        for (int i = 0; i < frames; ++i) {
            FrameTime time;
            time.renderTime = static_cast<double>(i) * step;
            time.deltaTime = i == 0 ? 0.0 : step;
            time.frameIndex = static_cast<std::uint64_t>(i);
            params.resetFinals();
            comp->updateFields(time, bus, modulator);
            modulator.applyRoutes(bus, params, time.deltaTime);
            comp->updateBehaviour(time, bus);
            comp->update(time);

            const scene::ParticleSystem* beam = beamOf(comp->scene());
            REQUIRE(beam != nullptr);
            beamRadius = beam->extent.x;
            beamLifetimeMax = beam->lifetimeMax;
            const entity::Entity* craft = comp->entityWorld().find("visitor");
            REQUIRE(craft != nullptr);
            const glm::vec3 craftNode = craft->visualPosition();
            const bool visible = beamVisible->baseComponent(0) > 0.5f;

            // ---- report 2: the pool's age at the moment the beam is shown ----
            if (beam->enabled && beam->spawnRate > 1.0f) {
                everEmitted = true;
                enabledSince = 0.0;
            } else if (beam->enabled) {
                enabledSince += time.deltaTime;
            }
            if (visible && !wasVisible && i > 0) {
                drainedForAtShow.push_back(enabledSince);
                hadEmittedBeforeShow.push_back(everEmitted);
                emitterMovedWhileOff.push_back(
                    everEmitted ? glm::length(glm::vec2(beam->position.x - emitterWhenHidden.x,
                                                        beam->position.z - emitterWhenHidden.z))
                                : 0.0f);
            }
            if (!visible && wasVisible) {
                emitterWhenHidden = beam->position;
            }
            wasVisible = visible;

            // ---- the lag arm ----
            if (havePrev) {
                const float ej = glm::length(glm::vec2(beam->position.x - lastEmitter.x,
                                                       beam->position.z - lastEmitter.z));
                const float cj = glm::length(glm::vec2(craftNode.x - lastCraftNode.x,
                                                       craftNode.z - lastCraftNode.z));
                sameFrame += std::abs(static_cast<double>(cj) - ej);
                oneFrameLagged += std::abs(static_cast<double>(prevCraftStep) - ej);
                prevCraftStep = cj;
            }
            lastEmitter = beam->position;
            lastCraftNode = craftNode;
            havePrev = true;
            worstCraftToNode = std::max(
                worstCraftToNode,
                glm::length(glm::vec2(craftNode.x - craft->state().position().x,
                                      craftNode.z - craft->state().position().z)));
            worstNodeToEmitter =
                std::max(worstNodeToEmitter, glm::length(glm::vec2(beam->position.x - craftNode.x,
                                                                   beam->position.z - craftNode.z)));

            // ---- report 1: the animal against the beam ----
            const std::string beat(comp->director().beat("abduction"));
            const std::string_view target = comp->director().binding("abduction", "target");
            if (beat != "abduct" || target.empty()) {
                liftAnimal.clear();
                continue;
            }
            // The corners themselves, not the box round them: the beam is a circle, and the box of
            // a spinning 3.6x cow is up to its own diagonal wider than the cow.
            const std::vector<glm::vec3> corners = comp->nodeCorners(std::string(target));
            const entity::Entity* prey = comp->entityWorld().find(target);
            if (corners.empty() || prey == nullptr) {
                continue;
            }
            const glm::vec3 origin = prey->visualPosition();
            if (liftAnimal != target) {
                liftAnimal = target;
                lifts.push_back(Lift{.animal = liftAnimal, .startedAt = time.renderTime});
                lifts.back().fromY = origin.y;
            }
            Lift& lift = lifts.back();
            const glm::vec2 axis(beam->position.x, beam->position.z);
            float corner = 0.0f;
            float reach = 0.0f;
            for (const glm::vec3& p : corners) {
                corner = std::max(corner, glm::length(glm::vec2(p.x, p.z) - axis));
                reach = std::max(reach, glm::length(glm::vec2(p.x - origin.x, p.z - origin.z)));
            }
            ++lift.frames;
            lift.beamRadius = beam->extent.x;
            lift.worstCorner = std::max(lift.worstCorner, corner);
            lift.worstOrigin =
                std::max(lift.worstOrigin, glm::length(glm::vec2(origin.x, origin.z) - axis));
            lift.bodyReach = std::max(lift.bodyReach, reach);
            lift.toY = std::max(lift.toY, origin.y);
        }
    }
};

} // namespace

// ---- the probe ----------------------------------------------------------------------------------
//
// Reports rather than asserts, and is hidden from the default run because it plays two and a half
// minutes of a 700k-triangle world through the full composition update.
TEST_CASE("probe: the animal, the beam and the lag", "[.probe][stage][beam]") {
    if (!farmAssetsPresent()) {
        SKIP("assets/farm is not present (the GLBs are gitignored; run tools/make_farm_animals.sh)");
    }
    Run run(sceneFile());
    run.play(150.0);

    fmt::print("\nlifts: {}\n", run.lifts.size());
    for (const Lift& l : run.lifts) {
        fmt::print("  t={:6.2f} {:<12} frames={:4}  R={:.2f}  body reach={:5.2f}  "
                   "worst origin off axis={:5.2f}  worst corner={:5.2f}  rose {:5.2f} m  {}\n",
                   l.startedAt, l.animal, l.frames, l.beamRadius, l.bodyReach, l.worstOrigin,
                   l.worstCorner, l.toY - l.fromY,
                   l.worstCorner <= l.beamRadius ? "INSIDE" : "OUT OF THE BEAM");
    }
    fmt::print("\nemitter/craft step disagreement: same-frame {:.4f}  one-frame-lagged {:.4f}\n",
               run.sameFrame, run.oneFrameLagged);
    fmt::print("worst gap, simulated craft to drawn craft: {:.3f} m\n", run.worstCraftToNode);
    fmt::print("worst gap, drawn craft to beam axis:       {:.3f} m\n", run.worstNodeToEmitter);
    // And every animal the query *could* pick, not just the six it did. A run abducts six of
    // eighteen; a beam sized on those six is sized on the draw rather than on the cast.
    fmt::print("\nreach from the node origin as authored, by animal:\n");
    for (const auto& [name, reach] : run.cast) {
        fmt::print("  {:<12} {:5.2f} m\n", name, reach);
    }
    const auto [widestName, widest] = run.widestAnimal();
    fmt::print("  widest: {} at {:.2f} m\n", widestName, widest);

    fmt::print("\nbeam shown {} time(s) after being hidden:\n", run.drainedForAtShow.size());
    for (std::size_t i = 0; i < run.drainedForAtShow.size(); ++i) {
        fmt::print("  enabled for {:.2f} s since it last emitted ({}); emitter moved {:.1f} m while "
                   "off\n",
                   run.drainedForAtShow[i],
                   run.hadEmittedBeforeShow[i] ? "had emitted" : "never emitted", i == 0 ? 0.0f : run.emitterMovedWhileOff[i]);
    }
    CHECK(!run.lifts.empty());
}

// ---- report 1: the animal is inside the beam, the whole way up -----------------------------------
//
// Failed before the fix, and by how much is the point. Over 110 s of the shipped scene, with this
// exact measurement code: the sixth abduction -- a cow -- put a corner **6.31 m** from the axis of a
// beam whose radius was **3.60 m**, so nearly half the animal hung outside the column it was
// supposedly being lifted by. The static check below failed harder still: the widest animal the
// query can pick reaches 5.80 m on its own, against that 3.60 m beam, so *no* alignment could have
// fixed a bull.
TEST_CASE("An abducted animal is inside the beam for the whole lift", "[stage][beam][abduction]") {
    if (!farmAssetsPresent()) {
        SKIP("assets/farm is not present (the GLBs are gitignored; run tools/make_farm_animals.sh)");
    }
    Run run(sceneFile());
    // Long enough to reach the fifth and sixth abductions, which is where the scenario's query first
    // picks one of the big animals: the first four are a chick, two goats and a chicken, and a test
    // that stopped there would be asserting about poultry.
    run.play(110.0);

    REQUIRE(run.lifts.size() >= 5);
    float worstOrigin = 0.0f;
    bool sawALargeOne = false;
    for (const Lift& l : run.lifts) {
        INFO(l.animal << " at t=" << l.startedAt << ": " << l.frames << " frames, body reach "
                      << l.bodyReach << " m, origin off axis " << l.worstOrigin
                      << " m, worst corner " << l.worstCorner << " m against a beam of "
                      << l.beamRadius << " m");
        REQUIRE(l.frames > 100);   // a lift is 4.6 s; a handful of frames would prove nothing
        REQUIRE(l.beamRadius > 0.0f);
        CHECK(l.worstCorner <= l.beamRadius);
        worstOrigin = std::max(worstOrigin, l.worstOrigin);
        sawALargeOne = sawALargeOne || l.bodyReach > 4.0f;
    }
    // Without one of the big animals in the run the assertion above is satisfied by a chick, and a
    // chick fits in any beam anybody would ever author (ADR-182).
    CHECK(sawALargeOne);

    // And the animals the run did not happen to pick. The query filters on a tag, so every one of
    // the eighteen is a candidate, and the beam has to hold the widest of them plus however far off
    // the axis the lift actually puts it.
    const auto [widest, reach] = run.widestAnimal();
    INFO("widest animal " << widest << " reaches " << reach
                          << " m from its own origin; the worst the lift put an origin off the beam "
                             "axis was "
                          << worstOrigin << " m; the beam is " << run.beamRadius << " m");
    CHECK(reach + worstOrigin <= run.beamRadius);
}

// ---- report 2: the beam does not resume somewhere else -------------------------------------------
//
// "there are times where the UFO can move and the particle beam will start dropping from its old
// position briefly before updating to new position."
//
// Two candidates were offered. The first -- a one-frame lag between the director moving the craft
// and the emitter's world position being recomputed -- is measured here and is **not** what is
// happening; the second is, and in a sharper form than "particles persist": they are *frozen*.
// `ParticleRenderer::update` skips a system that is not enabled, before the simulate pass, so a
// hidden beam neither ages nor clears. Showing it again resumes every particle it was holding,
// unaged, at the position it was emitted -- which the shipped scene measured at 31.9, 204.6, 45.6,
// 59.9 and 31.9 metres away, at five out of five shows, every one of them with 0.00 s of drain.
TEST_CASE("The beam never resumes particles emitted somewhere else", "[stage][beam][particles]") {
    if (!farmAssetsPresent()) {
        SKIP("assets/farm is not present (the GLBs are gitignored; run tools/make_farm_animals.sh)");
    }
    Run run(sceneFile());
    run.play(110.0);

    // ---- the lag arm ----
    //
    // Each frame, |the emitter's horizontal step| against |the craft's, this frame| and |the
    // craft's, last frame|. A one-frame lag would make the second the better fit. It is not: the
    // world transform is recomputed in `applyParameters`, in the same frame the entity pass wrote
    // the node's position. Kept as a guard, and labelled as the negative result it is.
    INFO("emitter/craft step disagreement: same-frame " << run.sameFrame << ", one-frame-lagged "
                                                        << run.oneFrameLagged);
    REQUIRE(run.sameFrame > 0.0); // the craft moved at all, or neither number means anything
    CHECK(run.sameFrame < run.oneFrameLagged);

    // ---- the freeze arm ----
    REQUIRE(run.drainedForAtShow.size() >= 4);
    REQUIRE(run.beamLifetimeMax > 0.0f);
    bool anyMoved = false;
    for (std::size_t i = 0; i < run.drainedForAtShow.size(); ++i) {
        INFO("show #" << i << ": enabled for " << run.drainedForAtShow[i]
                      << " s since it last emitted, emitter " << run.emitterMovedWhileOff[i]
                      << " m from where it was hidden, against a particle lifetime of "
                      << run.beamLifetimeMax << " s");
        if (!run.hadEmittedBeforeShow[i]) {
            continue; // a pool that has never emitted is empty whatever it did while hidden
        }
        // A particle only ages while its system is enabled. So this, and nothing about the
        // renderer's internals, is the question "was the pool empty when the beam came back on".
        CHECK(run.drainedForAtShow[i] >= static_cast<double>(run.beamLifetimeMax));
        anyMoved = anyMoved || run.emitterMovedWhileOff[i] > 10.0f;
    }
    // If the emitter had never moved while the beam was off, a frozen pool would have resumed in
    // the right place and the whole assertion above would be about nothing (ADR-182).
    CHECK(anyMoved);
}

// ---- and the guard that stops the first fix from causing a worse bug ------------------------------
//
// `Anchor::Visual` is what makes the lift line up, and it is also what turns ADR-210's "the
// horizontal half of that coupling is fine" into a runaway: the saucer's 2.4 m drift at 0.031 Hz
// integrates to about twelve metres once it is inside the loop. A description shaped like that is
// refused at load rather than measured later.
TEST_CASE("A beat where two roles chase each other's drawn position is refused",
          "[stage][staging][validation]") {
    const auto describe = [](bool hold, stage::Anchor anchor) {
        stage::StagingDesc desc;
        desc.actors.push_back(stage::ActorDesc{.name = "saucer", .body = "craft"});

        stage::StepDesc follow;
        follow.kind = stage::StepKind::Follow;
        follow.name = "hold";
        follow.toRole = "target";
        follow.duration = stage::literal(2.0f);
        follow.height = stage::literal(20.0f);
        follow.aboveGround = true; // anchors the *vertical* cycle, which is a different check
        follow.hold = hold;

        stage::StepDesc lift;
        lift.kind = stage::StepKind::MoveTo;
        lift.name = "lift";
        lift.toRole = "actor";
        lift.duration = stage::literal(2.0f);
        lift.height = stage::literal(-3.0f);
        lift.anchor = anchor;

        stage::BeatDesc beat;
        beat.name = "abduct";
        beat.find.push_back(stage::QueryDesc{.bind = "target", .tag = "animal"});
        beat.cues.push_back(stage::CueDesc{.role = "actor", .steps = {follow}});
        beat.cues.push_back(stage::CueDesc{.role = "target", .steps = {lift}});

        stage::ScenarioDesc scenario;
        scenario.name = "abduction";
        scenario.actor = "saucer";
        scenario.beats.push_back(beat);
        desc.scenarios.push_back(scenario);
        return desc;
    };

    // What the scene used to say, and still may: both ends on the simulation's own numbers. The
    // wobble that goes round this loop has a bounded integral of about eleven centimetres.
    stage::Staging plain;
    CHECK(plain.setDesc(describe(false, stage::Anchor::Travel)).has_value());

    // The same beat, with the lift asking for the drawn position and nothing anchoring the craft.
    stage::Staging bad;
    const auto refused = bad.setDesc(describe(false, stage::Anchor::Visual));
    REQUIRE_FALSE(refused.has_value());
    INFO(refused.error().message);
    CHECK(refused.error().message.find("hold") != std::string::npos);

    // And with the craft's station held, which is what the shipped scenario does.
    stage::Staging good;
    CHECK(good.setDesc(describe(true, stage::Anchor::Visual)).has_value());
}

// The two new step fields survive a round trip through the scene format, and a scenario that does
// not mention them reads back as what every scenario meant before they existed.
TEST_CASE("A step's anchor and hold round-trip, and default to what existed",
          "[stage][staging][json]") {
    stage::StepDesc bare;
    CHECK(bare.anchor == stage::Anchor::Travel);
    CHECK_FALSE(bare.hold);

    stage::CueDesc cue;
    cue.role = "target";
    stage::StepDesc step;
    step.kind = stage::StepKind::Follow;
    step.name = "hold";
    step.toRole = "actor";
    step.anchor = stage::Anchor::Visual;
    step.hold = true;
    cue.steps.push_back(step);
    stage::BeatDesc beat;
    beat.name = "abduct";
    beat.cues.push_back(cue);
    stage::ScenarioDesc scenario;
    scenario.name = "abduction";
    scenario.actor = "saucer";
    scenario.beats.push_back(beat);

    const nlohmann::json doc = stage::scenarioToJson(scenario);
    INFO(doc.dump(1));
    const auto back = stage::scenarioFromJson(doc);
    REQUIRE(back.has_value());
    REQUIRE(back->beats.size() == 1);
    REQUIRE(back->beats[0].cues.size() == 1);
    REQUIRE(back->beats[0].cues[0].steps.size() == 1);
    CHECK(back->beats[0].cues[0].steps[0].anchor == stage::Anchor::Visual);
    CHECK(back->beats[0].cues[0].steps[0].hold);

    // An unknown anchor is refused rather than silently taken as the default -- a step that measures
    // from the wrong place is the defect this whole file is about.
    nlohmann::json broken = doc;
    broken["beats"][0]["cues"][0]["steps"][0]["anchor"] = "wherever";
    CHECK_FALSE(stage::scenarioFromJson(broken).has_value());
}
