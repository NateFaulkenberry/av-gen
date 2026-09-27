// One definition of musical time, through the engine (ADR-896): every place a beat count becomes a
// bar -- `beat.bar`, `beat.count`, `music.downbeat`, the effect triggers, the LFOs' beat sync, the
// timeline's beats time base, the transport's bars readout, the saved setting that pins the phase --
// lands on the bar lines a synthetic groove PLACED, with a one-beat pickup so the first beat the
// tracker hears is not beat 1. The controls pin the phase a beat wrong and watch everything move.

#include "analysis/meter.hpp"
#include "app/engine.hpp"
#include "app/music_runtime.hpp"
#include "core/time.hpp"
#include "signals/musical_events.hpp"
#include "signals/source.hpp"
#include "support/groove.hpp"
#include "support/temp_dir.hpp"
#include "ui/ui_logic.hpp"
#include "world/effects/effect_trigger.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kFps = 60.0;
constexpr double kFrame = 1.0 / kFps;

struct Fixture {
    testsupport::Groove groove;
    std::filesystem::path wav;
};

const Fixture& pickupGroove() {
    static const Fixture f = [] {
        testsupport::GrooveSpec spec;
        spec.pickupBeats = 1;
        spec.bars = 20;
        Fixture out{testsupport::makeGroove(spec), testsupport::processTempDir() / "musical_time_pickup1.wav"};
        REQUIRE(out.groove.file.writeWav(out.wav).has_value());
        return out;
    }();
    return f;
}

struct Observed {
    std::vector<double> downbeatEvents; // music.downbeat, on the analysis clock
    std::vector<double> barWraps;       // frames where beat.bar wrapped
    std::vector<double> lfoWraps;       // frames where a 4-beat saw LFO wrapped
    std::map<double, float> count;      // beat.count at each frame
};

Observed play(app::Engine& engine, double seconds, const char* lfo = nullptr) {
    FixedStepClock clock(kFps);
    Observed out;
    double lastDownbeat = app::MusicRuntime::kNever;
    float lastBar = -1.0f;
    float lastLfo = -1.0f;
    const auto barId = engine.timeSignals().barPhase;
    const auto countId = engine.timeSignals().beatCount;
    const std::optional<signals::SignalId> lfoId =
        lfo != nullptr ? engine.signals().find(std::string("lfo.") + lfo) : std::nullopt;
    const auto frames = static_cast<int>(seconds * kFps);
    for (int i = 0; i < frames; ++i) {
        const FrameTime time = engine.tick(clock);
        engine.update(time);
        const double when = engine.music().lastEventTime(signals::MusicalEvent::Downbeat);
        if (when != lastDownbeat) {
            lastDownbeat = when;
            out.downbeatEvents.push_back(when);
        }
        const float bar = engine.signals().value(barId);
        if (lastBar >= 0.0f && bar < lastBar - 0.5f) {
            out.barWraps.push_back(time.renderTime);
        }
        lastBar = bar;
        if (lfoId) {
            const float v = engine.signals().value(*lfoId);
            if (lastLfo >= 0.0f && v < lastLfo - 0.5f) {
                out.lfoWraps.push_back(time.renderTime);
            }
            lastLfo = v;
        }
        out.count[time.renderTime] = engine.signals().value(countId);
    }
    return out;
}

// The largest distance from each measured time to the nearest placed one.
double worstAgainst(const std::vector<double>& measured, const std::vector<double>& truth) {
    return testsupport::worstMiss(measured, truth);
}

} // namespace

