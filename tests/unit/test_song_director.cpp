// Song Mode (ADR-249).
//
// The thing under test is not "does a camera move" -- the shot vocabulary and the bake were tested
// long before this existed. It is the four claims Song Mode is *for*:
//
//   1. The director consumes an authored section timeline and honours its boundaries.
//   2. The director consumes **semantic intent** and never a musical label. The test that holds
//      that one is `scrambling every label and intent id changes nothing`, which is a structural
//      guarantee rather than a documented intention: it cannot pass if anybody writes
//      `if (section.name == "Chorus")`.
//   3. The director makes real decisions -- which camera, how many cuts, what each shot is of --
//      so two passes over the same section are different films of the same music.
//   4. It does all that without costing ADR-091's scrub determinism, because every decision is a
//      function of the section's coordinates rather than of a stream or a frame.

#include "app/camera_director.hpp"
#include "app/engine.hpp"
#include "app/song_director.hpp"
#include "app/song_plan.hpp"
#include "directing/plan.hpp"
#include "entity/entity.hpp"
#include "scene/camera_rig.hpp"
#include "scene/composition.hpp"
#include "song/section_cue.hpp"
#include "song/section_timeline.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <map>
#include <set>
#include <string>

using namespace avgen;
using Catch::Matchers::WithinAbs;

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

std::vector<world::HeroPoint> threeHeroes() {
    return {hero("elder", glm::vec3(0.0f, 0.0f, -40.0f), 20.0f, 4.0f, 0.95f),
            hero("spire", glm::vec3(60.0f, 0.0f, 20.0f), 12.0f, 2.0f, 0.72f),
            hero("bloom", glm::vec3(-45.0f, 0.0f, 30.0f), 3.0f, 1.5f, 0.48f)};
}

app::DirectionBrief threeHeroBrief() {
    auto brief = app::briefFromHeroes(threeHeroes());
    REQUIRE(brief.has_value());
    return *brief;
}

// Three cameras with three different optical and compositional identities, which is what makes the
// camera *choice* a choice at all. Modelled on the shipped Glowmere demo: a hero camera the
// director bakes onto, a wide camera somebody placed, and a camera that rides something.
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

app::ShotIntentProfile intent(const char* id, float heroEmphasis, float distance, float movement,
                              float variation, float cutRate, int cameras) {
    app::ShotIntentProfile p;
    p.id = id;
    p.heroEmphasis = heroEmphasis;
    p.distance = distance;
    p.movement = movement;
    p.variation = variation;
    p.cutRate = cutRate;
    p.cameras = cameras;
    return p;
}

// The demonstration plan the brief's section 20 asks for, including a custom section type that no
// analyzer would ever produce. Every intent here is an *id plus six numbers*; the ids are for a
// person reading a log and nothing in the director looks at them.
app::SongPlan demoPlan() {
    app::SongPlan plan;
    plan.name = "glowmere-song";
    const auto add = [&](const char* label, double start, double end, app::ShotIntentProfile i,
                         float energy, float density, float transition, int occurrence) {
        app::SongPlanSection s;
        s.label = label;
        s.startSeconds = start;
        s.endSeconds = end;
        s.intent = std::move(i);
        s.energy = energy;
        s.density = density;
        s.transition = transition;
        s.occurrence = occurrence;
        plan.sections.push_back(std::move(s));
    };
    add("Intro", 0.0, 18.0, intent("Atmospheric Establishing", 0.15f, 0.92f, 0.22f, 0.20f, 0.10f, 1),
        0.20f, 0.18f, 0.0f, 0);
    add("Verse", 18.0, 43.0, intent("Hero Performance", 0.80f, 0.40f, 0.45f, 0.35f, 0.35f, 2),
        0.45f, 0.40f, 0.22f, 0);
    add("Pre-Chorus", 43.0, 57.0, intent("Building Tension", 0.60f, 0.55f, 0.80f, 0.60f, 0.60f, 2),
        0.62f, 0.66f, 0.18f, 0);
    add("Chorus", 57.0, 83.0, intent("Dynamic Hero Coverage", 0.88f, 0.60f, 0.85f, 0.85f, 0.80f, 3),
        0.92f, 0.85f, 0.32f, 0);
    // The second pass over the same material: the identical intent, one occurrence later.
    add("Verse", 83.0, 108.0, intent("Hero Performance", 0.80f, 0.40f, 0.45f, 0.35f, 0.35f, 2),
        0.45f, 0.40f, 0.30f, 1);
    // The custom section. Nothing in the engine has ever heard of it.
    add("Ocean Ambience", 108.0, 132.0,
        intent("Slow Environmental Exploration", 0.10f, 0.85f, 0.50f, 0.25f, 0.12f, 2), 0.25f, 0.20f,
        0.24f, 0);
    add("Final Chorus", 132.0, 166.0,
        intent("Peak Multi-Camera Hero", 0.90f, 0.62f, 0.90f, 0.90f, 0.85f, 3), 0.98f, 0.90f, 0.40f,
        1);
    add("Outro", 166.0, 190.0, intent("Slow Pullback", 0.30f, 0.95f, 0.30f, 0.20f, 0.08f, 1), 0.15f,
        0.12f, 0.35f, 0);
    return plan;
}

app::SongDirectorOptions guided() {
    app::SongDirectorOptions o;
    o.minShotSeconds = 4.0;
    o.maxShotSeconds = 12.0;
    o.seed = 1;
    o.autonomy = app::Autonomy::Expressive;
    return o;
}

// Everything about a decision except the two display strings. What must be identical when the
// labels are scrambled.
struct Decided {
    double start = 0.0;
    double end = 0.0;
    scene::CameraId camera = 0;
    app::ShotKind kind = app::ShotKind::Establish;
    std::string subject;
    float startDistance = 0.0f;
    float endDistance = 0.0f;
    friend bool operator==(const Decided&, const Decided&) = default;
};

std::vector<Decided> decided(const app::SongDirection& d) {
    std::vector<Decided> out;
    out.reserve(d.decisions.size());
    for (const app::SongDecision& s : d.decisions) {
        out.push_back(Decided{s.startSeconds, s.endSeconds, s.camera, s.kind, s.subject,
                              s.startDistance, s.endDistance});
    }
    return out;
}

} // namespace

// ================================================================================================
// The section timeline is the script
// ================================================================================================

TEST_CASE("Song Mode consumes the section timeline and keeps inside its boundaries",
          "[song][director]") {
    const auto cameras = threeCameras();
    auto direction = app::directSong(demoPlan(), threeHeroBrief(), cameras, guided());
    INFO((direction ? std::string() : direction.error().message));
    REQUIRE(direction.has_value());

    const app::SongPlan plan = demoPlan();
    REQUIRE(direction->sequence.shots.size() >= plan.sections.size());

    // Every shot lies wholly inside exactly one section. A shot that straddles a boundary is a cut
    // that is not on the music, which is the whole reason to read a section timeline at all.
    for (const app::Shot& shot : direction->sequence.shots) {
        const app::SongPlanSection* owner = nullptr;
        for (const app::SongPlanSection& section : plan.sections) {
            if (shot.startSeconds >= section.startSeconds - 1e-6 &&
                shot.endSeconds() <= section.endSeconds + 1e-6) {
                owner = &section;
                break;
            }
        }
        INFO("shot " << shot.name << " runs " << shot.startSeconds << ".." << shot.endSeconds());
        CHECK(owner != nullptr);
    }
    // ...and the film covers the piece: the first shot opens where the plan does and the last ends
    // where it ends.
    CHECK_THAT(direction->sequence.shots.front().startSeconds, WithinAbs(plan.sections.front().startSeconds, 1e-6));
    CHECK_THAT(direction->sequence.shots.back().endSeconds(), WithinAbs(plan.sections.back().endSeconds, 1e-6));

    // The camera track covers it too, with no gap and no overlap.
    REQUIRE_FALSE(direction->shots.empty());
    double cursor = plan.sections.front().startSeconds;
    for (const scene::CameraShot& shot : direction->shots) {
        CHECK_THAT(shot.startSeconds, WithinAbs(cursor, 1e-6));
        CHECK(shot.endSeconds > shot.startSeconds);
        cursor = shot.endSeconds;
    }
    CHECK_THAT(cursor, WithinAbs(plan.sections.back().endSeconds, 1e-6));
}

TEST_CASE("A section too short for the cut rate it asks for becomes one shot, and says so",
          "[song][director]") {
    app::SongPlan plan;
    app::SongPlanSection s;
    s.label = "Impact";
    s.startSeconds = 0.0;
    s.endSeconds = 9.0;
    s.intent = intent("Immediate Dramatic Framing", 0.9f, 0.3f, 0.9f, 0.9f, 1.0f, 3);
    s.energy = 1.0f;
    s.density = 1.0f;
    plan.sections.push_back(s);

    app::SongDirectorOptions options = guided();
    options.minShotSeconds = 5.0;
    auto direction = app::directSong(plan, threeHeroBrief(), threeCameras(), options);
    REQUIRE(direction.has_value());
    // 9 s against a 5 s floor is one shot, not one-and-four-fifths.
    CHECK(direction->sequence.shots.size() == 1);
    CHECK_FALSE(direction->warnings.empty());
}

// ================================================================================================
// The director consumes intent, never a label
// ================================================================================================

