// GPU-free half of the OutputManager: descriptors and JSON (unit-tested without a device).
#include "app/output_manager.hpp"

#include "core/log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace avgen::app {

bool operator==(const OutputDesc& a, const OutputDesc& b) {
    return a.name == b.name && a.display == b.display && a.fullscreen == b.fullscreen && a.width == b.width &&
           a.height == b.height && a.borderless == b.borderless && a.alwaysOnTop == b.alwaysOnTop &&
           a.mapping == b.mapping && a.enabled == b.enabled;
}

Result<void> OutputDesc::validate() const {
    if (name.empty()) {
        return fail("output: name must not be empty");
    }
    if (width == 0 || height == 0) {
        return fail("output '{}': size must be positive", name);
    }
    if (display < -1) {
        return fail("output '{}': display must be -1 or a display index", name);
    }
    return mapping.validate();
}

nlohmann::json OutputDesc::toJson() const {
    return nlohmann::json{{"name", name},
                          {"display", display},
                          {"fullscreen", fullscreen},
                          {"width", width},
                          {"height", height},
                          {"borderless", borderless},
                          {"alwaysOnTop", alwaysOnTop},
                          {"enabled", enabled},
                          {"mapping", mapping.toJson()}};
}

namespace {
template <typename T>
Result<void> readField(const nlohmann::json& j, const char* key, T& out) {
    if (!j.contains(key)) {
        return {};
    }
    const auto& v = j.at(key);
    bool ok = false;
    if constexpr (std::is_same_v<T, bool>) {
        ok = v.is_boolean();
    } else if constexpr (std::is_same_v<T, std::string>) {
        ok = v.is_string();
    } else if constexpr (std::is_unsigned_v<T>) {
        ok = v.is_number_unsigned();
    } else {
        ok = v.is_number_integer();
    }
    if (!ok) {
        return fail("output: field '{}' has the wrong type", key);
    }
    out = v.get<T>();
    return {};
}
} // namespace

Result<OutputDesc> OutputDesc::fromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("output: expected an object");
    }
    OutputDesc d;
    if (auto r = readField(j, "name", d.name); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = readField(j, "display", d.display); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = readField(j, "fullscreen", d.fullscreen); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = readField(j, "width", d.width); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = readField(j, "height", d.height); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = readField(j, "borderless", d.borderless); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = readField(j, "alwaysOnTop", d.alwaysOnTop); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = readField(j, "enabled", d.enabled); !r) {
        return std::unexpected(r.error());
    }
    if (j.contains("mapping")) {
        auto m = rendering::OutputMapping::fromJson(j.at("mapping"));
        if (!m) {
            return fail("output '{}': {}", d.name, m.error().message);
        }
        d.mapping = *m;
    }
    if (auto r = d.validate(); !r) {
        return std::unexpected(r.error());
    }
    return d;
}

OutputManager::OutputManager() = default;
OutputManager::~OutputManager() = default;

Result<Output*> OutputManager::add(OutputDesc desc) {
    if (auto r = desc.validate(); !r) {
        return std::unexpected(r.error());
    }
    if (find(desc.name) != nullptr) {
        return fail("output '{}' already exists", desc.name);
    }
    auto output = std::make_unique<Output>();
    output->desc = std::move(desc);
    outputs_.push_back(std::move(output));
    return outputs_.back().get();
}

bool OutputManager::remove(const std::string& name) {
    auto it = std::find_if(outputs_.begin(), outputs_.end(), [&](const auto& o) { return o->desc.name == name; });
    if (it == outputs_.end()) {
        return false;
    }
    outputs_.erase(it); // the runtime's deleter (bound at open) destroys surface then window
    return true;
}

Output* OutputManager::find(const std::string& name) {
    for (auto& o : outputs_) {
        if (o->desc.name == name) {
            return o.get();
        }
    }
    return nullptr;
}

const Output* OutputManager::find(const std::string& name) const {
    for (const auto& o : outputs_) {
        if (o->desc.name == name) {
            return o.get();
        }
    }
    return nullptr;
}

std::size_t OutputManager::openCount() const {
    return static_cast<std::size_t>(std::count_if(outputs_.begin(), outputs_.end(), [](const auto& o) { return o->open(); }));
}

nlohmann::json OutputManager::toJson() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& o : outputs_) {
        arr.push_back(o->desc.toJson());
    }
    return arr;
}

Result<void> OutputManager::fromJson(const nlohmann::json& outputs) {
    if (!outputs.is_array()) {
        return fail("outputs: expected an array");
    }
    std::vector<OutputDesc> descs;
    for (const auto& item : outputs) {
        auto d = OutputDesc::fromJson(item);
        if (!d) {
            return std::unexpected(d.error());
        }
        for (const auto& existing : descs) {
            if (existing.name == d->name) {
                return fail("outputs: duplicate name '{}'", d->name);
            }
        }
        descs.push_back(std::move(*d));
    }
    outputs_.clear(); // closes open windows
    for (auto& d : descs) {
        auto output = std::make_unique<Output>();
        output->desc = std::move(d);
        outputs_.push_back(std::move(output));
    }
    return {};
}

} // namespace avgen::app