TEST_CASE("The bus's bars and downbeats land on the placed bar lines, and the pin moves them",
          "[integration][meter]") {
    const Fixture& f = pickupGroove();
    const auto& truth = f.groove.truth.downbeats;
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(f.wav).has_value());
    REQUIRE(engine.track() != nullptr);
    // Estimated, not pinned: the tracked beat after the pickup.
    CHECK_FALSE(engine.barOffsetBeats().has_value());
    CHECK(engine.meter().downbeat == 1);

    const Observed o = play(engine, 30.0);
    INFO(o.downbeatEvents.size() << " downbeats, " << o.barWraps.size() << " bar wraps");
    REQUIRE(o.downbeatEvents.size() >= 12);
    REQUIRE(o.barWraps.size() >= 12);
    // Every music.downbeat within 30 ms of a placed bar line, and every placed bar line has one.
    CHECK(worstAgainst(o.downbeatEvents, truth) <= 0.030);
    std::vector<double> reached;
    for (const double t : truth) {
        if (t < 29.5) {
            reached.push_back(t);
        }
    }
    CHECK(testsupport::worstMiss(reached, o.downbeatEvents) <= 0.030);
    // beat.bar wraps on the first render frame at or after the bar line.
    CHECK(worstAgainst(o.barWraps, truth) <= 0.030 + kFrame);
    // beat.count is the musical beat: 0 on beat 1 of bar 1, -1 through the pickup.
    const auto countAt = [&o](double seconds) {
        return o.count.lower_bound(seconds)->second;
    };
    CHECK(countAt(truth[0] + 0.1) == 0.0f);
    CHECK(countAt(truth[0] - 0.2) == -1.0f);
    CHECK(countAt(truth[3] + 0.1) == 12.0f);

    SECTION("control: pinning the bar a beat late moves every bar line a beat") {
        app::Engine pinned(app::EngineMode::Offline);
        REQUIRE(pinned.loadAudio(f.wav).has_value());
        pinned.setBarOffsetBeats(2);
        CHECK(pinned.meter().downbeat == 2);
        const Observed late = play(pinned, 30.0);
        REQUIRE(late.downbeatEvents.size() >= 12);
        // Now every downbeat is a beat (0.5 s) after a placed bar line: off the grid...
        for (const double t : late.downbeatEvents) {
            double nearest = 1e9;
            for (const double b : truth) {
                nearest = std::min(nearest, std::fabs(t - b));
            }
            CHECK(nearest > 0.4);
        }
        // ...and exactly a beat after the estimated ones.
        CHECK(worstAgainst(late.downbeatEvents, [&] {
                  std::vector<double> shifted;
                  for (const double t : truth) {
                      shifted.push_back(t + 0.5);
                  }
                  return shifted;
              }()) <= 0.030);
    }
}

TEST_CASE("The effect trigger's Beat source counts the beats the bus counts", "[integration][meter]") {
    const Fixture& f = pickupGroove();
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(f.wav).has_value());
    const Observed o = play(engine, 20.0);
    REQUIRE(o.downbeatEvents.size() >= 8);

    // The clock the engine binds its effects to, with the engine's meter.
    world::TriggerClock clock;
    clock.bind(engine.track(), {}, nullptr, 20.0, engine.meter());
    world::Trigger every4;
    every4.source = world::TriggerSource::Beat;
    every4.everyN = 4;
    every4.offset = 0;
    std::array<double, 64> out{};
    const std::size_t n = clock.lastTriggers(every4, "", 19.99, out);
    REQUIRE(n >= 8);
    // Every trigger instant is a music.downbeat instant (they differ only by the half hop between a
    // refined beat time and the analysis frame it is stamped on)...
    std::vector<double> fired(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(n));
    CHECK(worstAgainst(fired, o.downbeatEvents) <= 0.006);
    // ...and a placed bar line. Before ADR-896 the trigger fired on beats 0, 4, 8 of the tracker and
    // the bus on beats 3, 7, 11: never the same instant.
    CHECK(worstAgainst(fired, f.groove.truth.downbeats) <= 0.030);

    // Offset k is the beat `beat.count` reads k on.
    world::Trigger third = every4;
    third.everyN = 1000;
    third.offset = 5;
    std::array<double, 1> first{};
    REQUIRE(clock.lastTriggers(third, "", 19.99, first) == 1);
    const auto after = o.count.lower_bound(first[0] + 0.02);
    REQUIRE(after != o.count.end());
    CHECK(after->second == 5.0f);
}