TEST_CASE("Scrambling every label and intent name changes nothing about the film",
          "[song][director][semantics]") {
    const auto cameras = threeCameras();
    const auto brief = threeHeroBrief();

    auto original = app::directSong(demoPlan(), brief, cameras, guided());
    REQUIRE(original.has_value());

    // The same plan with every human-readable string replaced by something meaningless. If a single
    // decision depended on a name -- a section function, an intent title, "Chorus", "Verse" -- this
    // is where it would show.
    app::SongPlan scrambled = demoPlan();
    int n = 0;
    for (app::SongPlanSection& section : scrambled.sections) {
        section.label = "zzz" + std::to_string(n);
        section.intent.id = "qqq" + std::to_string(n);
        ++n;
    }
    auto after = app::directSong(scrambled, brief, cameras, guided());
    REQUIRE(after.has_value());

    CHECK(decided(*original) == decided(*after));
    REQUIRE(original->shots.size() == after->shots.size());
    for (std::size_t i = 0; i < original->shots.size(); ++i) {
        // Everything but the label, which is the one field a name is allowed to reach.
        CHECK(original->shots[i].camera == after->shots[i].camera);
        CHECK_THAT(original->shots[i].startSeconds, WithinAbs(after->shots[i].startSeconds, 1e-6));
        CHECK_THAT(original->shots[i].endSeconds, WithinAbs(after->shots[i].endSeconds, 1e-6));
        CHECK(original->shots[i].transition == after->shots[i].transition);
    }
}

TEST_CASE("A custom section type is directed exactly as a built-in one with the same intent",
          "[song][director][semantics][custom]") {
    const auto cameras = threeCameras();
    const auto brief = threeHeroBrief();
    const auto profile = intent("Slow Environmental Exploration", 0.10f, 0.85f, 0.50f, 0.25f, 0.12f, 2);

    const auto direct = [&](const char* label) {
        app::SongPlan plan;
        app::SongPlanSection s;
        s.label = label;
        s.startSeconds = 0.0;
        s.endSeconds = 40.0;
        s.intent = profile;
        s.energy = 0.25f;
        s.density = 0.20f;
        plan.sections.push_back(s);
        auto d = app::directSong(plan, brief, cameras, guided());
        REQUIRE(d.has_value());
        return decided(*d);
    };

    // "Ocean Ambience" is a user-invented type; "Chorus" is as built-in as it gets. The director
    // cannot tell them apart, which is exactly the property the brief asks to be proved.
    CHECK(direct("Ocean Ambience") == direct("Chorus"));
    CHECK(direct("Ocean Ambience") == direct("Dream Sequence"));
    CHECK(direct("Ocean Ambience") == direct(""));
}

TEST_CASE("The intent, not the label, is what changes the film", "[song][director][semantics]") {
    const auto cameras = threeCameras();
    const auto brief = threeHeroBrief();

    const auto direct = [&](app::ShotIntentProfile i) {
        app::SongPlan plan;
        app::SongPlanSection s;
        s.label = "Chorus"; // identical in both arms
        s.startSeconds = 0.0;
        s.endSeconds = 40.0;
        s.intent = std::move(i);
        s.energy = 0.6f;
        s.density = 0.5f;
        plan.sections.push_back(s);
        auto d = app::directSong(plan, brief, cameras, guided());
        REQUIRE(d.has_value());
        return *d;
    };

    // Same name. Opposite intents. Different films -- which is the other half of the claim: the
    // labels are inert *and* the intent is not.
    const auto wideEnvironment = direct(intent("environment", 0.10f, 0.95f, 0.15f, 0.1f, 0.05f, 1));
    const auto closeHero = direct(intent("hero", 0.95f, 0.10f, 0.80f, 0.9f, 0.95f, 3));

    CHECK(wideEnvironment.sequence.shots.front().kind == app::ShotKind::Establish);
    CHECK(closeHero.sequence.shots.front().kind == app::ShotKind::Approach);
    // Wide means further away, in radii, everywhere in this director.
    CHECK(wideEnvironment.sequence.shots.front().startDistance >
          closeHero.sequence.shots.front().startDistance * 2.0f);
    // And a high cut rate cuts more than a low one.
    CHECK(closeHero.sequence.shots.size() > wideEnvironment.sequence.shots.size());
}

// ================================================================================================
// Multi-camera
// ================================================================================================

TEST_CASE("Song Mode uses the scene's cameras rather than one of them", "[song][director][multicam]") {
    auto direction = app::directSong(demoPlan(), threeHeroBrief(), threeCameras(), guided());
    REQUIRE(direction.has_value());
    CHECK(direction->camerasUsed() == 3);

    // ...and the shot track is what says so. Nothing here selects a camera at runtime: these are
    // authored shots, and `scene::resolveActiveCamera` remains the one thing that resolves them.
    std::set<scene::CameraId> onTrack;
    for (const scene::CameraShot& shot : direction->shots) {
        onTrack.insert(shot.camera);
        CHECK(shot.origin == scene::CameraShot::Origin::Authored); // the director marks them on install
    }
    CHECK(onTrack.size() == 3);
}

TEST_CASE("An intent that wants one camera gets one, and one that wants coverage gets more",
          "[song][director][multicam]") {
    const auto cameras = threeCameras();
    const auto brief = threeHeroBrief();
    const auto direct = [&](int wanted) {
        app::SongPlan plan;
        app::SongPlanSection s;
        s.label = "section";
        s.startSeconds = 0.0;
        s.endSeconds = 60.0;
        s.intent = intent("coverage", 0.7f, 0.5f, 0.6f, 0.7f, 0.9f, wanted);
        s.energy = 0.8f;
        s.density = 0.7f;
        plan.sections.push_back(s);
        auto d = app::directSong(plan, brief, cameras, guided());
        REQUIRE(d.has_value());
        return d->camerasUsed();
    };
    CHECK(direct(1) == 1);
    CHECK(direct(2) == 2);
    CHECK(direct(3) == 3);
}

TEST_CASE("An intent asking for more cameras than the scene has is told, not granted",
          "[song][director][multicam]") {
    std::vector<scene::CameraRig> one = {threeCameras().front()};
    app::SongPlan plan;
    app::SongPlanSection s;
    s.label = "section";
    s.startSeconds = 0.0;
    s.endSeconds = 60.0;
    s.intent = intent("coverage", 0.7f, 0.5f, 0.6f, 0.7f, 0.9f, 4);
    plan.sections.push_back(s);

    auto d = app::directSong(plan, threeHeroBrief(), one, guided());
    REQUIRE(d.has_value());
    CHECK(d->camerasUsed() == 1);
    REQUIRE_FALSE(d->warnings.empty());
}

TEST_CASE("Song Mode with no camera available to it is refused by name", "[song][director][multicam]") {
    auto d = app::directSong(demoPlan(), threeHeroBrief(), {}, guided());
    REQUIRE_FALSE(d.has_value());
    CHECK(d.error().message.find("available to the Auto-director") != std::string::npos);
}

TEST_CASE("Only the cameras the author made available are drawn from", "[song][director][multicam]") {
    scene::CameraDirection direction;
    direction.ensureMainCamera();
    scene::CameraRig wide;
    wide.name = "Valley Wide";
    wide.focalLength = 24.0f;
    wide.autoDirectorEligible = false; // a composition somebody made for one moment
    static_cast<void>(direction.addCamera(wide));
    scene::CameraRig roam;
    roam.name = "Second Unit";
    roam.focalLength = 50.0f;
    roam.autoDirectorEligible = true;
    static_cast<void>(direction.addCamera(roam));
    direction.cameras.front().autoDirectorEligible = true; // the main camera

    const auto eligible = app::eligibleCameras(direction);
    REQUIRE(eligible.size() == 2);
    for (const scene::CameraRig& rig : eligible) {
        CHECK(rig.name != "Valley Wide");
    }
}

// ================================================================================================
// Not deterministic shot playback
// ================================================================================================

TEST_CASE("Two passes over the same section are different films of the same music",
          "[song][director][variation]") {
    auto direction = app::directSong(demoPlan(), threeHeroBrief(), threeCameras(), guided());
    REQUIRE(direction.has_value());

    // The plan's two Verses carry a byte-identical intent and differ only in `occurrence`.
    const app::SongPlan plan = demoPlan();
    REQUIRE(plan.sections[1].intent == plan.sections[4].intent);
    REQUIRE(plan.sections[1].occurrence != plan.sections[4].occurrence);

    const auto decisionsIn = [&](std::size_t sectionIndex) {
        std::vector<app::SongDecision> out;
        for (const app::SongDecision& d : direction->decisions) {
            if (d.sectionIndex == sectionIndex) {
                out.push_back(d);
            }
        }
        return out;
    };
    const auto first = decisionsIn(1);
    const auto second = decisionsIn(4);
    REQUIRE_FALSE(first.empty());
    REQUIRE_FALSE(second.empty());

    // *Something* differs -- camera, subject or framing. This is the assertion that separates Song
    // Mode from playing back a precomputed shot list: an identical intent, executed twice, is not
    // executed identically.
    bool differs = false;
    const std::size_t n = std::min(first.size(), second.size());
    for (std::size_t i = 0; i < n; ++i) {
        differs = differs || first[i].camera != second[i].camera ||
                  first[i].subject != second[i].subject ||
                  std::abs(first[i].startDistance - second[i].startDistance) > 0.05f;
    }
    CHECK(differs);

    // ...and both are still appropriate: the intent is a hero intent, so both passes are hero
    // moves at hero distances, not one hero move and one aerial.
    for (const auto* pass : {&first, &second}) {
        for (const app::SongDecision& d : *pass) {
            INFO(d.line());
            CHECK((d.kind == app::ShotKind::Approach || d.kind == app::ShotKind::Track ||
                   d.kind == app::ShotKind::HeroReveal));
            CHECK(d.startDistance < 10.0f);
        }
    }
}

