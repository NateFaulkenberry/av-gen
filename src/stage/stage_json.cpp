// The director, as scene data (ADR-210).
//
// Everything in `staging.hpp` is authorable, because the architectural claim the POC has to support
// is that a *different* dynamic sequence needs no new C++. A scenario that had to be constructed in
// code would make that claim untestable: the UFO abduction lives in
// `examples/world/glowmere-valley-2.scene.json` under `"staging"`, and the only thing about it that
// is C++ is the eleven step kinds every scenario shares.
//
// Shape:
//
//   "staging": {
//     "actors": [ { "name": "saucer", "body": "visitor",
//                   "parts": [ { "name": "beam", "entity": "visitor-beam" } ] } ],
//     "scenarios": [ {
//       "name": "abduction", "actor": "saucer", "seed": 20260915,
//       "autoStart": true, "maxCycles": 6, "searchInterval": 0.5,
//       "params": [ { "name": "searchRadius", "value": 220, "min": 10, "max": 600 } ],
//       "beats": [ {
//         "name": "acquire",
//         "find": [ { "bind": "target", "tag": "animal", "from": "actor",
//                     "radius": { "param": "searchRadius" }, "pick": "nearest" } ],
//         "cues": [ { "role": "actor", "steps": [ ... ] } ],
//         "then": "approach", "otherwise": ""
//       } ]
//     } ]
//   }
//
// A number is either a literal or `{ "param": "<name>" }` with an optional `"value"` fallback --
// one spelling, everywhere a number appears, which is what makes "expose these as director
// parameters rather than hardcoding them" a property of the format rather than of the author's
// diligence.

#include "stage/staging.hpp"

#include <algorithm>

namespace avgen::stage {

namespace {

[[nodiscard]] Result<Value> valueFromJson(const nlohmann::json& j, const char* where) {
    Value v;
    if (j.is_number()) {
        v.literal = j.get<float>();
        return v;
    }
    if (j.is_object()) {
        if (j.contains("param")) {
            if (!j.at("param").is_string()) {
                return fail("{}: `param` must be a string", where);
            }
            v.param = j.at("param").get<std::string>();
        }
        if (j.contains("value")) {
            if (!j.at("value").is_number()) {
                return fail("{}: `value` must be a number", where);
            }
            v.literal = j.at("value").get<float>();
        }
        if (v.param.empty() && !j.contains("value")) {
            return fail("{}: a value object names neither `param` nor `value`", where);
        }
        return v;
    }
    return fail("{}: expected a number or {{ \"param\": ... }}", where);
}

[[nodiscard]] nlohmann::json valueToJson(const Value& v) {
    if (!v.bound()) {
        return v.literal;
    }
    nlohmann::json j = nlohmann::json::object();
    j["param"] = v.param;
    if (v.literal != 0.0f) {
        j["value"] = v.literal;
    }
    return j;
}

[[nodiscard]] Result<void> readValue(const nlohmann::json& j, const char* key, Value& out,
                                     const char* where) {
    if (!j.contains(key)) {
        return {};
    }
    auto v = valueFromJson(j.at(key), where);
    if (!v) {
        return std::unexpected(v.error());
    }
    out = std::move(*v);
    return {};
}

void writeValue(nlohmann::json& j, const char* key, const Value& v, float omitWhen) {
    if (!v.bound() && v.literal == omitWhen) {
        return;
    }
    j[key] = valueToJson(v);
}

[[nodiscard]] Result<glm::vec3> vec3FromJson(const nlohmann::json& j, const char* where) {
    if (!j.is_array() || j.size() != 3) {
        return fail("{}: expected three numbers", where);
    }
    return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
}

[[nodiscard]] nlohmann::json vec3ToJson(const glm::vec3& v) {
    return nlohmann::json::array({v.x, v.y, v.z});
}

[[nodiscard]] std::string readString(const nlohmann::json& j, const char* key) {
    return j.contains(key) && j.at(key).is_string() ? j.at(key).get<std::string>() : std::string{};
}

[[nodiscard]] bool readBool(const nlohmann::json& j, const char* key, bool fallback) {
    return j.contains(key) && j.at(key).is_boolean() ? j.at(key).get<bool>() : fallback;
}

} // namespace

// ---- queries ------------------------------------------------------------------------------------

namespace {

[[nodiscard]] Result<QueryDesc> queryFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("a find must be an object");
    }
    QueryDesc q;
    q.bind = readString(j, "bind");
    if (q.bind.empty()) {
        return fail("a find must name the role it binds (`bind`)");
    }
    q.name = readString(j, "name");
    q.tag = readString(j, "tag");
    q.from = readString(j, "from");
    if (j.contains("center")) {
        auto c = vec3FromJson(j.at("center"), "a find's center");
        if (!c) return std::unexpected(c.error());
        q.center = *c;
    }
    if (auto r = readValue(j, "radius", q.radius, "a find's radius"); !r) return std::unexpected(r.error());
    if (auto r = readValue(j, "minRadius", q.minRadius, "a find's minRadius"); !r)
        return std::unexpected(r.error());
    if (auto r = readValue(j, "clearance", q.clearance, "a find's clearance"); !r)
        return std::unexpected(r.error());
    if (j.contains("pick")) {
        const std::string name = readString(j, "pick");
        const auto pick = pickFromName(name);
        if (!pick) {
            return fail("a find's `pick` is '{}', which is not nearest/farthest/random/first", name);
        }
        q.pick = *pick;
    }
    q.excludeClaimed = readBool(j, "excludeClaimed", true);
    q.excludeRetired = readBool(j, "excludeRetired", true);
    q.claim = readBool(j, "claim", true);
    q.requireNavigable = readBool(j, "requireNavigable", false);
    return q;
}