TEST_CASE("A beat-synced LFO cycles on the bar the bus has, and the timeline counts from bar 1",
          "[integration][meter]") {
    const Fixture& f = pickupGroove();
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(f.wav).has_value());
    auto& lfo = static_cast<signals::LfoSource&>(engine.addSource("lfo", "bars"));
    lfo.setShape(signals::LfoShape::Saw);
    engine.params().findAs<bool>("sources/bars/beatSync")->setBase(true);
    engine.params().findAs<float>("sources/bars/beatsPerCycle")->setBase(4.0f);

    // A timeline track in beats: 1 at beat 0 (bar 1's downbeat), 3 at beat 4 (bar 2's). On
    // camera/height, which carries no default route (the orb's scale answers the audio).
    params::Track track;
    track.target = "camera/height";
    track.timeBase = params::TimeBase::Beats;
    track.addKey({.time = 0.0, .value = {1.0f}, .interp = params::KeyInterp::Linear});
    track.addKey({.time = 4.0, .value = {3.0f}, .interp = params::KeyInterp::Linear});
    engine.timeline().enabled = true;
    engine.timeline().addTrack(track);
    engine.rebind();

    const auto& truth = f.groove.truth.downbeats;
    FixedStepClock clock(kFps);
    float lastLfo = -1.0f;
    float lastBar = -1.0f;
    std::vector<double> lfoWraps;
    std::vector<double> barWraps;
    std::map<double, double> beats;
    std::map<double, float> scale;
    for (int i = 0; i < static_cast<int>(12.0 * kFps); ++i) {
        const FrameTime time = engine.tick(clock);
        engine.update(time);
        const float v = engine.signals().value(*engine.signals().find("lfo.bars"));
        const float bar = engine.signals().value(engine.timeSignals().barPhase);
        if (lastLfo >= 0.0f && v < lastLfo - 0.5f) {
            lfoWraps.push_back(time.renderTime);
        }
        if (lastBar >= 0.0f && bar < lastBar - 0.5f) {
            barWraps.push_back(time.renderTime);
        }
        lastLfo = v;
        lastBar = bar;
        beats[time.renderTime] = engine.timelineClock().beats;
        scale[time.renderTime] = engine.params().find("camera/height")->finalComponent(0);
    }
    REQUIRE(lfoWraps.size() >= 4);
    // The LFO wraps on the frame the bar does -- the same meter -- which is a placed bar line.
    CHECK(lfoWraps == barWraps);
    CHECK(worstAgainst(lfoWraps, truth) <= 0.030 + kFrame);
    // The timeline's clock reads 0 on bar 1 and 4 on bar 2, so a key there lands there.
    const auto near = [](const auto& map, double t) { return map.lower_bound(t)->second; };
    CHECK_THAT(near(beats, truth[0] + 0.25), WithinAbs(0.5, 0.06));
    CHECK_THAT(near(beats, truth[1] + 0.25), WithinAbs(4.5, 0.06));
    CHECK_THAT(static_cast<double>(near(scale, truth[0] + 0.5)), WithinAbs(1.5, 0.05));
    CHECK_THAT(static_cast<double>(near(scale, truth[1])), WithinAbs(3.0, 0.05));
}

