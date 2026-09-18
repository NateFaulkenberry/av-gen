// **Does changing a section's type or treatment change the film?** (ADR-247, ADR-249, ADR-225)
//
// Reported as "I have eight Build sections all set to Rapid Multi-Shot Coverage; when I change this
// to anything I still get the same shot". Three separate things were true at once and this file
// keeps each of them measured rather than argued:
//
//   1. The *path* works. A section's treatment reaches the director through
//      `cueSheet -> songPlanFromCues -> directSong`, and changing it changes the bake. That is the
//      control arm ADR-182 demands: a probe that cannot tell the two apart proves nothing about
//      the case where they are the same, so the first test here has to FAIL if the feature were
//      genuinely unwired.
//
//   2. At the film-wide freedom the reporting project actually carries -- `"autonomy": "locked"` --
//      Build's own treatment (`increasing_movement`) and `rapid_multi_shot` bake **bit-identically**,
//      because the only three axes they differ on are `variation`, `cutRate` and `cameras`, and
//      Locked reads none of the three. That is not a bug in the director; it is Locked doing
//      exactly what it says. It is the whole of the reported symptom.
//
//   3. `songPlanFromCues` used to leave every section's autonomy at the `Guided` default, and the
//      effective autonomy is `min(section, film)`. So `Expressive` -- the setting whose entire
//      purpose is to read the section's own loudness -- was unreachable for **every** film directed
//      from its section timeline, which is every film Song Mode directs since the timeline became
//      the authority. A ceiling that cannot be raised to its own top value is a control nobody
//      keeps (ADR-225).

#include "app/camera_director.hpp"
#include "app/song_director.hpp"
#include "app/song_plan.hpp"
#include "scene/camera_rig.hpp"
#include "song/section_cue.hpp"
#include "song/section_timeline.hpp"
#include "song/shot_language.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fmt/format.h>

#include <algorithm>

#include <optional>
#include <string>
#include <vector>

using namespace avgen;

namespace {

world::HeroPoint hero(const char* name, glm::vec3 position, float height, float radius,
                      float importance) {
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
}

app::DirectionBrief threeHeroBrief() {
    const std::vector<world::HeroPoint> heroes = {
        hero("elder", glm::vec3(0.0f, 0.0f, -40.0f), 20.0f, 4.0f, 0.95f),
        hero("spire", glm::vec3(60.0f, 0.0f, 20.0f), 12.0f, 2.0f, 0.72f),
        hero("bloom", glm::vec3(-45.0f, 0.0f, 30.0f), 3.0f, 1.5f, 0.48f)};
    auto brief = app::briefFromHeroes(heroes);
    REQUIRE(brief.has_value());
    return *brief;
}

std::vector<scene::CameraRig> threeCameras() {
    scene::CameraRig main;
    main.id = scene::kMainCamera;
    main.name = "Hero Free Roam";
    main.autoDirectorEligible = true;

    scene::CameraRig wide;
    wide.id = 2;
    wide.name = "Valley Wide";
    wide.slug = "valleywide";
    wide.focalLength = 24.0f;
    wide.position = glm::vec3(-168.0f, 96.0f, -150.0f);
    wide.target = glm::vec3(-8.0f, 6.0f, 46.0f);
    wide.autoDirectorEligible = true;

    scene::CameraRig watch;
    watch.id = 3;
    watch.name = "UFO Watch";
    watch.slug = "ufowatch";
    watch.focalLength = 85.0f;
    watch.followNode = "visitor";
    watch.aimNode = "visitor";
    watch.autoDirectorEligible = true;
    return {main, wide, watch};
}

// The reporting project's own shape: eight Build sections of about three and a half seconds each,
// measured quiet (`energy` 0.11, `density` 0` -- both straight out of
// `glowmere-valley-2-multicam.json`), with an establishing section before them so the piece has
// somewhere to start.
song::SectionTimeline buildRun(std::optional<song::ShotIntentId> treatment) {
    song::SectionTimeline timeline;
    const auto add = [&](const char* type, double start, double end,
                         std::optional<song::ShotIntentId> intent) {
        song::Section s;
        s.type = type;
        s.startSeconds = start;
        s.endSeconds = end;
        s.energy = 0.1102084144949913f;
        s.density = 0.0f;
        s.shotIntent = std::move(intent);
        timeline.sections.push_back(std::move(s));
    };
    add("intro", 0.0, 59.5625, std::nullopt);
    double t = 59.5625;
    for (int i = 0; i < 8; ++i) {
        add("build", t, t + 3.6875, treatment);
        t += 3.6875;
    }
    timeline.durationSeconds = t;
    timeline.renumber();
    return timeline;
}

app::SongPlan planFor(const song::SectionTimeline& timeline) {
    const song::ShotLanguage language;
    auto plan = app::songPlanFromCues(song::cueSheet(timeline, language));
    REQUIRE(plan.has_value());
    return *plan;
}

// The reporting project's own timing band by default, so the numbers below are the numbers it
// gets: `"minShot": 3.7, "maxShot": 6.5`.
app::SongDirectorOptions options(app::Autonomy ceiling, double floorSeconds = 3.7) {
    app::SongDirectorOptions o;
    o.minShotSeconds = floorSeconds;
    o.maxShotSeconds = 6.5;
    o.seed = 1;
    o.autonomy = ceiling;
    return o;
}

app::SongDirection direct(const song::SectionTimeline& timeline, app::Autonomy ceiling,
                          double floorSeconds = 3.7) {
    auto d = app::directSong(planFor(timeline), threeHeroBrief(), threeCameras(),
                             options(ceiling, floorSeconds));
    REQUIRE(d.has_value());
    return *d;
}

// The bake, reduced to the structural quantities ADR-170 prefers over milliseconds: what was
// decided, not how long deciding took. Two of these being equal is two identical films.
struct Bake {
    std::size_t framingShots = 0;
    std::size_t cameraCuts = 0;
    std::size_t camerasUsed = 0;
    std::vector<std::string> lines;
    friend bool operator==(const Bake&, const Bake&) = default;
};

Bake baked(const app::SongDirection& d) {
    Bake b;
    b.framingShots = d.sequence.shots.size();
    b.cameraCuts = d.shots.size();
    b.camerasUsed = d.camerasUsed();
    for (const app::SongDecision& decision : d.decisions) {
        // Deliberately not `line()`: that carries the intent id, which differs by construction here
        // and would make every comparison below trivially true.
        b.lines.push_back(fmt::format("{:.4f}-{:.4f} cam{} {} {} {:.4f}->{:.4f}",
                                      decision.startSeconds, decision.endSeconds, decision.camera,
                                      app::shotKindName(decision.kind), decision.subject,
                                      static_cast<double>(decision.startDistance),
                                      static_cast<double>(decision.endDistance)));
    }
    return b;
}

} // namespace

