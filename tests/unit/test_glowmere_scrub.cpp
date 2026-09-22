// A scrub of the Glowmere film lands where the play does -- the craft, the animals it lifts, and the
// five aliens that watch it (ADR-671, the owner's ruling of 2026-09-21; ADR-360).
//
// Before ADR-671 a scrub reset the director (ADR-209) and replayed the entities alone, so the
// abduction restarted from its top at every scrub and anything that perceived the craft or the
// animals diverged: measured on this film, 30.6 m apart at 30 s and 101 m at 90 s once the aliens
// ran the awareness layer. The bound here is the one `test_entity_seek.cpp` already holds a scrub
// to: exactly zero.
//
// And the second half of the ruling: a render that starts mid-film -- which is a seek followed by
// ordinary frames -- produces the same frames as one that started at zero, including the tractor
// beam's cut-out, which is director state (`beamSize`, the drain) the replay now reconstructs.
//
// testing.md #33: the generators this film's mushrooms and trees come from are registered here and
// asserted, not inherited from whatever ran before.

#include "assets/asset_registry.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "organism/mushroom.hpp"
#include "params/modulation.hpp"
#include "params/parameter_set.hpp"
#include "scene/composition.hpp"
#include "scene/procedural.hpp"
#include "scene/tree_generated.hpp"
#include "signals/signal_bus.hpp"

#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>

#include <filesystem>
#include <map>
#include <sstream>
#include <memory>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

// The film's last frame: `duration` is 226.293 s and a scrub is clamped to it, so the last whole
// 60 Hz frame at or before it is 13,577.
constexpr double kFilmEnd = 13577.0 / 60.0;

fs::path film() {
    return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.scene.json";
}
bool assetsPresent() {
    return fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "farm" / "cow.glb") &&
           fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb");
}

struct Film {
    assets::AssetRegistry registry;
    params::ParameterSet params;
    params::Modulator modulator;
    signals::SignalBus bus;
    std::unique_ptr<scene::Composition> comp;
    long long frame = 0;

    Film() : registry(film().parent_path()) {
        organism::registerMushroomGenerator();
        scene::registerTreeGenerator();
        REQUIRE(scene::hasGenerator("mushroom"));
        auto loaded = scene::Composition::loadFile(film(), registry);
        REQUIRE(loaded.has_value());
        comp = std::move(*loaded);
        comp->attach(params, modulator);
        comp->setViewport(640, 360);
        comp->scene().detailLimits.entityDistanceCull = false;
    }
    [[nodiscard]] double now() const { return static_cast<double>(frame) / 60.0; }
    // One ordinary frame at the film's current second, exactly as the application ticks it.
    void tick() {
        FrameTime time;
        time.renderTime = now();
        time.deltaTime = frame == 0 ? 0.0 : 1.0 / 60.0;
        time.frameIndex = static_cast<std::uint64_t>(frame);
        params.resetFinals();
        comp->updateFields(time, bus, modulator);
        modulator.applyRoutes(bus, params, time.deltaTime);
        comp->updateBehaviour(time, bus);
        comp->update(time);
        ++frame;
    }
    // Frames 0 .. seconds*60 inclusive: the last one is *at* `seconds`.
    void playTo(double seconds) {
        const auto last = static_cast<long long>(std::llround(seconds * 60.0));
        while (frame <= last) {
            tick();
        }
    }
    // What `Engine::seekSeconds` does to the entities and the director, landing on `seconds`; the
    // next `tick` is the frame after it.
    // The engine's own budget (ADR-700: `kEditorBodySteps`), so a scrub here is the scrub the
    // editor does, first-time cost and fallback rule included.
    void seekTo(double seconds, entity::SeekMode mode = entity::SeekMode::Checkpointed) {
        comp->seekWithDirector(seconds, params,
                               entity::SeekBudget{.maxSeconds = 90.0,
                                                  .maxBodySteps = entity::SeekBudget::kEditorBodySteps,
                                                  .mode = mode},
                               1.0 / 60.0);
        frame = static_cast<long long>(std::llround(seconds * 60.0)) + 1;
    }
    [[nodiscard]] std::map<std::string, glm::vec3> drawn() const {
        std::map<std::string, glm::vec3> out;
        for (const auto& e : comp->entityWorld().entities()) {
            out[e->name()] = e->visualPosition();
        }
        return out;
    }
    [[nodiscard]] std::string beat() const {
        std::string b;
        for (const auto& e : comp->director().log()) {
            if (e.kind == stage::StageEventKind::Beat) {
                b = e.beat;
            }
        }
        return b;
    }
};

