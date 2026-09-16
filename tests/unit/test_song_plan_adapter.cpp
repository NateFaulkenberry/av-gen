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
