// An `Engine` scrub of the Glowmere film lands where an `Engine` play does (ADR-800).
//
// `test_glowmere_scrub.cpp` holds the composition to exactly zero, but its harness goes through no
// `Engine`. Through one, on 2026-09-24, a scrub put the saucer 12 m and the aliens 98 m from where a
// play put them at 150 s. Three causes were measured, each with the other two removed:
//
//   1. The live entity distance cull. A play culls far bodies and the replay never does. This is
//      the preview's documented trade (ADR-186); offline renders lift it (ADR-191), so the cases
//      here lift it as well.
//   2. Audio-reactive world events and behaviours. The replay had no signal bus: Vane 92 m off by
//      90 s. Fixed by ADR-870 -- offline, the replay rebuilds the bus a play saw at every step, and
//      the live pipeline continues from the replay's state. The cases tagged [adr870] hold it.
//   3. The landing instant was evaluated twice. `loadProject` ends in a seek to zero and the first
//      frame is at zero, and a render seeks to its start and then ticks at its start. The second
//      pass ran the saucer's director again and entered its next beat a frame early; everything
//      downstream of the saucer stayed a frame early. That is fixed, and the first case holds it.

#include "app/engine.hpp"
#include "core/time.hpp"
#include "entity/entity.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_test_macros.hpp>

#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace avgen;
namespace fs = std::filesystem;

namespace {

fs::path project() { return fs::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-valley-2-multicam.json"; }
bool assetsPresent() {
    return fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "farm" / "cow.glb") &&
           fs::exists(fs::path(AVGEN_SOURCE_DIR) / "assets" / "aliens" / "alien-scout.glb");
}

void frameAt(app::Engine& engine, long long frame) {
    engine.update(FrameTime{static_cast<double>(frame) / 60.0, frame == 0 ? 0.0 : 1.0 / 60.0,
                            static_cast<std::uint64_t>(frame)});
}

std::map<std::string, glm::vec3> drawn(const app::Engine& engine) {
    std::map<std::string, glm::vec3> out;
    for (const auto& e : engine.composition()->entityWorld().entities()) {
        out[e->name()] = e->visualPosition();
    }
    return out;
}

// The film as an offline render sees it: the entity distance cull lifted (ADR-191). `withAudio`
// false removes the audio, and with it every audio-reactive signal.
void load(app::Engine& engine, bool withAudio) {
    REQUIRE(engine.loadProject(project()).has_value());
    scene::DetailLimits limits = engine.detailLimits();
    limits.entityDistanceCull = false;
    engine.setDetailLimits(limits);
    if (!withAudio) {
        REQUIRE(engine.setAudioClips({}).has_value());
    }
}

// For each target frame: a play from zero through it and one frame more, against a fresh engine that
// scrubs to it and plays that one frame. The worst body over the whole cast, by name.
void requireScrubEqualsPlay(bool withAudio, const std::vector<long long>& targets) {
    app::Engine played(app::EngineMode::Offline);
    load(played, withAudio);
    std::map<long long, std::map<std::string, glm::vec3>> playedAt;
    long long frame = 0;
    for (long long t : targets) {
        for (; frame <= t + 1; ++frame) {
            frameAt(played, frame);
        }
        playedAt[t] = drawn(played);
    }
    for (long long t : targets) {
        app::Engine scrubbed(app::EngineMode::Offline);
        load(scrubbed, withAudio);
        scrubbed.seekSeconds(static_cast<double>(t) / 60.0);
        frameAt(scrubbed, t + 1);
        const auto b = drawn(scrubbed);
        REQUIRE(b.size() == playedAt[t].size());
        float worst = 0.0f;
        std::ostringstream who;
        for (const auto& [name, p] : playedAt[t]) {
            const float d = glm::length(p - b.at(name));
            if (d > 0.0f) {
                who << name << ' ' << d << " m; ";
            }
            worst = std::max(worst, d);
        }
        INFO("scrub to " << static_cast<double>(t) / 60.0 << " s: " << who.str());
        CHECK(worst == 0.0f);
    }
}

} // namespace

