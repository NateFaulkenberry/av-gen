#include "core/json_keys.hpp"

#include "core/log.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace avgen::json_keys {

std::vector<std::string> unknownKeys(const nlohmann::json& obj, std::span<const std::string_view> known) {
    std::vector<std::string> out;
    if (!obj.is_object()) {
        return out;
    }
    for (const auto& entry : obj.items()) {
        const std::string& key = entry.key();
        if (!key.empty() && key.front() == '_') {
            continue; // the repository's comment convention: "_note" and friends
        }
        if (std::find(known.begin(), known.end(), std::string_view(key)) == known.end()) {
            out.push_back(key);
        }
    }
    return out;
}

void warnUnknownKeys(const nlohmann::json& obj, std::span<const std::string_view> known,
                     std::string_view where) {
    for (const std::string& key : unknownKeys(obj, known)) {
        log::warn("{}: key '{}' is not one this build reads and was ignored", where, key);
    }
}

} // namespace avgen::json_keys
