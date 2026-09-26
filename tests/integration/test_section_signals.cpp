// Sections on the bus, and what each one sounds like handed to the director (ADR-899).
//
// `section.index`, `section.progress`, `section.energy` and the `section.change` event come from the
// sequence's authored section timeline, are declared among the frame signals a seek replays, and a
// route on the event moves a parameter. The measured profile of each section's audio reaches the cue
// sheet, the song plan, the Director's musical context and `director.inspect_scene`.

#include "ai/director_tools.hpp"
#include "ai/tool_api.hpp"
#include "ai/tool_context.hpp"
#include "analysis/span_profile.hpp"
#include "app/camera_director.hpp"
#include "app/directing_context.hpp"
#include "app/engine.hpp"
#include "app/song_plan.hpp"
#include "core/time.hpp"
#include "directing/time_ref.hpp"
#include "song/section_cue.hpp"
#include "song/shot_language.hpp"
#include "support/groove.hpp"
#include "support/temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <map>
#include <vector>

using namespace avgen;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kFps = 60.0;

// 24 bars at 120 BPM: a groove, then two bars with no kick (a pull-back), then the groove again.
const testsupport::Groove& groove() {
    static const testsupport::Groove g = [] {
        testsupport::GrooveSpec spec;
        spec.bars = 24;
        spec.kicklessBars = {8, 9};
        return testsupport::makeGroove(spec);
    }();
    return g;
}

std::filesystem::path grooveWav() {
    static const std::filesystem::path path = [] {
        const auto p = testsupport::processTempDir() / "section_signals.wav";
        REQUIRE(groove().file.writeWav(p).has_value());
        return p;
    }();
    return path;
}

// Three sections on the groove's own bar lines, each with the energy its author gave it.
song::SectionTimeline timeline() {
    const auto& bars = groove().truth.downbeats;
    song::SectionTimeline t;
    t.durationSeconds = groove().truth.seconds;
    const auto section = [](const char* type, double start, double end, float energy) {
        song::Section s;
        s.type = type;
        s.startSeconds = start;
        s.endSeconds = end;
        s.energy = energy;
        s.density = energy * 0.5f;
        s.authored = true;
        return s;
    };
    t.sections.push_back(section("intro", 0.0, bars[8], 0.4f));
    t.sections.push_back(section("verse", bars[8], bars[10], 0.2f));
    t.sections.push_back(section("chorus", bars[10], t.durationSeconds, 0.9f));
    t.renumber();
    return t;
}

void install(app::Engine& engine) {
    REQUIRE(engine.loadAudio(grooveWav()).has_value());
    seq::Sequence piece = engine.sequence();
    piece.sectionTimeline = timeline();
    REQUIRE(engine.setSequence(std::move(piece)).has_value());
}

} // namespace