[[nodiscard]] nlohmann::json queryToJson(const QueryDesc& q) {
    nlohmann::json j = nlohmann::json::object();
    j["bind"] = q.bind;
    if (!q.name.empty()) j["name"] = q.name;
    if (!q.tag.empty()) j["tag"] = q.tag;
    if (!q.from.empty()) j["from"] = q.from;
    if (q.center != glm::vec3(0.0f)) j["center"] = vec3ToJson(q.center);
    writeValue(j, "radius", q.radius, 0.0f);
    writeValue(j, "minRadius", q.minRadius, 0.0f);
    writeValue(j, "clearance", q.clearance, 0.0f);
    if (q.pick != Pick::Nearest) j["pick"] = pickName(q.pick);
    if (!q.excludeClaimed) j["excludeClaimed"] = false;
    if (!q.excludeRetired) j["excludeRetired"] = false;
    if (!q.claim) j["claim"] = false;
    if (q.requireNavigable) j["requireNavigable"] = true;
    return j;
}

// ---- steps --------------------------------------------------------------------------------------

[[nodiscard]] Result<StepDesc> stepFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("a step must be an object");
    }
    const std::string kindName = readString(j, "kind");
    const auto kind = stepKindFromName(kindName);
    if (!kind) {
        return fail("'{}' is not a step kind", kindName);
    }
    StepDesc s;
    s.kind = *kind;
    // `moveBy` is `moveTo` with the offset measured from where the body already is; the alias is
    // the brief's own vocabulary and it costs one line rather than a second step kind.
    if (kindName == "moveBy") {
        s.relative = true;
    }
    s.name = readString(j, "name");
    s.role = readString(j, "role");
    s.toRole = readString(j, "to");
    s.target = readString(j, "target");
    s.activity = readString(j, "activity");
    s.socket = readString(j, "socket");
    if (j.contains("point")) {
        auto p = vec3FromJson(j.at("point"), "a step's point");
        if (!p) return std::unexpected(p.error());
        s.point = *p;
    }
    s.aboveGround = readBool(j, "aboveGround", false);
    s.relative = readBool(j, "relative", s.relative);
    s.hold = readBool(j, "hold", false);
    if (j.contains("anchor")) {
        const std::string name = readString(j, "anchor");
        const auto anchor = anchorFromName(name);
        if (!anchor) {
            return fail("a step's `anchor` is '{}', which is not travel/visual/drawn", name);
        }
        s.anchor = *anchor;
    }
    if (j.contains("place")) {
        const std::string name = readString(j, "place");
        const auto place = anchorFromName(name);
        if (!place) {
            return fail("a step's `place` is '{}', which is not travel/visual/drawn", name);
        }
        s.place = *place;
    }
    if (j.contains("travel")) {
        const std::string name = readString(j, "travel");
        const auto travel = travelFromName(name);
        if (!travel) {
            return fail("a step's `travel` is '{}', which is not fly/walk", name);
        }
        s.travel = *travel;
    }
    const char* where = stepKindName(s.kind);
    if (auto r = readValue(j, "duration", s.duration, where); !r) return std::unexpected(r.error());
    if (auto r = readValue(j, "height", s.height, where); !r) return std::unexpected(r.error());
    if (auto r = readValue(j, "speed", s.speed, where); !r) return std::unexpected(r.error());
    if (auto r = readValue(j, "tolerance", s.tolerance, where); !r) return std::unexpected(r.error());
    if (auto r = readValue(j, "clearance", s.clearance, where); !r) return std::unexpected(r.error());
    if (auto r = readValue(j, "spin", s.spin, where); !r) return std::unexpected(r.error());
    if (auto r = readValue(j, "wobble", s.wobble, where); !r) return std::unexpected(r.error());
    if (auto r = readValue(j, "wobbleRate", s.wobbleRate, where); !r) return std::unexpected(r.error());
    // `to` is overloaded on purpose: a string is the role a destination is measured from, and a
    // number (or a parameter reference) is the value a `set` arrives at. One key, because to an
    // author both of them are "what this step is aimed at", and two would be a distinction the
    // format made and the reader had to remember.
    if (j.contains("to") && j.at("to").is_string()) {
        s.toRole = j.at("to").get<std::string>();
    } else if (auto r = readValue(j, "to", s.to, where); !r) {
        return std::unexpected(r.error());
    }
    if (auto r = readValue(j, "rate", s.rate, where); !r) return std::unexpected(r.error());
    if (j.contains("from")) {
        if (auto r = readValue(j, "from", s.from, where); !r) return std::unexpected(r.error());
        s.hasFrom = true;
    }
    if (j.contains("actions")) {
        auto actions = entity::actionsFromJson(j.at("actions"));
        if (!actions) return std::unexpected(actions.error());
        s.actions = std::move(*actions);
    }
    return s;
}

