#include "help/features.hpp"

#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace avgen::help {
namespace {

// `json::value()` throws when the key is present with the wrong type, which would undo the whole
// point of parsing with the non-throwing overload: a hand-edited features.json must produce a
// message, not an exception out of an ImGui frame.
std::string stringOr(const nlohmann::json& object, std::string_view key, std::string fallback = {}) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : fallback;
}

bool boolOr(const nlohmann::json& object, std::string_view key, bool fallback) {
    const auto it = object.find(key);
    return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

std::vector<std::string> stringArray(const nlohmann::json& value) {
    std::vector<std::string> out;
    if (value.is_array()) {
        for (const auto& item : value) {
            if (item.is_string()) {
                out.push_back(item.get<std::string>());
            }
        }
    }
    return out;
}

} // namespace

std::string_view featureKindName(FeatureKind kind) {
    switch (kind) {
    case FeatureKind::Panel:
        return "panel";
    case FeatureKind::Command:
        return "command";
    case FeatureKind::Shortcut:
        return "shortcut";
    case FeatureKind::Subsystem:
        return "subsystem";
    }
    return "subsystem";
}

bool parseFeatureKind(std::string_view name, FeatureKind& out) {
    if (name == "panel") {
        out = FeatureKind::Panel;
        return true;
    }
    if (name == "command") {
        out = FeatureKind::Command;
        return true;
    }
    if (name == "shortcut") {
        out = FeatureKind::Shortcut;
        return true;
    }
    if (name == "subsystem") {
        out = FeatureKind::Subsystem;
        return true;
    }
    return false;
}

void HelpFeatureTable::add(HelpFeature feature) {
    const auto it = std::ranges::find(features_, feature.id, &HelpFeature::id);
    if (it == features_.end()) {
        features_.push_back(std::move(feature));
        return;
    }
    // A later row fills in what the earlier one left blank and overrides what it set. This is what
    // lets a panel row be derived from the editor's registry first -- id, name, summary -- and then
    // given a documentId by the authored file without either half knowing about the other.
    HelpFeature& existing = *it;
    const auto keep = [](std::string& target, std::string incoming) {
        if (!incoming.empty()) {
            target = std::move(incoming);
        }
    };
    keep(existing.name, std::move(feature.name));
    keep(existing.category, std::move(feature.category));
    keep(existing.summary, std::move(feature.summary));
    keep(existing.documentId, std::move(feature.documentId));
    keep(existing.shortcut, std::move(feature.shortcut));
    keep(existing.menuPath, std::move(feature.menuPath));
    keep(existing.availability, std::move(feature.availability));
    // Subsystem is the parser's default for a row that named no kind, so it never overrides one
    // that was stated -- the same "non-empty wins" rule the strings above follow.
    if (feature.kind != FeatureKind::Subsystem) {
        existing.kind = feature.kind;
    }
    if (!feature.related.empty()) {
        existing.related = std::move(feature.related);
    }
}

void HelpFeatureTable::addShortcut(HelpShortcut shortcut) {
    const auto it = std::ranges::find(shortcuts_, shortcut.command, &HelpShortcut::command);
    if (it == shortcuts_.end()) {
        shortcuts_.push_back(std::move(shortcut));
    } else {
        *it = std::move(shortcut);
    }
}

void HelpFeatureTable::clear() {
    features_.clear();
    shortcuts_.clear();
}

const HelpFeature* HelpFeatureTable::find(std::string_view id) const {
    const auto it = std::ranges::find(features_, id, &HelpFeature::id);
    return it == features_.end() ? nullptr : &*it;
}

const HelpShortcut* HelpFeatureTable::findShortcut(std::string_view command) const {
    const auto it = std::ranges::find(shortcuts_, command, &HelpShortcut::command);
    return it == shortcuts_.end() ? nullptr : &*it;
}

std::vector<std::string> HelpFeatureTable::shortcutContexts() const {
    std::vector<std::string> out;
    for (const HelpShortcut& shortcut : shortcuts_) {
        if (std::ranges::find(out, shortcut.context) == out.end()) {
            out.push_back(shortcut.context);
        }
    }
    return out;
}

Result<void> HelpFeatureTable::loadFile(const std::filesystem::path& file) {
    std::ifstream in(file);
    if (!in) {
        return fail("cannot open '{}'", file.string());
    }
    nlohmann::json doc = nlohmann::json::parse(in, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        return fail("'{}' is not a JSON object", file.string());
    }

    if (const auto it = doc.find("features"); it != doc.end()) {
        if (!it->is_array()) {
            return fail("'{}': 'features' must be an array", file.string());
        }
        for (const auto& entry : *it) {
            if (!entry.is_object() || !entry.contains("id") || !entry["id"].is_string()) {
                return fail("'{}': every feature needs a string 'id'", file.string());
            }
            HelpFeature feature;
            feature.id = entry["id"].get<std::string>();
            feature.name = stringOr(entry, "name");
            feature.category = stringOr(entry, "category");
            feature.summary = stringOr(entry, "summary");
            feature.documentId = stringOr(entry, "document");
            feature.shortcut = stringOr(entry, "shortcut");
            feature.menuPath = stringOr(entry, "menu");
            feature.availability = stringOr(entry, "availability");
            if (const auto related = entry.find("related"); related != entry.end()) {
                feature.related = stringArray(*related);
            }
            const std::string kind = stringOr(entry, "kind", "subsystem");
            if (!parseFeatureKind(kind, feature.kind)) {
                return fail("'{}': feature '{}' has unknown kind '{}'", file.string(), feature.id, kind);
            }
            add(std::move(feature));
        }
    }

    if (const auto it = doc.find("shortcuts"); it != doc.end()) {
        if (!it->is_array()) {
            return fail("'{}': 'shortcuts' must be an array", file.string());
        }
        for (const auto& entry : *it) {
            if (!entry.is_object() || !entry.contains("command") || !entry["command"].is_string()) {
                return fail("'{}': every shortcut needs a string 'command'", file.string());
            }
            HelpShortcut shortcut;
            shortcut.command = entry["command"].get<std::string>();
            shortcut.keys = stringOr(entry, "keys");
            shortcut.description = stringOr(entry, "description");
            shortcut.context = stringOr(entry, "context", "General");
            shortcut.documentId = stringOr(entry, "document");
            shortcut.configurable = boolOr(entry, "configurable", false);
            shortcut.callSite = stringOr(entry, "callSite");
            if (shortcut.keys.empty()) {
                return fail("'{}': shortcut '{}' has no 'keys'", file.string(), shortcut.command);
            }
            addShortcut(std::move(shortcut));
        }
    }
    return {};
}

} // namespace avgen::help
