#pragma once

// Conformance for the atmospheric effect family (ADR-230, ADR-387, and the ADR this lands with).
//
// **What this is for.** ADR-387 established that ADR-230's family is a table-driven authoring
// interface: two `constexpr` field tables and one case in each of `floatFields`/`colorFields`/
// `boolFields` buy registration, modulation, timeline keys, presets, apply, capture and a panel
// row with no bespoke code. That claim is true, and it is true of exactly three directions --
// register, apply and capture, which are one loop over one table each.
//
// It is NOT true of everything else the family needs. Beside the table sit five more per-kind or
// per-field lists that are written by hand: `toJson`, `fromJson`, `sanitise`, the style presets,
// and `defaultAtmosphericRoutes`; plus the panel's rows, which are string literals. Adding a kind
// means remembering all of them, and forgetting one does not fail to compile, does not throw, and
// does not log -- the exact shape ADR-387 named when it wrote that **a parameter path is three
// things at once and a correct value is not a reached value.**
//
// So this file is the mechanism that makes forgetting one *loud*. It asks the engine the same
// questions the engine asks itself, per kind, and reports what does not line up:
//
//   1. every default modulation route names a path that kind actually registers, and is modulatable;
//   2. a non-default value set through the parameter system survives save -> load -> save (ADR-350's
//      prescribed round trip, run through the registration table rather than beside it);
//   3. every registered component's default lies inside its hard range and its soft range lies
//      inside its hard range;
//   4. an effect of this kind resolves as *its own kind*, which is what catches a new kind falling
//      through `resolveAtmosphericEffects`'s `if comet / else if aurora / else` into the vortex arm;
//   5. the kind's name survives `atmosphereKindName` -> `atmosphereKindFromName`.
//
// **How the path set is obtained, and why it matters.** `registeredPaths` does not read the field
// tables. It registers a probe effect into a scratch `ParameterSet` -- the same call `Engine` makes
// -- and reads back what appeared. ADR-382's rule is that a path a panel computes needs a test that
// computes it the same way; the generalisation is that a path *anything* computes needs a check
// that obtains it the way the engine does. A conformance layer that read the tables would agree
// with the tables and learn nothing.
//
// This is deliberately not a `WorldEffect` base class. §2 of the brief asks for a reusable
// environmental simulation layer and ADR-230's family already is one; what it lacked was a
// statement of its own contract that a machine could check. That is what this is.

#include "world/atmospherics.hpp"

#include <array>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::params {
class ParameterSet;
}

namespace avgen::world::conformance {

// One thing that does not line up. `subject` is the kind, `rule` is the short name of the check
// (stable, so a baseline can name one), `detail` is what to go and look at.
struct Finding {
    std::string subject;
    std::string rule;
    std::string detail;
};

struct Report {
    std::vector<Finding> findings;

    [[nodiscard]] bool clean() const { return findings.empty(); }
    // One line per finding, for a test's INFO(). Never empty when `clean()` is false.
    [[nodiscard]] std::string summary() const;
};

// Every kind of the family. Kept beside `atmosphereKindIndex` below, whose switch has no `default`,
// so a new enumerator is at minimum a `-Wswitch` diagnostic here -- and `test_effect_conformance`
// reads the enum out of the header and fails by name if this array has fallen behind it, because
// `-Werror` is off by default in this build and a warning nobody reads is not a guard.
inline constexpr std::array<AtmosphereKind, 3> kAtmosphereKinds{
    AtmosphereKind::Comet,
    AtmosphereKind::Aurora,
    AtmosphereKind::Vortex,
};

// The kind's position in `kAtmosphereKinds`. The switch is exhaustive and has no `default` on
// purpose: that is the compile-time half of the guard above.
[[nodiscard]] constexpr std::size_t atmosphereKindIndex(AtmosphereKind k) {
    switch (k) {
    case AtmosphereKind::Comet: return 0;
    case AtmosphereKind::Aurora: return 1;
    case AtmosphereKind::Vortex: return 2;
    }
    return kAtmosphereKinds.size(); // unreachable for a declared enumerator
}

static_assert(atmosphereKindIndex(AtmosphereKind::Comet) == 0);
static_assert(atmosphereKindIndex(AtmosphereKind::Aurora) == 1);
static_assert(atmosphereKindIndex(AtmosphereKind::Vortex) == 2);

// The canonical authored effect of a kind -- the same factory the "Add ..." button calls, so a
// probe is a thing an artist can actually make rather than a default-constructed struct no scene
// contains. Exhaustive switch, no `default`.
[[nodiscard]] AtmosphericEffect probeEffect(AtmosphereKind kind, std::string name);

// Every parameter path registering `effect` produces, in registration order. Obtained by
// registering into a scratch `ParameterSet`, not by reading the field tables -- see the header note.
[[nodiscard]] std::vector<std::string> registeredPaths(const AtmosphericEffect& effect);

// The leaf of each path in `registeredPaths`, i.e. the part after `atmos/<name>/`.
[[nodiscard]] std::vector<std::string> registeredLeaves(const AtmosphericEffect& effect);

// Runs checks 1-5 above for one kind.
[[nodiscard]] Report checkAtmospheric(AtmosphereKind kind);

// Runs `checkAtmospheric` for every kind in `kAtmosphereKinds`.
[[nodiscard]] Report checkAtmosphericFamily();

// Checks that every leaf in `leaves` is registered by `kind`, reporting one finding per leaf that
// is not. This is what a panel's row table is held to: the panel's rows are string literals, and a
// leaf five characters wrong draws an empty box and says nothing (ADR-382). Lives here rather than
// in the UI so that the row tables and the registration can be compared by the CPU suite.
[[nodiscard]] Report checkLeavesExist(AtmosphereKind kind, std::span<const std::string_view> leaves,
                                      std::string_view rule);

} // namespace avgen::world::conformance