TEST_CASE("A second pass over the same material opens on a different camera",
          "[song][director][variation]") {
    const auto cameras = threeCameras();
    const auto brief = threeHeroBrief();

    // The identical intent twice, differing only in `occurrence` -- which is exactly what a Verse
    // and its reprise are. The opening camera is a guarantee rather than a tendency (ADR-249), so
    // this is a `CHECK`, not a "something differed".
    const auto openingCamera = [&](int occurrence, int wanted) {
        app::SongPlan plan;
        app::SongPlanSection s;
        s.label = "Verse";
        s.startSeconds = 0.0;
        s.endSeconds = 30.0;
        s.intent = intent("Hero Performance", 0.82f, 0.38f, 0.45f, 0.55f, 0.50f, wanted);
        s.energy = 0.45f;
        s.density = 0.38f;
        s.occurrence = occurrence;
        plan.sections.push_back(s);
        auto d = app::directSong(plan, brief, cameras, guided());
        REQUIRE(d.has_value());
        REQUIRE_FALSE(d->decisions.empty());
        return d->decisions.front().camera;
    };
    for (int pass = 0; pass < 6; ++pass) {
        INFO("pass " << pass);
        CHECK(openingCamera(pass, 2) != openingCamera(pass + 1, 2));
        CHECK(openingCamera(pass, 3) != openingCamera(pass + 1, 3));
    }
    // ...and a section that asked for one camera keeps it, because there is nothing to advance to.
    CHECK(openingCamera(0, 1) == openingCamera(1, 1));

    // Which is exactly why the *framing* has to vary on occurrence too, and separately: a section
    // with one camera has no camera to advance to, and would otherwise be the one case where a
    // second pass really is a replay. Same section index, same intent, one camera -- only the
    // occurrence differs, so only the decision hash can produce the difference.
    const auto framingOf = [&](int occurrence) {
        app::SongPlan plan;
        app::SongPlanSection s;
        s.label = "Verse";
        s.startSeconds = 0.0;
        s.endSeconds = 30.0;
        s.intent = intent("Hero Performance", 0.82f, 0.38f, 0.45f, 0.55f, 0.50f, 1);
        s.energy = 0.45f;
        s.density = 0.38f;
        s.occurrence = occurrence;
        plan.sections.push_back(s);
        auto d = app::directSong(plan, brief, cameras, guided());
        REQUIRE(d.has_value());
        std::vector<float> out;
        for (const app::SongDecision& decision : d->decisions) {
            out.push_back(decision.startDistance);
        }
        return out;
    };
    CHECK(framingOf(0) != framingOf(1));
    CHECK(framingOf(1) != framingOf(2));
}

TEST_CASE("The seed re-cuts the same plan into a different film", "[song][director][variation]") {
    const auto cameras = threeCameras();
    const auto brief = threeHeroBrief();
    app::SongDirectorOptions a = guided();
    app::SongDirectorOptions b = guided();
    b.seed = 7;

    auto first = app::directSong(demoPlan(), brief, cameras, a);
    auto second = app::directSong(demoPlan(), brief, cameras, b);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(decided(*first) != decided(*second));
}

TEST_CASE("Song Mode is deterministic: same inputs, same film", "[song][director][determinism]") {
    const auto cameras = threeCameras();
    const auto brief = threeHeroBrief();
    auto a = app::directSong(demoPlan(), brief, cameras, guided());
    auto b = app::directSong(demoPlan(), brief, cameras, guided());
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    CHECK(a->decisions == b->decisions);
    REQUIRE(a->shots.size() == b->shots.size());
    for (std::size_t i = 0; i < a->shots.size(); ++i) {
        CHECK(a->shots[i] == b->shots[i]);
    }
}

TEST_CASE("A directed camera track is a function of the second, not of how the playhead got there",
          "[song][director][determinism][adr091]") {
    auto direction = app::directSong(demoPlan(), threeHeroBrief(), threeCameras(), guided());
    REQUIRE(direction.has_value());

    scene::CameraDirection cameras;
    cameras.ensureMainCamera();
    cameras.cameras.front().name = "Hero Free Roam";
    for (const scene::CameraRig& rig : threeCameras()) {
        if (rig.id == scene::kMainCamera) {
            continue;
        }
        scene::CameraRig copy = rig;
        copy.id = scene::kNoCamera;
        static_cast<void>(cameras.addCamera(copy));
    }
    cameras.shots = direction->shots;
    REQUIRE(cameras.validate().has_value());

    // Resolve forwards, then resolve the same instants in a scrambled order. ADR-091: a seeked
    // second must be a function of the second.
    std::vector<double> instants;
    for (int i = 0; i < 400; ++i) {
        instants.push_back(static_cast<double>(i) * 0.47);
    }
    std::map<double, scene::CameraId> forward;
    for (const double t : instants) {
        forward[t] = scene::resolveActiveCamera(cameras, {}, t).camera;
    }
    for (std::size_t i = instants.size(); i-- > 0;) {
        const double t = instants[(i * 37) % instants.size()];
        CHECK(scene::resolveActiveCamera(cameras, {}, t).camera == forward[t]);
    }
}

// ================================================================================================
// Autonomy
// ================================================================================================

TEST_CASE("Locked is one shot on one camera; freedom buys coverage", "[song][director][autonomy]") {
    const auto cameras = threeCameras();
    const auto brief = threeHeroBrief();
    const auto direct = [&](app::Autonomy sectionAutonomy, app::Autonomy ceiling) {
        app::SongPlan plan;
        app::SongPlanSection s;
        s.label = "Chorus";
        s.startSeconds = 0.0;
        s.endSeconds = 60.0;
        s.intent = intent("Dynamic Hero Coverage", 0.85f, 0.55f, 0.8f, 0.8f, 0.8f, 3);
        s.energy = 0.95f;
        s.density = 0.85f;
        s.autonomy = sectionAutonomy;
        plan.sections.push_back(s);
        app::SongDirectorOptions o = guided();
        o.autonomy = ceiling;
        auto d = app::directSong(plan, brief, cameras, o);
        REQUIRE(d.has_value());
        return *d;
    };

    const auto locked = direct(app::Autonomy::Locked, app::Autonomy::Expressive);
    CHECK(locked.sequence.shots.size() == 1);
    CHECK(locked.camerasUsed() == 1);
    CHECK(locked.shots.size() == 1);
    // A locked section is an authored moment, so its shot is protected from an event camera.
    CHECK(locked.shots.front().locked);

    const auto guidedRun = direct(app::Autonomy::Guided, app::Autonomy::Expressive);
    CHECK(guidedRun.sequence.shots.size() > 1);
    CHECK(guidedRun.camerasUsed() > 1);
    CHECK_FALSE(guidedRun.shots.front().locked);

    // Expressive reads the section's own energy, so a loud section cuts at least as often as the
    // same section directed without that reading.
    const auto expressive = direct(app::Autonomy::Expressive, app::Autonomy::Expressive);
    CHECK(expressive.sequence.shots.size() >= guidedRun.sequence.shots.size());
}

TEST_CASE("The film-wide autonomy is a ceiling, so it can always be trusted to reduce",
          "[song][director][autonomy]") {
    app::SongPlan plan = demoPlan();
    for (app::SongPlanSection& s : plan.sections) {
        s.autonomy = app::Autonomy::Expressive; // every section asks for the most freedom there is
    }
    app::SongDirectorOptions o = guided();
    o.autonomy = app::Autonomy::Locked;

    auto d = app::directSong(plan, threeHeroBrief(), threeCameras(), o);
    REQUIRE(d.has_value());
    // One shot per section, whatever the sections asked for.
    CHECK(d->sequence.shots.size() == plan.sections.size());
    for (const app::SongDecision& decision : d->decisions) {
        CHECK(decision.autonomy == app::Autonomy::Locked);
    }
}

// ================================================================================================
// The seam, and the measurement-derived plan
// ================================================================================================

TEST_CASE("A song plan round-trips through JSON", "[song][plan][adr225]") {
    const app::SongPlan plan = demoPlan();
    auto back = app::songPlanFromJson(plan.toJson());
    INFO((back ? std::string() : back.error().message));
    REQUIRE(back.has_value());
    CHECK(*back == plan);
}

TEST_CASE("A song plan refuses what it cannot mean", "[song][plan]") {
    SECTION("an axis outside 0..1") {
        nlohmann::json doc = demoPlan().toJson();
        doc["sections"][0]["intent"]["distance"] = 1.4f;
        CHECK_FALSE(app::songPlanFromJson(doc).has_value());
    }
    SECTION("a section with no intent") {
        nlohmann::json doc = demoPlan().toJson();
        doc["sections"][0].erase("intent");
        CHECK_FALSE(app::songPlanFromJson(doc).has_value());
    }
    SECTION("overlapping sections") {
        nlohmann::json doc = demoPlan().toJson();
        doc["sections"][1]["start"] = 0.0;
        CHECK_FALSE(app::songPlanFromJson(doc).has_value());
    }
    SECTION("an autonomy nobody has heard of") {
        nlohmann::json doc = demoPlan().toJson();
        doc["sections"][0]["autonomy"] = "improvisational";
        CHECK_FALSE(app::songPlanFromJson(doc).has_value());
    }
    SECTION("a request for four hundred cameras") {
        nlohmann::json doc = demoPlan().toJson();
        doc["sections"][0]["intent"]["cameras"] = 400;
        CHECK_FALSE(app::songPlanFromJson(doc).has_value());
    }
}

