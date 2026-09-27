#include "directing/plan_route.hpp"

#include "directing/text.hpp"
#include "params/serialization.hpp"

#include <fmt/format.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace avgen::directing {
namespace {

using json = nlohmann::json;

constexpr std::array<std::pair<ReactiveLevel, const char*>, 3> kLevels{{
    {ReactiveLevel::Micro, "micro"},
    {ReactiveLevel::Meso, "meso"},
    {ReactiveLevel::Macro, "macro"},
}};

// Every key a project route and its chain may carry: what `params::routeToJson`/`chainToJson` write.
const std::vector<std::string>& routeKeys() {
    static const std::vector<std::string> keys{"source", "target", "component", "amount", "op", "polarity",
                                               "enabled", "chain", "depthSource", "depthMin", "depthMax"};
    return keys;
}
const std::vector<std::string>& chainKeys() {
    static const std::vector<std::string> keys = [] {
        std::vector<std::string> out;
        for (const auto& [k, v] : params::chainToJson(params::ProcessorChain{}).items()) {
            out.push_back(k);
        }
        return out;
    }();
    return keys;
}

Issue issueAt(Severity severity, IssueCode code, std::string location, std::string message) {
    Issue issue;
    issue.severity = severity;
    issue.code = code;
    issue.location = std::move(location);
    issue.message = std::move(message);
    return issue;
}

// SCHEMA_UNKNOWN_FIELD for every key of `object` that is not in `known`, with the nearest known key.
void reportUnknown(const json& object, const std::vector<std::string>& known, const std::string& location,
                   std::vector<Issue>& issues) {
    for (const auto& [key, value] : object.items()) {
        if (std::find(known.begin(), known.end(), key) != known.end()) {
            continue;
        }
        Issue issue = issueAt(Severity::Warning, IssueCode::SchemaUnknownField, location + "/" + key,
                              fmt::format("'{}' is not a field of this object; it is ignored", key));
        issue.suggestions = text::nearest(key, known);
        issues.push_back(std::move(issue));
    }
}

// A string field; "" when absent. Wrong type = SCHEMA_INVALID.
std::string readString(const json& j, const char* key, const std::string& location, std::vector<Issue>& issues,
                       bool& ok, bool required = false) {
    const auto it = j.find(key);
    if (it == j.end()) {
        if (required) {
            issues.push_back(issueAt(Severity::Error, IssueCode::SchemaInvalid, location + "/" + key,
                                     fmt::format("'{}' is required", key)));
            ok = false;
        }
        return {};
    }
    if (!it->is_string()) {
        issues.push_back(issueAt(Severity::Error, IssueCode::SchemaInvalid, location + "/" + key,
                                 fmt::format("'{}' must be a string", key)));
        ok = false;
        return {};
    }
    return it->get<std::string>();
}

void putString(json& j, const char* key, const std::string& v) {
    if (!v.empty()) {
        j[key] = v;
    }
}

} // namespace

const char* reactiveLevelName(ReactiveLevel level) {
    for (const auto& [l, n] : kLevels) {
        if (l == level) {
            return n;
        }
    }
    return "meso";
}

std::optional<ReactiveLevel> reactiveLevelFromName(std::string_view name) {
    for (const auto& [l, n] : kLevels) {
        if (name == n) {
            return l;
        }
    }
    return std::nullopt;
}

bool sameRoute(const params::ModRoute& a, const params::ModRoute& b) {
    return params::routeToJson(a) == params::routeToJson(b);
}

bool operator==(const PlanRoute& a, const PlanRoute& b) {
    return a.key == b.key && a.level == b.level && a.group == b.group && a.owner == b.owner && a.layer == b.layer &&
           a.reason == b.reason && sameRoute(a.route, b.route);
}

std::string planItemId(std::string_view planId, std::string_view key) {
    return fmt::format("{}/{}", planId, key);
}

std::optional<std::pair<std::string, std::string>> splitPlanItemId(std::string_view id) {
    const auto slash = id.find('/');
    if (slash == std::string_view::npos || slash == 0 || slash + 1 >= id.size()) {
        return std::nullopt;
    }
    return std::make_pair(std::string(id.substr(0, slash)), std::string(id.substr(slash + 1)));
}

json planRouteToJson(const PlanRoute& item) {
    params::ModRoute route = item.route;
    route.planItem.clear(); // the compiler's, never the plan's
    json j{{"key", item.key}, {"level", reactiveLevelName(item.level)}, {"route", params::routeToJson(route)}};
    putString(j, "group", item.group);
    putString(j, "owner", item.owner);
    putString(j, "layer", item.layer);
    putString(j, "reason", item.reason);
    return j;
}

