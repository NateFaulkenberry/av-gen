#pragma once

#include <cstdint>
#include <string_view>

namespace avgen::scene {

// ADR-1097: a node's visual importance, for the live scalability levers only (LOD bias, draw distance, shadow-caster
// floor, particle culling). Inferred by default -- a node that is a hero (`Composition::heroes()`) is Hero, everything
// else Normal -- so nobody has to author it. Hero is exempt from every lever; Background and Ambient take them sooner.
enum class Importance : std::uint8_t { Hero, Foreground, Normal, Background, Ambient };
[[nodiscard]] const char* importanceName(Importance importance);
[[nodiscard]] bool importanceFromName(std::string_view name, Importance& out);
// How hard the live levers press on a node of this importance: 0 = exempt, 1 = as set, >1 = sooner.
[[nodiscard]] float importanceLeverWeight(Importance importance);

} // namespace avgen::scene