// ================================================================================================
// A. The control arm: the probe can tell two treatments apart
// ================================================================================================

TEST_CASE("A section's treatment reaches the director and changes the bake",
          "[song][director][sections][adr182]") {
    // Build's own default treatment is `increasing_movement`; the owner's override is
    // `rapid_multi_shot`. At Guided the director may choose which camera, how many cuts and how
    // many cameras, which is what those two disagree about.
    const Bake byDefault = baked(direct(buildRun(std::nullopt), app::Autonomy::Guided));
    const Bake rapid = baked(direct(buildRun("rapid_multi_shot"), app::Autonomy::Guided));
    CHECK(byDefault != rapid);

    // **But not in the way the name promises, and this is the second half of the report.** The
    // reporting project's Build sections are 3.6875 s long and its shot floor is 3.7 s, so the
    // director may not put two shots in one of them however hard the treatment asks: `shotCount`
    // is `min(asked, floor(span / minShotSeconds))` and the second term is 1. Then
    // `wanted = min(wanted, shotCount)` takes the camera count down to 1 with it. So the two axes
    // `rapid_multi_shot` exists to move -- cut often, from several viewpoints -- are both pinned by
    // the section's *length*, and what survives is which single camera each section lands on.
    CHECK(rapid.framingShots == byDefault.framingShots);

    // Give the same two treatments room and the difference is the one the name promises. This is
    // the control ADR-182 asks for: an instrument that reports "no difference" for the case above
    // has to report a difference here, or it is measuring nothing.
    const Bake roomyDefault = baked(direct(buildRun(std::nullopt), app::Autonomy::Guided, 0.9));
    const Bake roomyRapid = baked(direct(buildRun("rapid_multi_shot"), app::Autonomy::Guided, 0.9));
    CHECK(roomyRapid.framingShots > roomyDefault.framingShots);
    CHECK(roomyRapid.cameraCuts > roomyDefault.cameraCuts);
}

