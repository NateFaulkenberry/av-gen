#include "app/film_validate.hpp"

#include "app/engine.hpp"
#include "app/transform_watch.hpp"
#include "core/log.hpp"
#include "core/time.hpp"
#include "params/parameter.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <vector>

#pragma clang diagnostic ignored "-Wdouble-promotion"

namespace avgen::app {

namespace {

using nlohmann::json;

constexpr float kInf = std::numeric_limits<float>::infinity();

double r3(double v) {
    return std::round(v * 1000.0) / 1000.0;
}

json vecJson(const glm::vec3& v) {
    return json::array({r3(v.x), r3(v.y), r3(v.z)});
}

std::string fmtTime(double t) {
    const int m = static_cast<int>(t / 60.0);
    return fmt::format("{}:{:05.2f}", m, t - m * 60.0);
}

// ---- the live SDF -------------------------------------------------------------------------------------

struct LiveObject {
    const scene::SdfObject* so = nullptr;
    glm::mat4 inv{1.0f};
    float scale = 1.0f;
    glm::vec3 lo{0.0f}, hi{0.0f}; // world box of the march bounds
};

float boxDistance(const glm::vec3& p, const glm::vec3& lo, const glm::vec3& hi) {
    const glm::vec3 c = (lo + hi) * 0.5f;
    const glm::vec3 h = (hi - lo) * 0.5f;
    const glm::vec3 q = glm::abs(p - c) - h;
    return glm::length(glm::max(q, glm::vec3(0.0f))) + std::min(std::max({q.x, q.y, q.z}), 0.0f);
}

std::vector<LiveObject> liveObjects(const scene::Scene& sc) {
    std::vector<LiveObject> out;
    for (const scene::SdfObject& so : sc.sdfs) {
        if (!so.visible) continue;
        LiveObject o;
        o.so = &so;
        const glm::mat4 m = so.transform.matrix();
        o.inv = glm::inverse(m);
        o.scale = std::abs(so.transform.scale.x);
        o.lo = glm::vec3(kInf);
        o.hi = glm::vec3(-kInf);
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 c{(i & 1) ? so.boundsMax.x : so.boundsMin.x, (i & 2) ? so.boundsMax.y : so.boundsMin.y,
                              (i & 4) ? so.boundsMax.z : so.boundsMin.z};
            const glm::vec3 w = glm::vec3(m * glm::vec4(c, 1.0f));
            o.lo = glm::min(o.lo, w);
            o.hi = glm::max(o.hi, w);
        }
        out.push_back(o);
    }
    return out;
}

// The drawn distance: the tree's, clipped to the march bounds (nothing outside them is drawn).
float objectDistance(const LiveObject& o, const glm::vec3& p, double time, const spatial::FieldSet* fields) {
    const glm::vec3 local = glm::vec3(o.inv * glm::vec4(p, 1.0f));
    const float d = o.so->tree.evaluate(local, time, fields) * o.scale;
    const float b = boxDistance(local, o.so->boundsMin, o.so->boundsMax) * o.scale;
    return std::max(d, b);
}

struct Nearest {
    float d = kInf;
    int object = -1;
};

Nearest sceneDistance(const std::vector<LiveObject>& objs, const glm::vec3& p, float cull, double time,
                      const spatial::FieldSet* fields) {
    Nearest n;
    for (std::size_t i = 0; i < objs.size(); ++i) {
        if (boxDistance(p, objs[i].lo, objs[i].hi) > std::min(cull, n.d)) continue;
        const float d = objectDistance(objs[i], p, time, fields);
        if (d < n.d) {
            n.d = d;
            n.object = static_cast<int>(i);
        }
    }
    return n;
}

// ---- what a point belongs to, from the static report's entity boxes -----------------------------------

struct EntityBox {
    std::string id, category, object;
    glm::vec3 lo{0.0f}, hi{0.0f};
    bool room = false;
    glm::vec3 inLo{0.0f}, inHi{0.0f};
    double opensAt = -1.0;
};

glm::vec3 v3(const json& j) {
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

std::vector<EntityBox> entityBoxes(const json& report) {
    std::vector<EntityBox> out;
    if (!report.contains("entities")) return out;
    for (const auto& e : report["entities"]) {
        if (!e.contains("min") || !e.contains("max")) continue;
        EntityBox b;
        b.id = e.value("id", std::string());
        b.category = e.value("category", std::string());
        b.object = e.value("node", std::string());
        b.lo = v3(e["min"]);
        b.hi = v3(e["max"]);
        if (e.contains("interior")) {
            b.room = true;
            b.inLo = v3(e["interior"][0]);
            b.inHi = v3(e["interior"][1]);
        }
        b.opensAt = e.value("opensAt", -1.0);
        out.push_back(b);
    }
    return out;
}

std::string nameAt(const std::vector<EntityBox>& ents, const std::string& object, const glm::vec3& p) {
    const EntityBox* best = nullptr;
    float bestVolume = kInf;
    for (const EntityBox& e : ents) {
        if (e.object != object) continue;
        if (p.x < e.lo.x - 0.03f || p.y < e.lo.y - 0.03f || p.z < e.lo.z - 0.03f || p.x > e.hi.x + 0.03f ||
            p.y > e.hi.y + 0.03f || p.z > e.hi.z + 0.03f) {
            continue;
        }
        const glm::vec3 s = e.hi - e.lo;
        const float v = s.x * s.y * s.z * (e.category == "room" ? 1e6f : 1.0f); // a room only when nothing else
        if (v < bestVolume) {
            bestVolume = v;
            best = &e;
        }
    }
    return best != nullptr ? best->id + (best->category == "room" ? " (walls)" : "") : std::string();
}

// ---- events, grouped into runs --------------------------------------------------------------------------

struct Event {
    std::string rule, object, entity;
    double time = 0.0;
    std::uint64_t frame = 0;
    float value = 0.0f; // depth / distance / share
    glm::vec3 at{0.0f};
    glm::vec3 eye{0.0f};
};

struct Run {
    Event first;
    double t1 = 0.0;
    std::uint64_t f1 = 0;
    float worst = 0.0f;
    glm::vec3 at{0.0f};
    glm::vec3 eye{0.0f};
    double worstTime = 0.0;
    int frames = 0;
};

// ---- the scene's semantic map: which categories a named SDF node / an object / a node carries -----------

struct SemanticMap {
    std::map<std::string, std::set<std::string>> byObject;           // object -> categories
    std::map<std::string, std::set<std::string>> byNode;             // object/node -> categories
    std::map<std::string, bool> movesNode;                           // object/node -> an entity says "moves"
    std::map<std::string, bool> movesObject;
};

void collectSemantic(const json& n, const std::string& object, const std::string& inherited, SemanticMap& m,
                     std::set<std::string>& below, bool& movesBelow, bool inheritedMoves, int depth) {
    if (!n.is_object() || depth > 40) return;
    std::string cat = inherited;
    bool moves = inheritedMoves;
    std::set<std::string> mine;
    bool myMoves = false;
    if (n.contains("entity") && n["entity"].is_object() && n["entity"].contains("category")) {
        cat = n["entity"]["category"].get<std::string>();
        moves = n["entity"].value("moves", false);
        mine.insert(cat);
        myMoves = moves;
    }
    if (n.contains("children") && n["children"].is_array()) {
        for (const auto& c : n["children"]) collectSemantic(c, object, cat, m, mine, myMoves, moves, depth + 1);
    }
    if (n.contains("name") && n["name"].is_string()) {
        std::set<std::string> cats = mine;
        if (!inherited.empty()) cats.insert(inherited);
        if (cats.empty() && !cat.empty()) cats.insert(cat);
        const std::string key = object + "/" + n["name"].get<std::string>();
        m.byNode[key] = cats;
        m.movesNode[key] = myMoves || inheritedMoves || n.value("moves", false);
    }
    below.insert(mine.begin(), mine.end());
    movesBelow = movesBelow || myMoves;
}

SemanticMap semanticMap(const json& scene) {
    SemanticMap m;
    if (!scene.contains("nodes")) return m;
    for (const auto& node : scene["nodes"]) {
        const std::string name = node.value("name", std::string());
        std::set<std::string> cats;
        bool moves = false;
        if (node.contains("entity") && node["entity"].is_object()) {
            if (node["entity"].contains("category")) cats.insert(node["entity"]["category"].get<std::string>());
            moves = node["entity"].value("moves", false);
        }
        if (node.value("kind", std::string()) == "sdf" && node.contains("sdf") && node["sdf"].contains("tree")) {
            const json& t = node["sdf"]["tree"];
            collectSemantic(t.contains("root") ? t["root"] : t, name, "", m, cats, moves, false, 0);
        }
        if (node.value("kind", std::string()) == "procedural" && node.contains("procedural") &&
            node["procedural"].contains("source") && node["procedural"]["source"].value("kind", std::string()) == "text" &&
            cats.empty()) {
            cats.insert("wallText");
        }
        m.byObject[name] = cats;
        m.movesObject[name] = moves || node.value("moves", false);
    }
    return m;
}

// ---- motion watchers ------------------------------------------------------------------------------------

struct Watch {
    params::IParameter* param = nullptr;
    TransformClass cls;
    std::string path;
    params::IParameter* visible = nullptr;
    double settle = -1.0;     // construction ends here (the last timeline key, plus the settle margin)
    bool hasPrev = false;
    std::array<float, 3> prev{};
    std::array<float, 3> lo{kInf, kInf, kInf};
    std::array<float, 3> hi{-kInf, -kInf, -kInf};
    int active = 0;
    int reversals = 0;
    int lastSign = 0;
    double firstActive = -1.0, lastActive = -1.0;
    double firstVisible = -1.0;
};

float magnitude(const Watch& w, const std::array<float, 3>& d, const std::array<float, 3>& ref) {
    const std::size_t n = std::min<std::size_t>(3, w.param->componentCount());
    float d2 = 0.0f, r2 = 0.0f;
    for (std::size_t i = 0; i < n; ++i) {
        d2 += d[i] * d[i];
        r2 += ref[i] * ref[i];
    }
    const float m = std::sqrt(d2);
    return w.cls.measure == TransformMeasure::Relative ? m / std::max(std::sqrt(r2), 1e-3f) : m;
}

const char* unitOf(TransformMeasure m) {
    return m == TransformMeasure::Metres ? "m" : m == TransformMeasure::Degrees ? " degrees" : " (relative)";
}

} // namespace

Result<json> validateFilm(const std::string& project, const json& scene, const json& staticReport, const json& rules,
                          const FilmValidateOptions& options) {
    log::init(log::Level::Warn);
    json doc;
    {
        std::ifstream f(project);
        try {
            doc = json::parse(f);
        } catch (const std::exception& e) {
            return fail("film validator: {}", e.what());
        }
    }
    Engine engine(EngineMode::Offline);
    engine.setLiveControl(false);
    if (auto r = engine.loadProject(project); !r) return fail("film validator: {}", r.error().message);

    const json fr = rules.value("film", json::object());
    const json mr = rules.value("motion", json::object());
    const float closeDistance = fr.value("closeDistance", 0.3f);
    const float closeShare = fr.value("closeShare", 0.35f);
    const float clearance = fr.value("clearance", 0.12f);
    const float cutDistance = fr.value("cutDistance", 1.5f);
    const int raysX = fr.value("raysX", 9);
    const int raysY = fr.value("raysY", 5);
    const double settleMargin = mr.value("settle", 0.25);
    const float epsMetres = mr.value("epsilon", 0.0015f);
    const float epsRelative = mr.value("epsilonRelative", 0.003f);
    const float epsDegrees = mr.value("epsilonDegrees", 0.1f);
    const float twitchMetres = mr.value("twitchRange", 0.3f);
    const float twitchRelative = mr.value("twitchRangeRelative", 0.15f);
    const float twitchDegrees = mr.value("twitchRangeDegrees", 20.0f);
    const double minActive = mr.value("minActiveSeconds", 0.5);
    const int minReversals = mr.value("minReversals", 3);
    const json allow = mr.value("allow", json::array());

    const double rate = options.fps > 0.0 ? options.fps : std::max(1.0, engine.renderSettings().fps);
    const double renderFps = std::max(1.0, engine.renderSettings().fps);
    const double end = options.to > 0.0 ? options.to : engine.durationSeconds();
    const float aspect = static_cast<float>(engine.renderSettings().width) /
                         static_cast<float>(std::max<std::uint32_t>(1, engine.renderSettings().height));
    const auto ents = entityBoxes(staticReport);
    const SemanticMap sem = semanticMap(scene);
    const json& cats = rules.value("categories", json::object());

    // tracks and routes by target
    std::map<std::string, std::vector<const json*>> routesByTarget;
    std::map<std::string, double> lastKey;
    if (doc.contains("routes")) {
        for (const auto& r : doc["routes"]) routesByTarget[r.value("target", std::string())].push_back(&r);
    }
    if (doc.contains("timeline") && doc["timeline"].contains("tracks")) {
        for (const auto& t : doc["timeline"]["tracks"]) {
            double last = -1.0;
            if (t.contains("keys")) {
                for (const auto& k : t["keys"]) last = std::max(last, k.value("time", 0.0));
            }
            auto& slot = lastKey[t.value("target", std::string())];
            slot = std::max(slot, last);
        }
    }

    // the transforms to watch
    auto& set = engine.params();
    std::vector<Watch> watches;
    int skippedMoving = 0;
    auto categoryMoves = [&](const std::string& c) {
        if (!cats.contains(c)) return false;
        const json& cj = cats[c];
        const std::string g = cj.value("group", std::string());
        return cj.value("moves", false) || g == "character" || g == "typography";
    };
    auto structuralOf = [&](const Watch& w, std::string* why) -> int { // 2 structural, 1 untagged, 0 may move
        std::set<std::string> cs;
        bool moves = false;
        if (!w.cls.node.empty()) {
            const std::string key = w.cls.owner + "/" + w.cls.node;
            if (auto it = sem.byNode.find(key); it != sem.byNode.end()) cs = it->second;
            if (auto it = sem.movesNode.find(key); it != sem.movesNode.end()) moves = it->second;
        } else {
            if (auto it = sem.byObject.find(w.cls.owner); it != sem.byObject.end()) cs = it->second;
            if (auto it = sem.movesObject.find(w.cls.owner); it != sem.movesObject.end()) moves = it->second;
        }
        if (moves) return 0;
        std::string structural;
        bool allMove = !cs.empty();
        for (const auto& c : cs) {
            const std::string g = cats.contains(c) ? cats[c].value("group", std::string()) : std::string();
            if ((g == "architecture" || g == "furniture" || c == "room") && !categoryMoves(c)) structural += (structural.empty() ? "" : ", ") + c;
            if (!categoryMoves(c)) allMove = false;
        }
        if (!structural.empty()) {
            if (why) *why = structural;
            return 2;
        }
        if (allMove) return 0;
        if (why) *why = cs.empty() ? std::string("untagged") : [&] {
            std::string s;
            for (const auto& c : cs) s += (s.empty() ? "" : ", ") + c;
            return s;
        }();
        return cs.empty() ? 1 : 1;
    };
    if (options.motion) {
        for (params::IParameter* p : set.ordered()) {
            auto c = classifyTransform(p->path(), true);
            if (!c || p->path().starts_with("particles/")) continue;
            bool allowed = false;
            for (const auto& a : allow) {
                if (a.is_string() && p->path().find(a.get<std::string>()) != std::string::npos) allowed = true;
            }
            if (allowed) continue;
            Watch w;
            w.param = p;
            w.cls = *c;
            w.path = p->path();
            const std::string prefix = w.path.substr(0, w.path.find('/'));
            w.visible = set.find(prefix + "/" + w.cls.owner + "/visible");
            if (structuralOf(w, nullptr) == 0) {
                ++skippedMoving;
                continue;
            }
            if (auto it = lastKey.find(w.path); it != lastKey.end()) w.settle = it->second + settleMargin;
            watches.push_back(w);
        }
    }

    std::ofstream trace;
    if (!options.cameraTrace.empty()) {
        trace.open(options.cameraTrace);
        trace << "time,frame,eye_x,eye_y,eye_z,target_x,target_y,target_z,fov_deg,near,clearance,nearest\n";
    }
    std::vector<Event> events;
    float minClearance = kInf;
    double minClearanceAt = -1.0;
    std::size_t samples = 0;
    bool havePrev = false;
    glm::vec3 prevEye{0.0f};
    std::uint32_t prevCut = 0;
    const auto frames = static_cast<std::uint64_t>(std::ceil(end * rate));
    const auto firstFrame = static_cast<std::uint64_t>(std::floor(std::max(0.0, options.from) * rate));
    // Play from 0 regardless (the film's state is cumulative through its springs); check from `from`.
    for (std::uint64_t f = 0; f <= frames; ++f) {
        FrameTime t;
        t.renderTime = static_cast<double>(f) / rate;
        t.deltaTime = f == 0 ? 0.0 : 1.0 / rate;
        t.frameIndex = f;
        engine.update(t);
        if (f < firstFrame) continue;
        const double now = t.renderTime;
        const auto renderFrame = static_cast<std::uint64_t>(std::llround(now * renderFps));
        const scene::Scene& sc = engine.scene();
        const spatial::FieldSet* fields = &sc.fields;
        if (options.camera) {
            ++samples;
            const scene::Camera& cam = sc.camera;
            const glm::vec3 eye = cam.position;
            const auto objs = liveObjects(sc);
            const Nearest n = sceneDistance(objs, eye, 2.0f, now, fields);
            if (trace) {
                trace << fmt::format("{:.4f},{},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.4f},{:.3f},{:.4f},{:.4f},{}\n", now, renderFrame,
                                     eye.x, eye.y, eye.z, cam.target.x, cam.target.y, cam.target.z,
                                     glm::degrees(cam.effectiveFovY()), cam.nearPlane, n.d < kInf ? n.d : -1.0f,
                                     n.object >= 0 ? objs[static_cast<std::size_t>(n.object)].so->name : std::string());
            }
            if (n.d < minClearance) {
                minClearance = n.d;
                minClearanceAt = now;
            }
            auto label = [&](int oi, const glm::vec3& p) {
                const std::string obj = objs[static_cast<std::size_t>(oi)].so->name;
                return std::make_pair(obj, nameAt(ents, obj, p));
            };
            if (n.object >= 0 && n.d < 0.0f) {
                auto [o, e] = label(n.object, eye);
                events.push_back({"cameraInside", o, e, now, renderFrame, -n.d, eye, eye});
            } else if (n.object >= 0 && n.d < clearance) {
                auto [o, e] = label(n.object, eye);
                events.push_back({"cameraClearance", o, e, now, renderFrame, clearance - n.d, eye, eye});
            }
            // the path between this frame and the last
            const bool cut = !havePrev || cam.cutSerial != prevCut || glm::length(eye - prevEye) > cutDistance;
            if (!cut && n.d >= 0.0f) {
                const glm::vec3 a = prevEye;
                const glm::vec3 seg = eye - a;
                const float len = glm::length(seg);
                if (len > 1e-4f) {
                    const glm::vec3 dir = seg / len;
                    float s = 0.0f;
                    for (int k = 0; k < 256 && s < len; ++k) {
                        const glm::vec3 p = a + dir * s;
                        const Nearest q = sceneDistance(objs, p, len + 0.5f, now, fields);
                        if (q.object >= 0 && q.d < 0.0f) {
                            auto [o, e] = label(q.object, p);
                            events.push_back({"cameraCrossing", o, e, now, renderFrame, len, p, eye});
                            break;
                        }
                        s += std::max(q.d, 0.004f);
                    }
                }
            }
            // what the lens sees up close
            if (n.d >= 0.0f) {
                glm::vec3 fwd = cam.target - eye;
                if (glm::length(fwd) > 1e-6f) {
                    fwd = glm::normalize(fwd);
                    glm::vec3 right = glm::cross(fwd, cam.up);
                    right = glm::length(right) > 1e-6f ? glm::normalize(right) : glm::vec3(1.0f, 0.0f, 0.0f);
                    const glm::vec3 up = glm::cross(right, fwd);
                    const float th = std::tan(cam.effectiveFovY() * 0.5f);
                    int close = 0, clipped = 0, total = 0;
                    float nearestHit = kInf;
                    int nearestObj = -1;
                    glm::vec3 nearestAt{0.0f};
                    const float reach = std::max(closeDistance, cam.nearPlane) * 1.5f;
                    for (int i = 0; i < raysX; ++i) {
                        for (int j = 0; j < raysY; ++j) {
                            const float x = ((static_cast<float>(i) + 0.5f) / static_cast<float>(raysX) * 2.0f - 1.0f) * th * aspect;
                            const float y = ((static_cast<float>(j) + 0.5f) / static_cast<float>(raysY) * 2.0f - 1.0f) * th;
                            const glm::vec3 dir = glm::normalize(fwd + right * x + up * y);
                            const float cosA = glm::dot(dir, fwd);
                            ++total;
                            float s = 0.0f;
                            for (int k = 0; k < 64 && s < reach; ++k) {
                                const glm::vec3 p = eye + dir * s;
                                const Nearest q = sceneDistance(objs, p, reach, now, fields);
                                if (q.object >= 0 && q.d < 1e-3f) {
                                    const float depth = s * cosA; // view depth, what the near plane cuts
                                    if (depth < closeDistance) ++close;
                                    if (depth < cam.nearPlane) ++clipped;
                                    if (s < nearestHit) {
                                        nearestHit = s;
                                        nearestObj = q.object;
                                        nearestAt = p;
                                    }
                                    break;
                                }
                                s += std::max(q.d, 0.002f);
                            }
                        }
                    }
                    if (clipped > 0 && nearestObj >= 0) {
                        auto [o, e] = label(nearestObj, nearestAt);
                        events.push_back({"nearClip", o, e, now, renderFrame, static_cast<float>(clipped) / total, nearestAt, eye});
                    } else if (total > 0 && static_cast<float>(close) / total >= closeShare && nearestObj >= 0) {
                        auto [o, e] = label(nearestObj, nearestAt);
                        events.push_back({"closeGeometry", o, e, now, renderFrame, static_cast<float>(close) / total, nearestAt, eye});
                    }
                }
            }
            // entering a room before it has opened
            for (const EntityBox& r : ents) {
                if (!r.room || r.opensAt < 0.0 || now >= r.opensAt) continue;
                if (eye.x > r.inLo.x && eye.x < r.inHi.x && eye.y > r.inLo.y && eye.y < r.inHi.y && eye.z > r.inLo.z &&
                    eye.z < r.inHi.z) {
                    events.push_back({"cameraEarly", r.object, r.id, now, renderFrame, static_cast<float>(r.opensAt - now), eye, eye});
                }
            }
            havePrev = true;
            prevEye = eye;
            prevCut = cam.cutSerial;
        }
        for (Watch& w : watches) {
            const bool visible = w.visible == nullptr || w.visible->finalComponent(0) >= 0.5f;
            std::array<float, 3> v{};
            for (std::size_t i = 0; i < std::min<std::size_t>(3, w.param->componentCount()); ++i) v[i] = w.param->finalComponent(i);
            if (!visible) {
                w.hasPrev = false;
                continue;
            }
            if (w.firstVisible < 0.0) w.firstVisible = now;
            const double settle = std::max(w.settle, w.firstVisible + settleMargin);
            if (w.hasPrev && now > settle) {
                std::array<float, 3> d{};
                int dominant = 0;
                for (int i = 0; i < 3; ++i) {
                    d[static_cast<std::size_t>(i)] = v[static_cast<std::size_t>(i)] - w.prev[static_cast<std::size_t>(i)];
                    if (std::abs(d[static_cast<std::size_t>(i)]) > std::abs(d[static_cast<std::size_t>(dominant)])) dominant = i;
                }
                const float m = magnitude(w, d, v);
                const float eps = w.cls.measure == TransformMeasure::Metres ? epsMetres
                                  : w.cls.measure == TransformMeasure::Degrees ? epsDegrees : epsRelative;
                for (std::size_t i = 0; i < 3; ++i) {
                    w.lo[i] = std::min(w.lo[i], v[i]);
                    w.hi[i] = std::max(w.hi[i], v[i]);
                }
                if (m > eps) {
                    ++w.active;
                    if (w.firstActive < 0.0) w.firstActive = now;
                    w.lastActive = now;
                    const int sign = d[static_cast<std::size_t>(dominant)] > 0.0f ? 1 : -1;
                    if (w.lastSign != 0 && sign != w.lastSign) ++w.reversals;
                    w.lastSign = sign;
                }
            }
            w.prev = v;
            w.hasPrev = true;
        }
    }

    // ---- camera runs -> violations
    json violations = json::array();
    std::map<std::string, std::vector<Run>> runs; // rule|object|entity
    for (const Event& e : events) {
        const std::string key = e.rule + "|" + e.object + "|" + e.entity;
        auto& list = runs[key];
        const double gap = 1.5 / rate;
        if (!list.empty() && e.time - list.back().t1 <= gap + 1e-9) {
            Run& r = list.back();
            r.t1 = e.time;
            r.f1 = e.frame;
            ++r.frames;
            if (e.value > r.worst) {
                r.worst = e.value;
                r.at = e.at;
                r.eye = e.eye;
                r.worstTime = e.time;
            }
        } else {
            list.push_back({e, e.time, e.frame, e.value, e.at, e.eye, e.time, 1});
        }
    }
    auto what = [](const Run& r) {
        return r.first.entity.empty() ? "'" + r.first.object + "'" : r.first.entity + " ('" + r.first.object + "')";
    };
    auto span = [](const Run& r) {
        return r.first.frame == r.f1 ? fmt::format("frame {} ({} s, {})", r.first.frame, r3(r.first.time), fmtTime(r.first.time))
                                     : fmt::format("frames {}-{} ({}-{} s, {}-{})", r.first.frame, r.f1, r3(r.first.time), r3(r.t1),
                                                   fmtTime(r.first.time), fmtTime(r.t1));
    };
    std::vector<std::pair<int, json>> ordered;
    for (const auto& [key, list] : runs) {
        for (const Run& r : list) {
            const std::string& rule = r.first.rule;
            json v{{"rule", rule},
                   {"entities", json::array({r.first.entity.empty() ? r.first.object : r.first.entity})},
                   {"time", r3(r.first.time)},
                   {"timeEnd", r3(r.t1)},
                   {"frame", r.first.frame},
                   {"frameEnd", r.f1},
                   {"groups", json::array({"film"})}};
            json measured{{"at", vecJson(r.at)}, {"eye", vecJson(r.eye)}, {"worstAt", r3(r.worstTime)}, {"object", r.first.object}};
            int rank = 1;
            if (rule == "cameraInside") {
                v["severity"] = "ERROR";
                measured["depth"] = r3(r.worst);
                v["message"] = fmt::format("camera is inside {} at {} (up to {:.2f}m deep)", what(r), span(r), r.worst);
                v["suggestion"] = "move the camera path (journey path points, look-at or breath) out of the solid, or move the geometry";
                rank = 0;
            } else if (rule == "cameraCrossing") {
                v["severity"] = "ERROR";
                measured["step"] = r3(r.worst);
                v["message"] = fmt::format("camera passes through {} between frames at {} (the eye is outside on both "
                                           "frames; the path between them crosses solid)",
                                           what(r), span(r));
                v["suggestion"] = "the camera moves through a wall within one frame: route the path round it, or cut";
                rank = 0;
            } else if (rule == "nearClip") {
                v["severity"] = r.worst >= 0.1f ? "ERROR" : "WARNING";
                measured["share"] = r3(r.worst);
                v["message"] = fmt::format("near-plane clipping: {} is nearer than the near plane in {:.0f}% of the view at {}",
                                           what(r), r.worst * 100.0f, span(r));
                v["suggestion"] = "keep the camera at least the near plane plus a margin from geometry";
                rank = v["severity"] == "ERROR" ? 0 : 1;
            } else if (rule == "closeGeometry") {
                v["severity"] = "WARNING";
                measured["share"] = r3(r.worst);
                v["message"] = fmt::format("{} fills {:.0f}% of the view from under {:.2f}m at {} (wide-angle distortion)",
                                           what(r), r.worst * 100.0f, closeDistance, span(r));
                v["suggestion"] = "pull the camera back, narrow the field of view, or turn it off the surface";
            } else if (rule == "cameraClearance") {
                v["severity"] = "INFO";
                measured["closest"] = r3(clearance - r.worst);
                v["message"] = fmt::format("camera passes {:.2f}m from {} at {}", clearance - r.worst, what(r), span(r));
                rank = 2;
            } else if (rule == "cameraEarly") {
                v["severity"] = "WARNING";
                v["message"] = fmt::format("camera enters {} {:.2f}s before it opens at {}", r.first.entity, r.worst, span(r));
            }
            v["measured"] = measured;
            ordered.emplace_back(rank, v);
        }
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        if (a.first != b.first) return a.first < b.first;
        return a.second["time"].template get<double>() < b.second["time"].template get<double>();
    });

