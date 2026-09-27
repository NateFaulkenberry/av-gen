// Song Mode cutting "Rebuild", the song of Glowmere Valley 3 (ADR-920..923).
//
// The audit had the engine cut this song itself and measured what it made: 34 of 39 shots between
// 5.90 and 5.92 s, not one of 38 cuts on a downbeat, no pull-back, break, riser or drop detected, and
// the drop buried 0.44 s into a travelling shot. These tests are the proof that the Director now cuts
// to the music: the sections are the production's own 13 segments (docs/glowmere-valley-3/01-music.md
// §1.3), typed and treated as Glowmere Valley 3 types them, directed through the real path --
// section timeline, cue sheet with the analysed track, `songPlanFromCues`, `directSong` on the
// engine's beat grid -- and measured against the hand-measured grid, not the engine's own.
//
// SKIPs cleanly when the song is absent (tests/support/rebuild_track.hpp). Set AVGEN_SONG_REPORT to a
// path to have the cut report written there.

#include "app/camera_director.hpp"
#include "app/song_director.hpp"
#include "app/song_plan.hpp"
#include "song/section_cue.hpp"
#include "song/section_timeline.hpp"
#include "song/shot_language.hpp"
#include "support/rebuild_track.hpp"

#include <catch2/catch_test_macros.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace avgen;
using namespace avgen::testing;

namespace {

#define REQUIRE_REBUILD()                                                                                      \
    const analysis::AnalysisTrack* track = rebuildTrack();                                                    \
    if (track == nullptr) {                                                                                    \
        SKIP("Rebuild.mp3 (the GV3 master, sha256 53a7c00c...) is not present: set AVGEN_REBUILD_AUDIO or put " \
             "it at ~/Desktop/Rebuild.mp3");                                                                   \
    }

constexpr double kFps = 60.0;          // Glowmere Valley 3 renders at 60 fps
constexpr double kFilmEnd = 225.5;     // tools/make_glowmere_valley_3.py FILM_END

// The production's segmentation (01-music.md §1.3) with Glowmere Valley 3's section types and
// treatments (tools/make_glowmere_valley_3.py SECTION_TYPES) and its segment energies.
struct Segment {
    const char* name;
    int firstBar;
    int afterBar;
    const char* type;
    const char* intent;
    float energy;
};
constexpr Segment kSegments[] = {
    {"cold open", 1, 5, "establishing", "atmospheric_establishing", 0.69f},
    {"riff groove", 5, 15, "groove", "groove_coverage", 0.71f},
    {"first pull-back", 15, 17, "pause", "held_tension", 0.29f},
    {"groove 2", 17, 33, "groove", "slow_environmental_exploration", 0.73f},
    {"lift", 33, 41, "build", "increasing_movement", 0.80f},
    {"arrival", 41, 49, "peak", "rising_reveal", 0.81f},
    {"melodic plateau", 49, 73, "exploration", "environmental_performance_exploration", 0.81f},
    {"lead forward", 73, 81, "tension", "building_tension", 0.76f},
    {"suspension", 81, 89, "suspense", "suspended_locked_off", 0.74f},
    {"submerged break", 89, 93, "breakdown", "held_tension", 0.51f},
    {"riser", 93, 97, "riser", "increasing_movement", 0.87f},
    {"drop", 97, 121, "drop", "large_scale_dynamic_coverage", 0.83f},
    {"tail", 121, 123, "outro", "hard_transition", 0.41f},
};

song::SectionTimeline rebuildTimeline() {
    song::SectionTimeline timeline;
    for (const Segment& seg : kSegments) {
        song::Section s;
        s.type = seg.type;
        s.label = seg.name;
        // The first segment owns the silent pre-roll; the last runs to the film's end.
        s.startSeconds = seg.firstBar == 1 ? 0.0 : rebuildBeatAt(seg.firstBar);
        s.endSeconds = seg.afterBar == 123 ? kFilmEnd : rebuildBeatAt(seg.afterBar);
        s.shotIntent = std::string(seg.intent);
        s.energy = seg.energy;
        s.density = 0.0f; // as GV3 writes it: the measured profile is what carries density
        s.authored = true;
        timeline.sections.push_back(std::move(s));
    }
    timeline.durationSeconds = kFilmEnd;
    timeline.renumber();
    return timeline;
}

std::vector<world::HeroPoint> heroes() {
    const auto hero = [](const char* name, glm::vec3 position, float height, float radius, float importance) {
        world::HeroPoint h;
        h.name = name;
        h.assetId = "asset";
        h.position = position;
        h.height = height;
        h.radius = radius;
        h.importance = importance;
        h.preferredCameraDistance = std::max(height * 3.0f, 18.0f);
        h.activationRadius = h.preferredCameraDistance * 3.0f;
        return h;
    };
    return {hero("elder", glm::vec3(0.0f, 0.0f, -40.0f), 20.0f, 4.0f, 0.95f),
            hero("spire", glm::vec3(60.0f, 0.0f, 20.0f), 12.0f, 2.0f, 0.72f),
            hero("bloom", glm::vec3(-45.0f, 0.0f, 30.0f), 3.0f, 1.5f, 0.48f)};
}

std::vector<scene::CameraRig> cameras() {
    scene::CameraRig main;
    main.id = scene::kMainCamera;
    main.name = "Hero Free Roam";
    main.autoDirectorEligible = true;
    scene::CameraRig wide;
    wide.id = 2;
    wide.name = "Valley Wide";
    wide.focalLength = 24.0f;
    wide.position = glm::vec3(-168.0f, 96.0f, -150.0f);
    wide.target = glm::vec3(-8.0f, 6.0f, 46.0f);
    wide.autoDirectorEligible = true;
    scene::CameraRig watch;
    watch.id = 3;
    watch.name = "UFO Watch";
    watch.focalLength = 85.0f;
    watch.followNode = "visitor";
    watch.aimNode = "visitor";
    watch.autoDirectorEligible = true;
    return {main, wide, watch};
}

// The band the tests cut with: a bar (1.85 s at 130 BPM) to four, and a riser allowed down to a
// beat. Not GV3's current 4.6-6.5 s, which is 2.5 to 3.5 bars and forbids the two- and four-bar
// shots its own phrasing wants; the report shows what that band gives.
app::SongDirectorOptions optionsFor(const analysis::AnalysisTrack& track) {
    app::SongDirectorOptions o;
    o.minShotSeconds = 1.8;
    o.maxShotSeconds = 7.5;
    o.minBuildShotSeconds = 0.45;
    o.seed = 1;
    o.autonomy = app::Autonomy::Expressive;
    o.grid = app::musicalGridFrom(track.beats().beatTimes, app::estimatedMeter(track));
    return o;
}

app::SongPlan rebuildPlan(const analysis::AnalysisTrack& track) {
    const song::ShotLanguage language;
    auto plan = app::songPlanFromCues(song::cueSheet(rebuildTimeline(), language, &track));
    REQUIRE(plan.has_value());
    return *plan;
}

app::SongDirection cut(const analysis::AnalysisTrack& track, const app::SongPlan& plan,
                       const app::SongDirectorOptions& options, const char* suffix = "") {
    auto brief = app::briefFromHeroes(heroes());
    REQUIRE(brief.has_value());
    auto d = app::directSong(plan, *brief, cameras(), options);
    INFO((d ? std::string() : d.error().message));
    REQUIRE(d.has_value());
    if (const char* path = std::getenv("AVGEN_SONG_REPORT"); path != nullptr && *path != '\0') {
        std::ofstream(std::string(path) + suffix) << d->report().dump(2) << "\n";
    }
    return *d;
}

// The frame a cut or a beat first shows on, at 60 fps.
long frameOf(double seconds) { return static_cast<long>(std::ceil(seconds * kFps - 1e-9)); }

} // namespace

