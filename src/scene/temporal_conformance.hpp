#pragma once

// Conformance for the temporal effect family (ADR-400, and ADR-392's mechanism).
//
// **What this is for.** ADR-400's whole argument rests on one promise: no temporal effect is an
// accumulator, because each declares a bounded history depth. That promise is a *convention*. A
// kind that forgets to declare a bound, or declares one it does not honour, does not fail to
// compile, does not throw and does not log -- it just quietly reads more history than the ring was
// sized for, or reads history that a warm-up cannot rebuild. That is the exact shape ADR-387 named
// when it wrote that a correct value is not a reached value.
//
// So this file asks the family the same questions the engine asks it, per kind, and reports what
// does not line up:
//
//   1. **bounded-k** -- every enabled kind declares a depth in 1..kMaxTemporalFrames. Zero while
//      enabled means "I did not declare"; over the ceiling means "I declared more than the ring
//      can hold". Both are the accumulator ADR-400 forbids, arriving by different routes.
//   2. **paths** -- every parameter a kind's panel row asks for is a path that kind registers,
//      obtained by registering into a scratch `ParameterSet` rather than by reading a table. A
//      conformance layer that read the tables would agree with the tables and learn nothing.
//   3. **round-trip** -- a non-default value set through the parameter system survives
//      save -> load -> save (ADR-350), read back through re-registration rather than by comparing
//      JSON to JSON. `toJson(fromJson(toJson(e))) == toJson(e)` holds even when a field is missing
//      from BOTH directions, which is this repo's recurring reader-without-a-writer.
//   4. **ranges** -- every registered default lies inside its hard range and its soft range inside
//      its hard range.
//   5. **names** -- the kind's name survives `temporalEffectKindName` -> `temporalEffectKindFromName`.
//
// **Why `checkBounds` takes the bounds rather than computing them.** ADR-182: a probe that cannot
// fail proves nothing. If this function computed the bounds itself from the real switch, no test
// could ever make it report, and a green result would mean "the check ran", not "the family is
// bounded". Taking them as data lets `test_temporal_conformance` hand it a deliberately unbounded
// kind and prove the check fires -- and then hand it the real ones and mean something by the pass.

#include "scene/temporal_settings.hpp"

#include <span>
#include <string>
#include <vector>

namespace avgen::params {
class ParameterSet;
}

namespace avgen::scene::conformance {

// One thing that does not line up. `subject` is the kind, `rule` is the short stable name of the
// check (so a baseline can name one), `detail` is what to go and look at.
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

// What a kind says about itself. `frames` is its declared history depth at the given settings.
struct DeclaredBound {
    std::string subject;
    bool enabled = false;
    std::uint32_t frames = 0;
};

// The real declarations, straight from `temporalEffectHistoryFrames`.
[[nodiscard]] std::vector<DeclaredBound> declaredBounds(const TemporalSettings& settings);

// Check 1 in isolation, over bounds supplied by the caller -- see the header comment for why this
// takes data rather than computing it.
[[nodiscard]] Report checkBounds(std::span<const DeclaredBound> bounds);

// Checks 2-5, plus check 1 over the real declarations. `settings` supplies the defaults the probe
// registers with.
[[nodiscard]] Report checkTemporal(const TemporalSettings& settings);

// Every path the family registers, obtained the way the engine obtains it: by registering into a
// scratch set and reading back what appeared.
[[nodiscard]] std::vector<std::string> registeredPaths(const TemporalSettings& settings);

// True when `params` holds `path`. Used by the panel-row check and by tests that want the control.
[[nodiscard]] bool registered(const params::ParameterSet& params, const std::string& path);

} // namespace avgen::scene::conformance