TEST_CASE("The phrase length divides the phrase counters", "[integration][meter]") {
    const Fixture& f = pickupGroove();
    const auto& truth = f.groove.truth.downbeats;
    const auto phraseAt = [&](std::optional<int> pinned, double seconds) {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadAudio(f.wav).has_value());
        if (pinned) {
            engine.setPhraseBars(*pinned);
        }
        FixedStepClock clock(kFps);
        float count = 0.0f;
        for (int i = 0; i < static_cast<int>(seconds * kFps); ++i) {
            engine.update(engine.tick(clock));
            count = engine.signals().value(engine.timeSignals().phraseCount);
        }
        return std::make_pair(count, engine.meter().phraseBars);
    };
    // Just after bar 9's downbeat: phrase 2 (index 1) of 8 bars, phrase 3 (index 2) of 4, 5 of 2.
    const double t = truth[8] + 0.1;
    const auto [eight, bars8] = phraseAt(8, t);
    const auto [four, bars4] = phraseAt(4, t);
    const auto [two, bars2] = phraseAt(2, t);
    CHECK(bars8 == 8);
    CHECK(bars4 == 4);
    CHECK(bars2 == 2);
    CHECK(eight == 1.0f);
    CHECK(four == 2.0f);
    CHECK(two == 4.0f);

    // Phrases per section divides the section counter the same way (the parameter, set as the
    // Parameters panel sets it): 4-bar phrases, so just after bar 9 is section 2 (index 1) of 2-phrase
    // sections and still section 1 (index 0) of 4-phrase ones.
    const auto sectionAt = [&](int phrases) {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadAudio(f.wav).has_value());
        engine.params().findAs<int>(app::Engine::kPhraseBarsPath)->setBase(4);
        engine.params().findAs<int>(app::Engine::kSectionPhrasesPath)->setBase(phrases);
        FixedStepClock clock(kFps);
        float count = -9.0f;
        for (int i = 0; i < static_cast<int>(t * kFps); ++i) {
            engine.update(engine.tick(clock));
            count = engine.signals().value(engine.timeSignals().sectionCount);
        }
        return count;
    };
    CHECK(sectionAt(2) == 1.0f);
    CHECK(sectionAt(4) == 0.0f);
}