float worst(const std::map<std::string, glm::vec3>& a, const std::map<std::string, glm::vec3>& b,
            std::string& who) {
    float w = 0.0f;
    for (const auto& [name, p] : a) {
        const auto it = b.find(name);
        if (it == b.end()) {
            who = name + " (missing)";
            return 1e9f;
        }
        const float d = glm::length(p - it->second);
        if (d > w) {
            w = d;
            who = name;
        }
    }
    return w;
}

// A render that starts at `kStart` is a seek followed by ordinary frames; it must draw what a render
// from zero draws, frame for frame, for `kSpan` seconds: every body, and every particle system's
// enabled flag, spawn rate, size and emitter position.
void renderFromMatchesFull(double kStart, double kSpan, bool requireCutOut = false) {
    Film full;
    Film partial;
    full.playTo(kStart);
    partial.seekTo(kStart);
    REQUIRE(full.frame == partial.frame);
    std::size_t frames = 0;
    std::size_t beamFrames = 0;
    std::vector<std::string> beats;
    float worstBody = 0.0f;
    std::string worstWho;
    bool particlesAgree = true;
    bool beamOnSeen = false;
    bool beamCutOut = false;
    const auto last = static_cast<long long>(std::llround((kStart + kSpan) * 60.0));
    while (full.frame <= last) {
        full.tick();
        partial.tick();
        ++frames;
        std::string who;
        const float w = worst(full.drawn(), partial.drawn(), who);
        if (w > worstBody) {
            worstBody = w;
            worstWho = who;
        }
        const auto& a = full.comp->scene().particles;
        const auto& b = partial.comp->scene().particles;
        REQUIRE(a.size() == b.size());
        bool beamOnNow = false;
        for (std::size_t p = 0; p < a.size(); ++p) {
            if (a[p].name.find("beam") != std::string::npos && a[p].enabled && a[p].spawnRate > 0.0f) {
                beamOnNow = true;
            }
            particlesAgree = particlesAgree && a[p].enabled == b[p].enabled &&
                             a[p].spawnRate == b[p].spawnRate && a[p].sizeStart == b[p].sizeStart &&
                             a[p].position == b[p].position;
            if (a[p].enabled && a[p].spawnRate > 0.0f && a[p].name.find("beam") != std::string::npos) {
                ++beamFrames;
            }
        }
        beamCutOut = beamCutOut || (beamOnSeen && !beamOnNow);
        beamOnSeen = beamOnSeen || beamOnNow;
        if (beats.empty() || beats.back() != full.beat()) {
            beats.push_back(full.beat());
        }
    }
    std::string sequence;
    for (const std::string& b : beats) {
        sequence += b + " ";
    }
    INFO(frames << " frames; beats " << sequence << "; beam emitting on " << beamFrames
                << " frames; worst body " << worstWho << " " << worstBody << " m; beam cut out "
                << (beamCutOut ? "yes" : "no"));
    // Subject: the window really does contain a beam that switches off.
    REQUIRE(beamFrames > 0);
    REQUIRE(beats.size() >= 2);
    if (requireCutOut) {
        REQUIRE(beamCutOut);
    }
    CHECK(worstBody == 0.0f);
    CHECK(particlesAgree);
}