[[nodiscard]] nlohmann::json stepToJson(const StepDesc& s) {
    nlohmann::json j = nlohmann::json::object();
    j["kind"] = stepKindName(s.kind);
    if (!s.name.empty()) j["name"] = s.name;
    if (!s.role.empty()) j["role"] = s.role;
    if (!s.toRole.empty()) j["to"] = s.toRole;
    if (!s.target.empty()) j["target"] = s.target;
    if (!s.activity.empty()) j["activity"] = s.activity;
    if (!s.socket.empty()) j["socket"] = s.socket;
    if (s.point != glm::vec3(0.0f)) j["point"] = vec3ToJson(s.point);
    if (s.aboveGround) j["aboveGround"] = true;
    if (s.relative) j["relative"] = true;
    if (s.hold) j["hold"] = true;
    if (s.anchor != Anchor::Travel) j["anchor"] = anchorName(s.anchor);
    if (s.place != Anchor::Travel) j["place"] = anchorName(s.place);
    if (s.travel != Travel::Fly) j["travel"] = travelName(s.travel);
    writeValue(j, "duration", s.duration, 0.0f);
    writeValue(j, "height", s.height, 0.0f);
    writeValue(j, "speed", s.speed, 0.0f);
    writeValue(j, "tolerance", s.tolerance, 0.0f);
    writeValue(j, "clearance", s.clearance, 0.0f);
    writeValue(j, "spin", s.spin, 0.0f);
    writeValue(j, "wobble", s.wobble, 0.0f);
    writeValue(j, "wobbleRate", s.wobbleRate, 0.0f);
    writeValue(j, "rate", s.rate, 0.0f);
    if (s.toRole.empty()) {
        writeValue(j, "to", s.to, 0.0f);
    }
    if (s.hasFrom) j["from"] = valueToJson(s.from);
    if (!s.actions.empty()) j["actions"] = entity::actionsToJson(s.actions);
    return j;
}

[[nodiscard]] Result<CueDesc> cueFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("a cue must be an object");
    }
    CueDesc c;
    c.role = readString(j, "role");
    if (j.contains("steps")) {
        if (!j.at("steps").is_array()) {
            return fail("a cue's `steps` must be an array");
        }
        for (const nlohmann::json& item : j.at("steps")) {
            auto step = stepFromJson(item);
            if (!step) return std::unexpected(step.error());
            c.steps.push_back(std::move(*step));
        }
    }
    return c;
}

