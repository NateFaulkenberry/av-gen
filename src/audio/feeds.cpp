#include "audio/feeds.hpp"

#include "audio/stem_source.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace avgen::audio {

bool Feeds::anyResponse() const {
    if (!levels.empty()) {
        return true;
    }
    for (std::size_t k = 0; k < hit.size(); ++k) {
        if (!hitSource(k).empty()) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> Feeds::keys() const {
    std::vector<std::string> out;
    const auto add = [&](const std::string& k) {
        if (!k.empty() && std::find(out.begin(), out.end(), k) == out.end()) {
            out.push_back(k);
        }
    };
    add(beat);
    add(hits);
    for (const auto& k : hit) {
        if (k) {
            add(*k);
        }
    }
    add(levels);
    add(history);
    if (historySpectrum) {
        add(*historySpectrum);
    }
    if (historyOnsets) {
        add(*historyOnsets);
    }
    return out;
}

bool Feeds::feedsResponse(std::string_view key) const {
    if (key.empty()) {
        return false;
    }
    if (levels == key) {
        return true;
    }
    for (std::size_t k = 0; k < hit.size(); ++k) {
        if (hitSource(k) == key) {
            return true;
        }
    }
    return false;
}

std::string feedValue(const std::string& key) {
    if (key.empty()) {
        return "audio";
    }
    return feedRole(key) ? key : "stem." + key;
}

std::optional<SourceRole> feedRole(std::string_view key) {
    if (!key.starts_with("role.")) {
        return std::nullopt;
    }
    return sourceRoleFromName(key.substr(5));
}

std::string roleFeedKey(SourceRole role) {
    return std::string("role.") + sourceRoleName(role);
}

Result<std::string> feedKey(std::string_view value) {
    if (value == "audio") {
        return std::string();
    }
    if (value.starts_with("stem.") && validStemKey(value.substr(5))) {
        return std::string(value.substr(5));
    }
    if (feedRole(value)) {
        return std::string(value); // ADR-1630
    }
    return fail("'{}' is neither \"audio\" (the mix), \"stem.<key>\" nor \"role.<drums|bass|synth|vocal|other>\"",
                value);
}

Result<Feeds> Feeds::fromJson(const nlohmann::json& j, std::vector<std::string>* warnings) {
    if (!j.is_object()) {
        return fail("'feeds' must be an object");
    }
    Feeds f;
    for (const auto& [name, value] : j.items()) {
        if (!value.is_string()) {
            return fail("feeds.{} must be a string (\"audio\", \"stem.<key>\" or \"role.<role>\")", name);
        }
        auto key = feedKey(value.get<std::string>());
        if (!key) {
            return fail("feeds.{}: {}", name, key.error().message);
        }
        if (name == "beat") {
            f.beat = std::move(*key);
        } else if (name == "hits") {
            f.hits = std::move(*key);
        } else if (name == "levels") {
            f.levels = std::move(*key);
        } else if (name == "history") {
            f.history = std::move(*key);
        } else if (name == "historySpectrum") {
            f.historySpectrum = std::move(*key);
        } else if (name == "historyOnsets") {
            f.historyOnsets = std::move(*key);
        } else if (const auto it = std::find_if(kFeedHitNames.begin(), kFeedHitNames.end(),
                                                [&](const char* n) { return name == n; });
                   it != kFeedHitNames.end()) {
            const auto k = static_cast<std::size_t>(it - kFeedHitNames.begin());
            if (k == 0 && key->empty() && warnings != nullptr) {
                warnings->push_back("feeds.kick is \"audio\", but the mix has no kick (ADR-1620: the kick comes only "
                                    "from a drum source); it feeds nothing");
            }
            f.hit[k] = std::move(*key);
        } else {
            return fail("feeds.{} is not a feed (beat, hits, kick, low, snare, hat, onset, levels, history, "
                        "historySpectrum, historyOnsets)",
                        name);
        }
    }
    return f;
}

nlohmann::json Feeds::toJson() const {
    nlohmann::json j = nlohmann::json::object();
    if (!beat.empty()) j["beat"] = feedValue(beat);
    if (!hits.empty()) j["hits"] = feedValue(hits);
    for (std::size_t k = 0; k < hit.size(); ++k) {
        if (hit[k]) j[kFeedHitNames[k]] = feedValue(*hit[k]);
    }
    if (!levels.empty()) j["levels"] = feedValue(levels);
    if (!history.empty()) j["history"] = feedValue(history);
    if (historySpectrum) j["historySpectrum"] = feedValue(*historySpectrum);
    if (historyOnsets) j["historyOnsets"] = feedValue(*historyOnsets);
    return j;
}

} // namespace avgen::audio