TEST_CASE("Every built-in treatment reaches the director as a distinct set of dials",
          "[song][director][sections][inventory]") {
    // The inventory question, asked of the code rather than of the UI: 31 built-in intents, and no
    // two of them project onto the same six director axes. A collision here would be a picker with
    // two entries that cannot produce different films -- the silent no-op ADR-075 is about.
    const song::ShotLanguage language;
    std::vector<std::string> profiles;
    for (const song::ShotIntent* intent : language.intents()) {
        song::SectionTimeline timeline;
        song::Section s;
        s.type = "phrase";
        s.shotIntent = intent->id;
        s.endSeconds = 30.0;
        timeline.sections.push_back(s);
        timeline.durationSeconds = 30.0;
        // By value. A `const&` bound to `planFor(timeline).sections.front().intent` dangles --
        // lifetime extension does not reach through `front()` -- and the reward for that mistake
        // was thirty-one identical profiles and a test that looked like it had found the bug.
        const app::ShotIntentProfile p = planFor(timeline).sections.front().intent;
        profiles.push_back(fmt::format("{:.4f} {:.4f} {:.4f} {:.4f} {:.4f} {}",
                                       static_cast<double>(p.heroEmphasis),
                                       static_cast<double>(p.distance),
                                       static_cast<double>(p.movement),
                                       static_cast<double>(p.variation),
                                       static_cast<double>(p.cutRate), p.cameras));
    }
    const std::size_t total = profiles.size();
    CHECK(total >= 31);
    std::sort(profiles.begin(), profiles.end());
    profiles.erase(std::unique(profiles.begin(), profiles.end()), profiles.end());
    CHECK(profiles.size() == total);
}

// ================================================================================================
// B. The reported symptom, measured
// ================================================================================================

TEST_CASE("At Locked, Build's own treatment and Rapid Multi-Shot bake identically",
          "[song][director][sections][autonomy]") {
    // The reporting project carries `"autonomy": "locked"`. Locked is documented as "one shot, one
    // camera, framed exactly as the section asked", and these two treatments ask for the *same*
    // frame: they differ only on `variation`, `cutRate` and `cameras`, and Locked reads none of
    // them. So the film is identical, the report is accurate, and the fix is a setting rather than
    // a feature.
    const app::SongPlan byDefault = planFor(buildRun(std::nullopt));
    const app::SongPlan rapid = planFor(buildRun("rapid_multi_shot"));
    const app::ShotIntentProfile& a = byDefault.sections.back().intent;
    const app::ShotIntentProfile& b = rapid.sections.back().intent;
    // The three axes that differ...
    CHECK(a.variation != b.variation);
    CHECK(a.cutRate != b.cutRate);
    CHECK(a.cameras != b.cameras);
    // ...and the three that do not, which are the only three Locked acts on.
    CHECK(a.heroEmphasis == b.heroEmphasis);
    CHECK(a.distance == b.distance);
    CHECK(a.movement == b.movement);

    CHECK(baked(direct(buildRun(std::nullopt), app::Autonomy::Locked)) ==
          baked(direct(buildRun("rapid_multi_shot"), app::Autonomy::Locked)));
}

// ================================================================================================
// C. The defect: the film-wide ceiling could not reach its own top value
// ================================================================================================

TEST_CASE("The film-wide Expressive ceiling reaches a plan derived from the section timeline",
          "[song][director][sections][adr225]") {
    // A loud, busy section carrying an unremarkable treatment. Expressive exists to read exactly
    // those two measurements and cut more often for them.
    song::SectionTimeline timeline;
    song::Section s;
    s.type = "verse"; // -> hero_coverage, cutRate 0.30
    s.startSeconds = 0.0;
    s.endSeconds = 60.0;
    s.energy = 1.0f;
    s.density = 1.0f;
    timeline.sections.push_back(s);
    timeline.durationSeconds = 60.0;
    timeline.renumber();

    // A cue-derived section states no autonomy of its own, so it must not impose one: the effective
    // autonomy is `min(section, film)` and a section with no opinion has to sit at the top of the
    // range or the film-wide control can never reach it. This used to be `Guided` by default and
    // the assertion below failed -- silently, since a clamped ceiling looks exactly like a director
    // that read the measurements and was unimpressed.
    CHECK(planFor(timeline).sections.front().autonomy == app::Autonomy::Expressive);

    const auto guided = direct(timeline, app::Autonomy::Guided);
    const auto expressive = direct(timeline, app::Autonomy::Expressive);
    CHECK(expressive.sequence.shots.size() > guided.sequence.shots.size());

    // And the ceiling still only ever reduces: Locked is one shot whatever the section measured.
    CHECK(direct(timeline, app::Autonomy::Locked).sequence.shots.size() == 1);
}

TEST_CASE("A measurement-derived plan also lets the Expressive ceiling through",
          "[song][director][sections][adr225]") {
    // `songPlanFromMeasurements` is the other derivation, for a project with a detection and no
    // film. It hard-coded `Guided` for the same reason and with the same effect.
    analysis::SongStructure structure;
    analysis::SongSection s;
    s.startSeconds = 0.0;
    s.endSeconds = 60.0;
    s.energy = 1.0f;
    s.density = 1.0f;
    structure.sections.push_back(s);
    structure.durationSeconds = 60.0;

    const app::SongPlan plan = app::songPlanFromMeasurements(structure);
    REQUIRE(plan.sections.size() == 1);
    CHECK(plan.sections.front().autonomy == app::Autonomy::Expressive);
}