TEST_CASE("section.* follows the authored section timeline and its change event moves a parameter",
          "[integration][sections]") {
    app::Engine engine(app::EngineMode::Offline);
    install(engine);
    for (const char* name : {"section.index", "section.progress", "section.energy", "section.change"}) {
        INFO(name);
        REQUIRE(engine.signals().find(name).has_value());
    }
    CHECK(engine.signals().info(engine.timeSignals().timelineChange).isEvent);

    // A route on the event: camera/height carries no default route, so anything that moves it here.
    params::ModRoute route{.source = "section.change", .target = "camera/height", .amount = 4.0f};
    route.chain.envelope = params::EnvelopeMode::PeakHold;
    route.chain.envelopeHoldMs = 50.0f;
    route.chain.envelopeFallPerSecond = 8.0f;
    engine.modulator().addRoute(route);
    engine.rebind();
    auto* height = engine.params().find("camera/height");
    REQUIRE(height != nullptr);
    const float base = height->baseComponent(0);

    const auto& bars = groove().truth.downbeats;
    const song::SectionTimeline t = timeline();
    FixedStepClock clock(kFps);
    std::map<double, float> index;
    std::map<double, float> progress;
    std::map<double, float> energy;
    std::vector<double> raised;
    float lastIndex = -2.0f;
    std::vector<double> changes;
    for (int i = 0; i < static_cast<int>(44.0 * kFps); ++i) {
        const FrameTime time = engine.tick(clock);
        engine.update(time);
        const float idx = engine.signals().value(engine.timeSignals().timelineSection);
        index[time.renderTime] = idx;
        progress[time.renderTime] = engine.signals().value(engine.timeSignals().timelineProgress);
        energy[time.renderTime] = engine.signals().value(engine.timeSignals().timelineEnergy);
        if (lastIndex > -2.0f && idx != lastIndex) {
            changes.push_back(time.renderTime);
        }
        lastIndex = idx;
        if (height->finalComponent(0) > base + 2.0f) {
            raised.push_back(time.renderTime);
        }
    }
    const auto at = [](const std::map<double, float>& m, double seconds) { return m.lower_bound(seconds)->second; };
    CHECK(at(index, 1.0) == 0.0f);
    CHECK(at(index, bars[8] + 0.1) == 1.0f);
    CHECK(at(index, bars[20]) == 2.0f);
    CHECK_THAT(static_cast<double>(at(energy, 1.0)), WithinAbs(0.4, 1e-6));
    CHECK_THAT(static_cast<double>(at(energy, bars[9])), WithinAbs(0.2, 1e-6));
    CHECK_THAT(static_cast<double>(at(energy, bars[20])), WithinAbs(0.9, 1e-6));
    // Progress is how far through the section the playhead is.
    const double mid = 0.5 * (bars[8] + bars[10]);
    CHECK_THAT(static_cast<double>(at(progress, mid)), WithinAbs(0.5, 0.01));
    // The index changes on the first frame at or after each boundary...
    REQUIRE(changes.size() == 2);
    CHECK(changes[0] >= bars[8]);
    CHECK(changes[0] < bars[8] + 1.0 / kFps + 1e-9);
    CHECK(changes[1] >= bars[10]);
    CHECK(changes[1] < bars[10] + 1.0 / kFps + 1e-9);
    // ...and the event reached the route there, and only there.
    REQUIRE_FALSE(raised.empty());
    for (const double r : raised) {
        const bool nearChange = std::fabs(r - changes[0]) < 0.3 || std::fabs(r - changes[1]) < 0.3;
        CHECK(nearChange);
    }

    SECTION("control: without a section timeline nothing changes and the route stays still") {
        app::Engine bare(app::EngineMode::Offline);
        REQUIRE(bare.loadAudio(grooveWav()).has_value());
        bare.modulator().addRoute(route);
        bare.rebind();
        auto* h = bare.params().find("camera/height");
        FixedStepClock c(kFps);
        float peak = h->baseComponent(0);
        for (int i = 0; i < static_cast<int>(30.0 * kFps); ++i) {
            bare.update(bare.tick(c));
            peak = std::max(peak, h->finalComponent(0));
            CHECK(bare.signals().value(bare.timeSignals().timelineSection) == -1.0f);
        }
        CHECK(peak < h->baseComponent(0) + 0.01f);
    }
}

TEST_CASE("A seek lands on the section signals a play reaches", "[integration][sections][seek]") {
    const double target = 1080.0 / kFps; // 18 s, inside the middle section, on the play's frame grid
    app::Engine played(app::EngineMode::Offline);
    install(played);
    FixedStepClock clock(kFps);
    for (int i = 0; i <= 1080; ++i) {
        played.update(played.tick(clock));
    }
    REQUIRE_THAT(played.timelineClock().seconds, WithinAbs(target, 1e-9));

    app::Engine sought(app::EngineMode::Offline);
    install(sought);
    sought.seekSeconds(target);
    for (const auto id : {played.timeSignals().timelineSection, played.timeSignals().timelineProgress,
                          played.timeSignals().timelineEnergy, played.timeSignals().barPhase,
                          played.timeSignals().beatCount}) {
        INFO(played.signals().info(id).name);
        CHECK(sought.signals().value(id) == played.signals().value(id));
    }
    CHECK(sought.signals().value(sought.timeSignals().timelineSection) == 1.0f);
}