[[nodiscard]] nlohmann::json cueToJson(const CueDesc& c) {
    nlohmann::json j = nlohmann::json::object();
    if (!c.role.empty()) j["role"] = c.role;
    nlohmann::json steps = nlohmann::json::array();
    for (const StepDesc& s : c.steps) {
        steps.push_back(stepToJson(s));
    }
    j["steps"] = std::move(steps);
    return j;
}

[[nodiscard]] Result<BeatDesc> beatFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("a beat must be an object");
    }
    BeatDesc b;
    b.name = readString(j, "name");
    b.then = readString(j, "then");
    b.otherwise = readString(j, "otherwise");
    b.release = readBool(j, "release", false);
    if (j.contains("find")) {
        if (!j.at("find").is_array()) {
            return fail("beat '{}': `find` must be an array", b.name);
        }
        for (const nlohmann::json& item : j.at("find")) {
            auto q = queryFromJson(item);
            if (!q) return std::unexpected(q.error());
            b.find.push_back(std::move(*q));
        }
    }
    if (j.contains("cues")) {
        if (!j.at("cues").is_array()) {
            return fail("beat '{}': `cues` must be an array", b.name);
        }
        for (const nlohmann::json& item : j.at("cues")) {
            auto c = cueFromJson(item);
            if (!c) return std::unexpected(c.error());
            b.cues.push_back(std::move(*c));
        }
    }
    return b;
}

[[nodiscard]] nlohmann::json beatToJson(const BeatDesc& b) {
    nlohmann::json j = nlohmann::json::object();
    if (!b.name.empty()) j["name"] = b.name;
    if (!b.find.empty()) {
        nlohmann::json find = nlohmann::json::array();
        for (const QueryDesc& q : b.find) {
            find.push_back(queryToJson(q));
        }
        j["find"] = std::move(find);
    }
    nlohmann::json cues = nlohmann::json::array();
    for (const CueDesc& c : b.cues) {
        cues.push_back(cueToJson(c));
    }
    j["cues"] = std::move(cues);
    if (!b.then.empty()) j["then"] = b.then;
    if (!b.otherwise.empty()) j["otherwise"] = b.otherwise;
    if (b.release) j["release"] = true;
    return j;
}

} // namespace

// ---- actors and scenarios -------------------------------------------------------------------------

Result<ActorDesc> actorFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("an actor must be an object");
    }
    ActorDesc a;
    a.name = readString(j, "name");
    if (a.name.empty()) {
        return fail("an actor must have a name");
    }
    a.body = readString(j, "body");
    if (j.contains("parts")) {
        if (!j.at("parts").is_array()) {
            return fail("actor '{}': `parts` must be an array", a.name);
        }
        for (const nlohmann::json& item : j.at("parts")) {
            ActorPart p;
            p.name = readString(item, "name");
            p.entity = readString(item, "entity");
            if (p.name.empty() || p.entity.empty()) {
                return fail("actor '{}': a part needs both a `name` and an `entity`", a.name);
            }
            a.parts.push_back(std::move(p));
        }
    }
    return a;
}

nlohmann::json actorToJson(const ActorDesc& a) {
    nlohmann::json j = nlohmann::json::object();
    j["name"] = a.name;
    if (!a.body.empty()) j["body"] = a.body;
    if (!a.parts.empty()) {
        nlohmann::json parts = nlohmann::json::array();
        for (const ActorPart& p : a.parts) {
            parts.push_back(nlohmann::json{{"name", p.name}, {"entity", p.entity}});
        }
        j["parts"] = std::move(parts);
    }
    return j;
}

