#pragma once

// ADR-924: the reactivity items of a Director Plan -- a route, and the source a route may need.
//
// A `PlanRoute` is a route the plan asks for: a signal, through the route chain, onto a parameter.
// It compiles to an ordinary `params::ModRoute` in the project's route list -- the same route the
// Modulation panel's Routes tab lists and edits, saved in the project's `routes` -- stamped with the
// item that made it (`ModRoute::planItem`, "<planId>/<key>"), so the panel can say why it is there and
// the plan's next revision can find it. Nothing is a second copy: the item is the request, the route
// is the content, and `Plan::produced` records which route the item made.
//
// A `PlanSource` is a modulation source the plan makes because a route needs a signal the bus does
// not carry: a timeline keyed at the section boundaries (the mushrooms' colour, which follows the
// song's sections and never its audio), or a beat-synced LFO (a slow breath across two bars). It
// compiles to a source in the project's rack, listed in the Modulation panel's Sources tab. Only
// sources that are pure functions of time may be made -- LFO, noise, timeline -- so every compiled
// route is replayed by a seek (ADR-901) and lands where a play does.
//
// **The route is written as the project writes it.** `route` is exactly an entry of a project's
// `routes` array (`params::routeToJson`): source, target, component, amount, op, polarity, enabled,
// the chain (its `delayMs` first, ADR-900), and `depthSource`/`depthMin`/`depthMax` (ADR-900). A
// source is exactly an entry of `sources` (kind, name, settings) plus the values of its parameters
// under `sources/<name>/`. So a generator that reads a plan can install it by copying, and a person
// reading one reads the same words the project file uses.
//
// `level`, `group`, `owner`, `layer` and `reason` are what a person (or the Director panel's plan
// view) reads beside it: which level of the hierarchy it is (micro, meso, macro -- the owner's brief
// section 5), what kind of target it moves, what answers, which musical layer it answers, and why.

#include "directing/issue.hpp"
#include "params/modulation.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace avgen::directing {

// The owner's three levels (brief section 5): individual small things, clusters and heroes, the
// world.
enum class ReactiveLevel : std::uint8_t { Micro, Meso, Macro };
[[nodiscard]] const char* reactiveLevelName(ReactiveLevel level);
[[nodiscard]] std::optional<ReactiveLevel> reactiveLevelFromName(std::string_view name);

struct PlanRoute {
    std::string key;                          // unique in the plan; the route's `planItem` is "<planId>/<key>"
    ReactiveLevel level = ReactiveLevel::Meso;
    std::string group;                        // the catalogue group of the target: "hero-emission", "scatter-hue", ...
    std::string owner;                        // what answers: a hero, "<terrain>/<layer>", a particle system, "world"
    std::string layer;                        // the musical layer it answers, in words: "kick", "section energy"
    std::string reason;                       // one or two sentences: why this target answers this layer this way
    params::ModRoute route;                   // exactly a project route (runtime fields unused, `planItem` empty)
    friend bool operator==(const PlanRoute& a, const PlanRoute& b);
};

struct PlanSource {
    std::string key;                          // unique in the plan (shared with the plan's other items)
    std::string reason;
    std::string kind;                         // "lfo" | "noise" | "timeline": pure in time (ADR-901)
    std::string name;                         // the signal it publishes is "<kind>.<name>"
    nlohmann::json settings = nlohmann::json::object(); // as the rack writes a source's "settings"
    // Values of the source's own parameters, by leaf under "sources/<name>/" ("beatSync", ...), in the
    // order written. A bool parameter takes 0 or 1.
    std::vector<std::pair<std::string, float>> parameters;
    // The signal it publishes: "<kind>.<name>" for the three kinds a plan may make.
    [[nodiscard]] std::string signal() const { return kind + "." + name; }
    friend bool operator==(const PlanSource&, const PlanSource&) = default;
};

// Canonical JSON: the same item always writes the same bytes (the route with every chain field, as
// the project writes routes; empty strings omitted).
[[nodiscard]] nlohmann::json planRouteToJson(const PlanRoute& item);
[[nodiscard]] nlohmann::json planSourceToJson(const PlanSource& item);

// Readers for `parsePlan`: shape only -- types, required fields, known vocabulary -- with every
// problem an Issue at `location` ("/routes/3"), unknown fields as SCHEMA_UNKNOWN_FIELD warnings (so a
// model that writes "delay" instead of "delayMs" is told, instead of the field doing nothing).
// Whether the route can reach the picture is the validator's question (ADR-926).
[[nodiscard]] std::optional<PlanRoute> planRouteFromJson(const nlohmann::json& j, const std::string& location,
                                                         std::vector<Issue>& issues);
[[nodiscard]] std::optional<PlanSource> planSourceFromJson(const nlohmann::json& j, const std::string& location,
                                                           std::vector<Issue>& issues);

// The contract's entries for `planSchema()`.
[[nodiscard]] nlohmann::json planRouteSchema();
[[nodiscard]] nlohmann::json planSourceSchema();

// The item that made a route, as `ModRoute::planItem` writes it, and back.
[[nodiscard]] std::string planItemId(std::string_view planId, std::string_view key);
// {planId, key}, or nothing when `id` is not "<planId>/<key>".
[[nodiscard]] std::optional<std::pair<std::string, std::string>> splitPlanItemId(std::string_view id);

// Whether two routes are the same route as a project would save them (every serialised field).
[[nodiscard]] bool sameRoute(const params::ModRoute& a, const params::ModRoute& b);

} // namespace avgen::directing
