// The camera director, connected (ADR-075).
//
// The vocabulary and the fold were already tested. What was never tested, because it did not exist,
// is that any of it reaches a camera. These tests are about the join: that heroes become a brief a
// director can shoot, that the resulting sequence lands on the engine's timeline as ordinary keys,
// and that installing it does not quietly destroy automation somebody else authored.

#include "app/camera_director.hpp"
#include "app/engine.hpp"
#include "core/time.hpp"
#include "params/timeline.hpp"
#include "scene/composition.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <limits>
#include <set>

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
    auto a = app::directHeroes(heroes, structure, app::AutoDirectorSettings{.seed = 7u});
    auto b = app::directHeroes(heroes, structure, app::AutoDirectorSettings{.seed = 7u});
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
    // Bound, which is what makes the difference between a track and a camera that moves. The
    // timeline writes only bound tracks and skips the rest silently, so this assertion is the one
    // that separates "six tracks installed" from "the camera went somewhere". Its absence is why
    // the director looked correct in tests and did nothing in the application.
    CHECK(position->param != nullptr);
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

TEST_CASE("Glowmere's own heroes and score direct a camera that travels between them",
          "[director][camera][glowmere]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    // The whole chain on the real showcase: the scene's declared heroes, the real soundtrack, the
    // real fold, the real director. Verifying this through the *authored* camera would be the wrong
    // test -- that camera is a hand-authored path that knows nothing about the heroes, which is
    // precisely the thing directing replaces.
    const std::filesystem::path project =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" / "glowmere-stylized.json";
    if (!std::filesystem::exists(project)) {
        SKIP("the Glowmere project is not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    auto loaded = engine.loadProject(project);
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    REQUIRE(engine.composition() != nullptr);

    const auto& heroes = engine.composition()->heroes();
    INFO("declared heroes: " << heroes.size());
    REQUIRE(heroes.size() >= 3);
    // Strictly descending, so the director can choose what the film is about.
    for (std::size_t i = 1; i < heroes.size(); ++i) {
        CHECK(heroes[i].importance < heroes[i - 1].importance);
    }
    // The warm accent belongs to one hero and one only. A second hero wearing it is the cheapest
    // possible way to lose the thing that makes the elder findable from anywhere in frame.
    const glm::vec3 warm(1.0f, 0.47f, 0.15f);
    const auto warmHeroes = std::count_if(heroes.begin(), heroes.end(), [&](const world::HeroPoint& h) {
        return glm::length(h.colorAccent - warm) < 0.2f;
    });
    CHECK(warmHeroes == 1);

    // Requires the generated soundtrack. It is deterministic and gitignored, so a checkout that has
    // not run the generator skips rather than fails.
    if (engine.track() == nullptr) {
        SKIP("glowmere-valley.wav is generated, not committed: run tools/make_glowmere_score.py");
    }
    auto installed = app::directEngine(engine, heroes);
    INFO((installed ? std::string() : installed.error().message));
    REQUIRE(installed.has_value());
    CHECK(*installed > 0);

    // And it visits more than one of them. A director handed four subjects that shoots one of them
    // is a director that has not been given anything the authored path did not already do.
    auto structure = app::structureOfTrack(*engine.track());
    REQUIRE(structure.has_value());
    auto sequence = app::directHeroes(heroes, *structure);
    REQUIRE(sequence.has_value());
    // Counted over subject *and* handoff. A transition's subject is where it starts -- the previous
    // shot's subject -- and its handoff is where it goes, so counting `subject` alone reports one
    // hero for a sequence that visits three. That is what this assertion did on its first run, and
    // it read as the director refusing to leave the elder.
    std::set<std::string> visited;
    for (const auto& shot : sequence->shots) {
        if (!shot.subject.name.empty()) {
            visited.insert(shot.subject.name);
        }
        if (shot.handoff && !shot.handoff->name.empty()) {
            visited.insert(shot.handoff->name);
        }
    }
    INFO("shots: " << sequence->shots.size() << ", heroes visited: " << visited.size());
    CHECK(sequence->shots.size() >= 3);
    CHECK(visited.size() >= 3);

    // The payoff lands on the payoff. A drop the camera does not answer is the single most
    // conspicuous way for a directed sequence to feel undirected.
    auto drop = std::find_if(structure->sections.begin(), structure->sections.end(),
                             [](const signals::StructureSection& sec) {
                                 return sec.kind == signals::MusicalSection::Drop ||
                                        sec.kind == signals::MusicalSection::FinalDrop;
                             });
    REQUIRE(drop != structure->sections.end());
    const bool shotOpensOnDrop =
        std::any_of(sequence->shots.begin(), sequence->shots.end(), [&](const app::Shot& shot) {
            return std::abs(shot.startSeconds - drop->startSeconds) < 0.5;
        });
    INFO("drop at " << drop->startSeconds << "s");
    CHECK(shotOpensOnDrop);
#endif
}

// Directing bakes: the shot is timeline keys from the moment it is cut, so starring an object
// afterwards changed nothing until it was cut again -- and the only way to ask for that was to hand
// the camera back to the viewport and re-direct it. Two menu items to see the effect of one click.
//
// `refreshDirection` is the noticing. It does not make the director live (a bake is what makes a
// directed camera scrubbable and renderable); it re-cuts when the heroes it was cut from have moved
// on, and gets out of the way in every case where the camera is no longer the director's.
TEST_CASE("A directed shot re-cuts itself when the heroes change", "[director][camera][redirect]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const std::filesystem::path wav =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "audio" / "glowmere-valley.wav";
    if (!std::filesystem::exists(wav)) {
        SKIP("glowmere-valley.wav is generated, not committed: run tools/make_glowmere_score.py");
    }
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    REQUIRE(engine.loadAudio(wav).has_value());
    REQUIRE(engine.track() != nullptr);
    scene::Composition* composition = engine.composition();
    REQUIRE(composition->setHeroes(threeHeroes()).has_value());

    // What a shot looks like, so a re-cut can be told from a repeat.
    const auto cameraKeys = [&]() -> std::vector<float> {
        const auto& tracks = engine.timeline().tracks();
        const auto it = std::find_if(tracks.begin(), tracks.end(), [](const params::Track& t) {
            return t.target == "camera/position";
        });
        if (it == tracks.end()) {
            return {};
        }
        std::vector<float> out;
        for (const params::Key& key : it->keys) {
            out.insert(out.end(), key.value.begin(), key.value.end());
        }
        return out;
    };

    app::DirectorState state;
    // Nothing is directed yet, so nothing happens however much the heroes change.
    REQUIRE(composition->setHeroes({threeHeroes()[1]}).has_value());
    auto quiet = app::refreshDirection(engine, state);
    REQUIRE(quiet.has_value());
    CHECK(*quiet == app::Redirect::Nothing);
    CHECK(cameraKeys().empty());

    // Cut the shot, the way the menu item does.
    REQUIRE(composition->setHeroes(threeHeroes()).has_value());
    REQUIRE(app::directEngine(engine, composition->heroes()).has_value());
    app::noteDirected(engine, state);
    const std::vector<float> first = cameraKeys();
    REQUIRE(!first.empty());

    // A frame with nothing changed is a counter comparison and no more.
    auto idle = app::refreshDirection(engine, state);
    REQUIRE(idle.has_value());
    CHECK(*idle == app::Redirect::Nothing);
    CHECK(cameraKeys() == first);

    // Unstar the subject: the shot is cut again, and it is a different shot.
    std::vector<world::HeroPoint> fewer = composition->heroes();
    fewer.erase(fewer.begin());
    REQUIRE(composition->setHeroes(fewer).has_value());
    auto recut = app::refreshDirection(engine, state);
    INFO((recut ? std::string() : recut.error().message));
    REQUIRE(recut.has_value());
    CHECK(*recut == app::Redirect::Recut);
    const std::vector<float> second = cameraKeys();
    CHECK(!second.empty());
    CHECK(second != first);
    CHECK(state.heroRevision == composition->heroRevision());
    // ...and having caught up, it stops.
    auto settled = app::refreshDirection(engine, state);
    REQUIRE(settled.has_value());
    CHECK(*settled == app::Redirect::Nothing);
    CHECK(cameraKeys() == second);

    SECTION("the last hero going hands the camera back rather than flying at nothing") {
        REQUIRE(composition->setHeroes({}).has_value());
        auto handed = app::refreshDirection(engine, state);
        REQUIRE(handed.has_value());
        CHECK(*handed == app::Redirect::HandedBack);
        CHECK(cameraKeys().empty());
        CHECK_FALSE(engine.timeline().isAutomated("camera/position"));
        CHECK_FALSE(state.directed);   // and it stops claiming a camera it no longer drives
    }

    SECTION("somebody else taking the camera ends the claim") {
        // What handing the camera back to the viewport does, done behind the director's back --
        // which is also what a project load, an undo or a deleted track look like from here.
        auto& tracks = engine.timeline().tracks();
        const auto owned = app::directedCameraTargets();
        std::erase_if(tracks, [&](const params::Track& t) {
            return std::find(owned.begin(), owned.end(), t.target) != owned.end();
        });
        auto released = app::refreshDirection(engine, state);
        REQUIRE(released.has_value());
        CHECK(*released == app::Redirect::Released);
        CHECK_FALSE(state.directed);
        // A hero change now does nothing at all: the camera is somebody else's.
        REQUIRE(composition->setHeroes(threeHeroes()).has_value());
        auto after = app::refreshDirection(engine, state);
        REQUIRE(after.has_value());
        CHECK(*after == app::Redirect::Nothing);
        CHECK(cameraKeys().empty());
    }
#endif
}

// The behaviour the panel's "[ignored]" marker describes, asserted against the engine rather than
// against the comment that claims it. Three ways of placing a camera, each reading its own
// parameters: this is the test that would fail if free mode ever started reading the orbit trio, or
// if orbit mode stopped.
TEST_CASE("Each camera mode reads its own parameters and no others", "[camera][modes]") {
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    auto set = [&](const char* path, float value) {
        params::IParameter* p = engine.params().find(path);
        REQUIRE(p != nullptr);
        p->setBaseComponent(0, value);
    };
    auto setVec = [&](const char* path, glm::vec3 v) {
        params::IParameter* p = engine.params().find(path);
        REQUIRE(p != nullptr);
        for (int i = 0; i < 3; ++i) {
            p->setBaseComponent(static_cast<std::size_t>(i), v[i]);
        }
    };
    auto placed = [&] {
        FrameTime time;
        time.renderTime = 1.0;
        time.deltaTime = 1.0 / 60.0;
        engine.update(time);
        return engine.scene().camera.position;
    };

    set("camera/orbitSpeed", 0.0f);   // a still camera, so a move means a parameter and not time
    setVec("camera/position", glm::vec3(0.0f, 2.0f, 10.0f));
    setVec("camera/target", glm::vec3(0.0f));

    SECTION("orbit mode is placed by distance and height") {
        set("camera/mode", 0.0f);
        set("camera/distance", 10.0f);
        set("camera/height", 3.0f);
        const glm::vec3 before = placed();
        CHECK_THAT(before.y, Catch::Matchers::WithinAbs(3.0, 1e-3));
        set("camera/distance", 90.0f);
        set("camera/height", 40.0f);
        const glm::vec3 after = placed();
        CHECK_THAT(after.y, Catch::Matchers::WithinAbs(40.0, 1e-3));
        CHECK(glm::length(glm::vec2(after.x, after.z)) > glm::length(glm::vec2(before.x, before.z)) + 50.0f);

        // ...and it turns only when it is told to.
        const glm::vec3 still = placed();
        CHECK_THAT(glm::length(still - after), Catch::Matchers::WithinAbs(0.0, 1e-4));
        set("camera/orbitSpeed", 1.0f);
        CHECK(glm::length(placed() - after) > 1e-3f);
    }

    SECTION("free mode ignores them entirely -- which is the reported 'nothing happens'") {
        set("camera/mode", 1.0f);
        set("camera/distance", 10.0f);
        set("camera/height", 3.0f);
        const glm::vec3 before = placed();
        CHECK_THAT(before.y, Catch::Matchers::WithinAbs(2.0, 1e-4));   // camera/position, not height
        set("camera/distance", 900.0f);
        set("camera/height", 400.0f);
        set("camera/orbitSpeed", 2.0f);
        const glm::vec3 after = placed();
        CHECK_THAT(glm::length(after - before), Catch::Matchers::WithinAbs(0.0, 1e-4));
        // The free camera's own parameters do move it, so this is a mode boundary and not a stuck
        // camera.
        setVec("camera/position", glm::vec3(5.0f, 6.0f, 7.0f));
        CHECK(glm::length(placed() - before) > 1.0f);
    }
}

// A composition ignores `camera/position` and `camera/target` unless `camera/mode` is 1 (free), and
// a composition defaults to orbit. Directing a scene nobody had already switched over installed six
// tracks that bound, evaluated, wrote their values every frame and moved nothing: the camera went on
// circling the bounds centre. Glowmere and every generated world escaped it only because something
// else had set the mode -- a scene built by hand did not.
TEST_CASE("Directing places the camera even when the scene was left in orbit mode",
          "[director][camera][mode]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const std::filesystem::path wav =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "audio" / "glowmere-valley.wav";
    if (!std::filesystem::exists(wav)) {
        SKIP("glowmere-valley.wav is generated, not committed: run tools/make_glowmere_score.py");
    }
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    REQUIRE(engine.loadAudio(wav).has_value());
    scene::Composition* composition = engine.composition();
    REQUIRE(composition->setHeroes(threeHeroes()).has_value());

    const params::IParameter* mode = engine.params().find("camera/mode");
    REQUIRE(mode != nullptr);
    REQUIRE(mode->baseComponent(0) == 0.0f);   // orbit: the default nobody changed

    auto at = [&](double seconds) {
        engine.transport().seek(seconds);
        FrameTime time;
        time.renderTime = seconds;
        time.deltaTime = 1.0 / 60.0;
        engine.update(time);
        return engine.scene().camera.position;
    };
    REQUIRE(app::directEngine(engine, composition->heroes()).has_value());

    // The camera is where the shot says, and it travels the world rather than circling the origin
    // at the fitted distance.
    const glm::vec3 opening = at(1.0);
    const glm::vec3 later = at(60.0);
    CHECK(glm::length(later - opening) > 20.0f);
    CHECK(glm::length(opening) > 20.0f);
    // The mode is driven, not overwritten: the base value is still what the scene was authored
    // with, so handing the camera back gives that scene its own camera again.
    CHECK(mode->baseComponent(0) == 0.0f);
    CHECK(mode->finalComponent(0) == 1.0f);

    // And handing it back takes the mode with it -- the director owns it only while it owns the
    // camera.
    auto& tracks = engine.timeline().tracks();
    const auto owned = app::directedCameraTargets();
    CHECK(std::find(owned.begin(), owned.end(), std::string_view("camera/mode")) != owned.end());
    std::erase_if(tracks, [&](const params::Track& t) {
        return std::find(owned.begin(), owned.end(), t.target) != owned.end();
    });
    const glm::vec3 handedBack = at(61.0);
    CHECK(glm::length(handedBack - later) > 1.0f);   // back to the orbit it was authored with
#endif
}

// Three heroes, one film, and the film has to be about all three.
//
// Reported as "the camera only focuses on far-arch -- it seems to only pay attention to whatever
// hero is currently first in the list", and measured on the real score it was: the fold gives a
// ninety-second piece four sections (intro, verse, finalDrop, verse), the hero owned the intro and
// the outro as well as the drop, and both transitions *start* on the previous subject -- so one
// object held sixty-five of ninety seconds while two declared heroes were glimpsed at the end of a
// move. Two causes, both fixed: an establish is "the world, not the subject" and belongs to the
// cast, and a thirty-second passage is several shots rather than one hold.
TEST_CASE("A film with three heroes is about three heroes", "[director][camera][casting]") {
    // The structure the real Glowmere score actually folds to, written out so this measures the
    // director rather than the analyser.
    avgen::signals::MusicalStructure structure;
    structure.sections = {{signals::MusicalSection::Intro, 0.0, 31.1, 0.3f},
                          {signals::MusicalSection::Verse, 31.1, 24.8, 0.6f},
                          {signals::MusicalSection::FinalDrop, 55.9, 6.6, 1.0f},
                          {signals::MusicalSection::Verse, 62.5, 27.5, 0.6f}};
    // Three heroes designated in the editor: all at the default importance, so the ranking is the
    // order they were starred in and nothing about them says one deserves the whole film.
    std::vector<world::HeroPoint> heroes{hero("far-arch", glm::vec3(0.0f, 0.0f, -40.0f), 20.0f, 4.0f, 0.5f),
                                         hero("elder-crown", glm::vec3(60.0f, 0.0f, 20.0f), 20.0f, 4.0f, 0.5f),
                                         hero("wanderer", glm::vec3(-50.0f, 0.0f, 10.0f), 20.0f, 4.0f, 0.5f)};
    auto seq = app::directHeroes(heroes, structure);
    INFO((seq ? std::string() : seq.error().message));
    REQUIRE(seq.has_value());

    // Whoever the camera is actually looking at, second by second. Sampling the aim rather than
    // reading the shot's `subject` field on purpose: a Transition is *named* for where it came from,
    // so counting subjects would have called the old behaviour fair when it was not.
    std::map<std::string, int> seconds;
    for (double t = 0.0; t < 90.0; t += 1.0) {
        const app::Shot* shot = seq->shotAt(t);
        if (shot == nullptr) {
            continue;
        }
        const auto local = static_cast<float>((t - shot->startSeconds) / shot->durationSeconds);
        const glm::vec3 aim = shot->targetAt(local);
        std::string nearest;
        float best = std::numeric_limits<float>::max();
        for (const world::HeroPoint& h : heroes) {
            const float d = glm::length(h.position - aim);
            if (d < best) {
                best = d;
                nearest = h.name;
            }
        }
        ++seconds[nearest];
    }

    int total = 0;
    for (const auto& [name, count] : seconds) {
        INFO(name << ": " << count << " s");
        total += count;
    }
    REQUIRE(total > 80);
    for (const world::HeroPoint& h : heroes) {
        INFO(h.name << " holds " << seconds[h.name] << " of " << total << " s");
        // Every hero is really on screen -- not one frame at the end of a move.
        CHECK(seconds[h.name] >= total / 10);
        // ...and none of them is the whole film. The old behaviour put one at 72%.
        CHECK(seconds[h.name] <= total * 6 / 10);
    }

    // The hero still owns the drop: sharing the film is not the same as having no subject, and the
    // reveal landing on the drop is the reason for reading the structure at all.
    const app::Shot* atTheDrop = seq->shotAt(58.0);
    REQUIRE(atTheDrop != nullptr);
    CHECK(atTheDrop->subject.name == "far-arch");
    CHECK(atTheDrop->kind == app::ShotKind::Reveal);
    CHECK_THAT(atTheDrop->startSeconds, Catch::Matchers::WithinAbs(55.9, 1e-6));   // on the beat

    // A passage becomes several shots; the hero's own sections stay one move.
    const auto intros = std::count_if(seq->shots.begin(), seq->shots.end(), [](const app::Shot& s) {
        return s.startSeconds < 31.0;
    });
    CHECK(intros > 1);
    const auto drops = std::count_if(seq->shots.begin(), seq->shots.end(), [](const app::Shot& s) {
        return s.startSeconds >= 55.0 && s.startSeconds < 62.0;
    });
    CHECK(drops == 1);
}

// Reported as the director getting "stuck on a single hero during playback".
//
// Not the casting: heroes follow the objects they describe (ADR-106), Glowmere's wanderer walks, and
// every time it paused for breath the placement settled, bumped the hero revision and re-cut the
// entire film under a running playhead. A film re-cut at second fifty-four starts its intro at zero,
// so whatever section covers *now* decides the subject -- repeatedly, and often the same one.
//
// Changing the cast is a decision and still re-cuts at once, wherever the playhead is. A hero
// merely moving is usually the world moving, and waits until the transport is parked.
TEST_CASE("A hero walking about during playback does not re-cut the film", "[director][camera][redirect]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const std::filesystem::path wav =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "audio" / "glowmere-valley.wav";
    if (!std::filesystem::exists(wav)) {
        SKIP("glowmere-valley.wav is generated, not committed: run tools/make_glowmere_score.py");
    }
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    REQUIRE(engine.loadAudio(wav).has_value());
    scene::Composition* composition = engine.composition();
    composition->setHeroSettleSeconds(0.0);   // settle at once; the debounce has its own test
    REQUIRE(composition->setHeroes(threeHeroes()).has_value());
    REQUIRE(app::directEngine(engine, composition->heroes()).has_value());

    app::DirectorState state;
    app::noteDirected(engine, state);

    auto frame = [&](double at) {
        auto redirected = app::refreshDirection(engine, state);
        REQUIRE(redirected.has_value());
        FrameTime time;
        time.renderTime = at;
        time.deltaTime = 1.0 / 60.0;
        engine.update(time);
        return *redirected;
    };
    // A hero that has walked somewhere new, the way a wandering entity does.
    auto walk = [&](float metres) {
        std::vector<world::HeroPoint> moved = composition->heroes();
        moved.front().position.x += metres;
        REQUIRE(composition->editHero(moved.front().name, moved.front()).has_value());
    };

    REQUIRE(frame(0.0) == app::Redirect::Nothing);
    REQUIRE(engine.play().has_value());
    REQUIRE(engine.transport().isPlaying());

    for (int i = 1; i <= 10; ++i) {
        walk(2.0f);
        INFO("playing frame " << i);
        CHECK(frame(static_cast<double>(i) / 60.0) == app::Redirect::Nothing);
    }
    // ...and pausing does not then fire a re-cut for everything that happened while it played: the
    // movement was absorbed, not queued.
    engine.pause();
    CHECK(frame(1.0) == app::Redirect::Nothing);
    CHECK(frame(1.1) == app::Redirect::Nothing);

    // Parked, the same movement is an edit and does re-cut: this is somebody placing a hero. One
    // frame later than the edit, because the settle happens inside the update and `refreshDirection`
    // runs at the top of the next one -- the same order the application uses.
    walk(30.0f);
    CHECK(frame(1.2) == app::Redirect::Nothing);
    CHECK(frame(1.3) == app::Redirect::Recut);
    CHECK(frame(1.4) == app::Redirect::Nothing);

    // And a change to the cast re-cuts wherever the playhead is, because that is somebody asking.
    REQUIRE(engine.play().has_value());
    std::vector<world::HeroPoint> fewer = composition->heroes();
    fewer.pop_back();
    REQUIRE(composition->setHeroes(fewer).has_value());
    CHECK(frame(2.0) == app::Redirect::Recut);
#endif
}

// Reaching for the camera is asking for it back.
//
// A viewport drag under a directed camera used to write `camera/position` and have the timeline
// replace it on the very next frame: the mouse appeared to do nothing, and the way out was a menu
// item you had to know was there. Every path that moves the camera by hand now goes through the
// same release, which is this.
TEST_CASE("Taking the camera by hand ends the director's claim", "[director][camera][redirect]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const std::filesystem::path wav =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "audio" / "glowmere-valley.wav";
    if (!std::filesystem::exists(wav)) {
        SKIP("glowmere-valley.wav is generated, not committed: run tools/make_glowmere_score.py");
    }
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    REQUIRE(engine.loadAudio(wav).has_value());
    REQUIRE(engine.composition()->setHeroes(threeHeroes()).has_value());

    // Somebody else's automation, of something that is not the camera.
    params::Track other;
    other.target = "post/bloom/intensity";
    other.addKey(params::Key{0.0, {0.5f, 0.0f, 0.0f, 0.0f}});
    other.addKey(params::Key{10.0, {1.5f, 0.0f, 0.0f, 0.0f}});
    engine.timeline().addTrack(std::move(other));

    app::DirectorState state;
    REQUIRE(app::directEngine(engine, engine.composition()->heroes()).has_value());
    app::noteDirected(engine, state);
    REQUIRE(engine.timeline().isAutomated("camera/position"));

    const std::size_t removed = app::releaseDirectedCamera(engine, state);
    CHECK(removed > 0);
    CHECK_FALSE(engine.timeline().isAutomated("camera/position"));
    CHECK_FALSE(engine.timeline().isAutomated("camera/mode"));
    CHECK_FALSE(state.directed);
    // The bloom is untouched: handing the camera back is not a reason to stop everything else.
    CHECK(engine.timeline().isAutomated("post/bloom/intensity"));

    // Having let go, the director stays let go: a hero changing does not quietly take it again.
    std::vector<world::HeroPoint> fewer = engine.composition()->heroes();
    fewer.pop_back();
    REQUIRE(engine.composition()->setHeroes(fewer).has_value());
    auto after = app::refreshDirection(engine, state);
    REQUIRE(after.has_value());
    CHECK(*after == app::Redirect::Nothing);
    CHECK_FALSE(engine.timeline().isAutomated("camera/position"));

    // Releasing something that was never taken is not an error, and removes nothing of anyone's.
    app::DirectorState untouched;
    CHECK(app::releaseDirectedCamera(engine, untouched) == 0);
    CHECK(engine.timeline().isAutomated("post/bloom/intensity"));
#endif
}

// Reported: open a project whose camera is directed, drag in the viewport, and the camera does not
// budge -- the only way back is Camera > Hand Camera Back. It behaves correctly in the session that
// ran Direct to Music and wrongly in every session after.
//
// The claim lived in a runtime bool that Direct to Music set, while the tracks it describes were
// saved. A load restored the tracks and not the bool, so the drag handler saw an undirected camera.
//
// An earlier attempt at this stamped an owner onto each track and adopted from the stamp. That
// cannot work, and the reproduction case is why: a project written before the stamp existed carries
// no stamp, and those are exactly the projects with the problem. The signature has to be read from
// what the director always writes.
TEST_CASE("a project that arrives with a directed camera is recognised as directed",
          "[director][camera][redirect][persistence]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const std::filesystem::path wav =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "audio" / "glowmere-valley.wav";
    if (!std::filesystem::exists(wav)) {
        SKIP("glowmere-valley.wav is generated, not committed: run tools/make_glowmere_score.py");
    }
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    REQUIRE(engine.loadAudio(wav).has_value());
    REQUIRE(engine.composition()->setHeroes(threeHeroes()).has_value());
    REQUIRE(app::directEngine(engine, engine.composition()->heroes()).has_value());

    // What a saved project holds: the tracks, and nothing that says who wrote them.
    const nlohmann::json saved = engine.timeline().toJson();
    params::Timeline reopened;
    REQUIRE(reopened.fromJson(saved).has_value());
    engine.timeline() = std::move(reopened);
    // Binding is what a project load does after reading the tracks, and `isAutomated` -- which both
    // this and the existing release check ask -- only counts a track that resolved to a live
    // parameter. An unbound track drives nothing, so it should not read as a directed camera.
    REQUIRE(engine.timeline().bind(engine.params()).has_value());
    REQUIRE(app::cameraLooksDirected(engine));

    SECTION("a fresh session takes up the claim, so the viewport's hand-back can fire") {
        app::DirectorState fresh; // exactly what a newly-started session has
        REQUIRE_FALSE(fresh.directed);
        // One frame of the loop is all it takes: refreshDirection runs every frame already.
        REQUIRE(app::refreshDirection(engine, fresh).has_value());
        CHECK(fresh.directed);

        const std::size_t removed = app::releaseDirectedCamera(engine, fresh);
        CHECK(removed > 0);
        CHECK_FALSE(fresh.directed);
        CHECK_FALSE(app::cameraLooksDirected(engine));
    }

    SECTION("hand-authored camera animation is not mistaken for the director's") {
        // The control, and the one that matters: a person keys position and target to move the
        // camera. They do not key `camera/mode`, because in free mode it is already what they want.
        // If this fired, the first viewport drag would delete their work.
        app::Engine mine(app::EngineMode::Offline);
        mine.newComposition();
        for (const char* target : {"camera/position", "camera/target"}) {
            params::Track t;
            t.target = target;
            t.addKey(params::Key{0.0, {1.0f, 2.0f, 3.0f, 0.0f}});
            t.addKey(params::Key{6.0, {4.0f, 5.0f, 6.0f, 0.0f}});
            mine.timeline().addTrack(std::move(t));
        }
        REQUIRE(mine.timeline().bind(mine.params()).has_value());
        CHECK_FALSE(app::cameraLooksDirected(mine));

        app::DirectorState state;
        REQUIRE(app::refreshDirection(mine, state).has_value());
        CHECK_FALSE(state.directed);
    }
#endif
}

// ---- the aim follows the hero (ADR-158) ---------------------------------------------------------
//
// The gap this closes: directing bakes, so a hero that walks after the cut walks out of its own
// close-up and the only remedy was to cut the film again. Re-cutting for a moving hero is wrong --
// it replaces the whole film every time somebody's subject takes a step -- so what follows is the
// aim, inside the shot the director already chose.
TEST_CASE("A directed shot's aim follows the hero it was cut for", "[director][camera][follow]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const std::filesystem::path wav =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "assets" / "audio" / "glowmere-valley.wav";
    if (!std::filesystem::exists(wav)) {
        SKIP("glowmere-valley.wav is generated, not committed: run tools/make_glowmere_score.py");
    }
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    REQUIRE(engine.loadAudio(wav).has_value());
    scene::Composition* composition = engine.composition();
    REQUIRE(composition != nullptr);
    REQUIRE(composition->setHeroes(threeHeroes()).has_value());
    REQUIRE(app::directEngine(engine, composition->heroes()).has_value());

    const std::vector<scene::AimFollow> follow = composition->aimFollow();
    INFO("shots that hold a subject: " << follow.size());
    REQUIRE(!follow.empty());
    // Every entry names a hero that exists and covers real time, or it can never do anything.
    for (const scene::AimFollow& shot : follow) {
        CHECK(shot.endSeconds > shot.startSeconds);
        CHECK(std::any_of(composition->heroes().begin(), composition->heroes().end(),
                          [&](const world::HeroPoint& h) { return h.name == shot.hero; }));
    }

    const scene::AimFollow shot = follow.front();
    const auto heroNamed = [&](const std::string& name) {
        const auto it = std::find_if(composition->heroes().begin(), composition->heroes().end(),
                                     [&](const world::HeroPoint& h) { return h.name == name; });
        REQUIRE(it != composition->heroes().end());
        return *it;
    };
    const world::HeroPoint subject = heroNamed(shot.hero);

    FixedStepClock clock(60.0);
    const auto aimAt = [&](double seconds) {
        engine.seekSeconds(seconds);
        clock.seek(seconds);
        engine.update(engine.tick(clock));
        return engine.composition()->scene().camera.target;
    };

    const double inside = (shot.startSeconds + shot.endSeconds) * 0.5;
    const glm::vec3 before = aimAt(inside);
    // The bake aims a subject-holding shot at the subject, so this is the claim the follow is built
    // on: at the moment of the cut the offset is zero and the aim is already the hero.
    CHECK(glm::length(before - subject.position) < 0.5f);

    // The hero walks. Nothing else changes -- no re-cut, no new keys.
    const std::vector<float> keysBefore = [&] {
        std::vector<float> out;
        for (const params::Track& t : engine.timeline().tracks()) {
            if (t.target != "camera/target") {
                continue;
            }
            for (const params::Key& key : t.keys) {
                out.insert(out.end(), key.value.begin(), key.value.end());
            }
        }
        return out;
    }();
    REQUIRE(!keysBefore.empty());

    const glm::vec3 walk(17.0f, 0.0f, -9.0f);
    std::vector<world::HeroPoint> moved = composition->heroes();
    for (world::HeroPoint& h : moved) {
        if (h.name == shot.hero) {
            h.position += walk;
        }
    }
    REQUIRE(composition->setHeroes(std::move(moved)).has_value());

    const glm::vec3 after = aimAt(inside);
    INFO("aim before (" << before.x << ", " << before.z << ") after (" << after.x << ", " << after.z << ")");
    CHECK(glm::length((after - before) - walk) < 0.01f);
    // The keys did not move. That is the whole point: the cut is still a bake, still scrubbable,
    // still the same film -- only where it is pointed has been nudged.
    std::vector<float> keysAfter;
    for (const params::Track& t : engine.timeline().tracks()) {
        if (t.target != "camera/target") {
            continue;
        }
        for (const params::Key& key : t.keys) {
            keysAfter.insert(keysAfter.end(), key.value.begin(), key.value.end());
        }
    }
    CHECK(keysAfter == keysBefore);

    // Outside every shot's window the aim is the bake's, untouched. Without this the table would be
    // a global offset on the camera rather than a property of one shot.
    double lastEnd = 0.0;
    for (const scene::AimFollow& s : follow) {
        lastEnd = std::max(lastEnd, s.endSeconds);
    }
    const glm::vec3 past = aimAt(lastEnd + 5.0);
    REQUIRE(composition->aimFollow().size() == follow.size());
    const glm::vec3 pastAgain = aimAt(lastEnd + 5.0);
    CHECK(glm::length(past - pastAgain) < 1e-4f);

    // Handing the camera back takes the table with it, or a camera the viewport owns would still be
    // nudged by a film nobody is running.
    app::DirectorState state;
    app::noteDirected(engine, state);
    static_cast<void>(app::releaseDirectedCamera(engine, state));
    CHECK(composition->aimFollow().empty());
#endif
}

// The table travels with the project, because an offline render reloads the project before drawing
// it: a follow that only lived in memory would make the rendered file differ from the window that
// asked for it.
TEST_CASE("The aim-follow table round-trips with the project", "[director][camera][follow]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    app::Engine engine(app::EngineMode::Offline);
    engine.newComposition();
    scene::Composition* composition = engine.composition();
    REQUIRE(composition != nullptr);
    REQUIRE(composition->setHeroes(threeHeroes()).has_value());
    composition->setAimFollow({scene::AimFollow{.startSeconds = 2.0,
                                                .endSeconds = 9.5,
                                                .hero = "elder",
                                                .heroAtCut = glm::vec3(1.0f, 2.0f, 3.0f)},
                               scene::AimFollow{.startSeconds = 9.5,
                                                .endSeconds = 14.0,
                                                .hero = "spire",
                                                .heroAtCut = glm::vec3(-4.0f, 0.5f, 6.0f)}});

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "avgen-aim-follow-roundtrip.json";
    REQUIRE(engine.saveProject(path).has_value());

    app::Engine reopened(app::EngineMode::Offline);
    auto loaded = reopened.loadProject(path);
    INFO((loaded ? std::string() : loaded.error().message));
    REQUIRE(loaded.has_value());
    REQUIRE(reopened.composition() != nullptr);
    const std::vector<scene::AimFollow>& back = reopened.composition()->aimFollow();
    REQUIRE(back.size() == 2);
    CHECK(back[0].hero == "elder");
    CHECK(back[1].hero == "spire");
    CHECK(back[0].startSeconds == 2.0);
    CHECK(back[1].endSeconds == 14.0);
    CHECK(glm::length(back[0].heroAtCut - glm::vec3(1.0f, 2.0f, 3.0f)) < 1e-5f);
    CHECK(glm::length(back[1].heroAtCut - glm::vec3(-4.0f, 0.5f, 6.0f)) < 1e-5f);

    // A project that was never directed does not inherit the last one's cut.
    app::Engine plain(app::EngineMode::Offline);
    plain.newComposition();
    plain.composition()->setAimFollow({scene::AimFollow{
        .startSeconds = 0.0, .endSeconds = 1.0, .hero = "elder", .heroAtCut = glm::vec3(0.0f)}});
    const std::filesystem::path bare =
        std::filesystem::temp_directory_path() / "avgen-aim-follow-bare.json";
    app::Engine undirected(app::EngineMode::Offline);
    undirected.newComposition();
    REQUIRE(undirected.saveProject(bare).has_value());
    REQUIRE(plain.loadProject(bare).has_value());
    CHECK(plain.composition()->aimFollow().empty());

    std::filesystem::remove(path);
    std::filesystem::remove(bare);
#endif
}

// ---- the Auto-director's settings (section 9) --------------------------------------------------

TEST_CASE("Auto-director settings reach the film, and refuse what they cannot mean", "[director][autodirector]") {
    const auto heroes = threeHeroes();
    const auto structure = sevenSections();

    SECTION("shot-length bounds change the cut") {
        app::AutoDirectorSettings shortShots;
        shortShots.maxShotSeconds = 6.0;
        shortShots.minShotSeconds = 2.0;
        shortShots.minBuildShotSeconds = 1.0;
        app::AutoDirectorSettings longShots;
        longShots.maxShotSeconds = 60.0;
        const auto many = app::directHeroes(heroes, structure, shortShots);
        const auto few = app::directHeroes(heroes, structure, longShots);
        REQUIRE(many.has_value());
        REQUIRE(few.has_value());
        // A shorter ceiling splits long passages into more shots. If this ever stops being true the
        // control has stopped doing anything, which is the failure the brief forbids.
        REQUIRE(many->shots.size() > few->shots.size());
    }

    SECTION("the mode changes the film and the seed changes the edit") {
        app::AutoDirectorSettings take;
        take.mode = app::DirectorMode::ContinuousShot;
        app::AutoDirectorSettings cut = take;
        cut.mode = app::DirectorMode::EditedSequence;
        const auto a = app::directHeroes(heroes, structure, take);
        const auto b = app::directHeroes(heroes, structure, cut);
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        REQUIRE(a->shots[1].startPosition.has_value());
        REQUIRE_FALSE(b->shots[1].startPosition.has_value());

        app::AutoDirectorSettings other = take;
        other.seed = 99u;
        const auto c = app::directHeroes(heroes, structure, other);
        REQUIRE(c.has_value());
        bool castDiffers = false;
        for (std::size_t i = 0; i < a->shots.size() && i < c->shots.size(); ++i) {
            if (a->shots[i].subject.name != c->shots[i].subject.name) {
                castDiffers = true;
            }
        }
        REQUIRE(castDiffers);
    }

    SECTION("the wide lens reaches the baked keys") {
        app::AutoDirectorSettings wide;
        wide.wideFocalLength = 14.0f;
        const auto seq = app::directHeroes(heroes, structure, wide);
        REQUIRE(seq.has_value());
        bool sawIt = false;
        for (const app::Shot& s : seq->shots) {
            if (std::fabs(s.composition.focalLength - 14.0f) < 0.01f) {
                sawIt = true;
            }
        }
        REQUIRE(sawIt);
    }

    SECTION("settings that cannot mean anything are refused rather than clamped") {
        REQUIRE(app::AutoDirectorSettings{}.validate().has_value());
        app::AutoDirectorSettings bad;
        bad.maxShotSeconds = 1.0; // shorter than the minimum
        REQUIRE_FALSE(bad.validate().has_value());
        app::AutoDirectorSettings lens;
        lens.wideFocalLength = 90.0f; // a "wide" longer than the hero lens
        REQUIRE_FALSE(lens.validate().has_value());
        app::AutoDirectorSettings zero;
        zero.minShotSeconds = 0.0;
        REQUIRE_FALSE(zero.validate().has_value());
    }
}