TEST_CASE("Rebuild: every section boundary is cut on its downbeat, within one frame of the true bar",
          "[song][director][rebuild]") {
    REQUIRE_REBUILD();
    const app::SongPlan plan = rebuildPlan(*track);
    const app::SongDirection d = cut(*track, plan, optionsFor(*track));
    REQUIRE(d.sections.size() == std::size(kSegments));

    // Every boundary between two sections is a cut, and it shows on the frame of the true downbeat
    // or the one after it -- the measure a 60 fps render can see. The engine's grid is ADR-896's
    // (mean 8.9 ms, worst 18.9 ms from the true bars); the cut is exactly on it.
    for (std::size_t i = 1; i < d.sections.size(); ++i) {
        const app::SongSectionCut& s = d.sections[i];
        const double truth = rebuildBeatAt(kSegments[i].firstBar);
        INFO(s.label << " cut at " << s.startSeconds << " s, true bar " << kSegments[i].firstBar << " at " << truth);
        const long lag = frameOf(s.startSeconds) - frameOf(truth);
        CHECK(lag >= 0);
        CHECK(lag <= 1);
        CHECK(std::abs(s.startSeconds - truth) < 0.020);
        // ...and it is a real cut: a shot starts there.
        CHECK(d.decisions[s.firstShot].startSeconds == s.startSeconds);
    }

    // Every cut in the film is on a tracked beat; most are on bar lines.
    const nlohmann::json report = d.report();
    const auto& stats = report["stats"];
    const std::size_t cuts = stats["cuts"].get<std::size_t>();
    const std::size_t onBeat = stats["cutsOnBeat"].get<std::size_t>();
    const std::size_t onDownbeat = stats["cutsOnDownbeat"].get<std::size_t>();
    INFO(cuts << " cuts, " << onBeat << " on a beat, " << onDownbeat << " on a downbeat");
    CHECK(onBeat == cuts);
    CHECK(static_cast<double>(onDownbeat) >= 0.7 * static_cast<double>(cuts));
    // Measured against the true grid, not the engine's: every cut within 20 ms of a true beat.
    for (const auto& c : report["cuts"]) {
        const double t = c["seconds"].get<double>();
        const double beats = (t - kRebuildFirstDownbeat) / kRebuildBeat;
        const double nearest = kRebuildFirstDownbeat + std::round(beats) * kRebuildBeat;
        INFO("cut at " << t);
        CHECK(std::abs(t - nearest) < 0.020);
    }
}

