#pragma once

// ADR-924: the engine's modulation sources as one document, and back.
//
// A Director plan's routes can need a source of their own (a timeline keyed at the sections, a
// beat-synced LFO), so installing a plan changes the rack, and undoing it must change it back. The
// rack's own JSON (`SourceRack::toJson`) holds each source's kind, name and settings but not its
// parameters -- those are saved with the parameter set -- so on its own it cannot restore an LFO's
// `beatsPerCycle`. This document is the rack's JSON with every source's parameter bases beside its
// settings:
//
//   [{"kind": "lfo", "name": "breath", "settings": {...}, "parameters": {"rate": 0.25, ...}}, ...]
//
// `setSourcesDocument` changes only what differs: a source whose kind, name and settings are
// unchanged is left in the rack (its state and its parameter objects kept), a missing one is added,
// one the document no longer lists is removed, and parameter bases are written where they differ.
// Header-only, beside `directing_context.hpp`, for the same reason: the AI layer and the history's
// undo both reach it without an application source.

#include "app/engine.hpp"
#include "core/error.hpp"
#include "signals/source.hpp"

#include <nlohmann/json.hpp>

#include <string>

namespace avgen::app {

[[nodiscard]] inline nlohmann::json sourcesDocument(Engine& engine) {
    nlohmann::json out = nlohmann::json::array();
    for (const auto& source : engine.sources().sources()) {
        nlohmann::json parameters = nlohmann::json::object();
        const std::string prefix = source->parameterPrefix();
        for (const params::IParameter* p : engine.params().ordered()) {
            if (p != nullptr && p->componentCount() == 1 && p->path().starts_with(prefix)) {
                parameters[p->path().substr(prefix.size())] = p->baseComponent(0);
            }
        }
        out.push_back({{"kind", source->kind()},
                       {"name", source->name()},
                       {"settings", source->settingsToJson()},
                       {"parameters", std::move(parameters)}});
    }
    return out;
}

// Makes the rack match `doc`. Returns whether anything changed (the caller rebinds), or why not.
[[nodiscard]] inline Result<bool> setSourcesDocument(Engine& engine, const nlohmann::json& doc) {
    if (!doc.is_array()) {
        return fail("a sources document must be an array");
    }
    signals::SourceRack& rack = engine.sources();
    const auto same = [](const nlohmann::json& a, const signals::Source& b) {
        return a.value("kind", std::string()) == b.kind() && a.value("name", std::string()) == b.name();
    };
    bool changed = false;
    // Removed: in the rack, not in the document.
    std::vector<std::pair<std::string, std::string>> gone;
    for (const auto& source : rack.sources()) {
        if (std::none_of(doc.begin(), doc.end(), [&](const nlohmann::json& s) { return same(s, *source); })) {
            gone.emplace_back(source->kind(), source->name());
        }
    }
    for (const auto& [kind, name] : gone) {
        changed = rack.remove(kind, name) || changed;
    }
    for (const nlohmann::json& entry : doc) {
        const std::string kind = entry.value("kind", std::string());
        const std::string name = entry.value("name", std::string());
        const nlohmann::json settings = entry.value("settings", nlohmann::json::object());
        signals::Source* existing = rack.find(kind, name);
        if (existing == nullptr || existing->settingsToJson() != settings) {
            std::unique_ptr<signals::Source> made = signals::SourceRack::create(kind, name);
            if (made == nullptr) {
                return fail("source '{}.{}': unknown kind", kind, name);
            }
            if (auto r = made->settingsFromJson(settings); !r) {
                return fail("source '{}.{}': {}", kind, name, r.error().message);
            }
            rack.add(std::move(made)); // attaches, and replaces a same-named source of the kind
            changed = true;
        }
        if (const auto parameters = entry.find("parameters"); parameters != entry.end() && parameters->is_object()) {
            for (const auto& [leaf, value] : parameters->items()) {
                params::IParameter* p = engine.params().find("sources/" + name + "/" + leaf);
                if (p == nullptr || !value.is_number()) {
                    continue;
                }
                const float v = value.get<float>();
                if (p->baseComponent(0) != v) {
                    p->setBaseComponent(0, v);
                    changed = true;
                }
            }
        }
    }
    return changed;
}

} // namespace avgen::app