json planSourceToJson(const PlanSource& item) {
    json j{{"key", item.key}, {"kind", item.kind}, {"name", item.name}};
    putString(j, "reason", item.reason);
    if (!item.settings.is_null() && !item.settings.empty()) {
        j["settings"] = item.settings;
    }
    if (!item.parameters.empty()) {
        json params = json::object();
        for (const auto& [leaf, value] : item.parameters) {
            params[leaf] = value;
        }
        j["parameters"] = std::move(params);
    }
    return j;
}

std::optional<PlanRoute> planRouteFromJson(const json& j, const std::string& location, std::vector<Issue>& issues) {
    if (!j.is_object()) {
        issues.push_back(issueAt(Severity::Error, IssueCode::SchemaInvalid, location, "must be an object"));
        return std::nullopt;
    }
    bool ok = true;
    PlanRoute item;
    item.key = readString(j, "key", location, issues, ok, true);
    item.group = readString(j, "group", location, issues, ok);
    item.owner = readString(j, "owner", location, issues, ok);
    item.layer = readString(j, "layer", location, issues, ok);
    item.reason = readString(j, "reason", location, issues, ok);
    const std::string level = readString(j, "level", location, issues, ok);
    if (!level.empty()) {
        if (const auto l = reactiveLevelFromName(level)) {
            item.level = *l;
        } else {
            Issue issue = issueAt(Severity::Error, IssueCode::SchemaInvalid, location + "/level",
                                  fmt::format("'{}' is not one of the allowed values", level));
            issue.suggestions = {"micro", "meso", "macro"};
            issue.details = {{"allowed", issue.suggestions}};
            issues.push_back(std::move(issue));
            ok = false;
        }
    }
    const auto route = j.find("route");
    if (route == j.end()) {
        issues.push_back(issueAt(Severity::Error, IssueCode::SchemaInvalid, location + "/route",
                                 "'route' is required: the route as a project writes it (source, target, op, "
                                 "amount, chain, depthSource)"));
        ok = false;
    } else if (!route->is_object()) {
        issues.push_back(issueAt(Severity::Error, IssueCode::SchemaInvalid, location + "/route", "'route' must be an object"));
        ok = false;
    } else {
        const std::string at = location + "/route";
        for (const char* required : {"source", "target"}) {
            if (!route->contains(required)) {
                issues.push_back(issueAt(Severity::Error, IssueCode::SchemaInvalid, at + "/" + required,
                                         fmt::format("'{}' is required", required)));
                ok = false;
            }
        }
        if (route->contains("planItem")) {
            issues.push_back(issueAt(Severity::Warning, IssueCode::SchemaUnknownField, at + "/planItem",
                                     "'planItem' is set by the compiler from the item's key; it is ignored here"));
        }
        std::vector<std::string> known = routeKeys();
        known.emplace_back("planItem");
        reportUnknown(*route, known, at, issues);
        if (const auto chain = route->find("chain"); chain != route->end() && chain->is_object()) {
            reportUnknown(*chain, chainKeys(), at + "/chain", issues);
        }
        auto parsed = params::routeFromJson(*route);
        if (!parsed) {
            issues.push_back(issueAt(Severity::Error, IssueCode::SchemaInvalid, at, parsed.error().message));
            ok = false;
        } else {
            item.route = std::move(*parsed);
            item.route.planItem.clear();
            if (ok && (item.route.source.empty() || item.route.target.empty())) {
                issues.push_back(issueAt(Severity::Error, IssueCode::SchemaInvalid, at,
                                         "a route needs a non-empty 'source' and 'target'"));
                ok = false;
            }
            if (!std::isfinite(item.route.amount) || !std::isfinite(item.route.depthMin) ||
                !std::isfinite(item.route.depthMax)) {
                issues.push_back(issueAt(Severity::Error, IssueCode::SchemaInvalid, at, "amount and depth must be finite"));
                ok = false;
            }
        }
    }
    reportUnknown(j, {"key", "level", "group", "owner", "layer", "reason", "route"}, location, issues);
    return ok ? std::optional<PlanRoute>(std::move(item)) : std::nullopt;
}

