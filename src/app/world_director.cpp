#include "app/world_director.hpp"

#include "core/log.hpp"
#include "params/serialization.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>

namespace avgen::app {

namespace {
struct KnobInfo {
    DirectorKnob knob;
    const char* name;
    const char* description;
};
constexpr std::array<KnobInfo, 18> kKnobs{{
    {DirectorKnob::Scale, "scale", "How large the world feels: structure size, spacing, fog depth, camera distance."},
    {DirectorKnob::Density, "density", "How much is in frame: instance counts, filters, particle rates."},
    {DirectorKnob::Drama, "drama", "Lighting contrast, shadow depth, atmosphere and selective emission."},
    {DirectorKnob::Contrast, "contrast", "Tonal separation: grade contrast, key-to-fill ratio, exposure."},
    {DirectorKnob::Warmth, "warmth", "Colour temperature of key lights and the grade's white balance."},
    {DirectorKnob::Mystery, "mystery", "How much is hidden: fog density, falloff, darkness, occlusion."},
    {DirectorKnob::Energy, "energy", "Overall activity: motion amplitude, emission, particle output."},
    {DirectorKnob::Depth, "depth", "Layer separation: atmospheric perspective, depth of field, parallax."},
    {DirectorKnob::Atmosphere, "atmosphere", "Volumetric density, scattering and light shafts."},
    {DirectorKnob::Motion, "motion", "Speed of everything that moves, from fields to the camera."},
    {DirectorKnob::Chaos, "chaos", "Disorder: noise amplitude, turbulence, randomised variation."},
    {DirectorKnob::Stillness, "stillness", "The opposite of motion: damping, slower fields, held camera."},
    {DirectorKnob::Focus, "focus", "How strongly the frame points at its subject: clearance, framing, DOF."},
    {DirectorKnob::Glow, "glow", "Emissive intensity, bloom and halation."},
    {DirectorKnob::Organic, "organic", "Curvature, irregular variation, soft motion, warm palette."},
    {DirectorKnob::Mechanical, "mechanical", "Regularity, hard edges, metallic response, stepped motion."},
    {DirectorKnob::Sacred, "sacred", "Symmetry, verticality, shafts of light, restraint."},
    {DirectorKnob::Alien, "alien", "Unusual palette, asymmetry, strange emissives and proportions."},
}};
} // namespace

const char* directorKnobName(DirectorKnob knob) {
    for (const KnobInfo& info : kKnobs) {
        if (info.knob == knob) {
            return info.name;
        }
    }
    return "energy";
}

std::optional<DirectorKnob> directorKnobFromName(std::string_view name) {
    for (const KnobInfo& info : kKnobs) {
        if (name == info.name) {
            return info.knob;
        }
    }
    return std::nullopt;
}

const char* directorKnobDescription(DirectorKnob knob) {
    for (const KnobInfo& info : kKnobs) {
        if (info.knob == knob) {
            return info.description;
        }
    }
    return "";
}

std::vector<DirectorKnob> allDirectorKnobs() {
    std::vector<DirectorKnob> out;
    out.reserve(kKnobs.size());
    for (const KnobInfo& info : kKnobs) {
        out.push_back(info.knob);
    }
    return out;
}

Result<void> WorldDirector::validate() const {
    std::vector<DirectorKnob> seen;
    for (const DirectorMapping& m : mappings) {
        if (std::find(seen.begin(), seen.end(), m.knob) != seen.end()) {
            return fail("director '{}': knob '{}' appears twice", name, directorKnobName(m.knob));
        }
        seen.push_back(m.knob);
        if (m.defaultValue < 0.0f || m.defaultValue > 1.0f) {
            return fail("director '{}': knob '{}' default must be in 0..1", name, directorKnobName(m.knob));
        }
        for (const WorldMacroTarget& t : m.targets) {
            if (t.path.empty()) {
                return fail("director '{}': knob '{}' has a target with no path", name, directorKnobName(m.knob));
            }
        }
    }
    return {};
}

const DirectorMapping* WorldDirector::find(DirectorKnob knob) const {
    for (const DirectorMapping& m : mappings) {
        if (m.knob == knob) {
            return &m;
        }
    }
    return nullptr;
}

std::vector<WorldMacro> WorldDirector::macros() const {
    std::vector<WorldMacro> out;
    out.reserve(mappings.size());
    for (const DirectorMapping& m : mappings) {
        WorldMacro macro;
        macro.name = directorKnobName(m.knob);
        macro.label = macro.name;
        std::transform(macro.label.begin(), macro.label.begin() + 1, macro.label.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        macro.defaultValue = m.defaultValue;
        macro.targets = m.targets;
        out.push_back(std::move(macro));
    }
    return out;
}

nlohmann::json WorldDirector::toJson() const {
    nlohmann::json j;
    j["name"] = name;
    nlohmann::json knobs = nlohmann::json::array();
    for (const DirectorMapping& m : mappings) {
        nlohmann::json e;
        e["knob"] = directorKnobName(m.knob);
        e["default"] = m.defaultValue;
        nlohmann::json targets = nlohmann::json::array();
        for (const WorldMacroTarget& t : m.targets) {
            nlohmann::json tj;
            tj["path"] = t.path;
            tj["min"] = t.min;
            tj["max"] = t.max;
            if (t.component >= 0) {
                tj["component"] = t.component;
            }
            tj["curve"] = params::curveTypeName(t.curve);
            tj["curveAmount"] = t.curveAmount;
            tj["op"] = params::modOpName(t.op);
            targets.push_back(std::move(tj));
        }
        e["targets"] = std::move(targets);
        knobs.push_back(std::move(e));
    }
    j["knobs"] = std::move(knobs);
    return j;
}

Result<WorldDirector> WorldDirector::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("a world director must be an object");
    }
    WorldDirector out;
    if (j.contains("name") && j["name"].is_string()) {
        out.name = j["name"].get<std::string>();
    }
    if (!j.contains("knobs") || !j["knobs"].is_array()) {
        return fail("world director '{}': 'knobs' must be an array", out.name);
    }
    for (const nlohmann::json& e : j["knobs"]) {
        if (!e.is_object() || !e.contains("knob") || !e["knob"].is_string()) {
            return fail("world director '{}': each knob needs a 'knob' name", out.name);
        }
        const std::string knobName = e["knob"].get<std::string>();
        const auto knob = directorKnobFromName(knobName);
        if (!knob) {
            return fail("world director '{}': unknown knob '{}'", out.name, knobName);
        }
        DirectorMapping m;
        m.knob = *knob;
        if (e.contains("default") && e["default"].is_number()) {
            m.defaultValue = e["default"].get<float>();
        }
        if (e.contains("targets")) {
            if (!e["targets"].is_array()) {
                return fail("world director '{}': knob '{}': 'targets' must be an array", out.name, knobName);
            }
            for (const nlohmann::json& tj : e["targets"]) {
                if (!tj.is_object() || !tj.contains("path") || !tj["path"].is_string()) {
                    return fail("world director '{}': knob '{}': targets need a 'path'", out.name, knobName);
                }
                WorldMacroTarget t;
                t.path = tj["path"].get<std::string>();
                if (tj.contains("component") && tj["component"].is_number_integer()) {
                    t.component = tj["component"].get<int>();
                }
                if (tj.contains("min") && tj["min"].is_number()) {
                    t.min = tj["min"].get<float>();
                }
                if (tj.contains("max") && tj["max"].is_number()) {
                    t.max = tj["max"].get<float>();
                }
                if (tj.contains("curve") && tj["curve"].is_string()) {
                    const auto c = params::curveTypeFromName(tj["curve"].get<std::string>());
                    if (!c) {
                        return fail("world director '{}': unknown curve '{}'", out.name,
                                    tj["curve"].get<std::string>());
                    }
                    t.curve = *c;
                }
                if (tj.contains("curveAmount") && tj["curveAmount"].is_number()) {
                    t.curveAmount = tj["curveAmount"].get<float>();
                }
                if (tj.contains("op") && tj["op"].is_string()) {
                    const auto op = params::modOpFromName(tj["op"].get<std::string>());
                    if (!op) {
                        return fail("world director '{}': unknown op '{}'", out.name, tj["op"].get<std::string>());
                    }
                    t.op = *op;
                }
                m.targets.push_back(std::move(t));
            }
        }
        out.mappings.push_back(std::move(m));
    }
    if (auto ok = out.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    return out;
}

Result<WorldDirector> WorldDirector::loadFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return fail("cannot read world director '{}'", path.string());
    }
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded()) {
        return fail("world director '{}': invalid JSON", path.string());
    }
    return fromJson(j);
}