// ADR-700: as much of the simulation's state as the public surface shows, spelled exactly (hex
// floats), so two states that differ anywhere a later step could read are two different strings.
// Every entity's simulation and drawn position, facing, speeds, velocity, acceleration, activity,
// look target, director hold, declared properties, action queue, sense tick and working set; every
// decider's committed option, tick, counters, plan, attention and whole trace; the world's events;
// the director's log, retired animals and per-cue readings; and every parameter's base and final.
std::string stateDigest(const Film& f) {
    std::ostringstream o;
    o << std::hexfloat;
    const auto v3 = [&o](const glm::vec3& v) { o << v.x << ',' << v.y << ',' << v.z << ';'; };
    for (const auto& e : f.comp->entityWorld().entities()) {
        const entity::EntityState& st = e->state();
        o << e->name() << ':';
        v3(e->visualPosition());
        v3(st.position());
        o << st.yaw << ';' << st.speed << ';' << st.turnRate << ';' << st.reaction << ';';
        v3(st.velocity);
        v3(st.acceleration);
        v3(st.lookTarget);
        o << st.hasLookTarget << st.driven << st.airborne << static_cast<int>(st.activity) << ';';
        o << static_cast<int>(e->locomotion().activity) << ';' << e->locomotion().action << ';';
        o << e->directorMotion().active << ';';
        v3(e->directorMotion().position);
        for (const auto& [name, value] : e->properties()) {
            o << name << '=' << value << ';';
        }
        o << e->actions().pending() << ';' << e->actions().serial(entity::Authority::Routine) << ';';
        o << e->lastSenseTick() << ';' << e->percepts().size() << ';';
        for (const entity::Percept& p : e->percepts()) {
            v3(p.position);
        }
        for (const auto& b : e->behaviors()) {
            entity::DecisionDebug d;
            if (!b->decisionDebug(d)) {
                continue;
            }
            o << "decide:" << d.chosen << ';' << d.tick << ';' << d.committedTick << ';' << d.decisions << ';'
              << d.dwellRejections << ';' << d.marginRejections << ';' << d.remembered << ';' << d.stalls
              << ';' << d.subject << ';' << d.planActive << ';' << d.lastOutcome << ';'
              << d.attentionSubject << ';' << d.attentionScore << ';' << d.historyTotal << ';';
            for (const entity::DecisionTraceEntry& h : d.history) {
                o << h.time << h.option << h.subject << h.score << ';';
            }
        }
        o << '\n';
    }
    for (const entity::WorldEvent& w : f.comp->entityWorld().worldEvents()) {
        o << "event:" << w.sequence << ',' << w.type << ',' << w.time << ',' << w.source << ';';
    }
    o << "\nlog:" << f.comp->director().log().size() << ';';
    for (const auto& e : f.comp->director().log()) {
        o << e.beat << ',' << e.step << ',' << e.detail << ',' << e.time << ';';
    }
    for (const std::string& r : f.comp->director().retired()) {
        o << "retired:" << r << ';';
    }
    for (const auto& st : f.comp->director().sequenceStates()) {
        o << "\nseq:" << st.beat << ',' << st.cycle << ',' << st.beatStart << ',' << st.gated << ';';
        for (const auto& cue : st.cues) {
            o << cue.entity << ',' << cue.step << ',' << cue.elapsed << ',' << cue.progress << ',';
            v3(cue.position);
        }
    }
    o << "\nparams:";
    for (const params::IParameter* p : f.params.ordered()) {
        for (std::size_t c = 0; c < p->componentCount(); ++c) {
            o << p->baseComponent(c) << '/' << p->finalComponent(c) << ';';
        }
    }
    return o.str();
}

