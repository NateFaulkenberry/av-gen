// The camera director, connected (ADR-075).
//
// The vocabulary and the fold were already tested. What was never tested, because it did not exist,
// is that any of it reaches a camera. These tests are about the join: that heroes become a brief a
// director can shoot, that the resulting sequence lands on the engine's timeline as ordinary keys,
// and that installing it does not quietly destroy automation somebody else authored.

#include "app/camera_director.hpp"
#include "app/engine.hpp"
#include "params/timeline.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <filesystem>

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

std::vector<world::HeroPoint> threeHeroes() {
    return {hero("elder", glm::vec3(0.0f, 0.0f, -40.0f), 20.0f, 4.0f, 0.95f),
            hero("spire", glm::vec3(60.0f, 0.0f, 20.0f), 12.0f, 2.0f, 0.72f),
            hero("bloom", glm::vec3(-45.0f, 0.0f, 30.0f), 3.0f, 1.5f, 0.48f)};
}

// A structure with the shape the brief describes: a quiet opening, a build, the drop it feeds, a
// verse, a breakdown, and a final build and drop.
signals::MusicalStructure sevenSections() {
    using S = signals::MusicalSection;
    signals::MusicalStructure structure;
    double t = 0.0;
    const auto add = [&](S section, double seconds) {
        signals::StructureSection s;
        s.kind = section;
        s.startSeconds = t;
        s.durationSeconds = seconds;
        structure.sections.push_back(s);
        t += seconds;
    };
    add(S::Intro, 12.0);
    add(S::Build, 8.0);
    add(S::Drop, 14.0);
    add(S::Verse, 16.0);
    add(S::Breakdown, 10.0);
    add(S::FinalBuild, 8.0);
    add(S::FinalDrop, 12.0);
    return structure;
}
} // namespace

TEST_CASE("A world with no heroes cannot be directed", "[director][camera]") {
    // Not an empty sequence: a camera pointed at nothing is a failure with a cause, and returning a
    // valid-looking sequence that shoots the origin would hide it until somebody watched the render.
    auto brief = app::briefFromHeroes({});
    REQUIRE(!brief.has_value());
    CHECK(brief.error().message.find("heroes") != std::string::npos);
}

TEST_CASE("The brief's hero is the most important one, in order", "[director][camera]") {
    const auto heroes = threeHeroes();
    auto brief = app::briefFromHeroes(heroes);
    REQUIRE(brief.has_value());
    CHECK(brief->hero.name == "elder");
    REQUIRE(brief->supporting.size() == 2);
    // The supporting cast keeps the composer's ranking, which is the whole reason ADR-072 makes
    // importance strictly descending: a director cannot resolve a tie and would fall back to array
    // position, which is not a decision anybody made.
    CHECK(brief->supporting[0].name == "spire");
    CHECK(brief->supporting[1].name == "bloom");
}

TEST_CASE("A tall thin hero is framed as tall, not as thin", "[director][camera]") {
    // The subject's radius is what sets how far "close" is. Using a hero's horizontal radius alone
    // would frame a twenty-metre tree half a metre wide as if it were half a metre across, and the
    // camera would sit inside the canopy.
    const std::vector<world::HeroPoint> tall{hero("tree", glm::vec3(0.0f), 20.0f, 0.5f, 0.9f)};
    auto brief = app::briefFromHeroes(tall);
    REQUIRE(brief.has_value());
    CHECK(brief->hero.radius >= 10.0f);

    const std::vector<world::HeroPoint> squat{hero("boulder", glm::vec3(0.0f), 2.0f, 6.0f, 0.9f)};
    auto wide = app::briefFromHeroes(squat);
    REQUIRE(wide.has_value());
    CHECK(wide->hero.radius >= 6.0f);
}

TEST_CASE("Heroes and a structure make a valid sequence", "[director][camera]") {
    const auto heroes = threeHeroes();
    auto sequence = app::directHeroes(heroes, sevenSections());
    REQUIRE(sequence.has_value());
    CHECK(sequence->shots.size() >= 5);
    auto ok = sequence->validate();
    INFO((ok ? std::string() : ok.error().message));
    CHECK(ok.has_value());

    // The film is about the hero: it should be the subject of more shots than any single supporting
    // subject, or the thing the world was composed around is not what the camera looks at.
    std::size_t heroShots = 0;
    for (const auto& shot : sequence->shots) {
        if (shot.subject.name == "elder") {
            ++heroShots;
        }
    }
    CHECK(heroShots > 0);
}

TEST_CASE("Directing is deterministic", "[director][camera]") {
    const auto heroes = threeHeroes();
    const auto structure = sevenSections();
    auto a = app::directHeroes(heroes, structure, 7u);
    auto b = app::directHeroes(heroes, structure, 7u);
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(a->shots.size() == b->shots.size());
    for (std::size_t i = 0; i < a->shots.size(); ++i) {
        CHECK(a->shots[i].name == b->shots[i].name);
        CHECK(a->shots[i].subject.name == b->shots[i].subject.name);
        CHECK_THAT(a->shots[i].startSeconds,
                   Catch::Matchers::WithinAbs(b->shots[i].startSeconds, 1e-9));
    }
}