std::optional<PlanSource> planSourceFromJson(const json& j, const std::string& location, std::vector<Issue>& issues) {
    if (!j.is_object()) {
        issues.push_back(issueAt(Severity::Error, IssueCode::SchemaInvalid, location, "must be an object"));
        return std::nullopt;
    }
    bool ok = true;
    PlanSource item;
    item.key = readString(j, "key", location, issues, ok, true);
    item.reason = readString(j, "reason", location, issues, ok);
    item.kind = readString(j, "kind", location, issues, ok, true);
    item.name = readString(j, "name", location, issues, ok, true);
    if (item.name.find('/') != std::string::npos || item.name.find('.') != std::string::npos) {
        issues.push_back(issueAt(Severity::Error, IssueCode::SchemaInvalid, location + "/name",
                                 "a source's name may not contain '/' or '.': it is a parameter prefix and a signal name"));
        ok = false;
    }
    if (const auto s = j.find("settings"); s != j.end()) {
        if (!s->is_object()) {
            issues.push_back(issueAt(Severity::Error, IssueCode::SchemaInvalid, location + "/settings",
                                     "'settings' must be an object"));
            ok = false;
        } else {
            item.settings = *s;
        }
    }
    if (const auto p = j.find("parameters"); p != j.end()) {
        if (!p->is_object()) {
            issues.push_back(issueAt(Severity::Error, IssueCode::SchemaInvalid, location + "/parameters",
                                     "'parameters' must be an object of leaf -> number"));
            ok = false;
        } else {
            for (const auto& [leaf, value] : p->items()) {
                if (value.is_boolean()) {
                    item.parameters.emplace_back(leaf, value.get<bool>() ? 1.0f : 0.0f);
                } else if (value.is_number()) {
                    item.parameters.emplace_back(leaf, value.get<float>());
                } else {
                    issues.push_back(issueAt(Severity::Error, IssueCode::SchemaInvalid, location + "/parameters/" + leaf,
                                             fmt::format("'{}' must be a number or true/false", leaf)));
                    ok = false;
                }
            }
        }
    }
    reportUnknown(j, {"key", "reason", "kind", "name", "settings", "parameters"}, location, issues);
    return ok ? std::optional<PlanSource>(std::move(item)) : std::nullopt;
}

json planRouteSchema() {
    return json::array(
        {{{"key", "unique in the plan"},
          {"level", "micro|meso|macro: small fast local things | clusters, heroes, waves | the world"},
          {"group", "the catalogue group of the target (director.inspect_scene's reactive catalogue)"},
          {"owner", "what answers: a hero, a scatter layer, a particle system, or world"},
          {"layer", "the musical layer it answers, in words: kick, clap, hats, bar, section energy, ..."},
          {"reason", "why this answers that, in a sentence a person reads"},
          {"route",
           {{"source", "a signal on the bus (audio.onsetLow = kick, audio.onsetMid = clap, audio.onsetHigh = hats, "
                       "music.downbeat, beat.bar, section.energy, audio.energy, ...) or a plan source's signal"},
            {"target", "a parameter path from the reactive catalogue"},
            {"component", "-1 = all"},
            {"op", "add|multiply|replace|min|max"},
            {"amount", "number"},
            {"polarity", "unipolar|bipolar"},
            {"enabled", "bool"},
            {"chain",
             {{"delayMs", "0-4000: stagger phases with this, never give every route one instant"},
              {"attackMs", "number"},
              {"decayMs", "number"},
              {"remapEnabled", "bool"},
              {"remapInMin", "number"},
              {"remapInMax", "number"},
              {"remapOutMin", "number"},
              {"remapOutMax", "number"},
              {"...", "the rest of the project's chain fields"}}},
            {"depthSource", "a smoothed signal whose value scales the route (section.energy); never audio.rms or "
                            "a music.* event"},
            {"depthMin", "depth at the signal's 0"},
            {"depthMax", "depth at the signal's 1"}}}}});
}

json planSourceSchema() {
    return json::array({{{"key", "unique in the plan"},
                         {"kind", "lfo|noise|timeline (pure functions of time, so a seek lands where a play does)"},
                         {"name", "the signal is <kind>.<name>; name it for what it moves, e.g. mushroom-hue-by-section"},
                         {"settings", "as the project writes the source's settings: a timeline's keys and mode, an "
                                      "LFO's shape"},
                         {"parameters", "values under sources/<name>/: beatSync, beatsPerCycle, phase, rate, ..."},
                         {"reason", "why the plan needs it"}}});
}

} // namespace avgen::directing