TEST_CASE("A plan derived from measurements reads no labels at all", "[song][plan][semantics]") {
    analysis::SongStructure structure;
    structure.durationSeconds = 100.0;
    const auto add = [&](analysis::SectionFunction f, const char* label, double start, double end,
                         float energy, float density) {
        analysis::SongSection s;
        s.function = f;
        s.label = label;
        s.startSeconds = start;
        s.endSeconds = end;
        s.energy = energy;
        s.density = density;
        structure.sections.push_back(s);
    };
    add(analysis::SectionFunction::Intro, "", 0.0, 20.0, 0.2f, 0.15f);
    add(analysis::SectionFunction::Chorus, "", 20.0, 55.0, 0.9f, 0.8f);
    add(analysis::SectionFunction::Outro, "", 55.0, 100.0, 0.25f, 0.2f);

    const app::SongPlan a = app::songPlanFromMeasurements(structure);

    // Now relabel every one of them -- a different function, a person's own name -- and leave the
    // measurements alone. `Other` is included deliberately: ADR-206 says it is the correct answer
    // for a piece with no verses and no choruses, and Song Mode has to direct that piece.
    structure.sections[0].function = analysis::SectionFunction::Other;
    structure.sections[0].label = "Ocean Ambience";
    structure.sections[1].function = analysis::SectionFunction::Breakdown;
    structure.sections[1].label = "Character Reveal";
    structure.sections[2].function = analysis::SectionFunction::Drop;
    structure.sections[2].label = "Credits";
    const app::SongPlan b = app::songPlanFromMeasurements(structure);

    REQUIRE(a.sections.size() == b.sections.size());
    for (std::size_t i = 0; i < a.sections.size(); ++i) {
        // The label -- and only the label -- moved.
        CHECK(a.sections[i].label != b.sections[i].label);
        CHECK(a.sections[i].intent.heroEmphasis == b.sections[i].intent.heroEmphasis);
        CHECK(a.sections[i].intent.distance == b.sections[i].intent.distance);
        CHECK(a.sections[i].intent.movement == b.sections[i].intent.movement);
        CHECK(a.sections[i].intent.cutRate == b.sections[i].intent.cutRate);
        CHECK(a.sections[i].intent.cameras == b.sections[i].intent.cameras);
        CHECK(a.sections[i].energy == b.sections[i].energy);
    }

    // ...and the measurements really do reach the intent: the loud middle section is the one the
    // film is most about and the one it is closest to.
    CHECK(a.sections[1].intent.heroEmphasis > a.sections[0].intent.heroEmphasis);
    CHECK(a.sections[1].intent.distance < a.sections[0].intent.distance);
    CHECK(a.sections[1].intent.cameras > a.sections[0].intent.cameras);
}

// ================================================================================================
// The engine join
// ================================================================================================

namespace {

// A composition with the three demo cameras on it, so a test can install a Song Mode direction.
void installThreeCameras(app::Engine& engine) {
    scene::CameraDirection direction;
    direction.ensureMainCamera();
    direction.cameras.front().name = "Hero Free Roam";
    direction.cameras.front().autoDirectorEligible = true;
    for (const scene::CameraRig& rig : threeCameras()) {
        if (rig.id == scene::kMainCamera) {
            continue;
        }
        scene::CameraRig copy = rig;
        copy.id = scene::kNoCamera;
        copy.slug.clear();
        static_cast<void>(direction.addCamera(copy));
    }
    REQUIRE(engine.setCameraDirection(std::move(direction)).has_value());
}

} // namespace

TEST_CASE("Installing a song direction writes a camera track and replaces only its own shots",
          "[song][director][install]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    installThreeCameras(engine);

    // Somebody's own authored shot, which must survive every re-direction.
    {
        scene::CameraDirection direction = engine.composition()->cameraDirection();
        scene::CameraShot mine;
        mine.camera = scene::kMainCamera;
        mine.startSeconds = 300.0;
        mine.endSeconds = 305.0;
        mine.label = "mine";
        mine.locked = true;
        direction.shots.push_back(mine);
        REQUIRE(engine.setCameraDirection(std::move(direction)).has_value());
    }

    app::AutoDirectorSettings settings;
    settings.mode = app::DirectorMode::Song;
    settings.minShotSeconds = 4.0;

    auto direction = app::directSongFromPlan(threeHeroes(), demoPlan(),
                                             engine.composition()->cameraDirection(), settings);
    INFO((direction ? std::string() : direction.error().message));
    REQUIRE(direction.has_value());
    REQUIRE(app::installSongDirection(engine, *direction, settings).has_value());

    const auto& shots = engine.composition()->cameraDirection().shots;
    const auto authored = std::count_if(shots.begin(), shots.end(), [](const scene::CameraShot& s) {
        return s.origin == scene::CameraShot::Origin::Authored;
    });
    const auto directed = std::count_if(shots.begin(), shots.end(), [](const scene::CameraShot& s) {
        return s.origin == scene::CameraShot::Origin::Directed;
    });
    CHECK(authored == 1);
    CHECK(directed == static_cast<long>(direction->shots.size()));

    // Direct again: the director's shots are replaced, not stacked, and the person's is untouched.
    auto again = app::directSongFromPlan(threeHeroes(), demoPlan(),
                                         engine.composition()->cameraDirection(), settings);
    REQUIRE(again.has_value());
    REQUIRE(app::installSongDirection(engine, *again, settings).has_value());
    const auto& after = engine.composition()->cameraDirection().shots;
    CHECK(std::count_if(after.begin(), after.end(), [](const scene::CameraShot& s) {
              return s.origin == scene::CameraShot::Origin::Authored;
          }) == 1);
    CHECK(std::count_if(after.begin(), after.end(), [](const scene::CameraShot& s) {
              return s.origin == scene::CameraShot::Origin::Directed;
          }) == static_cast<long>(again->shots.size()));

    // Handing the camera back takes the director's shots off the camera -- parked, not deleted
    // (ADR-582) -- and leaves the person's.
    const std::vector<scene::CameraShot> directedBefore = [&] {
        std::vector<scene::CameraShot> out;
        for (const scene::CameraShot& s : engine.composition()->cameraDirection().shots) {
            if (s.origin == scene::CameraShot::Origin::Directed) {
                out.push_back(s);
            }
        }
        return out;
    }();
    const std::vector<scene::CameraShot> allBefore = engine.composition()->cameraDirection().shots;
    app::DirectorState state;
    app::noteDirected(engine, state);
    static_cast<void>(app::releaseDirectedCamera(engine, state));
    const auto& released = engine.composition()->cameraDirection().shots;
    CHECK(released.size() == 1);
    CHECK(released.front().label == "mine");
    CHECK(engine.parkedCut().cameraShots == directedBefore);

    // ...and Resume puts them back where they were, in the order they were in.
    REQUIRE(app::resumeDirectedCamera(engine, state).has_value());
    CHECK(engine.composition()->cameraDirection().shots == allBefore);
    CHECK_FALSE(engine.directorParked());
}

TEST_CASE("A camera shot's origin round-trips through the scene document", "[song][director][adr225]") {
    scene::CameraDirection direction;
    direction.ensureMainCamera();
    scene::CameraShot directed;
    directed.camera = scene::kMainCamera;
    directed.startSeconds = 0.0;
    directed.endSeconds = 4.0;
    directed.origin = scene::CameraShot::Origin::Directed;
    direction.shots.push_back(directed);
    scene::CameraShot authored;
    authored.camera = scene::kMainCamera;
    authored.startSeconds = 4.0;
    authored.endSeconds = 8.0;
    direction.shots.push_back(authored);

    auto back = scene::CameraDirection::fromJson(direction.toJson());
    REQUIRE(back.has_value());
    REQUIRE(back->shots.size() == 2);
    CHECK(back->shots[0].origin == scene::CameraShot::Origin::Directed);
    CHECK(back->shots[1].origin == scene::CameraShot::Origin::Authored);
    // The default is not written, so a project that has never used Song Mode is byte-identical.
    CHECK_FALSE(direction.toJson()["shots"][1].contains("origin"));
}

TEST_CASE("The song plan and the autonomy survive a project round trip", "[song][plan][adr225]") {
    const auto path = std::filesystem::temp_directory_path() / "avgen_song_plan_roundtrip.json";
    {
        app::Engine engine(app::EngineMode::Offline);
        engine.newComposition();
        engine.autoDirector().mode = app::DirectorMode::Song;
        engine.autoDirector().autonomy = app::Autonomy::Guided;
        engine.songPlan() = demoPlan();
        REQUIRE(engine.saveProject(path).has_value());
    }
    {
        app::Engine engine(app::EngineMode::Offline);
        engine.newComposition();
        REQUIRE(engine.loadProject(path).has_value());
        CHECK(engine.autoDirector().mode == app::DirectorMode::Song);
        CHECK(engine.autoDirector().autonomy == app::Autonomy::Guided);
        CHECK(engine.songPlan() == demoPlan());
    }
    std::filesystem::remove(path);
}

TEST_CASE("Song Mode needs a song, and says which one is missing", "[song][director][install]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    installThreeCameras(engine);
    app::AutoDirectorSettings settings;
    settings.mode = app::DirectorMode::Song;

    auto plan = app::songPlanForEngine(engine);
    REQUIRE_FALSE(plan.has_value());
    CHECK(plan.error().message.find("song structure") != std::string::npos);

    // An authored plan is enough on its own -- no audio, no analysis. Which is what lets an offline
    // render of a Song Mode cut work from the project alone.
    engine.songPlan() = demoPlan();
    auto authored = app::songPlanForEngine(engine);
    REQUIRE(authored.has_value());
    CHECK(authored->sections.size() == demoPlan().sections.size());
    REQUIRE(app::directEngine(engine, threeHeroes(), settings).has_value());
}