TEST_CASE("Each section's measured audio reaches the cue sheet, the plan and the Director",
          "[integration][sections][director]") {
    app::Engine engine(app::EngineMode::Offline);
    install(engine);
    const analysis::AnalysisTrack* track = engine.track();
    REQUIRE(track != nullptr);
    const song::SectionTimeline t = timeline();
    const song::ShotLanguage language;

    // The cue sheet: measured with the track, not without it.
    const std::vector<song::SectionCue> cues = song::cueSheet(t, language, track);
    REQUIRE(cues.size() == 3);
    for (const song::SectionCue& c : cues) {
        CHECK(c.audio.measured());
    }
    // The kickless middle section measures no kicks -- at most the next bar's first kick, whose onset
    // is stamped on an attack frame that can sit a few milliseconds before the boundary -- and the
    // groove around it two a second.
    CHECK_THAT(static_cast<double>(cues[0].audio.kickRate), WithinAbs(2.0, 0.2));
    CHECK(cues[1].audio.kickRate <= 0.25f);
    CHECK(cues[1].audio.onsetRate < cues[2].audio.onsetRate);
    CHECK(cues[2].audio.brightnessHz > 0.0f);
    // The section's own (authored) energy stays what its author said.
    CHECK(cues[1].energy == 0.2f);
    CHECK_FALSE(song::cueSheet(t, language).front().audio.measured());

    // The song plan carries it through, and through JSON.
    const auto plan = app::songPlanFromCues(cues);
    REQUIRE(plan.has_value());
    CHECK(plan->sections[1].audio == cues[1].audio);
    const auto back = app::SongPlan::fromJson(plan->toJson());
    REQUIRE(back.has_value());
    CHECK(back->sections[1].audio.kickRate == cues[1].audio.kickRate);
    CHECK(back->sections[1].audio.bandDb == cues[1].audio.bandDb);
    // ...and the engine's own plan is built with the track.
    const auto engines = app::songPlanForEngine(engine);
    REQUIRE(engines.has_value());
    CHECK(engines->sections[1].audio.measured());

    // The Director's musical context: the meter, the section's own numbers and the measured ones.
    const directing::MusicalContext music = app::musicalContextFor(engine);
    REQUIRE(music.sections.size() == 3);
    CHECK(music.downbeat == engine.meter().downbeat);
    CHECK(music.phraseBars == engine.meter().phraseBars);
    CHECK(music.sections[1].energy == 0.2f);
    CHECK(music.sections[1].audio.measured());
    CHECK(music.sections[1].audio.kickRate <= 0.25f);

    // director.inspect_scene says all of it.
    ai::ToolRegistry registry;
    ai::registerDirectorTools(registry);
    ai::ToolContext ctx(engine);
    const ai::ToolResult r = registry.invoke("director.inspect_scene", nlohmann::json::object(), ctx);
    REQUIRE(r.success);
    const nlohmann::json& sections = r.value["sections"];
    REQUIRE(sections.size() == 3);
    CHECK(sections[1]["energy"].get<float>() == 0.2f);
    REQUIRE(sections[1].contains("audio"));
    CHECK(sections[1]["audio"]["kickRate"].get<float>() <= 0.25f);
    CHECK(sections[0]["audio"]["bandsDb"].contains("bass"));
    CHECK(sections[0]["audio"].contains("brightnessHz"));
    CHECK(r.value["meter"]["downbeatBeat"] == engine.meter().downbeat);
    CHECK(r.value["meter"]["phraseBars"] == engine.meter().phraseBars);
}

TEST_CASE("The Director's bar numbers count from the meter's downbeat", "[directing][meter]") {
    directing::MusicalContext ctx;
    for (int i = 0; i < 40; ++i) {
        ctx.beatTimes.push_back(0.3 + 0.5 * i);
    }
    ctx.tempoBpm = 120.0;
    ctx.downbeat = 3; // a three-beat pickup
    directing::TimeRef bar2;
    bar2.kind = directing::TimeRef::Kind::Bar;
    bar2.bar = 2;
    bar2.beat = 1;
    const directing::TimeResolution r = directing::resolveTime(bar2, ctx);
    REQUIRE(r.seconds.has_value());
    CHECK(*r.seconds == ctx.beatTimes[3 + 4]);
    // The control: counted from the first tracked beat, as it was, "bar 2" was three beats early.
    ctx.downbeat = 0;
    CHECK(*directing::resolveTime(bar2, ctx).seconds == ctx.beatTimes[4]);
}