TEST_CASE("The meter's settings are exposed parameters, saved as parameters; detect stays detect",
          "[integration][meter][ui]") {
    const Fixture& f = pickupGroove();
    const auto dir = testsupport::processTempDir() / "musical_time_project";
    std::filesystem::create_directories(dir);
    const auto path = dir / "piece.json";
    {
        app::Engine engine(app::EngineMode::Offline);
        // Where an artist finds them: registered, exposed (Parameters panel > music > meter, and the
        // World Inspector), labelled with what they do and what their "detect" value is, serialized,
        // and not a modulation target.
        for (const char* p : {app::Engine::kBar1BeatPath, app::Engine::kPhraseBarsPath,
                              app::Engine::kSectionPhrasesPath}) {
            INFO(p);
            const params::IParameter* param = engine.params().find(p);
            REQUIRE(param != nullptr);
            CHECK(param->flags().exposed);
            CHECK(param->flags().serialized);
            CHECK_FALSE(param->flags().modulatable);
            CHECK(param->group() == "music");
            CHECK(param->kind() == params::ParamKind::Int);
            CHECK(param->label().find(' ') != std::string::npos); // a sentence, not a code name
            // The Parameters panel filters by the World window's authoring layer, which opens on
            // Intermediate: a group on neither the Beginner nor the Intermediate list is shown only
            // on Advanced (ADR-375). `music/` was on neither until this test.
            CHECK(ui::layerShowsPath(ui::AuthoringLayer::Beginner, param->path()));
            CHECK(ui::layerShowsPath(ui::AuthoringLayer::Intermediate, param->path()));
            // ...and it sits under the "meter" heading inside "music" (ADR-387's sub-groups).
            CHECK(ui::parameterSubGroup(param->path(), param->group()) == "meter");
        }
        CHECK(engine.params().find(app::Engine::kBar1BeatPath)->label().find("detect") != std::string::npos);
        CHECK(engine.params().find(app::Engine::kPhraseBarsPath)->label().find("detect") != std::string::npos);

        REQUIRE(engine.loadAudio(f.wav).has_value());
        engine.update(FrameTime{});
        REQUIRE(engine.saveProject(path).has_value());
        nlohmann::json doc = nlohmann::json::parse(std::ifstream(path));
        // Nothing pinned: the "detect" values are what is written, so the next analysis decides again.
        CHECK(doc["parameters"][app::Engine::kBar1BeatPath] == -1);
        CHECK(doc["parameters"][app::Engine::kPhraseBarsPath] == 0);
        CHECK(doc["parameters"][app::Engine::kSectionPhrasesPath] == 4);
        // And the old control keys are gone, not aliased.
        CHECK_FALSE(doc["control"].contains("phraseBars"));
        CHECK_FALSE(doc["control"].contains("sectionPhrases"));
    }
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadAudio(f.wav).has_value());
        // Set through the parameter, as the Parameters panel sets it.
        engine.params().findAs<int>(app::Engine::kBar1BeatPath)->setBase(5);
        engine.params().findAs<int>(app::Engine::kPhraseBarsPath)->setBase(8);
        CHECK(engine.meter().downbeat == 5);
        CHECK(engine.meter().phraseBars == 8);
        REQUIRE(engine.saveProject(path).has_value());
        nlohmann::json doc = nlohmann::json::parse(std::ifstream(path));
        CHECK(doc["parameters"][app::Engine::kBar1BeatPath] == 5);
        CHECK(doc["parameters"][app::Engine::kPhraseBarsPath] == 8);
    }
    app::Engine loaded(app::EngineMode::Offline);
    REQUIRE(loaded.loadProject(path).has_value());
    CHECK(loaded.barOffsetBeats() == 5);
    CHECK(loaded.phraseBarsPinned() == 8);
    CHECK(loaded.meter().downbeat == 5);
    CHECK(loaded.meter().phraseBars == 8);
    // The transport's bars readout counts from the pinned bar 1.
    const double origin = loaded.transport().snapshot().barOriginSeconds;
    CHECK_THAT(origin, WithinAbs(loaded.track()->beats().beatTimes[5], 1e-9));
    CHECK(app::formatBarsBeats(origin + 0.01, 120.0, 4, origin) == "1.1");
    CHECK(app::formatBarsBeats(origin - 0.01, 120.0, 4, origin) == "0.4");
    // A scene swap re-registers the parameter set; the settings survive it.
    loaded.newComposition();
    CHECK(loaded.meter().downbeat == 5);
    CHECK(loaded.params().findAs<int>(app::Engine::kBar1BeatPath)->base() == 5);
    // A new project starts detected again.
    loaded.newProject();
    CHECK_FALSE(loaded.barOffsetBeats().has_value());
    CHECK_FALSE(loaded.phraseBarsPinned().has_value());

    // A malformed value is refused by name rather than read as something.
    nlohmann::json doc = nlohmann::json::parse(std::ifstream(path));
    doc["parameters"][app::Engine::kBar1BeatPath] = "two";
    std::ofstream(path) << doc.dump();
    app::Engine refused(app::EngineMode::Offline);
    const auto r = refused.loadProject(path);
    REQUIRE_FALSE(r.has_value());
    CHECK(r.error().message.find(app::Engine::kBar1BeatPath) != std::string::npos);
    std::filesystem::remove_all(dir);
}

