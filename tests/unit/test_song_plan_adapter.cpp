// The one function that reads both song vocabularies (`app::songPlanFromCues`).
//
// Two agents built the halves of this feature in parallel against a stated contract: one owns
// `song::SectionCue`, the other owns `app::SongPlan`, and neither may know the other's types. This
// adapter is the whole of the coupling, so it is also the whole of what a merge can get wrong.
//
// What the tests below actually guard is the ORDERING of the axes. A projection that loses
// information is fine and intended -- a `ShotIntent` carries a name, a description, a framing range
// and an arc that a director cannot act on. A projection that loses the *order* is not: if a wider
// intent came out closer, every section in every film would be framed backwards and every unit test
// about the pieces would still pass.

#include "app/song_plan.hpp"
#include "song/section_cue.hpp"
#include "song/shot_intent.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using namespace avgen;
using Catch::Approx;

namespace {

song::SectionCue cueWith(song::ShotIntent intent, double start = 0.0, double end = 10.0) {
    song::SectionCue cue;
    cue.startSeconds = start;
    cue.endSeconds = end;
    cue.displayName = "section";
    cue.intent = std::move(intent);
    cue.energy = 0.5f;
    cue.density = 0.5f;
    return cue;
}

song::ShotIntent intentWith(song::SubjectFocus focus, float strength, song::Framing tightest,
                            song::Framing widest) {
    song::ShotIntent intent;
    intent.id = "test_intent";
    intent.focus = focus;
    intent.focusStrength = strength;
    intent.framing.tightest = tightest;
    intent.framing.widest = widest;
    return intent;
}

} // namespace

TEST_CASE("the adapter preserves the order of every axis it projects", "[song][plan][adapter]") {
    SECTION("a wider framing range is a greater distance") {
        const std::vector<song::SectionCue> close = {
            cueWith(intentWith(song::SubjectFocus::Mixed, 0.5f, song::Framing::ExtremeClose,
                               song::Framing::Close))};
        const std::vector<song::SectionCue> wide = {cueWith(intentWith(
            song::SubjectFocus::Mixed, 0.5f, song::Framing::Wide, song::Framing::VeryWide))};
        const auto a = app::songPlanFromCues(close);
        const auto b = app::songPlanFromCues(wide);
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        // The assertion that a backwards mapping fails. Without it, an inverted band would sail
        // through every other check in this file.
        CHECK(a->sections[0].intent.distance < b->sections[0].intent.distance);
    }

    SECTION("hero focus is above the middle and environment focus is below it") {
        const std::vector<song::SectionCue> hero = {
            cueWith(intentWith(song::SubjectFocus::Hero, 1.0f, song::Framing::Medium,
                               song::Framing::Medium))};
        const std::vector<song::SectionCue> world = {
            cueWith(intentWith(song::SubjectFocus::Environment, 1.0f, song::Framing::Medium,
                               song::Framing::Medium))};
        const auto h = app::songPlanFromCues(hero);
        const auto w = app::songPlanFromCues(world);
        REQUIRE(h.has_value());
        REQUIRE(w.has_value());
        CHECK(h->sections[0].intent.heroEmphasis > 0.5f);
        CHECK(w->sections[0].intent.heroEmphasis < 0.5f);
        CHECK(h->sections[0].intent.heroEmphasis > w->sections[0].intent.heroEmphasis);
    }

    SECTION("a stronger focus is further from the middle, in whichever direction it points") {
        const std::vector<song::SectionCue> weak = {cueWith(intentWith(
            song::SubjectFocus::Hero, 0.2f, song::Framing::Medium, song::Framing::Medium))};
        const std::vector<song::SectionCue> strong = {cueWith(intentWith(
            song::SubjectFocus::Hero, 0.9f, song::Framing::Medium, song::Framing::Medium))};
        const auto a = app::songPlanFromCues(weak);
        const auto b = app::songPlanFromCues(strong);
        REQUIRE(a.has_value());
        REQUIRE(b.has_value());
        CHECK(b->sections[0].intent.heroEmphasis > a->sections[0].intent.heroEmphasis);
    }

    SECTION("mixed focus ignores its strength, because strongly mixed means nothing") {
        const std::vector<song::SectionCue> a = {cueWith(intentWith(
            song::SubjectFocus::Mixed, 0.0f, song::Framing::Medium, song::Framing::Medium))};
        const std::vector<song::SectionCue> b = {cueWith(intentWith(
            song::SubjectFocus::Mixed, 1.0f, song::Framing::Medium, song::Framing::Medium))};
        CHECK(app::songPlanFromCues(a)->sections[0].intent.heroEmphasis ==
              Approx(app::songPlanFromCues(b)->sections[0].intent.heroEmphasis));
    }
}

