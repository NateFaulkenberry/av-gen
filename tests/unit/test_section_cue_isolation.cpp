// **The one architectural claim that a runtime test cannot make** (ADR-247).
//
// `SectionCue` exists so that a director *structurally cannot* find out what kind of section it is
// looking at. Everything else about that claim is checked by ordinary tests -- that a custom type
// produces the same cue as a built-in one, that `finalOfKind` is derived from position rather than
// from a musical enumerator. None of them would notice the way it will actually be lost: somebody
// adds `#include "song/section_type.hpp"` to `section_cue.hpp` for a convenience accessor, every
// test still passes, and six months later the director has a switch on `"chorus"` in it.
//
// So this translation unit includes the director's **entire** include surface and nothing else, and
// asserts at compile time that the section-type vocabulary is not reachable through it. A leak is a
// build failure in this file, naming the rule it broke.
//
// Keep this file free of any other `song/` include. That is the whole experiment.

#include "song/section_cue.hpp"
#include "song/shot_intent.hpp"

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

namespace avgen::song {
// Declared, never defined. If either header above dragged in `song/section_type.hpp`, the real
// definition is in scope and this becomes a redeclaration of a *complete* type.
struct SectionTypeReachabilityProbe;
struct SectionType;
} // namespace avgen::song

namespace {
// The standard completeness detector. `sizeof` on an incomplete type is a hard error in clang rather
// than a substitution failure, so a `requires` expression cannot ask this question -- partial
// specialisation can.
template <typename T, typename = void>
struct IsComplete : std::false_type {};
template <typename T>
struct IsComplete<T, std::void_t<decltype(sizeof(T))>> : std::true_type {};
} // namespace

static_assert(!IsComplete<avgen::song::SectionType>::value,
              "song/section_cue.hpp and song/shot_intent.hpp are the director's whole include "
              "surface, and they must not reach song::SectionType. If they do, the director can "
              "switch on a musical label and ADR-247's central guarantee is gone. Put the "
              "convenience you were adding on ShotLanguage or SectionTimeline instead.");

// The control for the assertion above: the same question about a type that is genuinely never
// defined anywhere. Without it, a detector that always answered "incomplete" would look exactly like
// a passing guard -- which is the shape of vacuous probe ADR-182 exists for.
static_assert(!IsComplete<avgen::song::SectionTypeReachabilityProbe>::value);
// ...and the positive control: a type these headers *do* define, proving the detector can see one.
static_assert(IsComplete<avgen::song::ShotIntent>::value);

TEST_CASE("The director's include surface carries a treatment and no vocabulary", "[song][cue]") {
    // Runtime half: a cue built by hand is usable without a `ShotLanguage`, a `SectionType` or a
    // timeline anywhere in sight -- which is what a director actually holds.
    avgen::song::SectionCue cue;
    cue.startSeconds = 10.0;
    cue.endSeconds = 30.0;
    cue.intent.id = "increasing_movement";
    cue.intent.movement = 0.6f;
    cue.intent.arc = avgen::song::Arc::Rising;

    CHECK(cue.durationSeconds() == 20.0);
    CHECK(cue.contains(15.0));
    CHECK(!cue.contains(30.0));
    CHECK(cue.progressAt(20.0) == 0.5f);
    CHECK(cue.intentAt(11.0).movement < cue.intentAt(29.0).movement);
}