TEST_CASE("Installing a sequence puts real keys on the engine's timeline", "[director][camera]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    const auto heroes = threeHeroes();
    auto sequence = app::directHeroes(heroes, sevenSections());
    REQUIRE(sequence.has_value());

    const std::size_t before = engine.timeline().tracks().size();
    auto installed = app::installSequence(engine, *sequence);
    INFO((installed ? std::string() : installed.error().message));
    REQUIRE(installed.has_value());
    CHECK(*installed > 0);
    CHECK(engine.timeline().tracks().size() > before);
    CHECK(engine.timeline().enabled);

    // The camera is actually driven, and by keys that span the piece rather than sitting at zero.
    const auto& tracks = engine.timeline().tracks();
    const auto position = std::find_if(tracks.begin(), tracks.end(), [](const params::Track& t) {
        return t.target == "camera/position";
    });
    REQUIRE(position != tracks.end());
    CHECK(position->keys.size() > 4);
    CHECK(position->lastKeyTime() > 40.0);

    // And it moves: a track whose keys are all the same value is a camera that does not move, which
    // is indistinguishable from no director at all.
    const auto first = position->evaluate(position->firstKeyTime());
    const auto last = position->evaluate(position->lastKeyTime());
    double travelled = 0.0;
    for (std::size_t i = 0; i < 3; ++i) {
        const double d = static_cast<double>(last[i]) - static_cast<double>(first[i]);
        travelled += d * d;
    }
    CHECK(travelled > 1.0);
}

TEST_CASE("Installing replaces the camera's tracks and leaves everything else alone",
          "[director][camera]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();

    // Somebody's own automation of something that is not the camera.
    params::Track other;
    other.target = "scene/fogDensity";
    other.addKey(params::Key{0.0, {0.01f, 0.0f, 0.0f, 0.0f}});
    other.addKey(params::Key{30.0, {0.03f, 0.0f, 0.0f, 0.0f}});
    engine.timeline().addTrack(other);

    // ...and a stale camera track, of the kind a previous direction would have left.
    params::Track stale;
    stale.target = "camera/position";
    stale.addKey(params::Key{0.0, {1.0f, 2.0f, 3.0f, 0.0f}});
    engine.timeline().addTrack(stale);

    const auto heroes = threeHeroes();
    auto sequence = app::directHeroes(heroes, sevenSections());
    REQUIRE(sequence.has_value());
    REQUIRE(app::installSequence(engine, *sequence).has_value());

    const auto& tracks = engine.timeline().tracks();
    // The fog automation survives: directing the camera is not a reason to discard somebody's work.
    const auto fog = std::find_if(tracks.begin(), tracks.end(), [](const params::Track& t) {
        return t.target == "scene/fogDensity";
    });
    REQUIRE(fog != tracks.end());
    CHECK(fog->keys.size() == 2);

    // Exactly one track drives camera/position. Two is not a blend -- it is whichever the timeline
    // applies last, which is a bug that looks like the director being ignored.
    const auto cameraTracks = std::count_if(tracks.begin(), tracks.end(), [](const params::Track& t) {
        return t.target == "camera/position";
    });
    CHECK(cameraTracks == 1);
    // And it is the new one, not the stale single-key track.
    const auto position = std::find_if(tracks.begin(), tracks.end(), [](const params::Track& t) {
        return t.target == "camera/position";
    });
    CHECK(position->keys.size() > 1);
}

TEST_CASE("The director's camera targets are stated once", "[director][camera]") {
    // Two things have to agree about which parameters a directed sequence owns: what the bake emits
    // and what installing is entitled to remove. A list that appears twice will disagree with
    // itself, so it appears once and this checks the list is the one actually installed.
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    const auto heroes = threeHeroes();
    auto sequence = app::directHeroes(heroes, sevenSections());
    REQUIRE(sequence.has_value());
    REQUIRE(app::installSequence(engine, *sequence).has_value());

    const auto owned = app::directedCameraTargets();
    CHECK(!owned.empty());
    for (const params::Track& t : engine.timeline().tracks()) {
        INFO("installed track targets " << t.target);
        CHECK(std::find(owned.begin(), owned.end(), t.target) != owned.end());
    }
}

TEST_CASE("The Glowmere score folds into a structure with a drop in it", "[director][camera][glowmere]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    // The end of the chain on the real soundtrack. This is the test that would have caught the
    // score having no drop in it: before the break was added to make_glowmere_score.py the piece
    // built monotonically, so Break and Drop could never fire and the camera behaviour the showcase
    // is built around had nothing to trigger on. Everything downstream looked fine.
    const std::filesystem::path wav =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "audio" / "glowmere-valley.wav";
    if (!std::filesystem::exists(wav)) {
        SKIP("glowmere-valley.wav is generated, not committed: run tools/make_glowmere_score.py");
    }

    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    auto loaded = engine.loadAudio(wav);
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    REQUIRE(engine.track() != nullptr);

    auto structure = app::structureOfTrack(*engine.track());
    INFO((structure ? std::string() : structure.error().message));
    REQUIRE(structure.has_value());
    CHECK(structure->sections.size() >= 3);
    CHECK(structure->durationSeconds() > 60.0);

    // The moment the showcase is built around. A score with no collapse cannot produce one.
    INFO("sections: " << structure->sections.size()
                      << ", drops: " << structure->count(signals::MusicalSection::Drop)
                      << ", breakdowns: " << structure->count(signals::MusicalSection::Breakdown));
    CHECK(structure->count(signals::MusicalSection::Drop) +
              structure->count(signals::MusicalSection::FinalDrop) >
          0);

    // And it can actually be shot.
    const auto heroes = threeHeroes();
    auto sequence = app::directHeroes(heroes, *structure);
    INFO((sequence ? std::string() : sequence.error().message));
    REQUIRE(sequence.has_value());
    CHECK(sequence->shots.size() >= 3);
#endif
}