TEST_CASE("the adapter carries the measurements and derives the boundary", "[song][plan][adapter]") {
    std::vector<song::SectionCue> cues;
    song::ShotIntent intent =
        intentWith(song::SubjectFocus::Mixed, 0.5f, song::Framing::Medium, song::Framing::Medium);
    for (int i = 0; i < 3; ++i) {
        song::SectionCue cue = cueWith(intent, i * 10.0, (i + 1) * 10.0);
        cue.energy = 0.2f + 0.3f * static_cast<float>(i);   // 0.2, 0.5, 0.8
        cue.occurrence = i;
        cues.push_back(std::move(cue));
    }
    const auto plan = app::songPlanFromCues(cues);
    REQUIRE(plan.has_value());
    REQUIRE(plan->sections.size() == 3);

    // Measured values are carried, not recomputed.
    CHECK(plan->sections[1].energy == Approx(0.5f));
    CHECK(plan->sections[2].occurrence == 2);

    // `transition` is a property of a boundary, so the first section has none and the rest measure
    // the step across their own opening.
    CHECK(plan->sections[0].transition == Approx(0.0f));
    CHECK(plan->sections[1].transition == Approx(0.3f));
    CHECK(plan->sections[2].transition == Approx(0.3f));
}

TEST_CASE("the adapter refuses a cue it cannot project", "[song][plan][adapter]") {
    // An intent with no id projects to a profile with no id, which `ShotIntentProfile::validate`
    // refuses -- and the refusal has to survive the projection rather than being papered over with
    // a default, or a malformed intent would become a plausible-looking plan.
    song::ShotIntent nameless =
        intentWith(song::SubjectFocus::Mixed, 0.5f, song::Framing::Medium, song::Framing::Medium);
    nameless.id.clear();
    const std::vector<song::SectionCue> cues = {cueWith(nameless)};
    CHECK_FALSE(app::songPlanFromCues(cues).has_value());
}

// The path the editor's "+ New type..." button takes, tested without the button.
//
// The popup itself cannot be exercised headlessly, but everything it does can: derive an id from a
// display name, define the type, apply it to a section, and resolve the section's treatment through
// it. That sequence is the whole of the feature, and it is what makes "Ocean Ambience" -- the spec's
// own proof that nothing is hard-coded around Verse and Chorus -- reachable from the editor rather
// than only from a hand-edited project file.

#include "song/section_timeline.hpp"
#include "song/shot_language.hpp"

TEST_CASE("a custom section type can be made from a display name and used", "[song][custom][ui]") {
    song::ShotLanguage language;   // built-ins are present without being written to a project
    const std::vector<const song::ShotIntent*> intents = language.intents();
    REQUIRE_FALSE(intents.empty());

    // Step 1: the id is DERIVED, not asked for. A person types a name; the project carries a key.
    const auto id = song::makeId("Ocean Ambience");
    REQUIRE(id.has_value());
    CHECK(*id == "ocean_ambience");
    CHECK_FALSE(language.hasType(*id));   // and it is not one of the sixty built-ins

    song::SectionType type;
    type.id = *id;
    type.name = "Ocean Ambience";
    type.description = "Slow underwater environment passage";
    type.category = song::SectionCategory::Custom;
    type.defaultShotIntent = intents.front()->id;
    REQUIRE(language.defineType(std::move(type)).has_value());
    CHECK(language.hasType(*id));

    // Step 2: it appears in the picker's list, which is what the combo is populated from. Without
    // this the type would exist and be unselectable, which is the state the button exists to fix.
    const std::vector<const song::SectionType*> types = language.types();
    CHECK(std::any_of(types.begin(), types.end(),
                      [&](const song::SectionType* t) { return t->id == *id; }));

    // Step 3: applying it to a section, and the section resolving its treatment through it.
    song::SectionTimeline timeline;
    song::Section section;
    section.type = types.front()->id;
    section.startSeconds = 0.0;
    section.endSeconds = 20.0;
    timeline.sections.push_back(section);
    REQUIRE(song::setSectionType(timeline, 0, *id, language));
    CHECK(timeline.sections[0].type == *id);
    // No override was set, so the treatment must come from the type -- which is what makes "change
    // the type and the treatment follows" true, and what a stored copy of the default would break.
    CHECK_FALSE(timeline.sections[0].shotIntent.has_value());

    SECTION("a name that cannot make an id is refused rather than silently keyed") {
        CHECK_FALSE(song::makeId("   ").has_value());
    }

    SECTION("defining a type whose default treatment does not exist is refused") {
        song::SectionType dangling;
        dangling.id = "dangling";
        dangling.name = "Dangling";
        dangling.defaultShotIntent = "no_such_intent";
        CHECK_FALSE(language.defineType(std::move(dangling)).has_value());
    }
}

