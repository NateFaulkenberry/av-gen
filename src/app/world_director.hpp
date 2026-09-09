#pragma once

// The World Director (ADR-041): art direction in words. A director knob is an ordinary world macro
// with an artistic name, and its targets are ordinary routes with remaps, so nothing new is
// evaluated per frame. What this file adds is the vocabulary, a way to build a director for a
// world from a small description, and look presets that capture a whole visual configuration.

#include "app/scene_states.hpp"
#include "core/error.hpp"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::app {

// The eighteen words a world can be directed with. Each maps onto whichever parameters that world
// has; a world is not required to implement all of them.
enum class DirectorKnob : std::uint8_t {
    Scale, Density, Drama, Contrast, Warmth, Mystery, Energy, Depth, Atmosphere, Motion, Chaos,
    Stillness, Focus, Glow, Organic, Mechanical, Sacred, Alien
};
[[nodiscard]] const char* directorKnobName(DirectorKnob knob);
[[nodiscard]] std::optional<DirectorKnob> directorKnobFromName(std::string_view name);
[[nodiscard]] std::vector<DirectorKnob> allDirectorKnobs();
// A one-line description of what the word should do to a world, for the UI and for authors.
[[nodiscard]] const char* directorKnobDescription(DirectorKnob knob);

// One knob of a world's director: the artistic word, its default, and where it reaches.
struct DirectorMapping {
    DirectorKnob knob = DirectorKnob::Energy;
    float defaultValue = 0.3f;
    std::vector<WorldMacroTarget> targets;
};

struct WorldDirector {
    std::string name = "director";
    std::vector<DirectorMapping> mappings;

    [[nodiscard]] Result<void> validate() const;
    [[nodiscard]] const DirectorMapping* find(DirectorKnob knob) const;
    // The world macros this director expands to; installing them is the ordinary macro path.
    [[nodiscard]] std::vector<WorldMacro> macros() const;
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<WorldDirector> fromJson(const nlohmann::json& j);
    static Result<WorldDirector> loadFile(const std::filesystem::path& path);
};

// A look is a named parameter snapshot that changes how a world is rendered without touching its
// geometry: light rig, exposure, grade, atmosphere, post and director knobs. It is an ordinary
// preset with a manifest saying which parameter prefixes it is allowed to carry, so applying a
// look to a world that lacks some of them is partial rather than wrong.
struct LookPreset {
    std::string name;
    std::string description;
    params::Preset preset;
    std::vector<std::string> prefixes{"camera/exposure/", "post/", "scene/", "env/", "lightrig/", "macros/"};

    // Values whose path starts with one of `prefixes`; anything else is dropped with a warning.
    [[nodiscard]] params::Preset filtered() const;
    [[nodiscard]] nlohmann::json toJson() const;
    static Result<LookPreset> fromJson(const nlohmann::json& j);
    static Result<LookPreset> loadFile(const std::filesystem::path& path);
};

// Captures the current look from a parameter set (everything under the look's prefixes).
[[nodiscard]] LookPreset captureLook(const params::ParameterSet& params, std::string name);
// Applies a look, returning how many values landed and how many were dropped as unknown.
struct LookApplyResult {
    std::size_t applied = 0;
    std::size_t missing = 0;
};
LookApplyResult applyLook(params::ParameterSet& params, const LookPreset& look);
[[nodiscard]] std::vector<LookPreset> scanLooks(const std::vector<std::filesystem::path>& dirs);

} // namespace avgen::app