Result<ScenarioDesc> scenarioFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("a scenario must be an object");
    }
    ScenarioDesc s;
    s.name = readString(j, "name");
    if (s.name.empty()) {
        return fail("a scenario must have a name");
    }
    s.actor = readString(j, "actor");
    if (j.contains("seed") && j.at("seed").is_number()) {
        s.seed = j.at("seed").get<std::uint32_t>();
    }
    s.autoStart = readBool(j, "autoStart", false);
    s.startOn = readString(j, "startOn");
    s.stopOn = readString(j, "stopOn");
    if (j.contains("maxCycles") && j.at("maxCycles").is_number()) {
        s.maxCycles = j.at("maxCycles").get<int>();
    }
    if (j.contains("searchInterval") && j.at("searchInterval").is_number()) {
        s.searchInterval = j.at("searchInterval").get<double>();
    }
    if (j.contains("params")) {
        if (!j.at("params").is_array()) {
            return fail("scenario '{}': `params` must be an array", s.name);
        }
        for (const nlohmann::json& item : j.at("params")) {
            ScenarioParam p;
            p.name = readString(item, "name");
            if (p.name.empty()) {
                return fail("scenario '{}': a parameter has no name", s.name);
            }
            if (item.contains("value")) p.value = item.at("value").get<float>();
            if (item.contains("min")) p.min = item.at("min").get<float>();
            if (item.contains("max")) p.max = item.at("max").get<float>();
            if (p.min == 0.0f && p.max == 0.0f) {
                // A parameter with no declared range gets one wide enough not to clamp what the
                // author wrote. A hard range of [0, 0] would silently zero every value.
                p.min = std::min(0.0f, p.value * 4.0f);
                p.max = std::max(1.0f, p.value * 4.0f);
            }
            s.params.push_back(std::move(p));
        }
    }
    if (!j.contains("beats") || !j.at("beats").is_array()) {
        return fail("scenario '{}': `beats` must be an array", s.name);
    }
    for (const nlohmann::json& item : j.at("beats")) {
        auto b = beatFromJson(item);
        if (!b) return std::unexpected(b.error());
        s.beats.push_back(std::move(*b));
    }
    return s;
}

nlohmann::json scenarioToJson(const ScenarioDesc& s) {
    nlohmann::json j = nlohmann::json::object();
    j["name"] = s.name;
    if (!s.actor.empty()) j["actor"] = s.actor;
    if (s.seed != 0) j["seed"] = s.seed;
    if (s.autoStart) j["autoStart"] = true;
    if (!s.startOn.empty()) j["startOn"] = s.startOn;
    if (!s.stopOn.empty()) j["stopOn"] = s.stopOn;
    if (s.maxCycles != 0) j["maxCycles"] = s.maxCycles;
    if (s.searchInterval != 0.5) j["searchInterval"] = s.searchInterval;
    if (!s.params.empty()) {
        nlohmann::json params = nlohmann::json::array();
        for (const ScenarioParam& p : s.params) {
            params.push_back(nlohmann::json{
                {"name", p.name}, {"value", p.value}, {"min", p.min}, {"max", p.max}});
        }
        j["params"] = std::move(params);
    }
    nlohmann::json beats = nlohmann::json::array();
    for (const BeatDesc& b : s.beats) {
        beats.push_back(beatToJson(b));
    }
    j["beats"] = std::move(beats);
    return j;
}

Result<StagingDesc> stagingFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("`staging` must be an object");
    }
    StagingDesc d;
    if (j.contains("actors")) {
        if (!j.at("actors").is_array()) {
            return fail("`staging.actors` must be an array");
        }
        for (const nlohmann::json& item : j.at("actors")) {
            auto a = actorFromJson(item);
            if (!a) return std::unexpected(a.error());
            d.actors.push_back(std::move(*a));
        }
    }
    if (j.contains("scenarios")) {
        if (!j.at("scenarios").is_array()) {
            return fail("`staging.scenarios` must be an array");
        }
        for (const nlohmann::json& item : j.at("scenarios")) {
            auto s = scenarioFromJson(item);
            if (!s) return std::unexpected(s.error());
            d.scenarios.push_back(std::move(*s));
        }
    }
    return d;
}

nlohmann::json stagingToJson(const StagingDesc& d) {
    nlohmann::json j = nlohmann::json::object();
    if (!d.actors.empty()) {
        nlohmann::json actors = nlohmann::json::array();
        for (const ActorDesc& a : d.actors) {
            actors.push_back(actorToJson(a));
        }
        j["actors"] = std::move(actors);
    }
    nlohmann::json scenarios = nlohmann::json::array();
    for (const ScenarioDesc& s : d.scenarios) {
        scenarios.push_back(scenarioToJson(s));
    }
    j["scenarios"] = std::move(scenarios);
    return j;
}

} // namespace avgen::stage