// Cause 3. One second is where the saucer first left the play (0.033 m), and thirty is well past its
// second and third beats. Measured with the fix at 1, 2, 3, 5, 10, 30, 60, 90, 120 and 150 s: every
// body exactly equal at every one.
TEST_CASE("an Engine scrub of the Glowmere film lands where an Engine play does", "[seek][engine][glowmere][adr800]") {
    if (!assetsPresent()) {
        SKIP("farm or alien assets missing");
    }
    requireScrubEqualsPlay(false, {60, 1800});
}

// Cause 2 (ADR-870). With the film's audio its audio-reactive world events and behaviours fire in a
// play; before the replay rebuilt the bus they never fired in a scrub, and the aliens parted company
// (Vane 92.0 m, Rook 42.2 m, Sage 7.5 m and Ember 6.9 m at 90 s). Thirty, ninety and a hundred and
// fifty seconds: every body exactly equal.
TEST_CASE("an Engine scrub of the Glowmere film with its audio lands where the play does", "[seek][engine][glowmere][adr800][adr870]") {
    if (!assetsPresent()) {
        SKIP("farm or alien assets missing");
    }
    requireScrubEqualsPlay(true, {1800, 5400, 9000});
}

namespace {

// The worst body between two engines, with who moved, for the message.
float worstBody(const std::map<std::string, glm::vec3>& a, const std::map<std::string, glm::vec3>& b,
                std::string& who) {
    REQUIRE(a.size() == b.size());
    float worst = 0.0f;
    std::ostringstream out;
    for (const auto& [name, p] : a) {
        const float d = glm::length(p - b.at(name));
        if (d > 0.0f) {
            out << name << ' ' << d << " m; ";
        }
        worst = std::max(worst, d);
    }
    who = out.str();
    return worst;
}

// The signal pipeline's carried state, field by field: where two engines' next frames would differ.
void requireSameClock(const app::SignalClock& a, const app::SignalClock& b) {
    CHECK(a.analysisCursor == b.analysisCursor);
    CHECK(a.hasFrame == b.hasFrame);
    CHECK(a.beatPhase == b.beatPhase);
    CHECK(a.beatCount == b.beatCount);
    CHECK(a.lastAnalysisBeatCount == b.lastAnalysisBeatCount);
    CHECK(a.lastPhraseIndex == b.lastPhraseIndex);
    CHECK(a.music.consumedFrames() == b.music.consumedFrames());
}

} // namespace

// ADR-870: a scrub that restores a checkpoint carries the signal pipeline's state in it. The first
// seek records a checkpoint every second to 60 s; the second lands between two of them and must
// resume from one -- it replays under a second, not from zero -- and still land where the play does.
TEST_CASE("an Engine scrub of the film with its audio that resumes from a checkpoint lands where the play does", "[seek][engine][glowmere][adr870]") {
    if (!assetsPresent()) {
        SKIP("farm or alien assets missing");
    }
    constexpr long long kTarget = 2710; // 45.17 s: ten frames past the 45 s checkpoint
    app::Engine played(app::EngineMode::Offline);
    load(played, true);
    for (long long frame = 0; frame <= kTarget + 1; ++frame) {
        frameAt(played, frame);
    }

    app::Engine scrubbed(app::EngineMode::Offline);
    load(scrubbed, true);
    REQUIRE(scrubbed.seekReplaysSignals());
    scrubbed.seekSeconds(60.0);
    REQUIRE(scrubbed.composition()->entityWorld().checkpointStats().count >= 45);
    scrubbed.seekSeconds(static_cast<double>(kTarget) / 60.0);
    const auto work = scrubbed.composition()->entityWorld().lastSeekWork();
    REQUIRE(work.exact);
    REQUIRE(work.restoredFrom == 45.0); // from the checkpoint, not from zero
    REQUIRE(work.steps == 10);
    frameAt(scrubbed, kTarget + 1);

    std::string who;
    const float worst = worstBody(drawn(played), drawn(scrubbed), who);
    INFO("scrub via a checkpoint to " << static_cast<double>(kTarget) / 60.0 << " s: " << who);
    CHECK(worst == 0.0f);
    requireSameClock(played.signalClock(), scrubbed.signalClock());
}