TEST_CASE("The two older modes are untouched by Song Mode's arrival", "[song][director][regression]") {
    // Continuous and Edited still direct from a fold of the audio, and neither has learned anything
    // about a plan. A structure is all they take.
    signals::MusicalStructure structure;
    double t = 0.0;
    const auto add = [&](signals::MusicalSection kind, double seconds) {
        signals::StructureSection s;
        s.kind = kind;
        s.startSeconds = t;
        s.durationSeconds = seconds;
        structure.sections.push_back(s);
        t += seconds;
    };
    add(signals::MusicalSection::Intro, 12.0);
    add(signals::MusicalSection::Build, 10.0);
    add(signals::MusicalSection::Drop, 18.0);
    add(signals::MusicalSection::Verse, 14.0);

    for (const app::DirectorMode mode :
         {app::DirectorMode::ContinuousShot, app::DirectorMode::EditedSequence}) {
        app::AutoDirectorSettings settings;
        settings.mode = mode;
        auto sequence = app::directHeroes(threeHeroes(), structure, settings);
        INFO(app::directorModeName(mode));
        REQUIRE(sequence.has_value());
        CHECK(sequence->shots.size() >= 4);
    }

    // ...and asking the old planner for Song Mode is refused rather than silently answered as
    // Edited, because the two read different inputs.
    app::DirectionBrief brief = threeHeroBrief();
    brief.mode = app::DirectorMode::Song;
    auto refused = app::directFromStructure(structure, brief);
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().message.find("Song Mode") != std::string::npos);
}

TEST_CASE("Song Mode does not enforce a relationship between controls it does not read",
          "[song][director][settings]") {
    // The panel disables `shortest build` in Song Mode, because Song Mode has no builds. Enforcing
    // the pair anyway made that a trap: lowering `shortest shot` past it produced a refusal whose
    // only cure was the slider the mode had just greyed out.
    app::AutoDirectorSettings s;
    s.minShotSeconds = 1.6;
    s.minBuildShotSeconds = 2.0; // the default, and now longer than the minimum

    s.mode = app::DirectorMode::ContinuousShot;
    CHECK_FALSE(s.validate().has_value());
    s.mode = app::DirectorMode::EditedSequence;
    CHECK_FALSE(s.validate().has_value());
    s.mode = app::DirectorMode::Song;
    CHECK(s.validate().has_value());

    // ...and it is still a positive number, in every mode. The value is checked; the relationship
    // is what is mode-dependent.
    s.minBuildShotSeconds = 0.0;
    CHECK_FALSE(s.validate().has_value());
}

TEST_CASE("Every director mode has a name and parses back", "[song][director]") {
    for (const app::DirectorMode mode : app::allDirectorModes()) {
        const auto parsed = app::directorModeFromName(app::directorModeName(mode));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == mode);
    }
    CHECK(app::allDirectorModes().size() == 3);
    for (const app::Autonomy a : app::allAutonomies()) {
        const auto parsed = app::autonomyFromName(app::autonomyName(a));
        REQUIRE(parsed.has_value());
        CHECK(*parsed == a);
    }
    CHECK(app::allAutonomies().size() == 3);
}

// ================================================================================================
// ADR-920..923: musical cuts, arcs, peaks, and the cut report
// ================================================================================================

namespace {

// A synthetic beat grid: `bpm`, the first beat at `first`, bar 1 on it, `phraseBars`-bar phrases. The
// director reads nothing else of the music's time, so this is the whole of what a grid test needs.
app::MusicalGrid gridOf(double bpm, double seconds, double first = 0.0, int phraseBars = 4) {
    app::MusicalGrid g;
    const double beat = 60.0 / bpm;
    for (double t = first; t <= seconds + 1e-9; t += beat) {
        g.beatTimes.push_back(t);
    }
    g.downbeat = 0;
    g.beatsPerBar = 4;
    g.phraseBars = phraseBars;
    return g;
}

app::SongPlanSection sectionOf(const char* label, double start, double end, app::ShotIntentProfile i,
                               float energy = 0.6f, float density = 0.5f) {
    app::SongPlanSection s;
    s.label = label;
    s.startSeconds = start;
    s.endSeconds = end;
    s.intent = std::move(i);
    s.energy = energy;
    s.density = density;
    s.autonomy = app::Autonomy::Expressive;
    return s;
}

app::ShotIntentProfile arcIntent(const char* id, song::Arc arc, float cutRate, float heroEmphasis = 0.6f,
                                 float distance = 0.5f, int cameras = 1) {
    app::ShotIntentProfile p = intent(id, heroEmphasis, distance, 0.5f, 0.5f, cutRate, cameras);
    p.arc = arc;
    return p;
}

app::SongDirectorOptions gridded(const app::MusicalGrid& grid) {
    app::SongDirectorOptions o = guided();
    o.minShotSeconds = 4.0;
    o.maxShotSeconds = 12.0;
    o.minBuildShotSeconds = 0.5;
    o.grid = grid;
    return o;
}

std::vector<double> lengthsIn(const app::SongDirection& d, std::size_t section) {
    std::vector<double> out;
    for (const app::SongDecision& s : d.decisions) {
        if (s.sectionIndex == section) {
            out.push_back(s.endSeconds - s.startSeconds);
        }
    }
    return out;
}

} // namespace

TEST_CASE("A Burst section opens on short shots and settles", "[song][director][arc][burst]") {
    const app::MusicalGrid grid = gridOf(120.0, 60.0);
    app::SongPlan plan;
    plan.sections.push_back(sectionOf("impact", 0.0, 32.0, arcIntent("impact", song::Arc::Burst, 0.9f)));
    auto d = app::directSong(plan, threeHeroBrief(), threeCameras(), gridded(grid));
    REQUIRE(d.has_value());
    const auto lengths = lengthsIn(*d, 0);
    std::string text;
    for (const app::SongDecision& s : d->decisions) {
        text += s.line() + " | " + s.timing.why + "\n";
    }
    INFO(text);
    REQUIRE(lengths.size() >= 3);
    CHECK(lengths.front() < 4.0);          // below the ordinary floor: a burst may
    CHECK(lengths.back() > lengths.front()); // and it settles
    for (std::size_t i = 1; i < lengths.size(); ++i) {
        CHECK(lengths[i] >= lengths[i - 1] - 0.13);
    }
}

TEST_CASE("A Rising section cuts monotonically shorter shots, below the ordinary floor",
          "[song][director][arc][rising]") {
    // 16 bars at 120 BPM, a treatment that accelerates.
    const app::MusicalGrid grid = gridOf(120.0, 60.0);
    const auto direct = [&](song::Arc arc) {
        app::SongPlan plan;
        plan.sections.push_back(sectionOf("build", 0.0, 32.0, arcIntent("build", arc, 0.8f)));
        auto d = app::directSong(plan, threeHeroBrief(), threeCameras(), gridded(grid));
        REQUIRE(d.has_value());
        return *d;
    };
    const app::SongDirection rising = direct(song::Arc::Rising);
    const auto lengths = lengthsIn(rising, 0);
    std::string text;
    for (const double l : lengths) {
        text += std::to_string(l) + " ";
    }
    INFO("rising: " << text);
    REQUIRE(lengths.size() >= 4);
    // Never longer than the shot before (a quarter beat of grid jitter allowed; this grid has none)...
    for (std::size_t i = 1; i < lengths.size(); ++i) {
        CHECK(lengths[i] <= lengths[i - 1] + 1e-6);
    }
    // ...an acceleration and not a plateau, and one that may go below the 4 s floor (ADR-921).
    CHECK(lengths.back() * 3.0 <= lengths.front());
    CHECK(lengths.back() < 4.0);
    CHECK(lengths.back() >= 0.5 - 1e-6); // the build floor: never below it

    // Control: the same section Steady never goes below the floor, and holds roughly one length.
    const auto steady = lengthsIn(direct(song::Arc::Steady), 0);
    REQUIRE_FALSE(steady.empty());
    for (const double l : steady) {
        CHECK(l >= 4.0 - 1e-6);
    }
    CHECK(*std::max_element(steady.begin(), steady.end()) <= 3.0 * *std::min_element(steady.begin(), steady.end()));
}

TEST_CASE("The build floor is the Auto-director panel's 'shortest build', and it reaches the cut",
          "[song][director][arc][settings]") {
    const app::MusicalGrid grid = gridOf(120.0, 60.0);
    app::SongPlan plan;
    plan.sections.push_back(sectionOf("riser", 0.0, 16.0, arcIntent("riser", song::Arc::Rising, 1.0f)));
    const auto shortest = [&](double buildFloor) {
        app::SongDirectorOptions o = gridded(grid);
        o.minBuildShotSeconds = buildFloor;
        auto d = app::directSong(plan, threeHeroBrief(), threeCameras(), o);
        REQUIRE(d.has_value());
        const auto lengths = lengthsIn(*d, 0);
        return *std::min_element(lengths.begin(), lengths.end());
    };
    // A beat (0.5 s here) lets the riser cut on the beat; two seconds holds it to a bar.
    CHECK(shortest(0.5) < 1.0);
    CHECK(shortest(2.0) >= 2.0 - 1e-6);
    // Never above the ordinary floor: a build may go below the floor, not above it.
    CHECK(shortest(10.0) <= 4.0 + 1e-6);
}