// ---- looks ---------------------------------------------------------------------------------

params::Preset LookPreset::filtered() const {
    params::Preset out;
    out.name = preset.name.empty() ? name : preset.name;
    for (const auto& [path, values] : preset.values) {
        const bool allowed = std::any_of(prefixes.begin(), prefixes.end(), [&](const std::string& prefix) {
            return path.size() >= prefix.size() && path.compare(0, prefix.size(), prefix) == 0;
        });
        if (allowed) {
            out.values.emplace(path, values);
        }
    }
    return out;
}

nlohmann::json LookPreset::toJson() const {
    nlohmann::json j;
    j["format"] = "avgen-look";
    j["name"] = name;
    if (!description.empty()) {
        j["description"] = description;
    }
    j["prefixes"] = prefixes;
    j["values"] = params::presetToJson(filtered())["values"];
    return j;
}

Result<LookPreset> LookPreset::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("a look must be an object");
    }
    LookPreset out;
    out.name = j.contains("name") && j["name"].is_string() ? j["name"].get<std::string>() : std::string("look");
    if (j.contains("description") && j["description"].is_string()) {
        out.description = j["description"].get<std::string>();
    }
    if (j.contains("prefixes") && j["prefixes"].is_array()) {
        out.prefixes.clear();
        for (const auto& p : j["prefixes"]) {
            if (p.is_string()) {
                out.prefixes.push_back(p.get<std::string>());
            }
        }
    }
    nlohmann::json presetJson = nlohmann::json::object();
    presetJson["name"] = out.name;
    presetJson["values"] = j.contains("values") ? j["values"] : nlohmann::json::object();
    auto preset = params::presetFromJson(presetJson);
    if (!preset) {
        return fail("look '{}': {}", out.name, preset.error().message);
    }
    out.preset = std::move(*preset);
    return out;
}

