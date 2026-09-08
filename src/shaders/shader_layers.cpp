#include "shaders/shader_layers.hpp"

#include "core/log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace avgen::shaders {

const char* layerStageName(LayerStage stage) { return stage == LayerStage::Post ? "post" : "background"; }

Result<LayerStage> layerStageFromName(const std::string& name) {
    if (name == "background") {
        return LayerStage::Background;
    }
    if (name == "post") {
        return LayerStage::Post;
    }
    return fail("unknown shader stage '{}'", name);
}

ShaderLayerSet::ShaderLayerSet(params::ParameterSet& params, double watchIntervalSeconds)
    : params_(params), watcher_(watchIntervalSeconds) {}

std::string ShaderLayerSet::uniqueName(const std::string& base) const {
    std::string name = base;
    for (auto& c : name) {
        if (c == '/' || c == ' ' || c == '.') {
            c = '_';
        }
    }
    std::string candidate = name;
    for (int i = 2;; ++i) {
        const bool taken = std::any_of(layers_.begin(), layers_.end(),
                                       [&](const auto& l) { return l->name == candidate; });
        if (!taken) {
            return candidate;
        }
        candidate = name + std::to_string(i);
    }
}

Result<std::uint32_t> ShaderLayerSet::add(const std::filesystem::path& path, LayerStage stage) {
    auto layer = std::make_unique<ShaderLayer>();
    layer->id = nextId_++;
    layer->path = path;
    layer->stage = stage;
    layer->name = uniqueName(path.stem().string());
    if (auto r = parseInto(*layer, false); !r) {
        --nextId_;
        return std::unexpected(r.error());
    }
    watcher_.watch(path);
    const std::uint32_t id = layer->id;
    log::info("shader layer '{}' ({}) loaded: {} inputs, {} passes", layer->name, layerStageName(stage),
              layer->parsed.description.inputs.size(), layer->parsed.description.passes.size());
    layers_.push_back(std::move(layer));
    return id;
}

Result<void> ShaderLayerSet::parseInto(ShaderLayer& layer, bool keepValues) {
    auto parsed = parseShaderFile(layer.path);
    if (!parsed) {
        layer.parseError = parsed.error().message;
        return std::unexpected(parsed.error());
    }
    std::vector<std::vector<float>> previous;
    if (keepValues) {
        for (auto* p : layer.inputParams) {
            std::vector<float> v;
            for (std::size_t c = 0; c < p->componentCount(); ++c) {
                v.push_back(p->baseComponent(c));
            }
            previous.push_back(std::move(v));
        }
        unregisterInputs(layer);
    }
    layer.parsed = std::move(*parsed);
    layer.layout = computeInputsLayout(layer.parsed.description);
    layer.moduleSource = generateModuleSource(layer.parsed.description, layer.parsed.body);
    layer.parseError.clear();
    registerInputs(layer, keepValues ? &previous : nullptr);
    return {};
}

void ShaderLayerSet::registerInputs(ShaderLayer& layer, const std::vector<std::vector<float>>* previousValues) {
    layer.inputParams.clear();
    const auto& desc = layer.parsed.description;
    for (std::size_t i = 0; i < desc.inputs.size(); ++i) {
        const auto& input = desc.inputs[i];
        const std::string path = "shader/" + layer.name + "/" + input.name;
        auto component = [&](const std::vector<float>& v, std::size_t c, float fallback) {
            return c < v.size() ? v[c] : fallback;
        };
        params::IParameter* param = nullptr;
        switch (input.type) {
        case InputType::Bool: {
            params::ParamDesc<bool> d;
            d.path = path;
            d.label = input.label;
            d.defaultValue = component(input.defaultValue, 0, 0.0f) >= 0.5f;
            d.hardMin = false;
            d.hardMax = true;
            param = &params_.add(std::move(d));
            break;
        }
        case InputType::Long: {
            params::ParamDesc<int> d;
            d.path = path;
            d.label = input.label;
            d.defaultValue = static_cast<int>(std::lround(component(input.defaultValue, 0, 0.0f)));
            d.hardMin = static_cast<int>(std::lround(component(input.minValue, 0, 0.0f)));
            d.hardMax = static_cast<int>(std::lround(component(input.maxValue, 0, 10.0f)));
            param = &params_.add(std::move(d));
            break;
        }
        case InputType::Color: {
            params::ParamDesc<glm::vec4> d;
            d.path = path;
            d.label = input.label;
            d.isColor = true;
            for (int c = 0; c < 4; ++c) {
                d.defaultValue[c] = component(input.defaultValue, static_cast<std::size_t>(c), c == 3 ? 1.0f : 0.0f);
                d.hardMin[c] = component(input.minValue, static_cast<std::size_t>(c), 0.0f);
                d.hardMax[c] = component(input.maxValue, static_cast<std::size_t>(c), 1.0f);
            }
            param = &params_.add(std::move(d));
            break;
        }
        case InputType::Point2D: {
            params::ParamDesc<glm::vec2> d;
            d.path = path;
            d.label = input.label;
            for (int c = 0; c < 2; ++c) {
                d.defaultValue[c] = component(input.defaultValue, static_cast<std::size_t>(c), 0.5f);
                d.hardMin[c] = component(input.minValue, static_cast<std::size_t>(c), 0.0f);
                d.hardMax[c] = component(input.maxValue, static_cast<std::size_t>(c), 1.0f);
            }
            param = &params_.add(std::move(d));
            break;
        }
        case InputType::Event:
        case InputType::Float:
        default: {
            params::ParamDesc<float> d;
            d.path = path;
            d.label = input.label;
            d.defaultValue = component(input.defaultValue, 0, 0.0f);
            d.hardMin = component(input.minValue, 0, 0.0f);
            d.hardMax = component(input.maxValue, 0, 1.0f);
            if (input.type == InputType::Event) {
                d.hardMin = 0.0f;
                d.hardMax = 1.0f;
            }
            param = &params_.add(std::move(d));
            break;
        }
        }
        if (previousValues != nullptr && i < previousValues->size()) {
            const auto& prev = (*previousValues)[i];
            for (std::size_t c = 0; c < param->componentCount() && c < prev.size(); ++c) {
                param->setBaseComponent(c, prev[c]);
            }
        }
        layer.inputParams.push_back(param);
    }
}