TEST_CASE("Every cut lands on the beat grid, and every section boundary on its downbeat",
          "[song][director][grid]") {
    // demoPlan's sections, authored a little off the grid -- as a hand-edited timeline or a detector
    // a beat early is (ADR-896) -- on a 120 BPM grid.
    app::SongPlan plan = demoPlan();
    const double offsets[] = {0.0, 0.31, -0.42, 0.18, -0.66, 0.51, -0.2, 0.37};
    for (std::size_t i = 1; i < plan.sections.size(); ++i) {
        plan.sections[i].startSeconds += offsets[i];
        plan.sections[i - 1].endSeconds = plan.sections[i].startSeconds;
    }
    const app::MusicalGrid grid = gridOf(120.0, 200.0);
    auto d = app::directSong(plan, threeHeroBrief(), threeCameras(), gridded(grid));
    REQUIRE(d.has_value());
    const auto beatIndex = [&](double t) -> std::optional<std::size_t> { return grid.nearestBeat(t, 1e-6); };
    for (std::size_t i = 1; i < d->decisions.size(); ++i) {
        const double t = d->decisions[i].startSeconds;
        INFO(d->decisions[i].line());
        const auto k = beatIndex(t);
        REQUIRE(k.has_value()); // on a beat, exactly
    }
    for (std::size_t si = 1; si < d->sections.size(); ++si) {
        const app::SongSectionCut& s = d->sections[si];
        INFO(s.label << " authored " << s.authoredStart << " cut " << s.startSeconds);
        const auto k = beatIndex(s.startSeconds);
        REQUIRE(k.has_value());
        CHECK(grid.strength(*k) >= 2);                             // a bar line
        CHECK(std::abs(s.startSeconds - s.authoredStart) <= 1.0); // within half a bar of the author's
        CHECK(d->decisions[s.firstShot].startSeconds == s.startSeconds);
    }
    // The film's own ends are not moved.
    CHECK(d->decisions.front().startSeconds == plan.sections.front().startSeconds);
    CHECK(d->decisions.back().endSeconds == plan.sections.back().endSeconds);

    // Control: without a grid the same plan cuts where the pace puts it, which is off the beat.
    auto free = app::directSong(plan, threeHeroBrief(), threeCameras(), guided());
    REQUIRE(free.has_value());
    std::size_t offBeat = 0;
    for (std::size_t i = 1; i < free->decisions.size(); ++i) {
        offBeat += beatIndex(free->decisions[i].startSeconds) ? 0 : 1;
    }
    CHECK(offBeat > 0);
}

TEST_CASE("A Suspended section holds", "[song][director][arc][suspended]") {
    const app::MusicalGrid grid = gridOf(120.0, 120.0);
    const auto shots = [&](double seconds) {
        app::SongPlan plan;
        plan.sections.push_back(sectionOf("stop", 0.0, seconds, arcIntent("stop", song::Arc::Suspended, 0.9f)));
        auto d = app::directSong(plan, threeHeroBrief(), threeCameras(), gridded(grid));
        REQUIRE(d.has_value());
        return lengthsIn(*d, 0);
    };
    // Held past the longest shot (12 s) -- a hold is the contrast -- up to twice it...
    CHECK(shots(20.0).size() == 1);
    // ...and split at the grid only beyond that.
    const auto long_ = shots(40.0);
    CHECK(long_.size() == 2);
    for (const double l : long_) {
        CHECK(l <= 24.0 + 2.0 + 1e-6);
    }
    // Control: the same cut rate Steady cuts many times.
    app::SongPlan plan;
    plan.sections.push_back(sectionOf("go", 0.0, 20.0, arcIntent("go", song::Arc::Steady, 0.9f)));
    auto d = app::directSong(plan, threeHeroBrief(), threeCameras(), gridded(grid));
    REQUIRE(d.has_value());
    CHECK(lengthsIn(*d, 0).size() >= 3);
}

TEST_CASE("A drop opens its own shot on its downbeat, with a cut", "[song][director][drop]") {
    // A groove, then an impact authored 0.3 s late, as a hand edit would put it.
    const app::MusicalGrid grid = gridOf(120.0, 80.0);
    app::SongPlan plan;
    plan.sections.push_back(sectionOf("groove", 0.0, 32.3, arcIntent("groove", song::Arc::Steady, 0.5f, 0.5f, 0.5f, 2),
                                      0.6f, 0.5f));
    plan.sections.push_back(sectionOf("drop", 32.3, 64.0, arcIntent("drop", song::Arc::Burst, 0.9f, 0.8f, 0.5f, 2),
                                      0.95f, 0.9f));
    plan.sections[1].intent.energy = 1.0f;
    auto d = app::directSong(plan, threeHeroBrief(), threeCameras(), gridded(grid));
    REQUIRE(d.has_value());
    const app::SongSectionCut& drop = d->sections[1];
    CHECK(drop.startSeconds == 32.0); // bar 17's downbeat, not the authored 32.3
    const app::SongDecision& opener = d->decisions[drop.firstShot];
    CHECK(opener.startSeconds == 32.0);
    CHECK(opener.transition == scene::ShotTransition::Cut);
    CHECK(opener.timing.visible);
    for (const app::SongDecision& s : d->decisions) {
        CHECK_FALSE((s.startSeconds < 32.0 - 1e-6 && s.endSeconds > 32.0 + 1e-6));
    }
    // The camera track has a cut exactly there too: an entry starts on the downbeat, or the
    // director's own camera re-frames across it.
    bool entry = false;
    for (const scene::CameraShot& s : d->shots) {
        entry = entry || std::abs(s.startSeconds - 32.0) < 1e-9;
    }
    CHECK((entry || opener.camera == scene::kMainCamera));
}

TEST_CASE("A peak section goes to its event's subject, else to the film's hero",
          "[song][director][peak]") {
    const app::MusicalGrid grid = gridOf(120.0, 90.0);
    const auto planWith = [](float peakPush) {
        app::SongPlan plan;
        plan.sections.push_back(sectionOf("verse", 0.0, 32.0, arcIntent("verse", song::Arc::Steady, 0.6f, 0.5f), 0.5f, 0.5f));
        plan.sections.push_back(sectionOf("chorus", 32.0, 64.0, arcIntent("chorus", song::Arc::Steady, 0.6f, 0.5f), 0.95f, 0.9f));
        plan.sections.push_back(sectionOf("outro", 64.0, 80.0, arcIntent("outro", song::Arc::Steady, 0.4f, 0.5f), 0.4f, 0.3f));
        plan.sections[0].intent.energy = 0.4f;
        plan.sections[1].intent.energy = peakPush;
        plan.sections[2].intent.energy = 0.3f;
        return plan;
    };
    const auto opener = [&](const app::SongPlan& plan) {
        auto d = app::directSong(plan, threeHeroBrief(), threeCameras(), gridded(grid));
        REQUIRE(d.has_value());
        REQUIRE(d->sections.size() == 3);
        return std::make_pair(d->decisions[d->sections[1].firstShot], d->sections[1]);
    };

    // The event: something happens to "bloom" -- the least important hero -- as the chorus lands.
    app::SongPlan withEvent = planWith(1.0f);
    withEvent.events.push_back(app::SongEvent{"setpiece/lift/beam", "bloom", 32.5, 40.0});
    const auto [shot, section] = opener(withEvent);
    CHECK(section.peak);
    CHECK(shot.subject == "bloom");
    CHECK(shot.subjectReason.find("setpiece/lift/beam") != std::string::npos);
    CHECK(section.eventSubject == "bloom");

    // No event: the film's hero, not whoever's turn it was.
    const auto [heroShot, heroSection] = opener(planWith(1.0f));
    CHECK(heroSection.peak);
    CHECK(heroShot.subject == "elder");

    // Control: the same chorus with an ordinary push is no peak, and the event does not take it.
    app::SongPlan ordinary = planWith(0.4f);
    ordinary.events = withEvent.events;
    ordinary.sections[0].intent.energy = 0.45f;
    ordinary.sections[2].intent.energy = 0.45f;
    const auto [rotShot, rotSection] = opener(ordinary);
    CHECK_FALSE(rotSection.peak);
    CHECK(rotShot.subjectReason == "rotation");

    // An event on somebody who is not a hero is said, not silently dropped.
    app::SongPlan stranger = planWith(1.0f);
    stranger.events.push_back(app::SongEvent{"abduction/beam", "visitor", 33.0, 0.0});
    auto d = app::directSong(stranger, threeHeroBrief(), threeCameras(), gridded(grid));
    REQUIRE(d.has_value());
    bool said = false;
    for (const std::string& w : d->warnings) {
        said = said || w.find("'visitor'") != std::string::npos;
    }
    CHECK(said);
}