TEST_CASE("Rebuild: shot lengths are not one global duration", "[song][director][rebuild]") {
    REQUIRE_REBUILD();
    const app::SongPlan plan = rebuildPlan(*track);
    const nlohmann::json stats = cut(*track, plan, optionsFor(*track)).report()["stats"];
    // The measure is the audit's: the largest share of shots within 50 ms of one length (the engine's
    // own cut had 34 of 39 there, 0.87), and the spread (standard deviation over the mean). On a beat
    // grid lengths are whole beats, so a musical cut has a most common length -- two bars -- and the
    // bar is not the audit's 0.87 of the film. Plus how many different lengths, in whole beats.
    const app::SongDirection d = cut(*track, plan, optionsFor(*track));
    const nlohmann::json report = d.report();
    const double modal = stats["modalShare"].get<double>();
    const double cv = stats["cv"].get<double>();
    std::set<long> distinct;
    for (const app::SongDecision& s : d.decisions) {
        distinct.insert(std::lround(s.timing.beats));
    }
    INFO(stats.dump() << "; " << distinct.size() << " distinct lengths in beats");
    CHECK(modal <= 0.5);
    CHECK(cv >= 0.40);
    CHECK(distinct.size() >= 5);

    // Control: the same song cut with one Steady treatment everywhere and nothing read from the
    // music -- Guided, so neither density nor visual density moves it -- is the uniform film the
    // measure exists to catch. If this came out varied, the measure would be measuring nothing.
    app::SongPlan flat = plan;
    for (app::SongPlanSection& s : flat.sections) {
        s.intent.arc = song::Arc::Steady;
        s.intent.variation = 0.0f;
        s.intent.cutRate = 0.5f;
        s.intent.energy = 0.5f;
        s.intent.visualDensity = 0.5f;
        s.intent.distance = 0.5f;
        s.intent.heroEmphasis = 0.5f;
        s.intent.cameras = 2;
    }
    app::SongDirectorOptions guided = optionsFor(*track);
    guided.autonomy = app::Autonomy::Guided;
    const nlohmann::json flatStats = cut(*track, flat, guided, ".flat").report()["stats"];
    INFO("control: " << flatStats.dump());
    CHECK(flatStats["modalShare"].get<double>() > 0.6);
    CHECK(flatStats["cv"].get<double>() < 0.25);
}

TEST_CASE("Rebuild: the break and the riser (bars 89-96) accelerate into the drop", "[song][director][rebuild]") {
    REQUIRE_REBUILD();
    const app::SongPlan plan = rebuildPlan(*track);
    const app::SongDirection d = cut(*track, plan, optionsFor(*track));
    const double from = rebuildBeatAt(89) - 0.03;
    const double to = rebuildBeatAt(97) - 0.03;
    std::vector<double> beats;
    std::string lengths;
    for (const app::SongDecision& s : d.decisions) {
        if (s.startSeconds >= from && s.startSeconds < to) {
            beats.push_back(std::round(s.timing.beats));
            lengths += std::to_string(s.endSeconds - s.startSeconds) + " ";
        }
    }
    INFO("shot lengths from bar 89: " << lengths);
    REQUIRE(beats.size() >= 4);
    // Never longer than the shot before, in whole beats...
    for (std::size_t i = 1; i < beats.size(); ++i) {
        CHECK(beats[i] <= beats[i - 1]);
    }
    // ...and the last is a fraction of the first: an acceleration, not a plateau.
    CHECK(beats.back() * 4.0 <= beats.front());
    // The riser itself (bars 93-96) cuts at least three times.
    const app::SongSectionCut& riser = d.sections[10];
    REQUIRE(riser.label == "riser");
    CHECK(riser.shotCount >= 4);
}

TEST_CASE("Rebuild: the drop opens its own shot on its downbeat, with a cut", "[song][director][rebuild]") {
    REQUIRE_REBUILD();
    const app::SongPlan plan = rebuildPlan(*track);
    const app::SongDirection d = cut(*track, plan, optionsFor(*track));
    const double drop = rebuildBeatAt(97); // 177.71 s
    const app::SongDecision* opener = nullptr;
    for (const app::SongDecision& s : d.decisions) {
        if (std::abs(s.startSeconds - drop) < 1.0 / kFps) {
            opener = &s;
        }
    }
    REQUIRE(opener != nullptr);
    INFO(opener->line());
    CHECK(frameOf(opener->startSeconds) - frameOf(drop) <= 1);
    CHECK(opener->transition == scene::ShotTransition::Cut);
    CHECK(opener->timing.visible);
    CHECK(opener->sectionLabel == "drop");
    // ADR-922: the drop is the film's peak (the heaviest treatment on the loudest music), so it goes
    // to the film's hero rather than to whoever's turn it was.
    CHECK(opener->peak);
    CHECK(opener->subject == "elder");
    // No shot runs across it.
    for (const app::SongDecision& s : d.decisions) {
        CHECK_FALSE((s.startSeconds < drop - 0.05 && s.endSeconds > drop + 0.05));
    }
}
