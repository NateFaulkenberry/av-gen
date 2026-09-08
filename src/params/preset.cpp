#include "params/preset.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>
#include <set>
#include <utility>

namespace avgen::params {

using nlohmann::json;

namespace {

void applyComponents(IParameter& param, const std::vector<float>& values) {
    const std::size_t count = std::min(param.componentCount(), values.size());
    for (std::size_t i = 0; i < count; ++i) {
        param.setBaseComponent(i, values[i]);
    }
}

} // namespace

Preset capturePreset(const ParameterSet& params, std::string name) {
    Preset preset;
    preset.name = std::move(name);
    for (const IParameter* param : params.ordered()) {
        if (!param->flags().serialized) {
            continue;
        }
        std::vector<float> components;
        components.reserve(param->componentCount());
        for (std::size_t i = 0; i < param->componentCount(); ++i) {
            components.push_back(param->baseComponent(i));
        }
        preset.values.emplace(param->path(), std::move(components));
    }
    return preset;
}

std::size_t applyPreset(ParameterSet& params, const Preset& preset) {
    std::size_t applied = 0;
    for (const auto& [path, values] : preset.values) {
        IParameter* param = params.find(path);
        if (param == nullptr) {
            continue;
        }
        applyComponents(*param, values);
        ++applied;
    }
    return applied;
}

std::size_t applyPresetBlend(ParameterSet& params, const Preset& a, const Preset& b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    std::size_t applied = 0;
    std::set<std::string> paths;
    for (const auto& [path, values] : a.values) {
        paths.insert(path);
    }
    for (const auto& [path, values] : b.values) {
        paths.insert(path);
    }
    for (const std::string& path : paths) {
        IParameter* param = params.find(path);
        if (param == nullptr) {
            continue;
        }
        const auto inA = a.values.find(path);
        const auto inB = b.values.find(path);
        if (inA != a.values.end() && inB != b.values.end()) {
            const std::size_t count =
                std::min({param->componentCount(), inA->second.size(), inB->second.size()});
            for (std::size_t i = 0; i < count; ++i) {
                const float va = inA->second[i];
                const float vb = inB->second[i];
                param->setBaseComponent(i, va + (vb - va) * t);
            }
            ++applied;
        } else if (inB != b.values.end()) {
            if (t >= 0.5f) {
                applyComponents(*param, inB->second);
                ++applied;
            }
        } else if (t < 0.5f) {
            applyComponents(*param, inA->second);
            ++applied;
        }
    }
    return applied;
}

json presetToJson(const Preset& preset) {
    json values = json::object();
    for (const auto& [path, components] : preset.values) {
        json array = json::array();
        for (const float v : components) {
            array.push_back(static_cast<double>(v));
        }
        values[path] = std::move(array);
    }
    return json{{"name", preset.name}, {"values", std::move(values)}};
}

Result<Preset> presetFromJson(const json& j) {
    if (!j.is_object()) {
        return fail("preset must be a JSON object");
    }
    Preset preset;
    const auto name = j.find("name");
    if (name == j.end() || !name->is_string()) {
        return fail("preset needs a string 'name'");
    }
    preset.name = name->get<std::string>();
    const auto values = j.find("values");
    if (values == j.end()) {
        return preset;
    }
    if (!values->is_object()) {
        return fail("preset '{}': 'values' must be an object", preset.name);
    }
    for (const auto& [path, value] : values->items()) {
        std::vector<float> components;
        if (value.is_number()) {
            components.push_back(value.get<float>());
        } else if (value.is_boolean()) {
            components.push_back(value.get<bool>() ? 1.0f : 0.0f);
        } else if (value.is_array()) {
            for (const auto& element : value) {
                if (!element.is_number()) {
                    return fail("preset '{}': '{}' must be an array of numbers", preset.name, path);
                }
                components.push_back(element.get<float>());
            }
        } else {
            return fail("preset '{}': '{}' must be a number or an array of numbers", preset.name, path);
        }
        preset.values.emplace(path, std::move(components));
    }
    return preset;
}

Preset& PresetBank::add(Preset preset) {
    for (Preset& existing : presets_) {
        if (existing.name == preset.name) {
            existing = std::move(preset);
            return existing;
        }
    }
    presets_.push_back(std::move(preset));
    return presets_.back();
}

bool PresetBank::remove(const std::string& name) {
    const auto it =
        std::find_if(presets_.begin(), presets_.end(), [&](const Preset& p) { return p.name == name; });
    if (it == presets_.end()) {
        return false;
    }
    presets_.erase(it);
    return true;
}

const Preset* PresetBank::find(const std::string& name) const {
    for (const Preset& preset : presets_) {
        if (preset.name == name) {
            return &preset;
        }
    }
    return nullptr;
}

Preset* PresetBank::find(const std::string& name) {
    for (Preset& preset : presets_) {
        if (preset.name == name) {
            return &preset;
        }
    }
    return nullptr;
}

json PresetBank::toJson() const {
    json array = json::array();
    for (const Preset& preset : presets_) {
        array.push_back(presetToJson(preset));
    }
    return array;
}

// Replaces the bank; a malformed document leaves it unchanged. Later duplicates replace earlier.
Result<void> PresetBank::fromJson(const json& j) {
    if (!j.is_array()) {
        return fail("'presets' must be an array");
    }
    PresetBank parsed;
    for (std::size_t i = 0; i < j.size(); ++i) {
        auto preset = presetFromJson(j[i]);
        if (!preset) {
            return fail("presets[{}]: {}", i, preset.error().message);
        }
        parsed.add(std::move(*preset));
    }
    presets_ = std::move(parsed.presets_);
    return {};
}

} // namespace avgen::params