// The first line on which two digests disagree, for the failure message.
std::string firstDifference(const std::string& a, const std::string& b) {
    std::istringstream sa(a);
    std::istringstream sb(b);
    std::string la;
    std::string lb;
    while (std::getline(sa, la) && std::getline(sb, lb)) {
        if (la != lb) {
            std::size_t at = 0;
            while (at < la.size() && at < lb.size() && la[at] == lb[at]) {
                ++at;
            }
            const std::size_t from = at > 160 ? at - 160 : 0;
            return la.substr(0, la.find(':') + 1) + " ..." + la.substr(from, 320) + "\n  vs\n ..." +
                   lb.substr(from, 320);
        }
    }
    return a.size() == b.size() ? std::string("(none)") : std::string("(lengths differ)");
}

} // namespace

TEST_CASE("a scrub of the Glowmere film lands every body where the play did, inside an abduction too",
          "[glowmere][seek][determinism][adr671]") {
    if (!assetsPresent()) {
        SKIP("farm or alien assets missing");
    }
    // 30 s is inside the first abduction's lift; 45 s is in the second cycle's beam; 90 s is the
    // end of the engine's replay window.
    Film played;
    for (const double t : {30.0, 45.0, 90.0}) {
        played.playTo(t);
        Film scrubbed;
        scrubbed.seekTo(t);
        INFO("t = " << t << " s, play beat '" << played.beat() << "', scrub beat '" << scrubbed.beat() << "'");
        // Subject: the director is running, and it has moved things.
        REQUIRE_FALSE(played.beat().empty());
        CHECK(scrubbed.beat() == played.beat());
        std::string who;
        const float w = worst(played.drawn(), scrubbed.drawn(), who);
        INFO("worst body: " << who << ", " << w << " m");
        CHECK(w == 0.0f);
    }
}

TEST_CASE("a render that starts mid-film draws the frames a render from zero draws, beam and all",
          "[glowmere][seek][determinism][beam][adr671]") {
    if (!assetsPresent()) {
        SKIP("farm or alien assets missing");
    }
    // Start inside the second cycle's beam and run four seconds, through the abduction and into the
    // depart -- the beam's fade and cut-out are director state the replay has to have rebuilt.
    renderFromMatchesFull(44.0, 4.0);
}

TEST_CASE("a render that starts past the old window draws the frames a render from zero draws",
          "[glowmere][seek][determinism][beam][adr700]") {
    if (!assetsPresent()) {
        SKIP("farm or alien assets missing");
    }
    // ADR-700. An offline render runs in a fresh process with no checkpoint, so its opening seek
    // replays the whole film to the start -- exact, and paid once. 150 s is in the ninth cycle's
    // approach; eleven seconds carries it through the beam (153.3 s), the lift (155.8 s) and the
    // animal's fade, which is where the beam has to cut out.
    SECTION("from 150 s, through the beam's cut-out") { renderFromMatchesFull(150.0, 11.0, true); }
    // And a seek that lands mid-beam, which is where the beam's own state -- its size, its drain --
    // is director state the checkpoint had to carry.
    SECTION("from 154 s, a seek that lands mid-beam") { renderFromMatchesFull(154.0, 7.0); }
}

TEST_CASE("a scrub past the old ninety-second window still lands every body where the play did",
          "[glowmere][seek][determinism][adr700]") {
    if (!assetsPresent()) {
        SKIP("farm or alien assets missing");
    }
    // ADR-700. 150 s and 200 s are past the ninety seconds the replay used to be allowed, and the
    // film's last frame is the far end of everything a scrub can ask for. Before checkpoints the
    // replay started from a reset director at `t - 90` and the craft landed about 200 m off at 150 s.
    Film played;
    for (const double t : {150.0, 200.0, kFilmEnd}) {
        played.playTo(t);
        Film scrubbed;
        scrubbed.seekTo(t);
        INFO("t = " << t << " s, play beat '" << played.beat() << "', scrub beat '" << scrubbed.beat() << "'");
        REQUIRE_FALSE(played.beat().empty());
        CHECK(scrubbed.beat() == played.beat());
        std::string who;
        const float w = worst(played.drawn(), scrubbed.drawn(), who);
        INFO("worst body: " << who << ", " << w << " m");
        CHECK(w == 0.0f);
        // Subject: past ninety seconds the old replay could not have got here.
        CHECK(scrubbed.comp->entityWorld().lastSeekWork().exact);
        // And the frame after, drawn: every body again, and every particle system's enabled flag,
        // spawn rate, size and emitter position -- the director state a render reads.
        played.tick();
        scrubbed.tick();
        CHECK(worst(played.drawn(), scrubbed.drawn(), who) == 0.0f);
        const auto& a = played.comp->scene().particles;
        const auto& b = scrubbed.comp->scene().particles;
        REQUIRE(a.size() == b.size());
        REQUIRE_FALSE(a.empty());
        for (std::size_t p = 0; p < a.size(); ++p) {
            INFO("particle system " << a[p].name);
            CHECK(a[p].enabled == b[p].enabled);
            CHECK(a[p].spawnRate == b[p].spawnRate);
            CHECK(a[p].sizeStart == b[p].sizeStart);
            CHECK(a[p].position == b[p].position);
        }
    }
}