void ShaderLayerSet::unregisterInputs(ShaderLayer& layer) {
    for (auto* p : layer.inputParams) {
        params_.remove(p->path());
    }
    layer.inputParams.clear();
}

bool ShaderLayerSet::remove(std::uint32_t id) {
    auto it = std::find_if(layers_.begin(), layers_.end(), [&](const auto& l) { return l->id == id; });
    if (it == layers_.end()) {
        return false;
    }
    unregisterInputs(**it);
    watcher_.unwatch((*it)->path);
    layers_.erase(it);
    return true;
}

void ShaderLayerSet::clear() {
    for (auto& layer : layers_) {
        unregisterInputs(*layer);
    }
    layers_.clear();
    watcher_.clear();
}

ShaderLayer* ShaderLayerSet::find(std::uint32_t id) {
    for (auto& layer : layers_) {
        if (layer->id == id) {
            return layer.get();
        }
    }
    return nullptr;
}

bool ShaderLayerSet::move(std::uint32_t id, int delta) {
    auto it = std::find_if(layers_.begin(), layers_.end(), [&](const auto& l) { return l->id == id; });
    if (it == layers_.end()) {
        return false;
    }
    const auto index = static_cast<int>(it - layers_.begin());
    const int target = std::clamp(index + delta, 0, static_cast<int>(layers_.size()) - 1);
    if (target == index) {
        return false;
    }
    std::swap(layers_[static_cast<std::size_t>(index)], layers_[static_cast<std::size_t>(target)]);
    return true;
}

Result<void> ShaderLayerSet::reload(std::uint32_t id) {
    auto* layer = find(id);
    if (layer == nullptr) {
        return fail("no shader layer {}", id);
    }
    auto r = parseInto(*layer, true);
    if (!r) {
        log::error("shader '{}': {}", layer->name, r.error().message);
        return r;
    }
    ++layer->version;
    ++reloads_;
    log::info("shader '{}' reloaded (version {})", layer->name, layer->version);
    return {};
}

void ShaderLayerSet::update(const FrameTime& time, const StdUniforms& base) {
    for (const auto& changed : watcher_.poll()) {
        for (auto& layer : layers_) {
            if (layer->path == changed) {
                (void)reload(layer->id);
            }
        }
    }
    std::vector<std::vector<float>> values;
    for (auto& layer : layers_) {
        layer->std = base;
        layer->std.time = static_cast<float>(time.renderTime);
        layer->std.timeDelta = static_cast<float>(time.deltaTime);
        layer->std.frameIndex = static_cast<float>(time.frameIndex);
        values.clear();
        for (auto* p : layer->inputParams) {
            std::vector<float> v(p->componentCount());
            for (std::size_t c = 0; c < v.size(); ++c) {
                v[c] = p->finalComponent(c);
            }
            values.push_back(std::move(v));
        }
        packInputs(layer->parsed.description, layer->layout, values, layer->packedInputs);
    }
}

void ShaderLayerSet::reattach() {
    for (auto& layer : layers_) {
        std::vector<std::vector<float>> previous;
        for (auto* p : layer->inputParams) {
            std::vector<float> v;
            for (std::size_t c = 0; c < p->componentCount(); ++c) {
                v.push_back(p->baseComponent(c));
            }
            previous.push_back(std::move(v));
        }
        registerInputs(*layer, previous.empty() ? nullptr : &previous);
    }
}

nlohmann::json ShaderLayerSet::toJson() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& layer : layers_) {
        arr.push_back({{"path", layer->path.string()}, {"stage", layerStageName(layer->stage)}, {"enabled", layer->enabled}});
    }
    return arr;
}

Result<void> ShaderLayerSet::fromJson(const nlohmann::json& j) {
    if (!j.is_array()) {
        return fail("'shaders' must be an array");
    }
    clear();
    for (const auto& entry : j) {
        if (!entry.is_object() || !entry.contains("path")) {
            return fail("shader entry must be an object with a 'path'");
        }
        auto stage = layerStageFromName(entry.value("stage", std::string("background")));
        if (!stage) {
            return std::unexpected(stage.error());
        }
        auto id = add(entry["path"].get<std::string>(), *stage);
        if (!id) {
            log::warn("project shader skipped: {}", id.error().message);
            continue;
        }
        find(*id)->enabled = entry.value("enabled", true);
    }
    return {};
}

} // namespace avgen::shaders