Result<LookPreset> LookPreset::loadFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return fail("cannot read look '{}'", path.string());
    }
    const nlohmann::json j = nlohmann::json::parse(in, nullptr, false);
    if (j.is_discarded()) {
        return fail("look '{}': invalid JSON", path.string());
    }
    return fromJson(j);
}

LookPreset captureLook(const params::ParameterSet& params, std::string name) {
    LookPreset out;
    out.name = std::move(name);
    const params::Preset all = params::capturePreset(params, out.name);
    out.preset.name = out.name;
    for (const auto& [path, values] : all.values) {
        const bool allowed = std::any_of(out.prefixes.begin(), out.prefixes.end(), [&](const std::string& prefix) {
            return path.size() >= prefix.size() && path.compare(0, prefix.size(), prefix) == 0;
        });
        if (allowed) {
            out.preset.values.emplace(path, values);
        }
    }
    return out;
}

LookApplyResult applyLook(params::ParameterSet& params, const LookPreset& look) {
    LookApplyResult result;
    const params::Preset preset = look.filtered();
    for (const auto& [path, values] : preset.values) {
        params::IParameter* param = params.find(path);
        if (param == nullptr) {
            ++result.missing;
            continue;
        }
        for (std::size_t i = 0; i < values.size() && i < param->componentCount(); ++i) {
            param->setBaseComponent(i, values[i]);
        }
        ++result.applied;
    }
    if (result.missing > 0) {
        log::info("look '{}': {} value(s) applied, {} not present in this world", look.name, result.applied,
                  result.missing);
    }
    return result;
}

std::vector<LookPreset> scanLooks(const std::vector<std::filesystem::path>& dirs) {
    std::vector<LookPreset> out;
    std::error_code ec;
    for (const std::filesystem::path& dir : dirs) {
        if (!std::filesystem::is_directory(dir, ec)) {
            continue;
        }
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_regular_file(ec) || entry.path().extension() != ".json") {
                continue;
            }
            auto look = LookPreset::loadFile(entry.path());
            if (look) {
                out.push_back(std::move(*look));
            }
        }
    }
    std::sort(out.begin(), out.end(), [](const LookPreset& a, const LookPreset& b) { return a.name < b.name; });
    return out;
}

} // namespace avgen::app