TEST_CASE("A meter setting left on detect says what the analysis decided", "[integration][meter][ui]") {
    const Fixture& f = pickupGroove();
    const auto noteFor = [](app::Engine& engine, const char* path) {
        const analysis::Meter meter = engine.meter();
        const app::Engine::MeterSource source = engine.meterSource();
        const bool downbeat = std::string_view(path) == app::Engine::kBar1BeatPath;
        const int base = engine.params().findAs<int>(path)->base();
        return ui::meterDetectNote(path, base, downbeat ? meter.downbeat : meter.phraseBars,
                                   downbeat ? source.downbeatDetected : source.phraseDetected,
                                   source.downbeatConfidence);
    };

    app::Engine engine(app::EngineMode::Offline);
    // No audio: nothing was detected, and the note says which default is in use.
    CHECK_FALSE(engine.meterSource().downbeatDetected);
    CHECK(noteFor(engine, app::Engine::kBar1BeatPath) == "nothing detected: beat 0");
    CHECK(noteFor(engine, app::Engine::kPhraseBarsPath) == "not clear from the audio: 4 bars");

    REQUIRE(engine.loadAudio(f.wav).has_value());
    const app::Engine::MeterSource source = engine.meterSource();
    CHECK(source.downbeatDetected);
    CHECK(source.downbeatConfidence > 0.5f);
    // The groove's bar 1 is the tracked beat after its one-beat pickup.
    const std::string downbeat = noteFor(engine, app::Engine::kBar1BeatPath);
    CHECK(downbeat.starts_with("detected: beat 1 (confidence "));
    // The phrase note agrees with the estimate, whichever way it went.
    const analysis::MeterEstimate& estimate = engine.track()->meterEstimate();
    CHECK(source.phraseDetected == (estimate.phraseBars > 0));
    CHECK(noteFor(engine, app::Engine::kPhraseBarsPath) ==
          (estimate.phraseBars > 0 ? "detected: " + std::to_string(estimate.phraseBars) + " bars"
                                   : std::string("not clear from the audio: 4 bars")));
    // Other parameters carry no note.
    CHECK(ui::meterDetectNote(app::Engine::kSectionPhrasesPath, 4, 4, false).empty());
    CHECK(ui::meterDetectNote("post/bloom/intensity", -1, 0, true).empty());

    // Control: pinned values are the person's word -- not detected, and the slider says them already.
    engine.setBarOffsetBeats(2);
    engine.setPhraseBars(8);
    CHECK_FALSE(engine.meterSource().downbeatDetected);
    CHECK_FALSE(engine.meterSource().phraseDetected);
    CHECK(noteFor(engine, app::Engine::kBar1BeatPath).empty());
    CHECK(noteFor(engine, app::Engine::kPhraseBarsPath).empty());
    // Back to detect, and the note returns.
    engine.setBarOffsetBeats(std::nullopt);
    engine.clearPhraseBars();
    CHECK(engine.meterSource().downbeatDetected);
    CHECK(noteFor(engine, app::Engine::kBar1BeatPath) == downbeat);
}

TEST_CASE("A project's old control.phraseBars is refused by name, not read", "[integration][meter]") {
    const Fixture& f = pickupGroove();
    const auto dir = testsupport::processTempDir() / "musical_time_stale_control";
    std::filesystem::create_directories(dir);
    const auto path = dir / "piece.json";
    {
        app::Engine engine(app::EngineMode::Offline);
        REQUIRE(engine.loadAudio(f.wav).has_value());
        REQUIRE(engine.saveProject(path).has_value());
    }
    const auto warningsAbout = [](const app::Engine& engine) {
        std::size_t n = 0;
        for (const std::string& w : engine.projectWarnings()) {
            n += w.find("control.phraseBars is no longer read") != std::string::npos ? 1u : 0u;
        }
        return n;
    };
    // Control: a project the engine saved carries no such key and draws no such warning.
    {
        app::Engine clean(app::EngineMode::Offline);
        REQUIRE(clean.loadProject(path).has_value());
        CHECK(warningsAbout(clean) == 0);
    }
    // A file written before ADR-896, with the old key: it loads, the phrase length is still the
    // estimate's (nothing pinned), and the loader says why the 3 did nothing.
    nlohmann::json doc = nlohmann::json::parse(std::ifstream(path));
    doc["control"]["phraseBars"] = 3;
    std::ofstream(path) << doc.dump();
    app::Engine old(app::EngineMode::Offline);
    REQUIRE(old.loadProject(path).has_value());
    CHECK(warningsAbout(old) == 1);
    CHECK_FALSE(old.phraseBarsPinned().has_value());
    CHECK(old.meter().phraseBars != 3);
    std::filesystem::remove_all(dir);
}