TEST_CASE("Density, the subject's motion and scale move shot length", "[song][director][duration]") {
    const app::MusicalGrid grid = gridOf(120.0, 90.0);
    const auto meanLength = [](const std::vector<double>& v) {
        double sum = 0.0;
        for (const double x : v) {
            sum += x;
        }
        return v.empty() ? 0.0 : sum / static_cast<double>(v.size());
    };

    SECTION("a denser section cuts faster, when the director may read the music") {
        const auto direct = [&](float onsets, app::Autonomy autonomy) {
            app::SongPlan plan;
            app::SongPlanSection s = sectionOf("groove", 0.0, 64.0, arcIntent("groove", song::Arc::Steady, 0.5f));
            s.audio.frames = 100;
            s.audio.energy = 0.5f;
            s.audio.onsetRate = onsets;
            plan.sections.push_back(s);
            app::SongDirectorOptions o = gridded(grid);
            o.autonomy = autonomy;
            auto d = app::directSong(plan, threeHeroBrief(), threeCameras(), o);
            REQUIRE(d.has_value());
            return lengthsIn(*d, 0).size();
        };
        CHECK(direct(9.0f, app::Autonomy::Expressive) > direct(1.0f, app::Autonomy::Expressive));
        // Control: Guided does not read the music, so the two are cut alike.
        CHECK(direct(9.0f, app::Autonomy::Guided) == direct(1.0f, app::Autonomy::Guided));
    }

    SECTION("a subject that moves on its own holds its shot longer") {
        const auto direct = [&](float walkerMotion) {
            app::DirectionBrief brief = threeHeroBrief();
            brief.supporting[0].motion = walkerMotion; // spire walks
            app::SongPlan plan;
            app::ShotIntentProfile i = arcIntent("coverage", song::Arc::Steady, 0.55f, 0.5f);
            i.movement = 0.3f;  // shots that hold their subject, so each is about exactly one
            i.variation = 0.0f; // and no length variation: only motion can make a length differ
            plan.sections.push_back(sectionOf("walk", 0.0, 80.0, i));
            auto d = app::directSong(plan, brief, threeCameras(), gridded(grid));
            REQUIRE(d.has_value());
            std::vector<double> walker;
            std::vector<double> still;
            for (const app::SongDecision& s : d->decisions) {
                (s.subject == "spire" ? walker : still).push_back(s.endSeconds - s.startSeconds);
            }
            return std::make_pair(meanLength(walker), meanLength(still));
        };
        const auto [walking, standing] = direct(1.0f);
        INFO("walker " << walking << " s, still " << standing << " s");
        CHECK(walking > standing * 1.1);
        // Control: nobody moves, and nobody is held longer for it.
        const auto [a, b] = direct(0.0f);
        INFO("control " << a << " s against " << b << " s");
        CHECK(a < b * 1.35);
        CHECK(b < a * 1.35);
    }

    SECTION("a wide opener that establishes scale is held longer than the rest") {
        // Cut fast enough that the rest aim at two bars, where the grid can say "longer": at a
        // four-bar pace every length snaps to the same phrase line whatever it aimed at.
        const auto openerShare = [&](float distance, float hero, bool onGrid) {
            app::SongPlan plan;
            app::ShotIntentProfile i = arcIntent("open", song::Arc::Steady, 1.0f, hero, distance, 2);
            i.variation = 0.0f; // no length variation: only scale can make the opener differ
            plan.sections.push_back(sectionOf("open", 0.0, 48.0, i));
            auto d = app::directSong(plan, threeHeroBrief(), threeCameras(), onGrid ? gridded(grid) : guided());
            REQUIRE(d.has_value());
            const auto lengths = lengthsIn(*d, 0);
            REQUIRE(lengths.size() >= 3);
            const std::vector<double> rest(lengths.begin() + 1, lengths.end());
            return lengths.front() / meanLength(rest);
        };
        CHECK(openerShare(1.0f, 0.0f, true) > 1.25);   // the world, wide: establishes scale
        CHECK(openerShare(1.0f, 0.0f, false) > 1.4);   // and without a grid, the rule's own 1.6
        CHECK(openerShare(0.1f, 0.9f, true) < 1.15);   // control: a close hero shot does not
        CHECK(openerShare(0.1f, 0.9f, false) < 1.05);
    }
}

TEST_CASE("Scrambling every label changes nothing on a beat grid either", "[song][director][semantics][grid]") {
    const app::MusicalGrid grid = gridOf(120.0, 200.0);
    app::SongPlan plan = demoPlan();
    plan.sections[2].intent.arc = song::Arc::Rising;
    plan.sections[3].intent.arc = song::Arc::Burst;
    plan.sections[3].intent.energy = 1.0f;
    plan.sections[5].intent.arc = song::Arc::Suspended;
    plan.events.push_back(app::SongEvent{"lift", "spire", 58.0, 0.0});
    auto original = app::directSong(plan, threeHeroBrief(), threeCameras(), gridded(grid));
    REQUIRE(original.has_value());
    app::SongPlan scrambled = plan;
    int n = 0;
    for (app::SongPlanSection& s : scrambled.sections) {
        s.label = "zzz" + std::to_string(n);
        s.intent.id = "qqq" + std::to_string(n++);
    }
    scrambled.events.front().name = "qqq-event";
    auto after = app::directSong(scrambled, threeHeroBrief(), threeCameras(), gridded(grid));
    REQUIRE(after.has_value());
    CHECK(decided(*original) == decided(*after));
    // ...and the same inputs twice are the same film, to the byte (ADR-091 is a bake).
    auto again = app::directSong(plan, threeHeroBrief(), threeCameras(), gridded(grid));
    REQUIRE(again.has_value());
    CHECK(again->decisions == original->decisions);
    CHECK(again->report() == original->report());
}

TEST_CASE("The treatment's arc reaches the director through the song model's own arc",
          "[song][plan][arc][adapter]") {
    // `SongPlanSection::intentAt` is `SectionCue::intentAt`'s twin: the same `ShotIntent::atProgress`
    // over the same dials. Checked against the cue at every arc and five points of every section.
    for (const song::Arc arc : song::allArcs()) {
        song::ShotIntent treatment;
        treatment.id = "t";
        treatment.arc = arc;
        treatment.movement = 0.7f;
        treatment.energy = 0.8f;
        treatment.variation = 0.6f;
        treatment.cutFrequency = 0.9f;
        treatment.visualDensity = 0.35f;
        song::SectionCue cue;
        cue.startSeconds = 10.0;
        cue.endSeconds = 30.0;
        cue.displayName = "section";
        cue.intent = treatment;
        auto plan = app::songPlanFromCues(std::vector<song::SectionCue>{cue});
        REQUIRE(plan.has_value());
        const app::SongPlanSection& s = plan->sections.front();
        // Carried, not dropped (ADR-920).
        CHECK(s.intent.arc == arc);
        CHECK(s.intent.energy == 0.8f);
        CHECK(s.intent.visualDensity == 0.35f);
        for (const double t : {10.0, 14.0, 20.0, 26.0, 30.0}) {
            INFO(song::arcName(arc) << " at " << t);
            const song::ShotIntent fromCue = cue.intentAt(t);
            const app::ShotIntentProfile fromPlan = s.intentAt(t);
            CHECK(fromPlan.cutRate == fromCue.cutFrequency);
            CHECK(fromPlan.movement == fromCue.movement);
            CHECK(fromPlan.variation == fromCue.variation);
            CHECK(fromPlan.energy == fromCue.energy);
        }
    }
}

TEST_CASE("The new intent dials and the events round-trip, and a bad arc is refused",
          "[song][plan][adr225]") {
    app::SongPlan plan = demoPlan();
    plan.sections[1].intent.arc = song::Arc::Rising;
    plan.sections[1].intent.energy = 0.75f;
    plan.sections[1].intent.visualDensity = 0.2f;
    plan.events.push_back(app::SongEvent{"abduction/beam", "visitor", 170.3, 177.7});
    plan.events.push_back(app::SongEvent{"crash", "", 177.71, 0.0});
    auto back = app::songPlanFromJson(plan.toJson());
    INFO((back ? std::string() : back.error().message));
    REQUIRE(back.has_value());
    CHECK(*back == plan);
    // An event list in `director.watch_events`'s shape reads as it stands.
    CHECK(plan.toJson()["events"][0]["end"].get<double>() == 177.7);
    CHECK_FALSE(plan.toJson()["events"][1].contains("end"));

    nlohmann::json doc = plan.toJson();
    doc["sections"][0]["intent"]["arc"] = "wobbly";
    CHECK_FALSE(app::songPlanFromJson(doc).has_value());
    doc = plan.toJson();
    doc["events"][0]["end"] = 1.0; // ends before it starts
    CHECK_FALSE(app::songPlanFromJson(doc).has_value());
    // A plan written before these keys existed reads as Steady, push 0.5, frame 0.5, no events.
    doc = plan.toJson();
    doc.erase("events");
    for (auto& s : doc["sections"]) {
        s["intent"].erase("arc");
        s["intent"].erase("energy");
        s["intent"].erase("visualDensity");
    }
    auto old = app::songPlanFromJson(doc);
    REQUIRE(old.has_value());
    CHECK(old->events.empty());
    CHECK(old->sections[1].intent.arc == song::Arc::Steady);
    CHECK(old->sections[1].intent.energy == 0.5f);
}

TEST_CASE("The cut report carries every shot's span, subject, arc and reason", "[song][director][report]") {
    const app::MusicalGrid grid = gridOf(120.0, 200.0);
    app::SongPlan plan = demoPlan();
    plan.sections[2].intent.arc = song::Arc::Rising;
    auto d = app::directSong(plan, threeHeroBrief(), threeCameras(), gridded(grid));
    REQUIRE(d.has_value());
    const nlohmann::json r = d->report();
    CHECK(r["format"] == "avgen-song-cut");
    CHECK(r["version"] == 1);
    REQUIRE(r["grid"].is_object());
    CHECK(r["grid"]["beats"].get<std::size_t>() == grid.beatTimes.size());
    REQUIRE(r["shots"].size() == d->decisions.size());
    REQUIRE(r["sections"].size() == plan.sections.size());
    for (std::size_t i = 0; i < d->decisions.size(); ++i) {
        const auto& s = r["shots"][i];
        CHECK(s["start"].get<double>() == d->decisions[i].startSeconds);
        CHECK(s["end"].get<double>() == d->decisions[i].endSeconds);
        CHECK(s["subject"] == d->decisions[i].subject);
        CHECK_FALSE(s["why"].get<std::string>().empty());
        CHECK_FALSE(s["endsOn"].get<std::string>().empty());
        CHECK(s.contains("arc"));
        CHECK(s.contains("beats"));
    }
    CHECK(r["sections"][2]["arc"] == "rising");
    CHECK(r["stats"]["shots"].get<std::size_t>() == d->decisions.size());
    CHECK(r["stats"]["cutsOnBeat"].get<std::size_t>() == r["stats"]["cuts"].get<std::size_t>());
    // Without a grid the report says so rather than inventing bars.
    auto free = app::directSong(plan, threeHeroBrief(), threeCameras(), guided());
    REQUIRE(free.has_value());
    CHECK(free->report()["grid"].is_null());
    CHECK(free->report()["shots"][0]["endsOn"] == "free");
}

