#pragma once

// What the path tracer does with every member of `scene::Scene` and every field of `scene::Material` (ADR-2243).
//
// The capability report (snapshot.hpp) said what the tracer could not do for the features somebody had remembered to
// list. Neon Dreams' clouds, material programs, effect lights, stars and Shockwave pulses were not on it, so a trace
// dropped the floor and the horizon of the scene and changed the colour of every window without a word. This table
// is the inventory the report is checked against: one row per member and per field, each with its disposition, and
// `tests/unit/test_pathtrace_coverage.cpp` reads `src/scene/scene.hpp` and `src/scene/scene_types.hpp` from disk and
// fails when a member or field exists that has no row here -- so a new scene feature cannot reach the tracer without
// somebody deciding what the tracer does with it.

#include <span>
#include <string_view>

namespace avgen::pathtrace {

enum class Disposition {
    Traced,        // the tracer represents it as the scene means it
    Degraded,      // represented by a documented approximation of the same thing (reported when present)
    Unsupported,   // not represented; reported, with the reason, whenever the frame contains it
    NotApplicable, // evaluation state, bookkeeping or a choice the tracer makes for itself (LOD 0, its own occlusion)
};

struct CoverageRow {
    std::string_view name;
    Disposition disposition;
    std::string_view note;
};

[[nodiscard]] std::span<const CoverageRow> sceneCoverage();
[[nodiscard]] std::span<const CoverageRow> materialCoverage();

} // namespace avgen::pathtrace
