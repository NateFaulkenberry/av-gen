#include "crowd/crowd_desc.hpp"

#include "entity/action.hpp"

#include <algorithm>

namespace avgen::crowd {

using nlohmann::json;

const char* commandKindName(CommandKind kind) {
    switch (kind) {
    case CommandKind::Goto: return "goto";
    case CommandKind::Wander: return "wander";
    case CommandKind::Hold: return "hold";
    }
    return "?";
}

namespace {

Result<glm::vec2> vec2Of(const json& v, const char* what) {
    if (!v.is_array() || v.size() < 2 || !v[0].is_number() || !v[v.size() == 3 ? 2 : 1].is_number()) {
        return fail("crowd: {} must be [x, z] or [x, y, z]", what);
    }
    // [x, z], or [x, y, z] with y ignored: a destination is a place on the ground.
    return v.size() == 3 ? glm::vec2(v[0].get<float>(), v[2].get<float>())
                         : glm::vec2(v[0].get<float>(), v[1].get<float>());
}

json vec2Json(glm::vec2 v) { return json::array({v.x, v.y}); }

Result<std::vector<glm::vec2>> pointsOf(const json& j, const char* what) {
    std::vector<glm::vec2> out;
    if (!j.is_array()) {
        return fail("crowd: {} must be an array of points", what);
    }
    for (const json& p : j) {
        auto v = vec2Of(p, what);
        if (!v) {
            return std::unexpected(v.error());
        }
        out.push_back(*v);
    }
    return out;
}

Result<AreaDesc> areaOf(const json& j) {
    AreaDesc a;
    if (!j.is_object() || !j.contains("center")) {
        return fail("crowd: an area needs a center");
    }
    auto c = vec2Of(j.at("center"), "area.center");
    if (!c) {
        return std::unexpected(c.error());
    }
    a.center = *c;
    if (j.contains("halfExtents")) {
        auto h = vec2Of(j.at("halfExtents"), "area.halfExtents");
        if (!h) {
            return std::unexpected(h.error());
        }
        a.halfExtents = glm::max(*h, glm::vec2(0.0f));
    }
    return a;
}

const char* policyName(SlotPolicy p) { return p == SlotPolicy::Index ? "index" : "spread"; }

} // namespace

Result<CrowdDesc> crowdFromJson(const json& j) {
    CrowdDesc d;
    if (!j.is_object()) {
        return fail("crowd: the block must be an object");
    }
    static const char* const kKeys[] = {"walls", "groups", "schedule", "pathsPerStep", "threads"};
    for (const auto& [key, value] : j.items()) {
        if (std::find_if(std::begin(kKeys), std::end(kKeys), [&](const char* k) { return key == k; }) ==
            std::end(kKeys)) {
            return fail("crowd: unknown key '{}'", key);
        }
    }
    d.pathsPerStep = std::max(1, j.value("pathsPerStep", d.pathsPerStep));
    d.threads = std::clamp(j.value("threads", d.threads), 1, 64);
    for (const json& w : j.value("walls", json::array())) {
        WallDesc wall;
        auto pts = pointsOf(w.value("points", json::array()), "walls[].points");
        if (!pts) {
            return std::unexpected(pts.error());
        }
        if (pts->size() < 2) {
            return fail("crowd: a wall needs at least two points");
        }
        wall.points = std::move(*pts);
        wall.thickness = std::max(0.05f, w.value("thickness", wall.thickness));
        wall.height = std::max(0.1f, w.value("height", wall.height));
        wall.base = w.value("base", wall.base);
        d.walls.push_back(std::move(wall));
    }
    for (const json& g : j.value("groups", json::array())) {
        GroupDesc group;
        group.name = g.value("name", std::string{});
        if (group.name.empty()) {
            return fail("crowd: a group needs a name (the animation.crowd its members carry)");
        }
        group.radius = std::max(0.05f, g.value("radius", group.radius));
        if (g.contains("speed")) {
            const json& s = g.at("speed");
            if (!s.is_array() || s.size() != 2) {
                return fail("crowd: group '{}': speed must be [min, max]", group.name);
            }
            group.speedMin = std::max(0.05f, s[0].get<float>());
            group.speedMax = std::max(group.speedMin, s[1].get<float>());
        }
        group.speedBuckets = std::clamp(g.value("speedBuckets", group.speedBuckets), 1, 16);
        group.maxSpeed = std::max(group.speedMax, g.value("maxSpeed", group.maxSpeed));
        group.neighbourDist = std::max(0.5f, g.value("neighbourDist", group.neighbourDist));
        group.maxNeighbours = std::clamp(g.value("maxNeighbours", group.maxNeighbours), 1, 32);
        group.timeHorizon = std::max(0.25f, g.value("timeHorizon", group.timeHorizon));
        group.turnRate = std::max(0.5f, g.value("turnRate", group.turnRate));
        group.seed = g.value("seed", group.seed);
        if (g.contains("gait")) {
            auto gait = entity::gaitFromJson(g.at("gait"));
            if (!gait) {
                return fail("crowd: group '{}': gait: {}", group.name, gait.error().message);
            }
            group.gait = *gait;
        }
        if (g.contains("clips")) {
            const json& c = g.at("clips");
            group.idleClip = c.value("idle", group.idleClip);
            group.walkClip = c.value("walk", group.walkClip);
            group.runClip = c.value("run", group.runClip);
        }
        if (std::any_of(d.groups.begin(), d.groups.end(), [&](const GroupDesc& o) { return o.name == group.name; })) {
            return fail("crowd: group '{}' is declared twice", group.name);
        }
        d.groups.push_back(std::move(group));
    }
    for (const json& s : j.value("schedule", json::array())) {
        ScheduleEntry e;
        e.at = s.value("at", 0.0);
        e.group = s.value("group", std::string{});
        if (std::none_of(d.groups.begin(), d.groups.end(), [&](const GroupDesc& g) { return g.name == e.group; })) {
            return fail("crowd: schedule entry at {} s names unknown group '{}'", e.at, e.group);
        }
        const std::string cmd = s.value("command", std::string("goto"));
        if (cmd == "goto") {
            e.command = CommandKind::Goto;
        } else if (cmd == "wander") {
            e.command = CommandKind::Wander;
        } else if (cmd == "hold") {
            e.command = CommandKind::Hold;
        } else {
            return fail("crowd: unknown command '{}' (goto, wander, hold)", cmd);
        }
        if (s.contains("points")) {
            auto pts = pointsOf(s.at("points"), "schedule[].points");
            if (!pts) {
                return std::unexpected(pts.error());
            }
            e.points = std::move(*pts);
        }
        if (s.contains("area")) {
            auto a = areaOf(s.at("area"));
            if (!a) {
                return std::unexpected(a.error());
            }
            e.area = *a;
        }
        if (e.command != CommandKind::Hold && e.points.empty() && !e.area) {
            return fail("crowd: '{}' at {} s needs points or an area", cmd, e.at);
        }
        const std::string policy = s.value("policy", std::string("spread"));
        if (policy != "spread" && policy != "index") {
            return fail("crowd: unknown slot policy '{}' (spread, index)", policy);
        }
        e.policy = policy == "index" ? SlotPolicy::Index : SlotPolicy::Spread;
        e.arriveRadius = std::max(0.05f, s.value("arriveRadius", e.arriveRadius));
        if (s.contains("face")) {
            auto f = vec2Of(s.at("face"), "schedule[].face");
            if (!f) {
                return std::unexpected(f.error());
            }
            e.face = *f;
        }
        e.stagger = std::max(0.0f, s.value("stagger", e.stagger));
        if (s.contains("dwell")) {
            const json& dw = s.at("dwell");
            if (!dw.is_array() || dw.size() != 2) {
                return fail("crowd: dwell must be [min, max] seconds");
            }
            e.dwellMin = std::max(0.0f, dw[0].get<float>());
            e.dwellMax = std::max(e.dwellMin, dw[1].get<float>());
        }
        d.schedule.push_back(std::move(e));
    }
    // A stable order by time; entries at the same second keep their file order.
    std::stable_sort(d.schedule.begin(), d.schedule.end(),
                     [](const ScheduleEntry& a, const ScheduleEntry& b) { return a.at < b.at; });
    return d;
}

json crowdToJson(const CrowdDesc& d) {
    json j = json::object();
    const CrowdDesc defaults;
    if (d.pathsPerStep != defaults.pathsPerStep) {
        j["pathsPerStep"] = d.pathsPerStep;
    }
    if (d.threads != defaults.threads) {
        j["threads"] = d.threads;
    }
    if (!d.walls.empty()) {
        json walls = json::array();
        for (const WallDesc& w : d.walls) {
            json pts = json::array();
            for (const glm::vec2& p : w.points) {
                pts.push_back(vec2Json(p));
            }
            walls.push_back({{"points", pts}, {"thickness", w.thickness}, {"height", w.height}, {"base", w.base}});
        }
        j["walls"] = walls;
    }
    json groups = json::array();
    for (const GroupDesc& g : d.groups) {
        json o = {{"name", g.name},
                  {"radius", g.radius},
                  {"speed", json::array({g.speedMin, g.speedMax})},
                  {"speedBuckets", g.speedBuckets},
                  {"maxSpeed", g.maxSpeed},
                  {"neighbourDist", g.neighbourDist},
                  {"maxNeighbours", g.maxNeighbours},
                  {"timeHorizon", g.timeHorizon},
                  {"turnRate", g.turnRate},
                  {"seed", g.seed},
                  {"gait", entity::gaitToJson(g.gait)},
                  {"clips", {{"idle", g.idleClip}, {"walk", g.walkClip}, {"run", g.runClip}}}};
        groups.push_back(std::move(o));
    }
    if (!groups.empty()) {
        j["groups"] = groups;
    }
    json schedule = json::array();
    for (const ScheduleEntry& e : d.schedule) {
        json o = {{"at", e.at}, {"group", e.group}, {"command", commandKindName(e.command)}};
        if (!e.points.empty()) {
            json pts = json::array();
            for (const glm::vec2& p : e.points) {
                pts.push_back(vec2Json(p));
            }
            o["points"] = pts;
        }
        if (e.area) {
            o["area"] = {{"center", vec2Json(e.area->center)}, {"halfExtents", vec2Json(e.area->halfExtents)}};
        }
        if (e.command != CommandKind::Hold) {
            o["policy"] = policyName(e.policy);
            o["arriveRadius"] = e.arriveRadius;
        }
        if (e.face) {
            o["face"] = vec2Json(*e.face);
        }
        if (e.stagger > 0.0f) {
            o["stagger"] = e.stagger;
        }
        if (e.command == CommandKind::Wander) {
            o["dwell"] = json::array({e.dwellMin, e.dwellMax});
        }
        schedule.push_back(std::move(o));
    }
    if (!schedule.empty()) {
        j["schedule"] = schedule;
    }
    return j;
}

} // namespace avgen::crowd