TEST_CASE("a checkpointed scrub is the whole-history replay, state for state, however it got there",
          "[glowmere][seek][determinism][checkpoint][adr700]") {
    if (!assetsPresent()) {
        SKIP("farm or alien assets missing");
    }
    // ADR-700's completeness check. The reference replays every step from zero and touches no
    // checkpoint. The subject has been somewhere else first -- to the film's end, which records
    // every checkpoint and leaves every piece of state as unlike the target's as the film allows,
    // and then through ordinary played frames, which touch the state only a play touches -- and
    // then scrubs back. Anything a step reads that a checkpoint does not carry arrives from the
    // perturbed state, and the digest disagrees.
    //
    // Targets on and between checkpoints (1 s apart), and one between two grid instants.
    Film perturbed;
    perturbed.seekTo(kFilmEnd);
    REQUIRE(perturbed.comp->entityWorld().checkpointStats().count > 200);
    for (int i = 0; i < 30; ++i) {
        perturbed.tick();
    }
    for (const double t : {37.5, 150.0, 200.0 + 17.0 / 60.0, 121.004}) {
        Film reference;
        reference.seekTo(t, entity::SeekMode::FullHistory);
        REQUIRE(reference.comp->entityWorld().lastSeekWork().restoredFrom < 0.0);
        perturbed.seekTo(t);
        const auto work = perturbed.comp->entityWorld().lastSeekWork();
        INFO("t = " << t << " s, restored from " << work.restoredFrom << " s, " << work.bodySteps
                    << " body-steps");
        // Subject: it really did resume from a checkpoint, and a recent one.
        REQUIRE(work.restoredFrom >= 0.0);
        REQUIRE(t - work.restoredFrom <= 1.0 + 1e-9);
        const std::string a = stateDigest(reference);
        const std::string b = stateDigest(perturbed);
        INFO("first difference:\n" << firstDifference(a, b));
        CHECK(a == b);
        // Played on from there, they stay together: the next frames build on the state the seek
        // published, not just on what the digest can see.
        for (int i = 0; i < 20; ++i) {
            reference.tick();
            perturbed.tick();
        }
        INFO("after 20 played frames:\n" << firstDifference(stateDigest(reference), stateDigest(perturbed)));
        CHECK(stateDigest(reference) == stateDigest(perturbed));
    }
    SECTION("control: the digest can tell two nearby instants apart") {
        Film a;
        a.seekTo(150.0, entity::SeekMode::FullHistory);
        Film b;
        b.seekTo(150.0 + 1.0 / 60.0, entity::SeekMode::FullHistory);
        CHECK(stateDigest(a) != stateDigest(b));
    }
}