// ADR-870: after a scrub the live pipeline continues from the replay's state, not from a reset. Five
// seconds of play after a scrub to 30 s, against a play from zero, compared on every frame.
TEST_CASE("an Engine that plays on after a scrub of the film with its audio stays with the play", "[seek][engine][glowmere][adr870]") {
    if (!assetsPresent()) {
        SKIP("farm or alien assets missing");
    }
    constexpr long long kScrub = 1800;
    constexpr long long kFrames = 300;
    app::Engine played(app::EngineMode::Offline);
    load(played, true);
    for (long long frame = 0; frame <= kScrub; ++frame) {
        frameAt(played, frame);
    }
    app::Engine scrubbed(app::EngineMode::Offline);
    load(scrubbed, true);
    scrubbed.seekSeconds(static_cast<double>(kScrub) / 60.0);
    requireSameClock(played.signalClock(), scrubbed.signalClock());

    float worst = 0.0f;
    long long worstFrame = -1;
    std::string worstWho;
    for (long long frame = kScrub + 1; frame <= kScrub + kFrames; ++frame) {
        frameAt(played, frame);
        frameAt(scrubbed, frame);
        std::string who;
        const float d = worstBody(drawn(played), drawn(scrubbed), who);
        if (d > worst) {
            worst = d;
            worstFrame = frame;
            worstWho = who;
        }
    }
    INFO("worst at frame " << worstFrame << ": " << worstWho);
    CHECK(worst == 0.0f);
    requireSameClock(played.signalClock(), scrubbed.signalClock());
    // And the signals ADR-870 replays, value by value: the analysis, the clock and the classifier.
    // (Control sources and scene states are not replayed and not claimed.)
    const auto& a = played.signals();
    const auto& b = scrubbed.signals();
    REQUIRE(a.size() == b.size());
    std::size_t differing = 0;
    std::size_t compared = 0;
    std::string first;
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto id = static_cast<signals::SignalId>(i);
        const std::string& name = a.info(id).name;
        const bool replayed = name.starts_with("audio.") || name.starts_with("time.") ||
                              name.starts_with("beat.") || name.starts_with("music.");
        if (!replayed) {
            continue;
        }
        ++compared;
        if (a.value(id) != b.value(id)) {
            if (differing++ == 0) {
                first = a.info(id).name;
            }
        }
    }
    INFO("first differing signal: " << first);
    CHECK(compared >= 30);
    CHECK(differing == 0);
}

// ADR-870: what a seek costs on the film, cold (no checkpoints: the first scrub after a load) and
// warm (the checkpoints the cold one recorded). Hidden: a timing, not a property. Run it by tag.
TEST_CASE("the cost of an Engine seek on the Glowmere film", "[.bench][seek][engine][glowmere][adr870]") {
    if (!assetsPresent()) {
        SKIP("farm or alien assets missing");
    }
    for (const bool withAudio : {false, true}) {
        for (const double at : {30.0, 90.0, 150.0}) {
            app::Engine engine(app::EngineMode::Offline);
            load(engine, withAudio);
            const auto t0 = std::chrono::steady_clock::now();
            engine.seekSeconds(at);
            const auto t1 = std::chrono::steady_clock::now();
            engine.seekSeconds(at - 0.5);
            const auto t2 = std::chrono::steady_clock::now();
            const auto ms = [](auto a, auto b) { return std::chrono::duration<double, std::milli>(b - a).count(); };
            WARN((withAudio ? "audio" : "no audio") << " seek to " << at << " s: cold " << ms(t0, t1)
                                                   << " ms, warm (to " << at - 0.5 << " s) " << ms(t1, t2) << " ms");
        }
    }
}
