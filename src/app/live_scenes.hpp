#pragma once

// The live scene switcher (ADR-1063): step through the Sonic VFX scenes while playing, without the File menu.
//
// The list is the examples index's Sonic Live followed by every "Sonic VFX" example, in the index's order. The Live
// panel's Scenes row, PageUp/PageDown and a MIDI program change (program n -> scene n mod count) all ask for one of
// them. A switch between two listed scenes opens the next one directly: a performer mid-set is not asked whether to
// save an example. A switch away from any other project goes through the unsaved-changes prompt as every open does
// (ADR-440).
//
// Live input and a running projection carry across by themselves (a `sonic.live` project keeps live input on;
// the projection is the machine's, ADR-1026). What would not carry is the performer's response: `sonic/response/*`
// are the project's parameters. So the switcher carries them as OFFSETS from each scene's own defaults -- a room
// that needs +0.15 sensitivity needs it in every scene, and a scene authored hotter stays hotter.
//
// ADR-1074: the switcher steps through the SET the open project belongs to. A set is an index category in
// `kLiveSceneSets`, in index order; "Sonic VFX" is led by Sonic Live (today's list, unchanged), and "Sonic Abstract"
// is its own examples alone. A project in no set (or no project) gets the default set, "Sonic VFX", as before.
//
// GPU-free and engine-free: decisions here, wiring in Application.

#include "app/examples.hpp"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace avgen::params {
class ParameterSet;
}

namespace avgen::app {

inline constexpr const char* kLiveSceneCategory = "Sonic VFX";
// ADR-1074: the categories that are live scene sets, the first being the default.
inline constexpr std::array<const char*, 2> kLiveSceneSets{"Sonic VFX", "Sonic Abstract"};

// Sonic Live (when present) and then the "Sonic VFX" examples, in the index's order: the default set.
[[nodiscard]] std::vector<ExampleInfo> liveSceneList(const std::vector<ExampleInfo>& examples);
// ADR-1074: one set's scenes: the category's examples in index order, led by Sonic Live for "Sonic VFX".
[[nodiscard]] std::vector<ExampleInfo> liveSceneSet(const std::vector<ExampleInfo>& examples, const std::string& category);
// ADR-1074: the set `project` belongs to (the first in kLiveSceneSets that lists it), else the default set.
[[nodiscard]] std::vector<ExampleInfo> liveSceneListFor(const std::vector<ExampleInfo>& examples,
                                                        const std::filesystem::path& project);
// The list's index of `project` (compared as absolute, normal paths), or -1.
[[nodiscard]] int liveSceneIndex(const std::vector<ExampleInfo>& list, const std::filesystem::path& project);
// `delta` steps from `current`, wrapping; from -1 (not a listed scene) +1 is the first and -1 the last.
[[nodiscard]] int steppedLiveScene(int current, int delta, int count);
// MIDI program change: program n is scene n mod count (-1 with no scenes).
[[nodiscard]] int liveSceneForProgram(int program, int count);
// The name the panel shows: the example's name without a leading "<its category> - " (e.g. "Sonic VFX - ").
[[nodiscard]] std::string liveSceneLabel(const ExampleInfo& scene);

// The performer's response, carried across a switch.
inline constexpr std::array<const char*, 5> kResponseParameterPaths{
    "sonic/response/sensitivity", "sonic/response/transient", "sonic/response/sustain", "sonic/response/attack",
    "sonic/response/release"};
struct ResponseCarry {
    std::array<float, 5> offset{}; // base - default per parameter (attack/release: base / default)
};
// What the open project's response parameters are, against their defaults. Nothing when it has none.
[[nodiscard]] std::optional<ResponseCarry> captureResponse(const params::ParameterSet& params);
// The same offsets on the new project's defaults, clamped to each parameter's range.
void applyResponse(const ResponseCarry& carry, params::ParameterSet& params);

} // namespace avgen::app