TEST_CASE("Pinning the meter after a sequence is baked moves its Bar events", "[integration][meter]") {
    const Fixture& f = pickupGroove();
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadAudio(f.wav).has_value());
    REQUIRE(engine.meter().downbeat == 1);
    const std::vector<double> beats = engine.track()->beats().beatTimes;
    const auto firstFiring = [&engine] {
        double first = 1e30;
        for (const auto* tier : {&engine.sequenceReport().events.baked, &engine.sequenceReport().events.dispatches}) {
            for (const seq::Firing& firing : *tier) {
                first = std::min(first, firing.timeSeconds);
            }
        }
        return first;
    };
    seq::Sequence piece = engine.sequence();
    piece.setBeatMarkers(beats);
    seq::SequenceEvent onBar;
    onBar.id = "every bar";
    onBar.when.kind = seq::TriggerKind::Bar;
    onBar.when.every = 1;
    onBar.what.kind = seq::EventActionKind::SetParameter;
    onBar.what.target = "camera/height";
    onBar.what.amount = glm::vec4(0.5f);
    onBar.what.mode = params::TrackMode::Add;
    onBar.what.holdSeconds = 0.1;
    piece.events.push_back(onBar);
    REQUIRE(engine.setSequence(std::move(piece)).has_value());
    CHECK(firstFiring() == beats[1]);

    // Pinned the way the Parameters panel pins it -- through the parameter, nobody told -- and the
    // next frame re-bakes: bar 1 is now tracked beat 2.
    engine.params().findAs<int>(app::Engine::kBar1BeatPath)->setBase(2);
    CHECK(firstFiring() == beats[1]); // nothing has run yet
    FixedStepClock clock(kFps);
    engine.update(engine.tick(clock));
    CHECK(firstFiring() == beats[2]);
    // Back to detect, and back to the estimate's bar 1.
    engine.params().findAs<int>(app::Engine::kBar1BeatPath)->setBase(-1);
    engine.update(engine.tick(clock));
    CHECK(firstFiring() == beats[1]);
}

TEST_CASE("A sequence's Bar and Beat events count from the meter's downbeat", "[integration][meter]") {
    seq::Sequence piece;
    std::vector<double> beats;
    for (int i = 0; i < 24; ++i) {
        beats.push_back(1.0 + 0.5 * i);
    }
    piece.setBeatMarkers(beats);
    analysis::Meter meter;
    meter.downbeat = 2;
    const seq::TriggerContext ctx = piece.triggerContext(meter);
    REQUIRE(ctx.barTimes.size() >= 5);
    CHECK(ctx.barTimes[0] == 2.0); // tracked beat 2
    CHECK(ctx.barTimes[1] == 4.0);
    // A Beat event with index 0 fires first on beat 1 of bar 1, as `beat.count` numbers it: every
    // fourth from there is every downbeat, and a Bar event lands on the same instants.
    seq::SequenceEvent onBeat;
    onBeat.id = "on the one";
    onBeat.when.kind = seq::TriggerKind::Beat;
    onBeat.when.every = 4;
    onBeat.when.index = 0;
    onBeat.what.kind = seq::EventActionKind::SetParameter;
    onBeat.what.target = "orb/scale";
    seq::SequenceEvent onBar = onBeat;
    onBar.id = "every bar";
    onBar.when.kind = seq::TriggerKind::Bar;
    onBar.when.every = 1;
    const std::vector<seq::SequenceEvent> events{onBeat, onBar};
    const seq::EventSchedule schedule = seq::resolveEvents(events, ctx);
    std::vector<double> beatTimes;
    std::vector<double> barTimes;
    for (const auto* tier : {&schedule.baked, &schedule.dispatches}) {
        for (const seq::Firing& firing : *tier) {
            (firing.eventIndex == 0 ? beatTimes : barTimes).push_back(firing.timeSeconds);
        }
    }
    std::sort(beatTimes.begin(), beatTimes.end());
    std::sort(barTimes.begin(), barTimes.end());
    REQUIRE(beatTimes.size() >= 5);
    CHECK(beatTimes.front() == 2.0);
    CHECK(beatTimes == barTimes);
}