TEST_CASE("installSequence drops no baked track", "[song][director][install][adr922]") {
    // Every track the bake emits names a parameter this build registers, so every one is installed.
    // `camera/focus/emphasis` did not: it was baked on every direction and left out of every install
    // with a log line -- the control this test would have failed on before ADR-922.
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    installThreeCameras(engine);
    app::AutoDirectorSettings settings;
    settings.mode = app::DirectorMode::Song;
    settings.minShotSeconds = 4.0;
    auto direction = app::directSongFromPlan(threeHeroes(), demoPlan(), engine.composition()->cameraDirection(),
                                             settings);
    REQUIRE(direction.has_value());
    // The directions spotlight their heroes, which is what the emphasis track used to carry.
    REQUIRE(std::any_of(direction->sequence.shots.begin(), direction->sequence.shots.end(),
                        [](const app::Shot& s) { return s.spotlight.active; }));
    const nlohmann::json baked = direction->sequence.toTimelineTracks();
    for (const auto& track : baked) {
        const std::string target = track["target"].get<std::string>();
        INFO(target);
        CHECK(engine.params().find(target) != nullptr);
    }
    REQUIRE(app::installSongDirection(engine, *direction, settings).has_value());
    std::set<std::string> installed;
    for (const params::Track& t : engine.timeline().tracks()) {
        installed.insert(t.target);
    }
    for (const auto& track : baked) {
        CHECK(installed.count(track["target"].get<std::string>()) == 1);
    }
    CHECK(installed.size() == baked.size());
}

// ================================================================================================
// ADR-922/923 through the engine
// ================================================================================================

TEST_CASE("A watched play's events and the saved plan's reach Song Mode's plan", "[song][director][events]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    installThreeCameras(engine);
    engine.songPlan() = demoPlan();
    engine.songPlan().events.push_back(app::SongEvent{"authored", "spire", 60.0, 0.0});
    // A directing plan that watched the film (ADR-767): a scenario's world event and an event
    // camera's span, in the shape `director.watch_events` records them.
    directing::Plan watched;
    watched.id = "watched";
    watched.observation = std::make_pair(
        std::vector<directing::ObservedEvent>{{"abduction/beam", "bloom", 57.5, 0.0},
                                              {"camera/UFO Watch", "abduction", 57.0, 70.0}},
        120.0);
    engine.directingPlans().push_back(watched);

    auto plan = app::songPlanForEngine(engine);
    REQUIRE(plan.has_value());
    REQUIRE(plan->events.size() == 3);
    CHECK(plan->events[0].name == "camera/UFO Watch"); // in time order
    CHECK(plan->events[1].subject == "bloom");
    CHECK(plan->events[2].name == "authored");

    // ...and through the director: the Chorus (57-83 s) is demoPlan's loudest, heaviest section, so
    // its opener goes to the subject of the event it opens on, rather than to the rotation.
    for (app::SongPlanSection& s : engine.songPlan().sections) {
        s.intent.energy = 0.3f;
    }
    engine.songPlan().sections[3].intent.energy = 1.0f;
    app::AutoDirectorSettings settings;
    settings.mode = app::DirectorMode::Song;
    settings.minShotSeconds = 4.0;
    app::SongDirection cut;
    REQUIRE(app::directEngine(engine, threeHeroes(), settings, nullptr, &cut).has_value());
    REQUIRE(cut.sections.size() == engine.songPlan().sections.size());
    CHECK(cut.sections[3].peak);
    CHECK(cut.decisions[cut.sections[3].firstShot].subject == "bloom");
}

TEST_CASE("A plan handed to the director for a run is the plan it cuts", "[song][director][install][adr923]") {
    // The film outranks a saved plan (a project's sections are what is being edited) -- which used to
    // mean `--song-plan` on a project with sections was stored and never cut.
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    installThreeCameras(engine);
    seq::Sequence piece = engine.sequence();
    song::Section a;
    a.type = "verse";
    a.startSeconds = 0.0;
    a.endSeconds = 50.0;
    song::Section b;
    b.type = "chorus";
    b.startSeconds = 50.0;
    b.endSeconds = 100.0;
    piece.sectionTimeline.sections = {a, b};
    piece.sectionTimeline.durationSeconds = 100.0;
    piece.sectionTimeline.renumber();
    REQUIRE(engine.setSequence(piece).has_value());

    app::AutoDirectorSettings settings;
    settings.mode = app::DirectorMode::Song;
    settings.minShotSeconds = 4.0;
    app::SongDirection fromFilm;
    REQUIRE(app::directEngine(engine, threeHeroes(), settings, nullptr, &fromFilm).has_value());
    CHECK(fromFilm.sections.size() == 2); // the film's two sections

    const app::SongPlan handed = demoPlan();
    app::SongDirection fromPlan;
    REQUIRE(app::directEngine(engine, threeHeroes(), settings, &handed, &fromPlan).has_value());
    CHECK(fromPlan.sections.size() == handed.sections.size());
    CHECK(fromPlan.decisions.back().endSeconds == handed.sections.back().endSeconds);
}

TEST_CASE("How much a hero moves is read off the scene", "[song][director][motion]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    const auto node = [&](const char* name) {
        scene::CompositionNode n;
        n.name = name;
        n.kind = scene::NodeKind::Group;
        REQUIRE(engine.addNode(std::move(n)).has_value());
    };
    node("walker");
    node("hoverer");
    node("statue");
    const auto body = [](const char* name, const char* behaviour) {
        entity::EntityDesc d;
        d.name = name;
        entity::BehaviorDesc b;
        b.kind = behaviour;
        d.behaviors.push_back(b);
        return d;
    };
    REQUIRE(engine.composition()->setEntities({body("walker", "wander"), body("hoverer", "hover")}).has_value());
    const auto hero = [](const char* name) {
        world::HeroPoint h;
        h.name = name;
        h.radius = 1.0f;
        h.height = 2.0f;
        return h;
    };
    CHECK(app::heroMotion(engine, hero("walker")) == 1.0f);
    CHECK(app::heroMotion(engine, hero("hoverer")) == 0.5f);
    CHECK(app::heroMotion(engine, hero("statue")) == 0.0f);

    // ...and the director hears it: the inputs carry every hero's motion.
    std::vector<world::HeroPoint> heroes = {hero("statue"), hero("walker")};
    heroes[0].importance = 0.9f;
    heroes[1].importance = 0.5f;
    REQUIRE(engine.composition()->setHeroes(heroes).has_value());
    const app::SongInputs inputs = app::songInputsForEngine(engine);
    REQUIRE(inputs.motion.size() == 2);
    CHECK(inputs.motion[1].first == "walker");
    CHECK(inputs.motion[1].second == 1.0f);
    CHECK(inputs.grid.empty()); // no track, no grid
}

TEST_CASE("The engine's grid is its meter over the tracked beats", "[song][director][grid][meter]") {
    // Beat 2 of the tracker is bar 1 (a two-beat pickup), 8-bar phrases.
    std::vector<double> beats;
    for (int i = 0; i < 64; ++i) {
        beats.push_back(0.4 + 0.5 * i);
    }
    analysis::Meter meter;
    meter.downbeat = 2;
    meter.phraseBars = 8;
    const app::MusicalGrid grid = app::musicalGridFrom(beats, meter);
    CHECK(grid.beatSeconds() == 0.5);
    CHECK(grid.barNumber(2) == 1);
    CHECK(grid.beatInBar(2) == 1);
    CHECK(grid.barNumber(1) == 0);    // the pickup is bar 0
    CHECK(grid.beatInBar(1) == 4);
    CHECK(grid.strength(2) == 4);     // bar 1 is a phrase line
    CHECK(grid.strength(2 + 16) == 3); // bar 5: half an 8-bar phrase
    CHECK(grid.strength(2 + 4) == 2);
    CHECK(grid.strength(2 + 2) == 1);
    CHECK(grid.strength(3) == 0);
    REQUIRE(grid.nearestDownbeat(3.0, 1.0).has_value());
    CHECK(*grid.nearestDownbeat(3.0, 1.0) == 6); // 3.4 s, bar 2
}

TEST_CASE("Every arc says what it does to the cutting, where a treatment is chosen", "[song][director][ui]") {
    // The Sequence panel's section inspector and the Auto-director panel's section tooltip show this
    // beside a treatment, so an accelerating riser or a held suspension can be traced from the
    // picture to the control that made it (the owner's UI-reach rule). Named for what a viewer sees.
    std::set<std::string> seen;
    for (const song::Arc arc : song::allArcs()) {
        const std::string note = app::arcCutNote(arc);
        INFO(song::arcName(arc) << ": " << note);
        CHECK(note.rfind(song::arcName(arc), 0) == 0); // starts with the arc's own name
        CHECK(note.find("--") != std::string::npos);    // ...and then what it does
        seen.insert(note);
    }
    CHECK(seen.size() == song::allArcs().size());
    CHECK(std::string(app::arcCutNote(song::Arc::Rising)).find("shortest shot") != std::string::npos);
}