// The wire from the authored film to the director.
//
// The adapter existing is not the same as the adapter being CALLED. Until this was checked,
// `songPlanForEngine` fell through to `songPlanFromMeasurements` in every case -- so retyping a
// section, or inventing "Ocean Ambience", changed the picker and changed nothing else. Everything
// passed: the model was right, the adapter was right, and they were not connected.
#include "app/camera_director.hpp"
#include "app/engine.hpp"

#include <filesystem>

TEST_CASE("Song Mode plans from the authored film, not the detector's report",
          "[song][plan][integration]") {
#ifndef AVGEN_SOURCE_DIR
    SKIP("AVGEN_SOURCE_DIR not defined");
#else
    const std::filesystem::path project =
        std::filesystem::path(AVGEN_SOURCE_DIR) / "examples" / "world" /
        "glowmere-valley-2-multicam.json";
    if (!std::filesystem::exists(project)) {
        SKIP("the Glowmere multi-camera demo is not present");
    }
    app::Engine engine(app::EngineMode::Offline);
    REQUIRE(engine.loadProject(project).has_value());

    // The timeline is BUILT here rather than taken from the project, and that is deliberate. The
    // first version of this test loaded the demo and skipped when it found no analyzed sections --
    // which it does, because nobody has pressed Analyze on it. A test that skips is a test that
    // proves nothing, and this one exists precisely to prove a connection.
    seq::Sequence piece = engine.sequence();
    piece.sectionTimeline.sections.clear();
    for (int i = 0; i < 2; ++i) {
        song::Section section;
        section.type = "verse";
        section.startSeconds = i * 20.0;
        section.endSeconds = (i + 1) * 20.0;
        piece.sectionTimeline.sections.push_back(std::move(section));
    }

    // Give one section a treatment nobody could have detected, which is the whole claim of the
    // feature: a person's decision reaches the camera.
    song::ShotIntent invented;
    invented.id = "locked_off_test";
    invented.name = "Locked Off";
    invented.focus = song::SubjectFocus::Environment;
    invented.focusStrength = 1.0f;
    invented.framing.tightest = song::Framing::VeryWide;
    invented.framing.widest = song::Framing::VeryWide;
    invented.movement = 0.0f;
    invented.cutFrequency = 0.0f;
    invented.cameras.fewest = 1;
    invented.cameras.most = 1;
    REQUIRE(piece.shotLanguage.defineIntent(invented).has_value());
    REQUIRE(song::setSectionShotIntent(piece.sectionTimeline, 0, "locked_off_test",
                                       piece.shotLanguage));
    REQUIRE(engine.setSequence(std::move(piece)).has_value());

    const auto plan = app::songPlanForEngine(engine);
    REQUIRE(plan.has_value());
    REQUIRE_FALSE(plan->sections.empty());

    // The authored intent reached the plan. Its id is opaque to the director, which is exactly why
    // finding it here proves the path rather than proving a coincidence: nothing else in the engine
    // could have produced that string.
    INFO("first section intent: " << plan->sections[0].intent.id);
    CHECK(plan->sections[0].intent.id == "locked_off_test");
    // ...and its numbers came with it, so this is the intent and not just its name.
    CHECK(plan->sections[0].intent.movement == Approx(0.0f));
    CHECK(plan->sections[0].intent.cameras == 1);
    CHECK(plan->sections[0].intent.heroEmphasis < 0.5f);   // Environment focus, fully weighted
#endif
}