TEST_CASE("an edit that changes the simulation drops the checkpoints it made stale",
          "[glowmere][seek][checkpoint][invalidation][adr700]") {
    if (!assetsPresent()) {
        SKIP("farm or alien assets missing");
    }
    // Checkpoint the whole film, then shorten the craft's approach -- a director knob, so every
    // abduction after the first starts earlier and the eight after it shift with it -- then scrub.
    // The answer must be a fresh replay with the edit, not the checkpoints taken before it.
    // (`travelSpeed` was the first choice and is a non-edit: by 150 s it has moved nothing, which
    // the control arm below is what showed.)
    constexpr double kT = 150.0;
    const auto edit = [](Film& f) { REQUIRE(f.comp->director().setParameter("abduction", "approachSeconds", 5.0f)); };
    Film fresh;
    edit(fresh);
    fresh.seekTo(kT, entity::SeekMode::FullHistory);

    Film edited;
    edited.seekTo(kFilmEnd);
    REQUIRE(edited.comp->entityWorld().checkpointStats().count > 200);
    edit(edited);
    edited.seekTo(kT);
    CHECK(edited.comp->entityWorld().lastSeekWork().invalidated);
    CHECK(edited.comp->entityWorld().lastSeekWork().restoredFrom < 0.0);
    std::string who;
    const float w = worst(fresh.drawn(), edited.drawn(), who);
    INFO("worst body " << who << " " << w << " m");
    CHECK(w == 0.0f);
    CHECK(stateDigest(fresh) == stateDigest(edited));
    // And the new set is the edited film's: a second scrub resumes from it and still agrees.
    edited.seekTo(kT);
    // (From the one before the target: a seek always replays at least the step it lands on.)
    CHECK(edited.comp->entityWorld().lastSeekWork().restoredFrom == kT - 1.0);
    CHECK(stateDigest(fresh) == stateDigest(edited));

    SECTION("control: kept across the edit, the stale checkpoints give a different film") {
        // What the invalidation is preventing, measured: the same sequence with the input key
        // ignored lands the bodies somewhere else. If this agreed, the edit would not be one that
        // matters and the arm above would prove nothing.
        Film stale;
        entity::CheckpointSettings cs = stale.comp->entityWorld().checkpointSettings();
        cs.ignoreInputKey = true;
        stale.comp->entityWorld().setCheckpointSettings(cs);
        stale.seekTo(kFilmEnd);
        edit(stale);
        stale.seekTo(kT);
        REQUIRE(stale.comp->entityWorld().lastSeekWork().restoredFrom == kT - 1.0);
        CHECK(worst(fresh.drawn(), stale.drawn(), who) > 1.0f);
    }
}

TEST_CASE("probe: the film's director beats over time", "[.probe][glowmere]") {
    if (!assetsPresent()) {
        SKIP("farm or alien assets missing");
    }
    Film f;
    std::string last;
    while (f.now() <= kFilmEnd) {
        f.tick();
        if (f.beat() != last) {
            last = f.beat();
            std::printf("%.3f %s\n", f.now() - 1.0 / 60.0, last.c_str());
        }
    }
}

TEST_CASE("a scrub replayed from zero after an earlier run is the scrub a fresh load gives",
          "[glowmere][seek][determinism][checkpoint][adr700]") {
    if (!assetsPresent()) {
        SKIP("farm or alien assets missing");
    }
    // A replay from zero starts from `EntityWorld::reset`, and a reset that forgets a field makes
    // the answer depend on what ran before it. The editor never scrubs a fresh load, so this is the
    // case that matters. Found by the checkpoint digest: `Decide`'s variety window read a span over
    // a vector the same step had reallocated, and the aliens were 28 m apart at 90 s.
    for (const double t : {90.0, 150.0}) {
        Film fresh;
        fresh.seekTo(t, entity::SeekMode::FullHistory);
        Film again;
        again.seekTo(60.0, entity::SeekMode::FullHistory);
        for (int i = 0; i < 10; ++i) {
            again.tick();
        }
        again.seekTo(t, entity::SeekMode::FullHistory);
        INFO("t = " << t << " s, first difference:\n" << firstDifference(stateDigest(fresh), stateDigest(again)));
        CHECK(stateDigest(fresh) == stateDigest(again));
    }
}