    // ---- motion -> violations
    int lockedOk = 0, twitch = 0, animated = 0;
    for (Watch& w : watches) {
        if (w.active == 0) {
            if (w.firstVisible >= 0.0) ++lockedOk;
            continue;
        }
        const double activeSeconds = w.active / rate;
        if (activeSeconds < minActive || w.reversals < minReversals) {
            ++lockedOk;
            continue;
        }
        std::array<float, 3> range{};
        std::array<float, 3> mid{};
        for (std::size_t i = 0; i < 3; ++i) {
            range[i] = w.hi[i] - w.lo[i];
            mid[i] = (w.hi[i] + w.lo[i]) * 0.5f;
        }
        const float extent = magnitude(w, range, mid);
        const float limit = w.cls.measure == TransformMeasure::Metres ? twitchMetres
                            : w.cls.measure == TransformMeasure::Degrees ? twitchDegrees : twitchRelative;
        std::string why;
        const int structural = structuralOf(w, &why);
        json drivers = json::array();
        std::string driverText;
        if (auto it = routesByTarget.find(w.path); it != routesByTarget.end()) {
            for (const json* r : it->second) {
                json c{{"source", r->value("source", std::string())}, {"amount", r->value("amount", 1.0)}};
                if (r->contains("depthSource")) c["depthSource"] = (*r)["depthSource"];
                if (r->contains("chain")) c["chain"] = (*r)["chain"];
                drivers.push_back(c);
                driverText += fmt::format("{}{} x {}", driverText.empty() ? "" : ", ", r->value("source", std::string()),
                                          r3(r->value("amount", 1.0)));
            }
        }
        const bool small = extent <= limit;
        json v{{"rule", small ? "buildLock" : "structuralMotion"},
               {"entities", json::array({w.path})},
               {"time", r3(w.firstActive)},
               {"timeEnd", r3(w.lastActive)},
               {"groups", json::array({"film"})},
               {"measured", {{"range", r3(extent)}, {"reversals", w.reversals}, {"activeSeconds", r3(activeSeconds)},
                             {"settledAt", r3(std::max(w.settle, w.firstVisible + settleMargin))}, {"carries", why}}}};
        if (!drivers.empty()) v["drivers"] = drivers;
        const std::string drive = driverText.empty() ? std::string("a timeline track") : "route " + driverText;
        if (small) {
            ++twitch;
            v["severity"] = structural == 2 ? "WARNING" : "INFO";
            v["message"] = fmt::format("{} ({}) keeps moving after it is built: {} direction changes over {:.1f}s between {} and {}, "
                                       "range {:.3f}{}; driven by {} -- continuous modulation on a structural transform "
                                       "(it reads as a twitch)",
                                       w.path, why, w.reversals, activeSeconds, fmtTime(w.firstActive), fmtTime(w.lastActive), extent,
                                       unitOf(w.cls.measure), drive);
            v["suggestion"] = "lock it once its construction event completes: gate the route with a depthSource that ends at "
                              "the build, move the modulation to light/emission/edges, or tag the entity \"moves\": true if the "
                              "motion is the effect";
        } else {
            ++animated;
            v["severity"] = "INFO";
            v["message"] = fmt::format("{} ({}) moves continuously after {} (range {:.2f}{}, {} direction changes); driven by {}",
                                       w.path, why, fmtTime(w.firstActive), extent, unitOf(w.cls.measure), w.reversals, drive);
        }
        ordered.emplace_back(v["severity"] == "WARNING" ? 1 : 2, v);
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (auto& [rank, v] : ordered) violations.push_back(v);
    json out{{"violations", violations},
             {"camera", {{"samples", samples}, {"fps", rate}, {"minClearance", minClearance < kInf ? json(r3(minClearance)) : json()},
                         {"minClearanceAt", r3(minClearanceAt)}}},
             {"motion", {{"watched", watches.size()}, {"mayMove", skippedMoving}, {"locked", lockedOk}, {"twitching", twitch},
                         {"animated", animated}}}};
    return out;
}

} // namespace avgen::app
