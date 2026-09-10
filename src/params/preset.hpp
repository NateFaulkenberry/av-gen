#pragma once

// Presets: parameter snapshots keyed by path (audiovisual-systems lesson 7). A preset stores
// base values only; routes and sources belong to the project. Morphing interpolates component-
// wise and lets each parameter clamp.

#include "core/error.hpp"
#include "params/parameter_set.hpp"

#include <nlohmann/json_fwd.hpp>

#include <map>
#include <string>
#include <vector>

namespace avgen::params {

struct Preset {
    std::string name;
    std::map<std::string, std::vector<float>> values; // path -> base components
};

Preset capturePreset(const ParameterSet& params, std::string name);
// Applies base values; paths missing from the set are ignored (returned count = applied).
std::size_t applyPreset(ParameterSet& params, const Preset& preset);
// Component-wise lerp between a and b (t in 0..1); paths present in only one preset are taken
// from that one at full weight when t favours it (>= 0.5), else left unchanged.
std::size_t applyPresetBlend(ParameterSet& params, const Preset& a, const Preset& b, float t);

// One parameter a preset will overwrite, and by how much.
//
// Presets outranking whatever is in the set is the point of a preset. Doing it *silently* is not:
// a timeline cue recalls its preset the moment the playhead reaches it, so a value an author wrote
// in a scene file and a value a cue preset names are not in competition -- the preset simply wins,
// from that cue onward, with nothing said. That cost a shot a dozen iterations of material edits
// (docs/shot-hyperspace.md), each of which did exactly nothing.
struct PresetConflict {
    std::string path;
    std::vector<float> current; // the base value in effect before the preset is applied
    std::vector<float> applied; // what the preset sets it to
};
// Every parameter `preset` names that exists in the set and whose base value it would change.
// Ordered by path. An empty result means applying the preset here is a no-op.
[[nodiscard]] std::vector<PresetConflict> presetConflicts(const ParameterSet& params, const Preset& preset);

nlohmann::json presetToJson(const Preset& preset);
Result<Preset> presetFromJson(const nlohmann::json& j);

class PresetBank {
public:
    Preset& add(Preset preset); // replaces a preset with the same name
    bool remove(const std::string& name);
    [[nodiscard]] const Preset* find(const std::string& name) const;
    [[nodiscard]] Preset* find(const std::string& name);
    [[nodiscard]] const std::vector<Preset>& presets() const { return presets_; }
    void clear() { presets_.clear(); }

    [[nodiscard]] nlohmann::json toJson() const;
    Result<void> fromJson(const nlohmann::json& j);

private:
    std::vector<Preset> presets_;
};

} // namespace avgen::params
