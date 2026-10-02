#include "scene/space_validator.hpp"

#include "comp/font.hpp"
#include "scene/journey.hpp"
#include "scene/text_mesh.hpp"
#include "spatial/sdf.hpp"

#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

// Violations are built with designated-order aggregate initialisers that leave the trailing fields
// defaulted, and measurements are float values formatted through fmt (which promotes them).
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wdouble-promotion"

namespace avgen::scene {

using nlohmann::json;

namespace {

constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr float kHuge = 1.0e4f;

// ---- the built-in rules --------------------------------------------------------------------------------

const char* kDefaultRules = R"json({
  "tolerances": {
    "contact": 0.015,
    "intersectError": 0.05,
    "floatWarn": 0.02,
    "floatError": 0.5,
    "floorPenetration": 0.02,
    "floorPenetrationError": 0.05,
    "tilt": 6.0,
    "wallDistance": 0.07,
    "wallAngle": 12.0,
    "embedDepth": 0.45,
    "anchorPenetration": 0.08,
    "roomSlack": 0.35,
    "supportGap": 0.03
  },
  "lyric": {
    "margin": 0.15,
    "obstacleDepth": 0.6,
    "planeTolerance": 0.08,
    "faceAngle": 15.0,
    "maxTilt": 25.0,
    "searchStep": 0.05
  },
  "door": {"clearanceDepth": 0.7, "minWidth": 0.7, "height": 1.9},
  "structure": {"alignWarn": 0.015, "alignError": 0.05, "sillWarn": 0.02, "wallGap": 0.15, "chairSpacing": 0.5},
  "window": {"clearanceDepth": 0.3},
  "camera": {"eyeHeight": 1.6, "clearance": 0.12, "step": 0.1},
  "film": {"closeDistance": 0.3, "closeShare": 0.35, "clearance": 0.12, "cutDistance": 1.5, "raysX": 9, "raysY": 5},
  "motion": {"settle": 0.25, "epsilon": 0.0015, "epsilonRelative": 0.003, "epsilonDegrees": 0.1, "twitchRange": 0.3,
             "twitchRangeRelative": 0.15, "twitchRangeDegrees": 20.0, "minActiveSeconds": 0.5, "minReversals": 3,
             "allow": []},
  "categories": {
    "room":        {"group": "architecture"},
    "door":        {"group": "architecture", "embeds": "wall", "opening": "door", "mayIntersect": ["room", "curtains"]},
    "doorway":     {"group": "architecture", "embeds": "wall", "opening": "door", "mayIntersect": ["room"]},
    "window":      {"group": "architecture", "embeds": "wall", "opening": "window", "mayIntersect": ["room", "curtains"]},
    "stairs":      {"group": "architecture", "rests": "floor", "mayIntersect": ["room", "railing"]},
    "landing":     {"group": "architecture", "mayIntersect": ["room", "stairs", "railing"]},
    "balcony":     {"group": "architecture", "mayIntersect": ["room", "railing"]},
    "railing":     {"group": "architecture", "mayIntersect": ["room", "stairs", "landing", "balcony"]},
    "roof":        {"group": "architecture", "mayIntersect": ["building", "room"]},
    "building":    {"group": "architecture", "mayIntersect": ["roof", "door", "window", "room"]},
    "trim":        {"group": "architecture", "mayIntersect": ["room", "door", "window"]},
    "sill":        {"group": "architecture", "mayIntersect": ["room", "window"]},
    "couch":       {"group": "furniture", "rests": "floor", "supports": ["sitting", "lying"], "seatHeight": 0.45,
                    "keepClear": {"min": [0.15, 0.55, 0.45], "max": [0.85, 1.25, 1.0]},
                    "expect": "nothing should occupy the couch's seating volume"},
    "armchair":    {"group": "furniture", "rests": "floor", "supports": ["sitting"], "seatHeight": 0.45},
    "chair":       {"group": "furniture", "rests": "floor", "supports": ["sitting"], "seatHeight": 0.46,
                    "faces": {"targets": ["table", "desk"], "maxDistance": 0.9, "maxAngle": 50.0},
                    "expect": "chair should occupy a seating position adjacent to the table or desk, facing it, with clearance for a seated occupant"},
    "table":       {"group": "furniture", "rests": "floor", "supports": ["dining", "working"], "surface": true},
    "coffeeTable": {"group": "furniture", "rests": "floor", "surface": true},
    "desk":        {"group": "furniture", "rests": "floor", "supports": ["working"], "surface": true},
    "bed":         {"group": "furniture", "rests": "floor", "supports": ["lying", "sitting"], "seatHeight": 0.5,
                    "surfaceHeight": 0.55, "againstWall": true},
    "nightstand":  {"group": "furniture", "rests": "floor", "surface": true},
    "cabinet":     {"group": "furniture", "rests": "floor", "surface": true, "againstWall": true},
    "wardrobe":    {"group": "furniture", "rests": "floor", "againstWall": true},
    "shelf":       {"group": "furniture", "rests": "floor", "surface": true, "againstWall": true},
    "counter":     {"group": "furniture", "rests": "floor", "surface": true, "againstWall": true, "rooms": ["kitchen", "kit"]},
    "fridge":      {"group": "furniture", "rests": "floor", "againstWall": true, "rooms": ["kitchen", "kit"]},
    "fireplace":   {"group": "furniture", "rests": "floor", "mayIntersect": ["room"]},
    "sink":        {"group": "furniture", "rests": ["floor", "wall"], "surface": true},
    "toilet":      {"group": "furniture", "rests": "floor", "supports": ["sitting"], "seatHeight": 0.42},
    "bathtub":     {"group": "furniture", "rests": "floor", "supports": ["lying"], "surfaceHeight": 0.2},
    "stove":       {"group": "furniture", "rests": "floor", "surface": true, "againstWall": true, "rooms": ["kitchen", "kit"]},
    "dishwasher":  {"group": "furniture", "rests": "floor", "rooms": ["kitchen", "kit"]},
    "washer":      {"group": "furniture", "rests": "floor", "rooms": ["laundry", "lau", "utility"]},
    "dryer":       {"group": "furniture", "rests": "floor", "rooms": ["laundry", "lau", "utility"]},
    "treadmill":   {"group": "furniture", "rests": "floor", "rooms": ["gym", "workout", "fitness"]},
    "weightBench": {"group": "furniture", "rests": "floor", "rooms": ["gym", "workout", "fitness"]},
    "weightRack":  {"group": "furniture", "rests": "floor", "rooms": ["gym", "workout", "fitness"]},
    "exerciseBike": {"group": "furniture", "rests": "floor", "rooms": ["gym", "workout", "fitness"]},
    "bar":         {"group": "furniture", "rests": "floor", "surface": true},
    "barStool":    {"group": "furniture", "rests": "floor", "supports": ["sitting"], "seatHeight": 0.75},
    "lamp":        {"group": "decor", "rests": "floor"},
    "tableLamp":   {"group": "decor", "rests": "surface"},
    "hangingLamp": {"group": "decor", "mounts": "ceiling", "mayIntersect": ["room"], "moves": true},
    "ceilingFan":  {"group": "decor", "mounts": "ceiling", "mayIntersect": ["room"], "moves": true},
    "painting":    {"group": "decor", "mounts": "wall", "mayIntersect": ["room"]},
    "mirror":      {"group": "decor", "mounts": "wall", "mayIntersect": ["room"]},
    "clock":       {"group": "decor", "mounts": "wall", "mayIntersect": ["room"]},
    "wallDecoration": {"group": "decor", "mounts": "wall", "mayIntersect": ["room"]},
    "coatHooks":   {"group": "decor", "mounts": "wall", "mayIntersect": ["room"]},
    "curtains":    {"group": "decor", "mounts": "wall", "mayIntersect": ["room", "window", "door"], "moves": true},
    "television":  {"group": "decor", "rests": ["floor", "surface", "wall"], "facesRoom": true},
    "monitor":     {"group": "decor", "rests": ["surface", "wall"], "facesRoom": true},
    "plant":       {"group": "decor", "rests": ["floor", "surface"], "moves": true},
    "rug":         {"group": "decor", "rests": "floor", "mayIntersect": ["*"]},
    "prop":        {"group": "decor", "rests": ["surface", "floor"]},
    "hangingObject": {"group": "decor", "mounts": "ceiling", "mayIntersect": ["room"], "moves": true},
    "mannequin":   {"group": "character", "requires": ["head", "body"]},
    "person":      {"group": "character", "requires": ["head", "body"]},
    "wallText":    {"group": "typography", "text": "wall"},
    "floorText":   {"group": "typography", "text": "surface"},
    "stairText":   {"group": "typography", "text": "surface"},
    "floatingText": {"group": "typography", "text": "floating"}
  },
  "poses": {
    "stand":         {"support": "floor"},
    "wait":          {"support": "floor"},
    "sit":           {"support": "seat", "needs": "sitting"},
    "thinker":       {"support": "seat", "needs": "sitting"},
    "headInHands":   {"support": "seat", "needs": "sitting"},
    "elbowsOnTable": {"support": "seat", "needs": "sitting"},
    "toilet":        {"support": "seat", "needs": "sitting"},
    "desk":          {"support": "seat", "needs": "sitting"},
    "lie":           {"support": "top", "needs": "lying"},
    "couchLying":    {"support": "top", "needs": "lying"},
    "bedLying":      {"support": "top", "needs": "lying"},
    "mirror":        {"support": "floor", "facesAnchor": 35.0},
    "window":        {"support": "floor", "facesAnchor": 45.0}
  }
})json";

// ---- small geometry ------------------------------------------------------------------------------------

struct SpBox {
    glm::vec3 lo{kInf};
    glm::vec3 hi{-kInf};
    [[nodiscard]] bool valid() const { return lo.x <= hi.x && lo.y <= hi.y && lo.z <= hi.z; }
    void add(const glm::vec3& p) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    void add(const SpBox& b) {
        if (b.valid()) {
            add(b.lo);
            add(b.hi);
        }
    }
    [[nodiscard]] glm::vec3 centre() const { return (lo + hi) * 0.5f; }
    [[nodiscard]] glm::vec3 size() const { return hi - lo; }
    [[nodiscard]] float volume() const {
        if (!valid()) return 0.0f;
        const glm::vec3 s = size();
        return s.x * s.y * s.z;
    }
    [[nodiscard]] bool contains(const glm::vec3& p, float slack = 0.0f) const {
        return p.x >= lo.x - slack && p.x <= hi.x + slack && p.y >= lo.y - slack && p.y <= hi.y + slack &&
               p.z >= lo.z - slack && p.z <= hi.z + slack;
    }
};

SpBox intersect(const SpBox& a, const SpBox& b) {
    SpBox r;
    r.lo = glm::max(a.lo, b.lo);
    r.hi = glm::min(a.hi, b.hi);
    return r;
}

SpBox transformBox(const SpBox& b, const glm::mat4& m) {
    SpBox r;
    if (!b.valid()) return r;
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 c{(i & 1) ? b.hi.x : b.lo.x, (i & 2) ? b.hi.y : b.lo.y, (i & 4) ? b.hi.z : b.lo.z};
        r.add(glm::vec3(m * glm::vec4(c, 1.0f)));
    }
    return r;
}

SpBox boxAround(float hx, float hy, float hz) {
    SpBox b;
    b.lo = {-hx, -hy, -hz};
    b.hi = {hx, hy, hz};
    return b;
}

SpBox hugeBox() {
    return boxAround(kHuge, kHuge, kHuge);
}

glm::vec3 vec3Of(const json& j, glm::vec3 fallback) {
    if (!j.is_array() || j.size() != 3) return fallback;
    for (const auto& v : j) {
        if (!v.is_number()) return fallback;
    }
    return {j[0].get<float>(), j[1].get<float>(), j[2].get<float>()};
}

glm::mat4 eulerMatrix(const glm::vec3& degrees) {
    return glm::mat4_cast(glm::quat(glm::radians(degrees)));
}

float angleBetween(glm::vec3 a, glm::vec3 b) {
    const float la = glm::length(a);
    const float lb = glm::length(b);
    if (la < 1e-6f || lb < 1e-6f) return 0.0f;
    return glm::degrees(std::acos(std::clamp(glm::dot(a / la, b / lb), -1.0f, 1.0f)));
}

double r3(double v) {
    return std::round(v * 1000.0) / 1000.0;
}

json vecJson(const glm::vec3& v) {
    return json::array({r3(v.x), r3(v.y), r3(v.z)});
}

std::string kindOf(const json& n) {
    return n.contains("kind") && n["kind"].is_string() ? n["kind"].get<std::string>() : std::string("sphere");
}

// Rigid enough to carry an entity frame: copies (repeat, mirror, fold, polar, screw, recurse) and bends
// (twist, bend) are not; displacements, the warp and the shell move a surface by a bounded amount.
bool isFrameBreaking(const std::string& k) {
    return k == "repeat" || k == "mirror" || k == "fold" || k == "polarRepeat" || k == "screw" ||
           k == "recurse" || k == "twist" || k == "bend";
}

float jf(const json& n, const char* key, float fallback) {
    return n.contains(key) && n[key].is_number() ? n[key].get<float>() : fallback;
}

// A conservative local bounding box of an SDF subtree, from the node semantics in spatial/sdf.hpp.
SpBox analyticBounds(const json& n, int depth = 0) {
    if (depth > 32 || !n.is_object()) return hugeBox();
    if (n.contains("enabled") && n["enabled"].is_boolean() && !n["enabled"].get<bool>()) return {};
    const std::string k = kindOf(n);
    const glm::vec3 size = vec3Of(n.value("size", json()), glm::vec3(1.0f));
    const float radius = jf(n, "radius", 1.0f);
    const float height = jf(n, "height", 2.0f);
    const float rounding = jf(n, "rounding", 0.1f);
    const auto& kids = n.contains("children") && n["children"].is_array() ? n["children"] : json::array();
    auto child = [&](std::size_t i) { return i < kids.size() ? analyticBounds(kids[i], depth + 1) : SpBox{}; };
    if (k == "sphere") return boxAround(radius, radius, radius);
    if (k == "box" || k == "roundedBox") return boxAround(std::abs(size.x), std::abs(size.y), std::abs(size.z));
    if (k == "cylinder" || k == "cone") return boxAround(radius, height * 0.5f, radius);
    if (k == "capsule") return boxAround(radius, height * 0.5f + radius, radius);
    if (k == "torus") return boxAround(radius + rounding, rounding, radius + rounding);
    if (k == "stairs" || k == "plane") return hugeBox();
    if (k == "union" || k == "morph" || k == "smoothUnion") {
        SpBox b;
        for (std::size_t i = 0; i < kids.size(); ++i) b.add(child(i));
        if (k == "smoothUnion" && b.valid()) {
            const float s = jf(n, "smooth", 0.5f);
            b.lo -= glm::vec3(s);
            b.hi += glm::vec3(s);
        }
        return b;
    }
    if (k == "intersection" || k == "smoothIntersection") {
        SpBox b = child(0);
        for (std::size_t i = 1; i < kids.size(); ++i) b = intersect(b, child(i));
        return b;
    }
    if (k == "difference" || k == "smoothDifference") return child(0);
    SpBox c = child(0);
    if (!c.valid()) return c;
    if (k == "translate") {
        const glm::vec3 t = vec3Of(n.value("translation", json()), glm::vec3(0.0f));
        c.lo += t;
        c.hi += t;
        return c;
    }
    if (k == "rotate") return transformBox(c, eulerMatrix(vec3Of(n.value("rotation", json()), glm::vec3(0.0f))));
    if (k == "scale") {
        const float s = jf(n, "scale", 1.0f);
        SpBox r;
        r.add(c.lo * s);
        r.add(c.hi * s);
        return r;
    }
    if (k == "shell") {
        const float o = std::abs(jf(n, "offset", 0.0f)) * 0.5f;
        c.lo -= glm::vec3(o);
        c.hi += glm::vec3(o);
        return c;
    }
    if (k == "displaceNoise" || k == "displaceVoronoi" || k == "displaceWave" || k == "displaceField" || k == "warp") {
        const float a = std::abs(jf(n, "amount", 0.0f));
        c.lo -= glm::vec3(a);
        c.hi += glm::vec3(a);
        return c;
    }
    if (k == "mirror") {
        SpBox r = c;
        for (int a = 0; a < 3; ++a) {
            if (size[a] > 0.0f) {
                const float m = std::max(std::abs(c.lo[a]), std::abs(c.hi[a]));
                r.lo[a] = -m;
                r.hi[a] = m;
            }
        }
        return r;
    }
    if (k == "repeat") {
        const int count = n.value("count", 0);
        if (count <= 0) return hugeBox();
        SpBox r = c;
        for (int a = 0; a < 3; ++a) {
            if (size[a] > 0.0f) {
                r.lo[a] -= size[a] * static_cast<float>(count);
                r.hi[a] += size[a] * static_cast<float>(count);
            }
        }
        return r;
    }
    if (k == "polarRepeat") {
        const float m = std::max({std::abs(c.lo.x), std::abs(c.hi.x), std::abs(c.lo.z), std::abs(c.hi.z)}) * 1.415f;
        SpBox r = c;
        r.lo.x = r.lo.z = -m;
        r.hi.x = r.hi.z = m;
        return r;
    }
    return hugeBox(); // twist, bend, fold, recurse, screw: unbounded as far as this is concerned
}

// ---- geometry with a frame ------------------------------------------------------------------------------

struct SpGeom {
    spatial::SdfTree tree;
    glm::mat4 world{1.0f};    // geometry-local -> world
    glm::mat4 inv{1.0f};
    float scale = 1.0f;       // uniform scale in `world` (distances scale by it)
    SpBox local;                // tight local bounds
    SpBox worldBox;
    bool ok = false;

    [[nodiscard]] float dist(const glm::vec3& w) const {
        const glm::vec3 p = glm::vec3(inv * glm::vec4(w, 1.0f));
        return tree.evaluate(p, 0.0) * scale;
    }
};

// Samples the analytic box on an n^3 grid: a cell whose centre is within its half diagonal of the surface
// may hold geometry, so the bounds of those cells (plus half a cell) contain the shape.
SpBox tightBounds(const spatial::SdfTree& tree, SpBox analytic, int n) {
    if (!analytic.valid()) return {};
    analytic.lo = glm::max(analytic.lo, glm::vec3(-200.0f));
    analytic.hi = glm::min(analytic.hi, glm::vec3(200.0f));
    if (!analytic.valid()) return {};
    const glm::vec3 ext = analytic.size();
    const glm::vec3 cell = glm::max(ext / static_cast<float>(n), glm::vec3(1e-4f));
    const float half = glm::length(cell) * 0.5f;
    SpBox out;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            for (int k = 0; k < n; ++k) {
                const glm::vec3 p = analytic.lo + cell * glm::vec3(i + 0.5f, j + 0.5f, k + 0.5f);
                if (tree.evaluate(p, 0.0) <= half) {
                    out.add(p - cell * 0.5f);
                    out.add(p + cell * 0.5f);
                }
            }
        }
    }
    return out.valid() ? intersect(out, analytic) : out;
}

// `clip` (geometry-local): the object's march bounds -- nothing outside them is drawn, so nothing outside them
// exists for the validator either (an infinite repeat is as long as the box it is marched in).
bool buildGeom(const json& node, const glm::mat4& world, float scale, int resolution, SpGeom& g,
               const SpBox* clip = nullptr) {
    auto parsed = spatial::SdfNode::fromJson(node);
    if (!parsed) return false;
    g.tree.root = std::move(*parsed);
    g.world = world;
    g.inv = glm::inverse(world);
    g.scale = scale;
    SpBox analytic = analyticBounds(node);
    const glm::vec3 ext = analytic.valid() ? analytic.size() : glm::vec3(0.0f);
    const bool unbounded = std::max({ext.x, ext.y, ext.z}) > 500.0f;
    if (unbounded && clip != nullptr && clip->valid()) analytic = intersect(analytic, *clip);
    g.local = tightBounds(g.tree, analytic, resolution);
    g.worldBox = transformBox(g.local, world);
    g.ok = g.local.valid();
    return true;
}

// The lowest (dir = +1: marching up from below) or highest (dir = -1) surface point over a grid of vertical
// columns: sphere tracing along the column, exact for the column. Returns kInf / -kInf when nothing is hit.
struct ColumnHit {
    float y = kInf;
    glm::vec3 at{0.0f};
    bool hit = false;
};

ColumnHit traceVertical(const SpGeom& g, int dir, int columns = 20) {
    ColumnHit best;
    if (!g.ok) return best;
    if (dir < 0) best.y = -kInf;
    const SpBox& b = g.worldBox;
    const float y0 = dir > 0 ? b.lo.y - 0.05f : b.hi.y + 0.05f;
    const float y1 = dir > 0 ? b.hi.y + 0.05f : b.lo.y - 0.05f;
    for (int i = 0; i < columns; ++i) {
        for (int j = 0; j < columns; ++j) {
            const float x = b.lo.x + (b.hi.x - b.lo.x) * (i + 0.5f) / static_cast<float>(columns);
            const float z = b.lo.z + (b.hi.z - b.lo.z) * (j + 0.5f) / static_cast<float>(columns);
            float y = y0;
            for (int s = 0; s < 160; ++s) {
                const float d = g.dist({x, y, z});
                if (d < 2e-4f) {
                    const bool better = dir > 0 ? y < best.y : y > best.y;
                    if (better) {
                        best.y = y;
                        best.at = {x, y, z};
                        best.hit = true;
                    }
                    break;
                }
                y += static_cast<float>(dir) * std::max(d, 2e-4f);
                if (dir > 0 ? y > y1 : y < y1) break;
            }
        }
    }
    return best;
}

struct SpOverlap {
    float depth = 0.0f;       // max over samples of min(-dA, -dB)
    float volume = 0.0f;
    glm::vec3 at{0.0f};
};

SpOverlap overlapOf(const SpGeom& a, const SpGeom& b, float minY = -kInf, float maxY = kInf, int maxCells = 40) {
    SpOverlap o;
    if (!a.ok || !b.ok) return o;
    SpBox box = intersect(a.worldBox, b.worldBox);
    box.lo.y = std::max(box.lo.y, minY);
    box.hi.y = std::min(box.hi.y, maxY);
    if (!box.valid()) return o;
    const glm::vec3 ext = box.size();
    const float step = std::clamp(std::max({ext.x, ext.y, ext.z}) / 24.0f, 0.008f, 0.06f);
    glm::ivec3 n = glm::max(glm::ivec3(glm::ceil(ext / step)), glm::ivec3(1));
    n = glm::min(n, glm::ivec3(maxCells));
    const glm::vec3 cell = ext / glm::vec3(n);
    const float cellVolume = cell.x * cell.y * cell.z;
    for (int i = 0; i < n.x; ++i) {
        for (int j = 0; j < n.y; ++j) {
            for (int k = 0; k < n.z; ++k) {
                const glm::vec3 p = box.lo + cell * glm::vec3(i + 0.5f, j + 0.5f, k + 0.5f);
                const float da = a.dist(p);
                if (da >= 0.0f) continue;
                const float db = b.dist(p);
                if (db >= 0.0f) continue;
                const float pen = std::min(-da, -db);
                o.volume += cellVolume;
                if (pen > o.depth) {
                    o.depth = pen;
                    o.at = p;
                }
            }
        }
    }
    return o;
}

// ---- the scene's model ----------------------------------------------------------------------------------

struct SpObject {
    std::string name;
    json treeJson;
    SpGeom geom;                 // the whole tree, in the world
    glm::mat4 world{1.0f};
    SpBox march;                 // the authored march bounds (tree-local)
    bool hasMarch = false;
};

struct SpPart {
    std::string name;
    SpGeom geom;
    glm::mat4 treeFrame{1.0f}; // part-local -> object-tree-local
    bool enabled = true;
};

struct SpEntity {
    std::string id;
    std::string category;
    std::string room;
    std::string anchor;
    std::string pose;
    json note;
    int object = -1;           // index into objects (-1: a composition node, e.g. text)
    std::string node;          // composition node name
    SpGeom geom;
    glm::mat4 treeFrame{1.0f}; // entity-local -> object-tree-local
    bool frameOk = true;
    std::vector<SpPart> parts;
    // text
    bool text = false;
    bool annotated = true; // text without an `entity` block: its kind is inferred (a wall it lies on, else floating)
    glm::vec2 rectLo{0.0f};
    glm::vec2 rectHi{0.0f};
    double t0 = -1.0;
    double t1 = -1.0;
    // derived
    glm::vec3 origin{0.0f};
    glm::vec3 front{0.0f, 0.0f, 1.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};
    bool mirrored = false;
    std::optional<glm::vec3> normal;
    std::optional<SpBox> interior; // rooms: world box of the interior
    std::optional<glm::vec3> hip;
    std::set<int> groups;
};

struct SpWall {
    std::string side;   // "-x", "+x", "-z", "+z"
    int axis = 0;       // 0 = x, 2 = z
    float coord = 0.0f; // the interior plane
    float sign = 1.0f;  // inward normal = sign * e_axis
    int uAxis = 2;
    float u0 = 0.0f, u1 = 0.0f, v0 = 0.0f, v1 = 0.0f;
    [[nodiscard]] glm::vec3 inward() const {
        glm::vec3 n{0.0f};
        n[axis] = sign;
        return n;
    }
    [[nodiscard]] float depth(const glm::vec3& p) const { return (p[axis] - coord) * sign; }
    [[nodiscard]] glm::vec3 point(float u, float v, float d) const {
        glm::vec3 p{0.0f};
        p[axis] = coord + sign * d;
        p[uAxis] = u;
        p.y = v;
        return p;
    }
};

std::vector<SpWall> wallsOf(const SpBox& in) {
    std::vector<SpWall> w;
    for (int s = 0; s < 4; ++s) {
        SpWall x;
        x.axis = s < 2 ? 0 : 2;
        x.uAxis = s < 2 ? 2 : 0;
        const bool minSide = (s % 2) == 0;
        x.coord = minSide ? in.lo[x.axis] : in.hi[x.axis];
        x.sign = minSide ? 1.0f : -1.0f;
        x.side = std::string(minSide ? "-" : "+") + (x.axis == 0 ? "x" : "z");
        x.u0 = in.lo[x.uAxis];
        x.u1 = in.hi[x.uAxis];
        x.v0 = in.lo.y;
        x.v1 = in.hi.y;
        w.push_back(x);
    }
    return w;
}

struct SpGroup {
    std::string name;
    std::vector<std::string> chapters;
    std::set<int> objects;
    std::vector<int> entities;
};

struct SpViolation {
    std::string severity; // ERROR, WARNING, INFO
    std::string rule;
    std::vector<std::string> ids;
    std::string message;
    std::string relationship;
    std::string expected;
    std::string suggestion;
    json measured = json::object();
    json fix;
    std::vector<std::string> groups;
};

struct SpCtx {
    const json& rules;
    const SpaceValidateOptions& options;
    std::vector<SpObject> objects;
    std::vector<SpEntity> entities;
    std::map<std::string, int> byId;
    std::vector<SpGroup> groups;
    std::vector<SpViolation> violations;
    std::map<std::string, std::size_t> seen;
    std::map<std::string, int> passes;
    std::vector<std::string> notes;

    [[nodiscard]] const json& cat(const std::string& name) const {
        static const json empty = json::object();
        const json& c = rules["categories"];
        return c.contains(name) ? c[name] : empty;
    }
    [[nodiscard]] float tol(const char* key) const {
        return rules["tolerances"].value(key, 0.0f);
    }
    [[nodiscard]] std::string groupOf(const std::string& category) const {
        return cat(category).value("group", std::string("decor"));
    }

    void add(SpViolation v, const std::string& group) {
        std::vector<std::string> sorted = v.ids;
        std::sort(sorted.begin(), sorted.end());
        std::string key = v.rule;
        for (const auto& s : sorted) key += "|" + s;
        if (auto it = seen.find(key); it != seen.end()) {
            auto& g = violations[it->second].groups;
            if (!group.empty() && std::find(g.begin(), g.end(), group) == g.end()) g.push_back(group);
            return;
        }
        if (!group.empty()) v.groups.push_back(group);
        seen[key] = violations.size();
        violations.push_back(std::move(v));
    }
    void pass(const std::string& kind) { ++passes[kind]; }
};

bool listHas(const json& v, const std::string& s) {
    if (v.is_string()) return v.get<std::string>() == s;
    if (v.is_array()) {
        for (const auto& x : v) {
            if (x.is_string() && x.get<std::string>() == s) return true;
        }
    }
    return false;
}

// Two entities that are never shown at the same time (both carry `t0`/`t1` and the spans do not overlap)
// are never checked against each other: the poses of one figure, a prop swapped for another.
bool coexist(const SpEntity& a, const SpEntity& b) {
    if (a.t0 < 0.0 || b.t0 < 0.0) return true;
    return a.t0 < b.t1 && b.t0 < a.t1;
}

bool presentAt(const SpEntity& e, double t) {
    return e.t0 < 0.0 || (t >= e.t0 && t < e.t1);
}

// ---- reading the scene ----------------------------------------------------------------------------------

void collectParts(SpCtx& ctx, const json& n, const glm::mat4& toTree, const glm::mat4& objWorld, float scale,
                  SpEntity& e, int depth) {
    if (!n.is_object() || depth > 32) return;
    glm::mat4 m = toTree;
    float s = scale;
    const std::string k = kindOf(n);
    if (n.contains("part") && n["part"].is_string()) {
        SpPart p;
        p.name = n["part"].get<std::string>();
        p.enabled = !(n.contains("enabled") && n["enabled"].is_boolean() && !n["enabled"].get<bool>());
        p.treeFrame = m;
        const SpBox clip = transformBox(ctx.objects[e.object].march, glm::inverse(m));
        buildGeom(n, objWorld * m, s, 16, p.geom, &clip);
        e.parts.push_back(std::move(p));
    }
    if (k == "translate") m = m * glm::translate(glm::mat4(1.0f), vec3Of(n.value("translation", json()), glm::vec3(0.0f)));
    if (k == "rotate") m = m * eulerMatrix(vec3Of(n.value("rotation", json()), glm::vec3(0.0f)));
    if (k == "scale") {
        const float f = jf(n, "scale", 1.0f);
        m = m * glm::scale(glm::mat4(1.0f), glm::vec3(f));
        s *= f;
    }
    if (n.contains("children") && n["children"].is_array()) {
        for (const auto& c : n["children"]) collectParts(ctx, c, m, objWorld, s, e, depth + 1);
    }
}

void fillEntityBasics(SpEntity& e, const json& note) {
    e.note = note;
    e.id = note.value("id", std::string());
    e.category = note.value("category", std::string("prop"));
    e.room = note.value("room", std::string());
    e.anchor = note.value("anchor", std::string());
    e.pose = note.value("pose", std::string());
    e.t0 = note.value("t0", -1.0);
    e.t1 = note.value("t1", -1.0);
}

void walkTree(SpCtx& ctx, int objectIndex, const json& n, const json* parent, int childIndex, const glm::mat4& toTree,
              float scale, bool frameOk, int depth) {
    if (!n.is_object() || depth > 32) return;
    const SpObject& obj = ctx.objects[objectIndex];
    if (n.contains("entity") && n["entity"].is_object() && n["entity"].contains("category")) {
        SpEntity e;
        fillEntityBasics(e, n["entity"]);
        e.object = objectIndex;
        e.node = obj.name;
        e.frameOk = frameOk;
        e.treeFrame = toTree;
        // A solid's cuts belong to it: the first child of a difference takes the difference.
        const json& geomJson =
            (parent != nullptr && childIndex == 0 && kindOf(*parent) == "difference") ? *parent : n;
        const bool big = e.category == "room";
        const SpBox clip = transformBox(obj.march, glm::inverse(toTree));
        buildGeom(geomJson, obj.world * toTree, scale, big ? 40 : 28, e.geom, &clip);
        collectParts(ctx, n, toTree, obj.world, scale, e, depth);
        if (e.id.empty()) e.id = fmt::format("{}_{}", e.category, ctx.entities.size() + 1);
        ctx.entities.push_back(std::move(e));
    }
    glm::mat4 m = toTree;
    float s = scale;
    bool ok = frameOk;
    const std::string k = kindOf(n);
    if (k == "translate") m = m * glm::translate(glm::mat4(1.0f), vec3Of(n.value("translation", json()), glm::vec3(0.0f)));
    else if (k == "rotate") m = m * eulerMatrix(vec3Of(n.value("rotation", json()), glm::vec3(0.0f)));
    else if (k == "scale") {
        const float f = jf(n, "scale", 1.0f);
        m = m * glm::scale(glm::mat4(1.0f), glm::vec3(f));
        s *= f;
    } else if (isFrameBreaking(k)) {
        ok = false;
    }
    if (n.contains("children") && n["children"].is_array()) {
        int i = 0;
        for (const auto& c : n["children"]) walkTree(ctx, objectIndex, c, &n, i++, m, s, ok, depth + 1);
    }
}

glm::mat4 nodeWorld(const json& node, float* uniformScale, bool* mirrored) {
    const glm::vec3 pos = vec3Of(node.value("position", json()), glm::vec3(0.0f));
    const glm::vec3 rot = vec3Of(node.value("rotation", json()), glm::vec3(0.0f));
    const glm::vec3 sc = vec3Of(node.value("scale", json()), glm::vec3(1.0f));
    if (uniformScale != nullptr) *uniformScale = std::abs(sc.x);
    if (mirrored != nullptr) *mirrored = sc.x * sc.y * sc.z < 0.0f;
    return glm::translate(glm::mat4(1.0f), pos) * eulerMatrix(rot) * glm::scale(glm::mat4(1.0f), sc);
}

void readTextNode(SpCtx& ctx, const json& node) {
    const json& src = node["procedural"]["source"];
    SpEntity e;
    json note = node.contains("entity") && node["entity"].is_object() ? node["entity"] : json::object();
    e.annotated = note.contains("category");
    if (!note.contains("category")) note["category"] = "wallText";
    if (!note.contains("id")) note["id"] = node.value("name", std::string("text"));
    if (src.contains("textTracking") && src["textTracking"].is_number()) note["tracking"] = src["textTracking"];
    fillEntityBasics(e, note);
    e.node = node.value("name", std::string());
    e.text = true;
    float sc = 1.0f;
    glm::mat4 world = nodeWorld(node, &sc, &e.mirrored);
    // A word that rises into place is authored below its rest position (liminal_text's `rise` style).
    if (note.contains("offset")) world = glm::translate(glm::mat4(1.0f), vec3Of(note["offset"], glm::vec3(0.0f))) * world;
    e.geom.world = world;
    e.geom.inv = glm::inverse(world);
    e.geom.scale = sc;
    // The glyph mesh's bounds (ADR-1046): the word's real rectangle.
    const float size = jf(src, "textSize", 1.0f);
    const float depth = jf(src, "textDepth", 0.08f);
    const std::string text = src.value("text", std::string());
    SpBox local;
    bool measured = false;
    if (ctx.options.text && !text.empty()) {
        TextMeshSpec spec;
        spec.text = text;
        spec.size = size;
        spec.depth = depth;
        spec.tracking = jf(src, "textTracking", 0.0f);
        const int align = src.value("textAlign", 1);
        spec.align = align == 0 ? TextAlign3d::Left : align == 2 ? TextAlign3d::Right : TextAlign3d::Center;
        spec.valign = src.value("textVAlign", 0) == 1 ? TextVAlign3d::Baseline : TextVAlign3d::Middle;
        if (src.contains("font")) {
            if (auto f = comp::FontDesc::fromJson(src["font"])) spec.font = *f;
        }
        if (auto mesh = cachedTextMesh(spec); mesh && *mesh && !(*mesh)->vertices.empty()) {
            const auto [lo, hi] = (*mesh)->bounds();
            local.lo = lo;
            local.hi = hi;
            measured = true;
        }
    }
    if (!measured) {
        // An estimate: 0.6 em per character, one em tall, centred.
        const float w = 0.6f * size * static_cast<float>(std::max<std::size_t>(1, text.size()));
        local.lo = {-w * 0.5f, -size * 0.5f, 0.0f};
        local.hi = {w * 0.5f, size * 0.5f, depth * size};
    }
    e.rectLo = {local.lo.x, local.lo.y};
    e.rectHi = {local.hi.x, local.hi.y};
    e.geom.local = local;
    e.geom.worldBox = transformBox(local, world);
    e.geom.ok = false; // no SDF: text is checked by its rectangle
    ctx.entities.push_back(std::move(e));
}

void deriveFrames(SpCtx& ctx) {
    for (SpEntity& e : ctx.entities) {
        const glm::mat4& w = e.geom.world;
        e.origin = glm::vec3(w * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        e.front = glm::normalize(glm::vec3(w * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
        e.up = glm::normalize(glm::vec3(w * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
        if (e.note.contains("normal")) {
            const glm::vec3 n = vec3Of(e.note["normal"], glm::vec3(0.0f));
            if (glm::length(n) > 1e-6f) e.normal = glm::normalize(n);
        }
        if (e.category == "room") {
            SpBox in;
            if (e.note.contains("interior") && e.note["interior"].is_array() && e.note["interior"].size() == 3) {
                const json& x = e.note["interior"];
                SpBox local;
                local.lo = {x[0][0].get<float>(), x[1][0].get<float>(), x[2][0].get<float>()};
                local.hi = {x[0][1].get<float>(), x[1][1].get<float>(), x[2][1].get<float>()};
                in = transformBox(local, w);
            } else if (e.geom.ok) {
                const float wall = e.note.value("wall", 0.15f);
                in = e.geom.worldBox;
                in.lo += glm::vec3(wall);
                in.hi -= glm::vec3(wall);
            }
            if (in.valid()) e.interior = in;
        }
        if (e.note.contains("hip")) {
            e.hip = glm::vec3(w * glm::vec4(vec3Of(e.note["hip"], glm::vec3(0.0f)), 1.0f));
        }
    }
    for (std::size_t i = 0; i < ctx.entities.size(); ++i) {
        auto [it, inserted] = ctx.byId.emplace(ctx.entities[i].id, static_cast<int>(i));
        if (!inserted) {
            ctx.notes.push_back(fmt::format("duplicate entity id '{}' (the first is used for anchors)",
                                            ctx.entities[i].id));
        }
    }
}

// ---- groups: what is shown together ---------------------------------------------------------------------

void buildGroups(SpCtx& ctx, const json& scene) {
    std::map<std::string, int> objectByName;
    for (std::size_t i = 0; i < ctx.objects.size(); ++i) objectByName[ctx.objects[i].name] = static_cast<int>(i);
    std::vector<std::pair<std::string, std::set<int>>> chapters;
    std::set<int> listed;
    const json* journey = nullptr;
    if (scene.contains("camera") && scene["camera"].is_object() && scene["camera"].contains("journey")) {
        journey = &scene["camera"]["journey"];
    }
    if (journey != nullptr && journey->contains("chapters") && (*journey)["chapters"].is_array()) {
        for (const auto& c : (*journey)["chapters"]) {
            std::set<int> objs;
            if (c.contains("nodes") && c["nodes"].is_array()) {
                for (const auto& n : c["nodes"]) {
                    if (!n.is_string()) continue;
                    if (auto it = objectByName.find(n.get<std::string>()); it != objectByName.end()) {
                        objs.insert(it->second);
                        listed.insert(it->second);
                    }
                }
            }
            chapters.emplace_back(c.value("name", std::string("chapter")), objs);
        }
    }
    std::set<int> global;
    for (std::size_t i = 0; i < ctx.objects.size(); ++i) {
        if (!listed.count(static_cast<int>(i))) global.insert(static_cast<int>(i));
    }
    if (chapters.empty()) {
        SpGroup g;
        g.name = "scene";
        g.objects = global;
        ctx.groups.push_back(g);
    } else {
        for (auto& [name, objs] : chapters) {
            std::set<int> all = objs;
            all.insert(global.begin(), global.end());
            auto same = std::find_if(ctx.groups.begin(), ctx.groups.end(), [&](const SpGroup& g) { return g.objects == all; });
            if (same != ctx.groups.end()) {
                same->chapters.push_back(name);
                continue;
            }
            SpGroup g;
            g.name = name;
            g.chapters.push_back(name);
            g.objects = all;
            ctx.groups.push_back(g);
        }
    }
    for (std::size_t gi = 0; gi < ctx.groups.size(); ++gi) {
        SpGroup& g = ctx.groups[gi];
        for (std::size_t i = 0; i < ctx.entities.size(); ++i) {
            SpEntity& e = ctx.entities[i];
            if (e.object >= 0 && g.objects.count(e.object)) {
                g.entities.push_back(static_cast<int>(i));
                e.groups.insert(static_cast<int>(gi));
            }
        }
    }
}

// The rooms of a group, and the one an entity is in.
std::vector<int> roomsIn(const SpCtx& ctx, const SpGroup& g) {
    std::vector<int> r;
    for (int i : g.entities) {
        if (ctx.entities[i].category == "room" && ctx.entities[i].interior) r.push_back(i);
    }
    return r;
}

int roomFor(const SpCtx& ctx, const SpGroup& g, const SpEntity& e) {
    const auto rooms = roomsIn(ctx, g);
    if (!e.room.empty()) {
        for (int r : rooms) {
            if (ctx.entities[r].id == e.room) return r;
        }
    }
    const glm::vec3 c = e.geom.worldBox.valid() ? e.geom.worldBox.centre() : e.origin;
    int best = -1;
    float bestVolume = kInf;
    for (int r : rooms) {
        const SpBox& in = *ctx.entities[r].interior;
        if (in.contains(c, ctx.tol("roomSlack")) && in.volume() < bestVolume) {
            best = r;
            bestVolume = in.volume();
        }
    }
    return best;
}

std::string fmtM(float v) {
    return fmt::format("{:.2f}m", v);
}

// ---- checks ---------------------------------------------------------------------------------------------

// Required parts, degenerate parts, and geometry the object's march bounds clip away (it is never drawn).
void checkIntegrity(SpCtx& ctx) {
    for (const SpEntity& e : ctx.entities) {
        if (e.text) continue;
        const json& c = ctx.cat(e.category);
        if (!e.frameOk) {
            ctx.add({"INFO", "frame", {e.id}, fmt::format("{} sits under a repeat/mirror/fold; its placement is not checked", e.id)}, "");
        }
        if (e.object >= 0 && !e.geom.ok) {
            ctx.add({"ERROR", "integrity", {e.id},
                     fmt::format("{} has no geometry (its SDF subtree is empty or degenerate)", e.id), "", "",
                     "check the node's sizes and that it is enabled"},
                    "");
            continue;
        }
        const SpObject* obj = e.object >= 0 ? &ctx.objects[e.object] : nullptr;
        auto clipped = [&](const SpGeom& g, const glm::mat4& treeFrame) -> std::pair<float, SpBox> {
            if (obj == nullptr || !obj->hasMarch || !g.ok) return {0.0f, {}};
            const SpBox inTree = transformBox(g.local, treeFrame);
            const SpBox kept = intersect(inTree, obj->march);
            const float v = inTree.volume();
            const float keptV = kept.valid() ? kept.volume() : 0.0f;
            return {v > 0.0f ? 1.0f - keptV / v : 0.0f, inTree};
        };
        bool ok = true;
        if (c.contains("requires") && c["requires"].is_array()) {
            for (const auto& rq : c["requires"]) {
                const std::string need = rq.get<std::string>();
                const SpPart* part = nullptr;
                for (const SpPart& p : e.parts) {
                    if (p.name == need) part = &p;
                }
                if (part == nullptr) {
                    // The body is the entity itself when no part claims to be it.
                    if (need == "body" && e.geom.ok) continue;
                    ctx.add({"ERROR", "integrity", {e.id}, fmt::format("{}: missing {} geometry", e.id, need),
                             "expected components: " + c["requires"].dump(), "", "",
                             json{{"missing", need}}},
                            "");
                    ok = false;
                    continue;
                }
                if (!part->enabled || !part->geom.ok || part->geom.local.volume() < 1e-7f) {
                    ctx.add({"ERROR", "integrity", {e.id},
                             fmt::format("{}: {} is disabled or degenerate (no volume)", e.id, need), "", "", "",
                             json{{"part", need}}},
                            "");
                    ok = false;
                    continue;
                }
                const auto [out, inTree] = clipped(part->geom, part->treeFrame);
                if (out > 0.02f) {
                    SpBox grown = obj->march;
                    grown.add(inTree);
                    SpViolation v{out > 0.98f ? "ERROR" : "WARNING", "integrity", {e.id},
                                fmt::format("{}: {} geometry is {:.0f}% outside the march bounds of '{}' (never drawn)",
                                            e.id, need, out * 100.0f, obj->name),
                                "", "every component inside its SDF object's boundsMin/boundsMax",
                                fmt::format("grow '{}' bounds to min {} max {}", obj->name, vecJson(grown.lo).dump(),
                                            vecJson(grown.hi).dump()),
                                json{{"part", need}, {"outside", r3(out)}}};
                    v.fix = json{{"object", obj->name}, {"boundsMin", vecJson(grown.lo - glm::vec3(0.05f))},
                                 {"boundsMax", vecJson(grown.hi + glm::vec3(0.05f))}};
                    ctx.add(std::move(v), "");
                    ok = false;
                }
            }
        }
        const auto [out, inTree] = clipped(e.geom, e.treeFrame);
        if (out > 0.02f && e.category != "room") {
            SpBox grown = obj->march;
            grown.add(inTree);
            SpViolation v{out > 0.98f ? "ERROR" : "WARNING", "clipped", {e.id},
                        fmt::format("{} is {:.0f}% outside the march bounds of '{}' (that part is never drawn)", e.id,
                                    out * 100.0f, obj->name),
                        "", "", "grow the object's bounds", json{{"outside", r3(out)}}};
            v.fix = json{{"object", obj->name}, {"boundsMin", vecJson(grown.lo - glm::vec3(0.05f))},
                         {"boundsMax", vecJson(grown.hi + glm::vec3(0.05f))}};
            ctx.add(std::move(v), "");
            ok = false;
        }
        if (ok) ctx.pass("integrity");
    }
}

// The same clip check without annotations: each top-level piece of every object (the children under its
// root unions) that lies wholly or mostly outside the march bounds.
void checkObjectPieces(SpCtx& ctx) {
    for (const SpObject& obj : ctx.objects) {
        if (!obj.hasMarch) continue;
        std::vector<std::pair<const json*, glm::mat4>> stack{{&obj.treeJson, glm::mat4(1.0f)}};
        int piece = 0;
        while (!stack.empty()) {
            auto [n, m] = stack.back();
            stack.pop_back();
            const std::string k = kindOf(*n);
            if ((k == "union" || k == "translate" || k == "rotate") && n->contains("children")) {
                glm::mat4 mm = m;
                if (k == "translate") mm = m * glm::translate(glm::mat4(1.0f), vec3Of(n->value("translation", json()), glm::vec3(0.0f)));
                if (k == "rotate") mm = m * eulerMatrix(vec3Of(n->value("rotation", json()), glm::vec3(0.0f)));
                if (k == "union") {
                    for (const auto& c : (*n)["children"]) stack.emplace_back(&c, mm);
                    continue;
                }
                // a placement: only descend if it wraps a union (a prop); otherwise it is a piece
                const json& c = (*n)["children"][0];
                const std::string ck = kindOf(c);
                if (ck == "union" || ck == "translate" || ck == "rotate") {
                    stack.emplace_back(&c, mm);
                    continue;
                }
            }
            ++piece;
            auto parsed = spatial::SdfNode::fromJson(*n);
            if (!parsed) continue;
            spatial::SdfTree t;
            t.root = std::move(*parsed);
            SpBox analytic = analyticBounds(*n);
            if (analytic.valid() && std::max({analytic.size().x, analytic.size().y, analytic.size().z}) > 500.0f) {
                analytic = intersect(analytic, transformBox(obj.march, glm::inverse(m)));
            }
            const SpBox local = tightBounds(t, analytic, 10);
            if (!local.valid()) continue;
            const SpBox inTree = transformBox(local, m);
            const SpBox kept = intersect(inTree, obj.march);
            const float v = inTree.volume();
            const float out = v > 0.0f ? 1.0f - (kept.valid() ? kept.volume() : 0.0f) / v : 0.0f;
            if (out > 0.5f && inTree.size().y < 50.0f) {
                std::string label = n->value("name", std::string());
                if (label.empty() && n->contains("entity")) label = (*n)["entity"].value("id", std::string());
                SpBox grown = obj.march;
                grown.add(inTree);
                SpViolation viol{out > 0.98f ? "ERROR" : "WARNING", "clipped", {obj.name + (label.empty() ? fmt::format("#{}", piece) : "/" + label)},
                               fmt::format("a piece of '{}' at {} is {:.0f}% outside its march bounds (never drawn)",
                                           obj.name, vecJson(inTree.centre()).dump(), out * 100.0f),
                               "", "", fmt::format("grow '{}' bounds", obj.name), json{{"outside", r3(out)}}};
                viol.fix = json{{"object", obj.name}, {"boundsMin", vecJson(grown.lo - glm::vec3(0.05f))},
                                {"boundsMax", vecJson(grown.hi + glm::vec3(0.05f))}};
                ctx.add(std::move(viol), "");
            }
        }
    }
}

bool mayIntersect(const SpCtx& ctx, const SpEntity& a, const SpEntity& b) {
    auto allows = [&](const SpEntity& x, const SpEntity& y) {
        const json& c = ctx.cat(x.category);
        if (!c.contains("mayIntersect")) return false;
        const json& m = c["mayIntersect"];
        return listHas(m, "*") || listHas(m, y.category) || listHas(m, ctx.groupOf(y.category));
    };
    return allows(a, b) || allows(b, a);
}

std::string expectationFor(const SpCtx& ctx, const SpEntity& a, const SpEntity& b) {
    for (const SpEntity* e : {&a, &b}) {
        const json& c = ctx.cat(e->category);
        if (c.contains("expect")) return c["expect"].get<std::string>();
    }
    return "no two solid objects should occupy the same volume";
}

void checkIntersections(SpCtx& ctx, const SpGroup& g, const std::string& gname) {
    const float contact = ctx.tol("contact");
    for (std::size_t ii = 0; ii < g.entities.size(); ++ii) {
        for (std::size_t jj = ii + 1; jj < g.entities.size(); ++jj) {
            const SpEntity& a = ctx.entities[g.entities[ii]];
            const SpEntity& b = ctx.entities[g.entities[jj]];
            if (!a.geom.ok || !b.geom.ok || !a.frameOk || !b.frameOk) continue;
            if (a.category == "room" && b.category == "room") continue;
            if (!coexist(a, b)) continue;
            if (!intersect(a.geom.worldBox, b.geom.worldBox).valid()) continue;
            const bool anchored = (!a.anchor.empty() && a.anchor == b.id) || (!b.anchor.empty() && b.anchor == a.id);
            if (mayIntersect(ctx, a, b)) {
                // ADR-1056: expected, so informational -- reported, never an error.
                if (a.category == "room" || b.category == "room") continue; // a window in its wall, a lamp through a ceiling
                const SpOverlap o = overlapOf(a.geom, b.geom, -kInf, kInf, 12);
                if (o.depth > contact) {
                    ctx.add({"INFO", "expectedIntersection", {a.id, b.id},
                             fmt::format("{} overlaps {} by {} (expected: {} may intersect {})", a.id, b.id, fmtM(o.depth), a.category,
                                         b.category),
                             "intended", "", "", json{{"penetration", r3(o.depth)}}},
                            gname);
                }
                continue;
            }
            // Against a room, the floor is the floor check's business: sample above it.
            float minY = -kInf;
            const SpEntity* room = a.category == "room" ? &a : b.category == "room" ? &b : nullptr;
            if (room != nullptr && room->interior) minY = room->interior->lo.y + contact;
            const SpOverlap o = overlapOf(a.geom, b.geom, minY);
            const float allowed = anchored ? ctx.tol("anchorPenetration") : contact;
            if (o.depth <= allowed) {
                if (anchored && o.depth > contact) {
                    const SpEntity& child = !a.anchor.empty() && a.anchor == b.id ? a : b;
                    const SpEntity& parent = &child == &a ? b : a;
                    ctx.add({"INFO", "expectedIntersection", {child.id, parent.id},
                             fmt::format("{} overlaps its anchor {} by {} (within the {} allowed a child)", child.id, parent.id,
                                         fmtM(o.depth), fmtM(allowed)),
                             "anchored", "", "", json{{"penetration", r3(o.depth)}}},
                            gname);
                }
                ctx.pass("placement");
                continue;
            }
            // The one to move: never the architecture; otherwise the smaller of the two.
            const bool aFixed = a.category == "room" || ctx.groupOf(a.category) == "architecture";
            const bool bFixed = b.category == "room" || ctx.groupOf(b.category) == "architecture";
            const SpEntity& mover = aFixed ? b : bFixed ? a : (a.geom.worldBox.volume() <= b.geom.worldBox.volume() ? a : b);
            const SpEntity& other = &mover == &a ? b : a;
            SpViolation v;
            v.severity = o.depth >= ctx.tol("intersectError") ? "ERROR" : "WARNING";
            v.rule = "intersection";
            v.ids = {mover.id, other.id};
            const bool wall = other.category == "room";
            v.message = wall ? fmt::format("{} intersects the walls of {}", mover.id, other.id)
                             : fmt::format("{} intersects {}", mover.id, other.id);
            v.relationship = anchored ? "anchored" : "separate";
            v.expected = expectationFor(ctx, a, b);
            v.measured = json{{"penetration", r3(o.depth)}, {"overlapVolume", r3(o.volume)}, {"at", vecJson(o.at)}};
            // Push the mover out along the shallower horizontal axis of the box overlap, away from the other.
            const SpBox ov = intersect(mover.geom.worldBox, other.geom.worldBox);
            glm::vec3 delta{0.0f};
            if (wall && other.interior) {
                const SpBox& in = *other.interior;
                const SpBox& mb = mover.geom.worldBox;
                const float pushes[4] = {in.lo.x + 0.01f - mb.lo.x, in.hi.x - 0.01f - mb.hi.x, in.lo.z + 0.01f - mb.lo.z,
                                         in.hi.z - 0.01f - mb.hi.z};
                if (pushes[0] > 0.0f) delta.x = pushes[0];
                if (pushes[1] < 0.0f) delta.x = pushes[1];
                if (pushes[2] > 0.0f) delta.z = pushes[2];
                if (pushes[3] < 0.0f) delta.z = pushes[3];
                if (mb.hi.y > in.hi.y) delta.y = in.hi.y - 0.01f - mb.hi.y;
            } else {
                const glm::vec3 s = ov.size();
                const glm::vec3 dir = mover.geom.worldBox.centre() - other.geom.worldBox.centre();
                if (s.x <= s.z) delta.x = (dir.x >= 0.0f ? 1.0f : -1.0f) * (s.x + 0.02f);
                else delta.z = (dir.z >= 0.0f ? 1.0f : -1.0f) * (s.z + 0.02f);
            }
            if (glm::length(delta) > 0.0f) {
                v.suggestion = fmt::format("move {} by {} ({})", mover.id, vecJson(delta).dump(), fmtM(glm::length(delta)));
                v.fix = json{{"id", mover.id}, {"translate", vecJson(delta)}};
            }
            ctx.add(std::move(v), gname);
        }
    }
}

std::vector<std::string> supportsOf(const json& c, const char* key) {
    std::vector<std::string> r;
    if (!c.contains(key)) return r;
    if (c[key].is_string()) r.push_back(c[key].get<std::string>());
    if (c[key].is_array()) {
        for (const auto& x : c[key]) r.push_back(x.get<std::string>());
    }
    return r;
}

// Is something (another entity) directly under the entity's lowest face? Probes a grid over its footprint at
// the height of its lowest point (where the entity itself is there), against every other entity.
bool supportedBy(const SpCtx& ctx, const SpGroup& g, const SpEntity& e, const glm::vec3& at, std::string* by) {
    const float gap = ctx.tol("supportGap");
    const SpBox& b = e.geom.worldBox;
    std::vector<glm::vec3> probes{at};
    for (int i = 0; i < 9; ++i) {
        for (int j = 0; j < 9; ++j) {
            const glm::vec3 p{b.lo.x + (b.hi.x - b.lo.x) * (i + 0.5f) / 9.0f, at.y + 0.004f,
                              b.lo.z + (b.hi.z - b.lo.z) * (j + 0.5f) / 9.0f};
            if (e.geom.dist(p) <= 0.008f) probes.push_back(p);
        }
    }
    for (int j : g.entities) {
        const SpEntity& o = ctx.entities[j];
        if (&o == &e || !o.geom.ok || o.category == "room") continue;
        for (const glm::vec3& q : probes) {
            if (!o.geom.worldBox.contains(q, gap + 0.02f)) continue;
            for (float dy : {0.01f, gap * 0.5f, gap}) {
                if (o.geom.dist(q - glm::vec3(0.0f, dy, 0.0f)) <= 0.004f) {
                    if (by != nullptr) *by = o.id;
                    return true;
                }
            }
        }
    }
    return false;
}

void floorContact(SpCtx& ctx, const SpGroup& g, const std::string& gname, const SpEntity& e, float floorY,
                  const std::string& roomId, bool character) {
    const ColumnHit bottom = traceVertical(e.geom, +1);
    if (!bottom.hit) return;
    const float gap = bottom.y - floorY;
    SpViolation v;
    v.ids = {e.id};
    v.relationship = "rests on the floor of " + roomId;
    if (gap > ctx.tol("floatWarn")) {
        std::string by;
        if (supportedBy(ctx, g, e, bottom.at, &by)) {
            ctx.pass("placement");
            return; // it stands on something (a rug, a step)
        }
        v.severity = gap > ctx.tol("floatError") && !character ? "ERROR" : "WARNING";
        v.rule = "floor";
        v.message = fmt::format("{}: floor clearance = {} ({} appears to float)", e.id, fmtM(gap), e.category);
        v.measured = json{{"clearance", r3(gap)}};
        v.suggestion = fmt::format("lower {} by {}", e.id, fmtM(gap));
        v.fix = json{{"id", e.id}, {"translate", json::array({0.0, r3(-gap), 0.0})}};
        ctx.add(std::move(v), gname);
        return;
    }
    if (-gap > ctx.tol("floorPenetration")) {
        v.severity = -gap > ctx.tol("floorPenetrationError") && !character ? "ERROR" : "WARNING";
        v.rule = "floor";
        v.message = character ? fmt::format("{}: foot penetration {}", e.id, fmtM(-gap))
                              : fmt::format("{}: floor penetration = {}", e.id, fmtM(-gap));
        v.measured = json{{"penetration", r3(-gap)}};
        v.suggestion = fmt::format("raise {} by {}", e.id, fmtM(-gap));
        v.fix = json{{"id", e.id}, {"translate", json::array({0.0, r3(-gap), 0.0})}};
        ctx.add(std::move(v), gname);
        return;
    }
    ctx.pass("placement");
}

void checkPlacement(SpCtx& ctx, const SpGroup& g, const std::string& gname) {
    for (int i : g.entities) {
        const SpEntity& e = ctx.entities[i];
        if (e.text || !e.geom.ok || !e.frameOk || e.category == "room") continue;
        const json& c = ctx.cat(e.category);
        const int r = roomFor(ctx, g, e);
        const SpEntity* room = r >= 0 ? &ctx.entities[r] : nullptr;
        const auto rests = supportsOf(c, "rests");
        const auto mounts = supportsOf(c, "mounts");
        const auto embeds = supportsOf(c, "embeds");
        const bool restsFloor = std::find(rests.begin(), rests.end(), "floor") != rests.end();
        const bool restsSurface = std::find(rests.begin(), rests.end(), "surface") != rests.end();
        const bool restsWall = std::find(rests.begin(), rests.end(), "wall") != rests.end();
        // Tilt: things that stand should stand up.
        if (!rests.empty() && rests != std::vector<std::string>{"wall"}) {
            const float tilt = angleBetween(e.up, {0.0f, 1.0f, 0.0f});
            if (tilt > ctx.tol("tilt") && !e.note.value("tilted", false)) {
                ctx.add({"WARNING", "tilt", {e.id}, fmt::format("{} is tilted {:.0f} degrees from upright", e.id, tilt),
                         "", "", "stand it upright, or mark the entity \"tilted\": true",
                         json{{"tilt", r3(tilt)}}},
                        gname);
            }
        }
        // Walls first: a wall-mounted thing needs a wall behind it.
        auto wallCheck = [&](bool embedded, bool optional) -> bool {
            if (room == nullptr) {
                if (!optional) {
                    ctx.add({"WARNING", "support", {e.id},
                             fmt::format("{} has no valid support relationship (no room contains it)", e.id)},
                            gname);
                }
                return false;
            }
            const auto walls = wallsOf(*room->interior);
            const glm::vec3 facing = e.normal.value_or(e.front);
            const glm::vec3 ref = e.normal ? e.geom.worldBox.centre() : e.origin;
            const SpWall* best = nullptr;
            float bestDepth = kInf;
            float bestAngle = 180.0f;
            for (const SpWall& w : walls) {
                const float d = std::abs(w.depth(ref));
                const float a = angleBetween(facing, w.inward());
                if (a < 45.0f && d < bestDepth) {
                    best = &w;
                    bestDepth = d;
                    bestAngle = a;
                }
            }
            const float maxD = embedded ? ctx.tol("embedDepth") : ctx.tol("wallDistance");
            if (best == nullptr || bestDepth > maxD || bestAngle > ctx.tol("wallAngle")) {
                if (optional) return false;
                SpViolation v{"WARNING", "support", {e.id, room->id},
                            best == nullptr
                                ? fmt::format("{} has no valid support relationship: it faces no wall of {}", e.id, room->id)
                                : fmt::format("{} has no valid support relationship: {} from wall {} of {}, facing {:.0f} degrees off",
                                              e.id, fmtM(bestDepth), best->side, room->id, bestAngle)};
                v.relationship = embedded ? "embedded in a wall" : "mounted on a wall";
                if (best != nullptr) {
                    const float d = best->depth(ref);
                    v.measured = json{{"wallDistance", r3(d)}, {"angle", r3(bestAngle)}, {"wall", best->side}};
                    glm::vec3 delta = -best->inward() * d;
                    v.suggestion = fmt::format("move {} onto wall {} ({})", e.id, best->side, fmtM(std::abs(d)));
                    v.fix = json{{"id", e.id}, {"translate", vecJson(delta)}};
                }
                ctx.add(std::move(v), gname);
                return false;
            }
            // within the wall's extent
            const SpBox& b = e.geom.worldBox;
            const float over = std::max({best->u0 - b.lo[best->uAxis], b.hi[best->uAxis] - best->u1, best->v0 - b.lo.y,
                                         b.hi.y - best->v1, 0.0f});
            if (over > 0.03f) {
                ctx.add({embedded ? "ERROR" : "WARNING", "wallExtent", {e.id, room->id},
                         fmt::format("{} extends {} beyond wall {} of {}", e.id, fmtM(over), best->side, room->id),
                         "", "inside the wall's rectangle", "", json{{"beyond", r3(over)}}},
                        gname);
                return false;
            }
            if (c.value("opening", std::string()) == "door") {
                const float bottomGap = b.lo.y - room->interior->lo.y;
                if (std::abs(bottomGap) > 0.06f) {
                    ctx.add({"WARNING", "door", {e.id}, fmt::format("{} does not reach the floor (gap {})", e.id, fmtM(bottomGap)),
                             "a door stands on the floor", "", "", json{{"gap", r3(bottomGap)}}},
                            gname);
                    return false;
                }
            }
            return true;
        };
        auto ceilingCheck = [&]() {
            if (room == nullptr) return;
            const ColumnHit top = traceVertical(e.geom, -1);
            if (!top.hit) return;
            const float gap = room->interior->hi.y - top.y;
            if (std::abs(gap) > ctx.tol("wallDistance")) {
                ctx.add({"WARNING", "support", {e.id, room->id},
                         gap > 0 ? fmt::format("{} hangs {} below the ceiling of {} (not attached)", e.id, fmtM(gap), room->id)
                                 : fmt::format("{} pokes {} through the ceiling of {}", e.id, fmtM(-gap), room->id),
                         "mounted on the ceiling", "", "", json{{"gap", r3(gap)}},
                         json{{"id", e.id}, {"translate", json::array({0.0, r3(gap), 0.0})}}},
                        gname);
            } else {
                ctx.pass("placement");
            }
        };
        if (!embeds.empty()) {
            if (wallCheck(true, false)) ctx.pass("architecture");
            continue;
        }
        if (std::find(mounts.begin(), mounts.end(), "wall") != mounts.end()) {
            if (wallCheck(false, false)) ctx.pass("placement");
            continue;
        }
        if (std::find(mounts.begin(), mounts.end(), "ceiling") != mounts.end()) {
            ceilingCheck();
            continue;
        }
        if (ctx.groupOf(e.category) == "character") continue; // poses own the character's support
        if (restsWall && wallCheck(false, true)) {
            ctx.pass("placement");
            continue;
        }
        if (restsFloor || restsSurface) {
            const ColumnHit bottom = traceVertical(e.geom, +1);
            if (!bottom.hit) continue;
            const float floorY = room != nullptr ? room->interior->lo.y : 0.0f;
            const bool nearFloor = std::abs(bottom.y - floorY) <= ctx.tol("floatWarn") ||
                                   (bottom.y < floorY && restsFloor);
            if (restsFloor && (nearFloor || !restsSurface)) {
                if (room == nullptr && !e.note.contains("floorY")) continue; // no floor known
                const float fy = e.note.contains("floorY") ? e.note["floorY"].get<float>() : floorY;
                floorContact(ctx, g, gname, e, fy, room != nullptr ? room->id : "the ground", false);
                continue;
            }
            std::string by;
            if (supportedBy(ctx, g, e, bottom.at, &by) || nearFloor) {
                ctx.pass("placement");
                continue;
            }
            ctx.add({"WARNING", "support", {e.id},
                     fmt::format("{} has no valid support relationship (nothing under it; {} above the floor)", e.id,
                                 fmtM(bottom.y - floorY)),
                     "resting on a surface", "", "set it on a table, shelf or the floor",
                     json{{"heightAboveFloor", r3(bottom.y - floorY)}}},
                    gname);
        }
    }
}

// Faces / near relationships (a chair to its table), seating volumes, rooms a thing should face into.
void checkRelationships(SpCtx& ctx, const SpGroup& g, const std::string& gname) {
    for (int i : g.entities) {
        const SpEntity& e = ctx.entities[i];
        if (e.text || !e.geom.ok || !e.frameOk) continue;
        const json& c = ctx.cat(e.category);
        if (c.contains("faces")) {
            const json& f = c["faces"];
            const float maxDistance = f.value("maxDistance", 1.0f);
            const float maxAngle = f.value("maxAngle", 45.0f);
            const SpEntity* target = nullptr;
            float bestGap = kInf;
            for (int j : g.entities) {
                const SpEntity& o = ctx.entities[j];
                if (&o == &e || !o.geom.ok) continue;
                const bool named = !e.anchor.empty() && o.id == e.anchor;
                if (!named && !listHas(f["targets"], o.category)) continue;
                const SpBox& a = e.geom.worldBox;
                const SpBox& b = o.geom.worldBox;
                const float gx = std::max({0.0f, b.lo.x - a.hi.x, a.lo.x - b.hi.x});
                const float gz = std::max({0.0f, b.lo.z - a.hi.z, a.lo.z - b.hi.z});
                const float gap = std::sqrt(gx * gx + gz * gz) - (named ? 1e3f : 0.0f);
                if (gap < bestGap) {
                    bestGap = gap;
                    target = &o;
                }
            }
            if (target != nullptr) {
                const bool named = !e.anchor.empty() && target->id == e.anchor;
                const float gap = named ? bestGap + 1e3f : bestGap;
                const SpBox& tb = target->geom.worldBox;
                const glm::vec3 ec = e.geom.worldBox.centre();
                // on top of it?
                if (ec.x > tb.lo.x && ec.x < tb.hi.x && ec.z > tb.lo.z && ec.z < tb.hi.z &&
                    e.geom.worldBox.lo.y >= tb.hi.y - 0.05f) {
                    ctx.add({"ERROR", "relationship", {e.id, target->id},
                             fmt::format("{} is placed on top of {}", e.id, target->id), "seating", c.value("expect", std::string())},
                            gname);
                } else if (gap <= maxDistance) {
                    glm::vec3 to = tb.centre() - e.origin;
                    to.y = 0.0f;
                    glm::vec3 fr = e.front;
                    fr.y = 0.0f;
                    const float a = angleBetween(fr, to);
                    if (a > maxAngle) {
                        SpViolation v{"WARNING", "orientation", {e.id, target->id},
                                    fmt::format("{} orientation is {:.0f} degrees away from {}", e.id, a, target->id),
                                    "faces " + target->category, c.value("expect", std::string())};
                        v.measured = json{{"angle", r3(a)}, {"gap", r3(gap)}};
                        // the yaw that turns its front onto the target
                        const float yaw = glm::degrees(std::atan2(fr.x, fr.z) - std::atan2(to.x, to.z));
                        v.suggestion = fmt::format("turn {} by {:.0f} degrees about +Y", e.id, -yaw);
                        v.fix = json{{"id", e.id}, {"yaw", r3(-yaw)}};
                        ctx.add(std::move(v), gname);
                    } else {
                        ctx.pass("relationship");
                    }
                } else if (named) {
                    ctx.add({"WARNING", "relationship", {e.id, target->id},
                             fmt::format("{} is {} from its {} {} (more than {})", e.id, fmtM(gap), target->category,
                                         target->id, fmtM(maxDistance)),
                             "adjacent", c.value("expect", std::string()), "", json{{"gap", r3(gap)}}},
                            gname);
                }
            }
        }
        if (c.contains("keepClear")) {
            const json& kc = c["keepClear"];
            const glm::vec3 fmin = vec3Of(kc.value("min", json()), glm::vec3(0.0f));
            const glm::vec3 fmax = vec3Of(kc.value("max", json()), glm::vec3(1.0f));
            const SpBox& lb = e.geom.local;
            SpBox vol;
            vol.lo = lb.lo + lb.size() * fmin;
            vol.hi = lb.lo + lb.size() * fmax;
            SpGeom clear;
            clear.tree.root.kind = spatial::SdfNodeKind::Box;
            clear.tree.root.size = vol.size() * 0.5f;
            clear.world = e.geom.world * glm::translate(glm::mat4(1.0f), vol.centre());
            clear.inv = glm::inverse(clear.world);
            clear.scale = e.geom.scale;
            clear.local = boxAround(clear.tree.root.size.x, clear.tree.root.size.y, clear.tree.root.size.z);
            clear.worldBox = transformBox(clear.local, clear.world);
            clear.ok = true;
            for (int j : g.entities) {
                const SpEntity& o = ctx.entities[j];
                if (&o == &e || !o.geom.ok || o.category == "room" || o.anchor == e.id) continue;
                if (mayIntersect(ctx, e, o) || !coexist(e, o)) continue;
                if (!intersect(clear.worldBox, o.geom.worldBox).valid()) continue;
                const SpOverlap ov = overlapOf(clear, o.geom);
                if (ov.depth > 0.03f) {
                    ctx.add({"WARNING", "keepClear", {o.id, e.id},
                             fmt::format("{} occupies the usable volume of {} (by {})", o.id, e.id, fmtM(ov.depth)),
                             "clear", c.value("expect", std::string()), "", json{{"depth", r3(ov.depth)}}},
                            gname);
                }
            }
        }
        if (c.value("facesRoom", false)) {
            const int r = roomFor(ctx, g, e);
            if (r >= 0) {
                glm::vec3 to = ctx.entities[r].interior->centre() - e.origin;
                to.y = 0.0f;
                glm::vec3 fr = e.front;
                fr.y = 0.0f;
                const float a = angleBetween(fr, to);
                if (a > 100.0f) {
                    ctx.add({"WARNING", "orientation", {e.id},
                             fmt::format("{} faces away from the room ({:.0f} degrees from its centre)", e.id, a), "faces room"},
                            gname);
                }
            }
        }
    }
}

// Doors: clear width and the clearance in front; rooms: an entrance; windows: not walled in.
void checkOpenings(SpCtx& ctx, const SpGroup& g, const std::string& gname) {
    const json& dr = ctx.rules["door"];
    const json& wr = ctx.rules["window"];
    std::map<int, int> openingsPerRoom;
    for (int i : g.entities) {
        const SpEntity& e = ctx.entities[i];
        const std::string opening = ctx.cat(e.category).value("opening", std::string());
        if (opening.empty() || !e.geom.ok) continue;
        const int r = roomFor(ctx, g, e);
        if (r < 0) continue;
        const SpEntity& room = ctx.entities[r];
        const auto walls = wallsOf(*room.interior);
        const glm::vec3 facing = e.normal.value_or(e.front);
        const glm::vec3 ref = e.normal ? e.geom.worldBox.centre() : e.origin;
        const SpWall* wall = nullptr;
        float bestD = kInf;
        for (const SpWall& w : walls) {
            const float d = std::abs(w.depth(ref));
            if (angleBetween(facing, w.inward()) < 45.0f && d < bestD) {
                wall = &w;
                bestD = d;
            }
        }
        if (wall == nullptr) continue;
        if (opening == "door") ++openingsPerRoom[r];
        const SpBox& b = e.geom.worldBox;
        const float u0 = b.lo[wall->uAxis];
        const float u1 = b.hi[wall->uAxis];
        const float depth = opening == "door" ? dr.value("clearanceDepth", 0.7f) : wr.value("clearanceDepth", 0.3f);
        const float vLo = opening == "door" ? room.interior->lo.y + 0.05f : b.lo.y + 0.05f;
        const float vHi = opening == "door" ? room.interior->lo.y + dr.value("height", 1.9f) : b.hi.y;
        // sample the clearance volume's columns across the opening
        const int nu = 16;
        int blockedColumns = 0;
        std::set<std::string> blockers;
        for (int k = 0; k < nu; ++k) {
            const float u = u0 + (u1 - u0) * (k + 0.5f) / static_cast<float>(nu);
            bool blocked = false;
            for (float d = 0.08f; d <= depth && !blocked; d += 0.1f) {
                for (float v = vLo; v <= vHi && !blocked; v += 0.15f) {
                    const glm::vec3 p = wall->point(u, v, d);
                    for (int j : g.entities) {
                        const SpEntity& o = ctx.entities[j];
                        if (&o == &e || !o.geom.ok || o.category == "room" || o.text || !coexist(e, o)) continue;
                        const std::string og = ctx.groupOf(o.category);
                        if (og == "architecture" || o.category == "rug" || o.category == "curtains" ||
                            og == "character") continue;
                        if (!o.geom.worldBox.contains(p)) continue;
                        if (o.geom.dist(p) < 0.0f) {
                            blocked = true;
                            blockers.insert(o.id);
                            break;
                        }
                    }
                }
            }
            if (blocked) ++blockedColumns;
        }
        const float width = u1 - u0;
        const float clearWidth = width * static_cast<float>(nu - blockedColumns) / static_cast<float>(nu);
        std::vector<std::string> ids{e.id};
        ids.insert(ids.end(), blockers.begin(), blockers.end());
        std::string who;
        for (const auto& s : blockers) who += (who.empty() ? "" : ", ") + s;
        if (opening == "door") {
            if (blockedColumns == nu) {
                ctx.add({"ERROR", "doorway", ids, fmt::format("{} is completely blocked by {}", e.id, who), "access",
                         "a door must be passable", "move the furniture out of the doorway's clearance",
                         json{{"clearWidth", 0.0}}},
                        gname);
            } else if (clearWidth < dr.value("minWidth", 0.7f) - 1e-3f) {
                ctx.add({"WARNING", "doorway", ids,
                         fmt::format("{} ({}) clearance is below the recommended minimum: {} clear of {}{}", e.id, room.id,
                                     fmtM(clearWidth), fmtM(dr.value("minWidth", 0.7f)),
                                     who.empty() ? std::string() : " (blocked by " + who + ")"),
                         "access", "", "", json{{"clearWidth", r3(clearWidth)}, {"width", r3(width)}}},
                        gname);
            } else {
                ctx.pass("architecture");
            }
        } else if (blockedColumns * 2 > nu) {
            ctx.add({"WARNING", "window", ids, fmt::format("{} is blocked by {}", e.id, who), "", "", "",
                     json{{"blockedFraction", r3(static_cast<float>(blockedColumns) / nu)}}},
                    gname);
        } else {
            ctx.pass("architecture");
        }
    }
    for (int r : roomsIn(ctx, g)) {
        if (!openingsPerRoom.count(r) && !ctx.entities[r].note.value("sealed", false)) {
            ctx.add({"WARNING", "access", {ctx.entities[r].id},
                     fmt::format("{} has no sensible entrance (no door or doorway on its walls)", ctx.entities[r].id),
                     "", "", "tag its door (category door/doorway), or mark the room \"sealed\": true"},
                    gname);
        } else if (openingsPerRoom.count(r)) {
            ctx.pass("architecture");
        }
    }
}

// The mannequin against its anchor: seated in the chair, lying along the couch, standing on the floor.
void checkPoses(SpCtx& ctx, const SpGroup& g, const std::string& gname) {
    for (int i : g.entities) {
        const SpEntity& e = ctx.entities[i];
        if (ctx.groupOf(e.category) != "character" || !e.geom.ok || !e.frameOk) continue;
        const std::string pose = e.pose.empty() ? std::string("stand") : e.pose;
        const json& poses = ctx.rules["poses"];
        if (!poses.contains(pose)) {
            ctx.add({"INFO", "pose", {e.id}, fmt::format("{}: pose '{}' has no rule; only intersections are checked", e.id, pose)}, gname);
            continue;
        }
        const json& p = poses[pose];
        const std::string support = p.value("support", std::string("floor"));
        const int r = roomFor(ctx, g, e);
        const SpEntity* anchor = nullptr;
        if (!e.anchor.empty()) {
            for (int j : g.entities) {
                if (ctx.entities[j].id == e.anchor) anchor = &ctx.entities[j];
            }
            if (anchor == nullptr) {
                ctx.add({"ERROR", "pose", {e.id, e.anchor},
                         fmt::format("{}: anchor {} is not in the scene with it", e.id, e.anchor)},
                        gname);
                continue;
            }
        }
        if (support == "floor") {
            if (r >= 0) floorContact(ctx, g, gname, e, ctx.entities[r].interior->lo.y, ctx.entities[r].id, true);
        } else if (anchor == nullptr) {
            ctx.add({"WARNING", "pose", {e.id}, fmt::format("{}: pose '{}' needs an anchor (a seat or a bed)", e.id, pose)}, gname);
            continue;
        }
        if (anchor != nullptr) {
            const json& ac = ctx.cat(anchor->category);
            const std::string needs = p.value("needs", std::string());
            if (!needs.empty() && !listHas(ac.value("supports", json::array()), needs) &&
                !listHas(anchor->note.value("supports", json::array()), needs)) {
                ctx.add({"WARNING", "pose", {e.id, anchor->id},
                         fmt::format("{}: pose '{}' needs {} but {} ({}) does not support it", e.id, pose, needs,
                                     anchor->id, anchor->category)},
                        gname);
            }
            const SpBox& ab = anchor->geom.worldBox;
            const SpBox& cb = e.geom.worldBox;
            if (support == "seat") {
                const float seat = anchor->note.value("seatHeight", ac.value("seatHeight", 0.45f));
                const float seatY = anchor->origin.y + seat;
                const glm::vec3 hip = e.hip.value_or(glm::vec3(cb.centre().x, cb.lo.y + 0.47f, cb.centre().z));
                const bool over = hip.x > ab.lo.x - 0.15f && hip.x < ab.hi.x + 0.15f && hip.z > ab.lo.z - 0.15f &&
                                  hip.z < ab.hi.z + 0.15f;
                const float dy = hip.y - seatY;
                if (!over) {
                    ctx.add({"ERROR", "pose", {e.id, anchor->id},
                             fmt::format("{}: seated pose but the hips are not over {}", e.id, anchor->id),
                             "character seated in " + anchor->category, "", "", json{{"hip", vecJson(hip)}}},
                            gname);
                } else if (std::abs(dy) > 0.1f) {
                    SpViolation v{"WARNING", "pose", {e.id, anchor->id},
                                fmt::format("{}: hips are {} {} the seat of {}", e.id, fmtM(std::abs(dy)),
                                            dy > 0 ? "above" : "below", anchor->id),
                                "character seated in " + anchor->category};
                    v.measured = json{{"hipToSeat", r3(dy)}};
                    v.fix = json{{"id", e.id}, {"translate", json::array({0.0, r3(-dy), 0.0})}};
                    ctx.add(std::move(v), gname);
                } else {
                    ctx.pass("character");
                }
            } else if (support == "top") {
                const float surface = anchor->note.value("surfaceHeight", ac.value("surfaceHeight", ac.value("seatHeight", 0.45f)));
                const glm::vec3 s = cb.size();
                const float topY = anchor->origin.y + surface;
                const float gap = cb.lo.y - topY;
                if (std::max(s.x, s.z) < s.y) {
                    ctx.add({"WARNING", "pose", {e.id}, fmt::format("{}: lying pose but the body is upright", e.id)}, gname);
                } else if (std::abs(gap) > 0.12f) {
                    ctx.add({"WARNING", "pose", {e.id, anchor->id},
                             fmt::format("{}: lies {} {} the surface of {}", e.id, fmtM(std::abs(gap)),
                                         gap > 0 ? "above" : "into", anchor->id),
                             "character lying along " + anchor->category, "", "", json{{"gap", r3(gap)}},
                             json{{"id", e.id}, {"translate", json::array({0.0, r3(-gap), 0.0})}}},
                            gname);
                } else {
                    SpBox fp = intersect(cb, ab);
                    const float share = fp.valid() ? (fp.size().x * fp.size().z) / std::max(1e-6f, s.x * s.z) : 0.0f;
                    if (share < 0.6f) {
                        ctx.add({"WARNING", "pose", {e.id, anchor->id},
                                 fmt::format("{}: only {:.0f}% of the body lies over {}", e.id, share * 100.0f, anchor->id)},
                                gname);
                    } else {
                        ctx.pass("character");
                    }
                }
            }
            if (p.contains("facesAnchor")) {
                glm::vec3 to = ab.centre() - e.origin;
                to.y = 0.0f;
                glm::vec3 fr = e.front;
                fr.y = 0.0f;
                const float a = angleBetween(fr, to);
                if (a > p["facesAnchor"].get<float>()) {
                    ctx.add({"WARNING", "orientation", {e.id, anchor->id},
                             fmt::format("{} orientation is {:.0f} degrees away from {}", e.id, a, anchor->id)},
                            gname);
                }
            }
        }
    }
}

// ---- lyrics ---------------------------------------------------------------------------------------------

struct SpRect {
    float u0, u1, v0, v1;
    [[nodiscard]] float area() const { return std::max(0.0f, u1 - u0) * std::max(0.0f, v1 - v0); }
};

float overlapArea(const SpRect& a, const SpRect& b) {
    const float w = std::min(a.u1, b.u1) - std::max(a.u0, b.u0);
    const float h = std::min(a.v1, b.v1) - std::max(a.v0, b.v0);
    return w > 0.0f && h > 0.0f ? w * h : 0.0f;
}

float rectGap(const SpRect& a, const SpRect& b) {
    const float du = std::max({0.0f, b.u0 - a.u1, a.u0 - b.u1});
    const float dv = std::max({0.0f, b.v0 - a.v1, a.v0 - b.v1});
    return std::sqrt(du * du + dv * dv);
}

struct SpObstacle {
    SpRect r;
    std::string id;
    std::string kind; // window, door, furniture, mounted, text
    const SpEntity* ent = nullptr; // its geometry, when it has an SDF (measured exactly, not by its box)
};

std::vector<SpObstacle> obstaclesOn(const SpCtx& ctx, const SpGroup& g, const SpWall& w, const SpEntity& self, float depthLimit) {
    std::vector<SpObstacle> out;
    for (int j : g.entities) {
        const SpEntity& o = ctx.entities[j];
        if (&o == &self || o.category == "room" || o.category == "rug" || !o.geom.worldBox.valid()) continue;
        if (!coexist(self, o)) continue;
        if (o.text) continue;
        const SpBox& b = o.geom.worldBox;
        // Only what stands on the room's side of this wall, within reach of it: not the next room's
        // furniture seen through the wall.
        const float d0 = std::min(w.depth(b.lo), w.depth(b.hi));
        const float d1 = std::max(w.depth(b.lo), w.depth(b.hi));
        if (d0 > depthLimit || d1 < -0.05f) continue;
        const std::string opening = ctx.cat(o.category).value("opening", std::string());
        const std::string kind = !opening.empty() ? opening
                                 : (supportsOf(ctx.cat(o.category), "mounts").empty() ? "furniture" : "mounted");
        out.push_back({{b.lo[w.uAxis], b.hi[w.uAxis], b.lo.y, b.hi.y}, o.id, kind, o.geom.ok ? &o : nullptr});
    }
    return out;
}

float rightSign(const SpWall& w);

void checkLyrics(SpCtx& ctx) {
    const json& lr = ctx.rules["lyric"];
    const float margin = ctx.options.lyricMargin >= 0.0 ? static_cast<float>(ctx.options.lyricMargin) : lr.value("margin", 0.15f);
    const float planeTol = lr.value("planeTolerance", 0.08f);
    const float faceTol = lr.value("faceAngle", 15.0f);
    const float maxTilt = lr.value("maxTilt", 25.0f);
    const float depthLimit = lr.value("obstacleDepth", 0.6f);
    const float step = lr.value("searchStep", 0.05f);
    for (std::size_t ti = 0; ti < ctx.entities.size(); ++ti) {
        const SpEntity& t = ctx.entities[ti];
        if (!t.text) continue;
        const std::string mode = ctx.cat(t.category).value("text", std::string("wall"));
        // orientation, for every kind of text
        const float tilt = angleBetween(t.up, {0.0f, 1.0f, 0.0f});
        if (t.mirrored) {
            ctx.add({"ERROR", "lyricOrientation", {t.id}, fmt::format("{} is mirrored", t.id), "", "readable", "", {}}, "");
        }
        if (mode == "wall" && t.annotated) {
            if (tilt > 90.0f && !t.note.value("upsideDown", false)) {
                ctx.add({"ERROR", "lyricOrientation", {t.id}, fmt::format("{} is upside down ({:.0f} degrees)", t.id, tilt)}, "");
            } else if (tilt > maxTilt + std::abs(t.note.value("tilt", 0.0f))) {
                ctx.add({"WARNING", "lyricOrientation", {t.id}, fmt::format("{} is tilted {:.0f} degrees off upright", t.id, tilt)}, "");
            }
        }
        if (mode == "floating") {
            ctx.pass("lyric");
            continue;
        }
        // Which room and wall: the candidates whose wall plane the text's back lies on.
        struct Fit {
            int group;
            int room;
            SpWall wall;
            float depth;
            float angle;
        };
        std::vector<Fit> fits;
        for (std::size_t gi = 0; gi < ctx.groups.size(); ++gi) {
            for (int r : roomsIn(ctx, ctx.groups[gi])) {
                const SpEntity& room = ctx.entities[r];
                if (!t.room.empty() && room.id != t.room) continue;
                for (const SpWall& w : wallsOf(*room.interior)) {
                    const float d = w.depth(t.origin);
                    const float a = angleBetween(t.front, w.inward());
                    const glm::vec3 c = t.geom.worldBox.centre();
                    const bool within = c[w.uAxis] > w.u0 - 0.3f && c[w.uAxis] < w.u1 + 0.3f && c.y > w.v0 - 0.3f &&
                                        c.y < w.v1 + 0.3f;
                    const bool plausible = t.annotated ? (std::abs(d) < 0.6f && (a < 60.0f || a > 120.0f))
                                                       : (std::abs(d) < 0.15f && a < 30.0f);
                    if (within && plausible) {
                        fits.push_back({static_cast<int>(gi), r, w, d, a});
                    }
                }
            }
        }
        if (fits.empty()) {
            if (mode == "wall" && t.annotated) {
                ctx.add({"WARNING", "lyricPlacement", {t.id},
                         fmt::format("{} is not on any room wall (no wall plane within 0.6m)", t.id), "", "on a wall",
                         "place it on a wall, or tag it floatingText"},
                        "");
            }
            continue;
        }
        // Prefer the closest plane; on a tie (identical rooms in several chapters) keep them all.
        std::sort(fits.begin(), fits.end(), [](const Fit& a, const Fit& b) {
            return std::abs(a.depth) + a.angle * 0.01f < std::abs(b.depth) + b.angle * 0.01f;
        });
        const float bestScore = std::abs(fits[0].depth) + fits[0].angle * 0.01f;
        std::vector<Fit> chosen;
        for (const Fit& f : fits) {
            if (std::abs(f.depth) + f.angle * 0.01f <= bestScore + 0.02f && f.wall.side == fits[0].wall.side) chosen.push_back(f);
        }
        if (!t.annotated && tilt > maxTilt) {
            ctx.add({"WARNING", "lyricOrientation", {t.id}, fmt::format("{} is tilted {:.0f} degrees off upright", t.id, tilt)}, "");
        }
        // Validate against the room with the fewest problems (an inferred room may belong to any of its chapters).
        struct Outcome {
            std::vector<SpViolation> v;
            int group;
        };
        std::optional<Outcome> best;
        std::set<std::string> roomsSeen;
        for (const Fit& f : chosen) {
            const SpEntity& room = ctx.entities[f.room];
            const std::string key = room.id + "/" + std::to_string(f.group);
            if (roomsSeen.count(key)) continue;
            roomsSeen.insert(key);
            const SpGroup& g = ctx.groups[f.group];
            const SpWall& w = f.wall;
            Outcome out{{}, f.group};
            // plane and facing
            if (std::abs(f.depth) > planeTol) {
                out.v.push_back({"WARNING", "lyricPlacement", {t.id, room.id},
                                 fmt::format("{} floats {} off wall {} of {}", t.id, fmtM(f.depth), w.side, room.id), "",
                                 "lying on the wall plane", "", json{{"wallDistance", r3(f.depth)}},
                                 json{{"id", t.id}, {"translate", vecJson(-w.inward() * (f.depth - 0.005f))}}});
            }
            if (f.angle > 90.0f) {
                out.v.push_back({"ERROR", "lyricOrientation", {t.id, room.id},
                                 fmt::format("{} faces into wall {} of {} (its back is to the room)", t.id, w.side, room.id)});
            } else if (f.angle > faceTol && mode == "wall") {
                out.v.push_back({"WARNING", "lyricOrientation", {t.id, room.id},
                                 fmt::format("{} is turned {:.0f} degrees off wall {}", t.id, f.angle, w.side)});
            }
            // the rectangle on the wall
            const SpBox& b = t.geom.worldBox;
            const SpRect tr{b.lo[w.uAxis], b.hi[w.uAxis], b.lo.y, b.hi.y};
            const SpRect wallRect{w.u0, w.u1, w.v0, w.v1};
            const float inside = overlapArea(tr, wallRect) / std::max(1e-6f, tr.area());
            const float rsign = rightSign(w);
            // ADR-1056: per edge, as you face the wall: how far the text runs past it (+) or clears it (-).
            const float overLeft = rsign > 0.0f ? w.u0 - tr.u0 : tr.u1 - w.u1;
            const float overRight = rsign > 0.0f ? tr.u1 - w.u1 : w.u0 - tr.u0;
            const float overTop = tr.v1 - w.v1;
            const float overBottom = w.v0 - tr.v0;
            if (inside < 0.999f) {
                std::string edges;
                for (const auto& [name, over] : {std::pair<const char*, float>{"left", overLeft}, {"right", overRight},
                                                 {"top", overTop}, {"bottom", overBottom}}) {
                    if (over > 0.001f) edges += (edges.empty() ? "" : ", ") + fmt::format("{} on the {} edge", fmtM(over), name);
                }
                out.v.push_back({"ERROR", "lyricPlacement", {t.id, room.id},
                                 fmt::format("text {} exceeds the bounds of wall {} of {} by {} ({:.0f}% of it is off the wall)", t.id,
                                             w.side, room.id, edges, (1.0f - inside) * 100.0f),
                                 "", "inside its wall's rectangle", "",
                                 json{{"outside", r3(1.0f - inside)}, {"left", r3(overLeft)}, {"right", r3(overRight)},
                                      {"top", r3(overTop)}, {"bottom", r3(overBottom)}}});
            }
            if (f.depth < -0.01f) {
                out.v.push_back({"WARNING", "lyricPlacement", {t.id, room.id},
                                 fmt::format("text {} is sunk {} into wall {} of {} (its extrusion passes into the wall)", t.id,
                                             fmtM(-f.depth), w.side, room.id),
                                 "", "lying on the wall plane", "",
                                 json{{"wallDistance", r3(f.depth)}},
                                 json{{"id", t.id}, {"translate", vecJson(w.inward() * (-f.depth + 0.005f))}}});
            }
            const float edgeGap = std::min({tr.u0 - w.u0, w.u1 - tr.u1, tr.v0 - w.v0, w.v1 - tr.v1});
            const char* nearEdge = -overLeft <= edgeGap + 1e-5f ? "left" : -overRight <= edgeGap + 1e-5f ? "right"
                                   : -overTop <= edgeGap + 1e-5f ? "top" : "bottom";
            auto obstacles = obstaclesOn(ctx, g, w, t, depthLimit);
            // other lyrics on this wall at the same time
            for (std::size_t oj = 0; oj < ctx.entities.size(); ++oj) {
                const SpEntity& o = ctx.entities[oj];
                if (oj == ti || !o.text || t.t0 < 0.0 || o.t0 < 0.0) continue;
                if (o.t1 <= t.t0 || t.t1 <= o.t0) continue;
                if (std::abs(w.depth(o.origin)) > planeTol || angleBetween(o.front, w.inward()) > 30.0f) continue;
                const SpBox& ob = o.geom.worldBox;
                obstacles.push_back({{ob.lo[w.uAxis], ob.hi[w.uAxis], ob.lo.y, ob.hi.y}, o.id, "text"});
            }
            bool clean = inside >= 0.999f;
            for (const SpObstacle& ob : obstacles) {
                float share = overlapArea(tr, ob.r) / std::max(1e-6f, tr.area());
                std::optional<float> exactGap;
                if (ob.ent != nullptr && ob.kind != "window" && ob.kind != "door" && (share > 0.0f || rectGap(tr, ob.r) < margin)) {
                    // Its real shape, not its box (a stair's box covers the wall above the treads): the share
                    // of the text's columns, in front of the wall within reach, that the shape occupies, and the
                    // nearest the shape comes to them.
                    const int nu = 16, nv = 8;
                    int hits = 0;
                    float nearest = kInf;
                    for (int iu = 0; iu < nu; ++iu) {
                        for (int iv = 0; iv < nv; ++iv) {
                            const float u = tr.u0 + (tr.u1 - tr.u0) * (iu + 0.5f) / nu;
                            const float v = tr.v0 + (tr.v1 - tr.v0) * (iv + 0.5f) / nv;
                            bool hit = false;
                            for (float d = 0.02f; d <= depthLimit + 1e-4f; d += 0.08f) {
                                const float dist = ob.ent->geom.dist(w.point(u, v, d));
                                nearest = std::min(nearest, dist);
                                if (dist < 0.0f) {
                                    hit = true;
                                    break;
                                }
                            }
                            hits += hit ? 1 : 0;
                        }
                    }
                    share = static_cast<float>(hits) / static_cast<float>(nu * nv);
                    exactGap = std::max(0.0f, nearest);
                }
                if (share > 0.0f) {
                    const bool hard = ob.kind == "window" || ob.kind == "door";
                    out.v.push_back({hard || share > 0.25f ? "ERROR" : "WARNING", "lyricPlacement", {t.id, ob.id},
                                     hard ? fmt::format("{} intersects {}: text overlaps the {} opening by {:.0f}%", t.id, ob.id, ob.kind, share * 100.0f)
                                          : fmt::format("{} intersects {}: text overlaps it by {:.0f}%", t.id, ob.id, share * 100.0f),
                                     "", "on a clear wall section", "", json{{"overlap", r3(share)}, {"wall", w.side}}});
                    clean = false;
                } else {
                    const float gap = exactGap ? *exactGap : rectGap(tr, ob.r);
                    if (gap < margin) {
                        out.v.push_back({"WARNING", "lyricClearance", {t.id, ob.id},
                                         fmt::format("{} is jammed against {} ({} of a {} margin)", t.id, ob.id, fmtM(gap), fmtM(margin)),
                                         "", "", "", json{{"gap", r3(gap)}, {"margin", r3(margin)}}});
                        clean = false;
                    }
                }
            }
            if (inside >= 0.999f && edgeGap < margin) {
                out.v.push_back({"WARNING", "lyricClearance", {t.id, room.id},
                                 fmt::format("{} is jammed against the {} edge of wall {} ({} of a {} margin)", t.id, nearEdge, w.side,
                                             fmtM(std::max(0.0f, edgeGap)), fmtM(margin)),
                                 "", "", "", json{{"gap", r3(edgeGap)}, {"margin", r3(margin)}}});
                clean = false;
            }
            // a suggestion: the nearest clear spot on this room's walls
            if (!clean) {
                const float wu = tr.u1 - tr.u0;
                const float hv = tr.v1 - tr.v0;
                const float cu = (tr.u0 + tr.u1) * 0.5f;
                const float cv = (tr.v0 + tr.v1) * 0.5f;
                std::optional<std::pair<float, glm::vec3>> spot;
                std::string spotWall;
                glm::vec3 spotNormal{0.0f};
                for (const SpWall& ww : wallsOf(*room.interior)) {
                    const auto obs = obstaclesOn(ctx, g, ww, t, depthLimit);
                    const float penalty = ww.side == w.side ? 0.0f : 2.0f;
                    for (float u = ww.u0 + margin + wu * 0.5f; u <= ww.u1 - margin - wu * 0.5f; u += step) {
                        for (float v = ww.v0 + margin + hv * 0.5f; v <= ww.v1 - margin - hv * 0.5f; v += step) {
                            const SpRect cand{u - wu * 0.5f - margin, u + wu * 0.5f + margin, v - hv * 0.5f - margin, v + hv * 0.5f + margin};
                            bool free = true;
                            for (const SpObstacle& ob : obs) {
                                if (overlapArea(cand, ob.r) > 0.0f) {
                                    free = false;
                                    break;
                                }
                            }
                            if (!free) continue;
                            const float cost = std::hypot(u - cu, v - cv) + penalty;
                            if (!spot || cost < spot->first) {
                                spot = std::make_pair(cost, ww.point(u, v, 0.005f));
                                spotWall = ww.side;
                                spotNormal = ww.inward();
                            }
                        }
                    }
                }
                if (spot) {
                    // where the node's origin goes: the rect centre's offset from the origin is kept
                    const glm::vec3 centreNow = w.point(cu, cv, w.depth(t.origin));
                    const glm::vec3 offset = t.origin - centreNow;
                    const glm::vec3 to = spot->second + (spotWall == w.side ? offset : glm::vec3(0.0f, offset.y, 0.0f));
                    const std::string text = spotWall == w.side ? fmt::format("move {} along wall {} to {}", t.id, w.side, vecJson(to).dump())
                                                                : fmt::format("move {} to adjacent wall segment {} at {} (normal {})", t.id, spotWall,
                                                                              vecJson(to).dump(), vecJson(spotNormal).dump());
                    for (SpViolation& v : out.v) {
                        if (v.suggestion.empty()) v.suggestion = text;
                        if (v.fix.is_null()) v.fix = json{{"id", t.id}, {"position", vecJson(to)}, {"normal", vecJson(spotNormal)}, {"wall", spotWall}};
                    }
                } else {
                    for (SpViolation& v : out.v) {
                        if (v.suggestion.empty()) v.suggestion = "no clear wall section in this room fits it: make it smaller";
                    }
                }
            }
            if (!best || out.v.size() < best->v.size()) best = std::move(out);
        }
        if (!best) continue;
        const std::string gname = ctx.groups[best->group].name;
        if (best->v.empty()) {
            ctx.pass("lyric");
        }
        for (SpViolation& v : best->v) ctx.add(std::move(v), gname);
    }
}

// ---- the camera's path ----------------------------------------------------------------------------------

void checkCameraPaths(SpCtx& ctx, const json& scene) {
    if (!scene.contains("camera") || !scene["camera"].is_object() || !scene["camera"].contains("journey")) return;
    auto journey = Journey::fromJson(scene["camera"]["journey"]);
    if (!journey) {
        ctx.notes.push_back("camera journey not checked: " + journey.error().message);
        return;
    }
    const json& cr = ctx.rules["camera"];
    const float eye = ctx.options.eyeHeight >= 0.0 ? static_cast<float>(ctx.options.eyeHeight) : cr.value("eyeHeight", 1.6f);
    const float clearance = cr.value("clearance", 0.12f);
    const float step = std::max(0.02f, cr.value("step", 0.1f));
    std::map<std::string, int> objectByName;
    for (std::size_t i = 0; i < ctx.objects.size(); ++i) objectByName[ctx.objects[i].name] = static_cast<int>(i);
    std::set<std::string> listed;
    for (std::size_t c = 0; c < journey->size(); ++c) {
        for (const auto& n : journey->chapter(c).nodes) listed.insert(n);
    }
    for (std::size_t c = 0; c < journey->size(); ++c) {
        const JourneyChapter& ch = journey->chapter(c);
        const JourneyPath& path = journey->path(c);
        std::vector<int> objs;
        for (const auto& n : ch.nodes) {
            if (auto it = objectByName.find(n); it != objectByName.end()) objs.push_back(it->second);
        }
        for (std::size_t i = 0; i < ctx.objects.size(); ++i) {
            if (!listed.count(ctx.objects[i].name)) objs.push_back(static_cast<int>(i));
        }
        // the span to check: what the film visits, or the authored path (first to last control point)
        const auto& pts = ch.world.path;
        double sBegin = 0.0;
        double sEnd = path.cellLength();
        if (!ctx.options.journeyDistances.empty()) {
            const double start = ch.start;
            const double next = c + 1 < journey->size() ? journey->chapter(c + 1).start : 1e300;
            double lo = 1e300, hi = -1e300;
            for (double d : ctx.options.journeyDistances) {
                if (d >= start && d < next) {
                    lo = std::min(lo, d);
                    hi = std::max(hi, d);
                }
            }
            if (lo > hi) continue; // the film never enters this chapter
            sBegin = journey->localDistance(c, lo);
            sEnd = journey->localDistance(c, hi);
        } else if (pts.size() >= 2) {
            float bestD = kInf;
            for (double s = 0.0; s < path.cellLength(); s += 0.02) {
                const float d = glm::length(path.sample(s).position - pts.back());
                if (d < bestD) {
                    bestD = d;
                    sEnd = s;
                }
            }
        }
        // The time the film reaches path metre s (global distance d), from the distance keys; -1 = unknown.
        auto timeAt = [&](double s) -> double {
            const auto& keys = ctx.options.journeyKeys;
            const double d = ch.start + (s - ch.from);
            for (std::size_t k = 0; k + 1 < keys.size(); ++k) {
                const double d0 = keys[k].second, d1 = keys[k + 1].second;
                if ((d >= std::min(d0, d1) && d <= std::max(d0, d1))) {
                    const double u = std::abs(d1 - d0) < 1e-9 ? 0.0 : (d - d0) / (d1 - d0);
                    return keys[k].first + u * (keys[k + 1].first - keys[k].first);
                }
            }
            return -1.0;
        };
        // An object is present at t unless all of its entities carry spans and none covers t.
        auto objectPresent = [&](int oi, double t) {
            if (t < 0.0) return true;
            bool any = false;
            for (const SpEntity& e : ctx.entities) {
                if (e.object != oi) continue;
                if (e.t0 < 0.0) return true;
                any = true;
                if (presentAt(e, t)) return true;
            }
            return !any;
        };
        struct Run {
            double s0 = 0, s1 = 0;
            float worst = kInf;
            glm::vec3 at{0.0f};
        };
        std::map<int, Run> inside, close;
        for (double s = sBegin; s <= sEnd + 1e-6; s += step) {
            const glm::vec3 local = path.sample(s).position + glm::vec3(0.0f, eye, 0.0f);
            const glm::vec3 p = ch.toWorldPoint(local);
            const double when = ctx.options.journeyKeys.empty() ? -1.0 : timeAt(s);
            for (int oi : objs) {
                const SpObject& o = ctx.objects[oi];
                if (!objectPresent(oi, when)) continue;
                if (!o.geom.tree.root.children.empty() || o.geom.ok) {
                    const glm::vec3 tl = glm::vec3(glm::inverse(o.world) * glm::vec4(p, 1.0f));
                    if (o.hasMarch && !o.march.contains(tl)) continue;
                    const float d = o.geom.dist(p);
                    auto& runs = d < 0.0f ? inside : d < clearance ? close : inside;
                    if (d >= clearance) continue;
                    auto [it, fresh] = runs.try_emplace(oi, Run{s, s, d, p});
                    if (!fresh) {
                        it->second.s1 = s;
                        if (d < it->second.worst) {
                            it->second.worst = d;
                            it->second.at = p;
                        }
                    }
                }
            }
        }
        auto entityAt = [&](int oi, const glm::vec3& p) -> std::string {
            std::string best;
            float bd = kInf;
            for (const SpEntity& e : ctx.entities) {
                if (e.object != oi || !e.geom.ok || e.category == "room") continue;
                const float d = e.geom.dist(p);
                if (d < bd) {
                    bd = d;
                    best = e.id;
                }
            }
            return bd < 0.05f ? best : std::string();
        };
        for (const auto& [oi, run] : inside) {
            const std::string ent = entityAt(oi, run.at);
            ctx.add({"ERROR", "cameraPath", {ch.name, ent.empty() ? ctx.objects[oi].name : ent},
                     fmt::format("camera path of chapter '{}' passes through {} (path metres {:.1f}-{:.1f}, {} deep)", ch.name,
                                 ent.empty() ? "'" + ctx.objects[oi].name + "'" : ent, run.s0, run.s1, fmtM(-run.worst)),
                     "navigation", "the camera stays inside the rooms' open space", "move the path points away from it",
                     json{{"depth", r3(-run.worst)}, {"from", r3(run.s0)}, {"to", r3(run.s1)}, {"at", vecJson(run.at)}}},
                    ch.name);
        }
        for (const auto& [oi, run] : close) {
            if (inside.count(oi)) continue;
            const std::string ent = entityAt(oi, run.at);
            ctx.add({"WARNING", "cameraPath", {ch.name, ent.empty() ? ctx.objects[oi].name : ent},
                     fmt::format("camera path of chapter '{}' grazes {} ({} away at path metres {:.1f}-{:.1f})", ch.name,
                                 ent.empty() ? "'" + ctx.objects[oi].name + "'" : ent, fmtM(run.worst), run.s0, run.s1),
                     "navigation", "", "", json{{"distance", r3(run.worst)}, {"at", vecJson(run.at)}}},
                    ch.name);
        }
        if (inside.empty() && close.empty()) ctx.pass("camera");
    }
}

// ---- ADR-1056: text against geometry and against other text ------------------------------------------------

void checkTextGeometry(SpCtx& ctx, const json& scene) {
    std::map<std::string, std::set<std::string>> chaptersOf;
    if (scene.contains("camera") && scene["camera"].is_object() && scene["camera"].contains("journey") &&
        scene["camera"]["journey"].contains("chapters")) {
        for (const auto& c : scene["camera"]["journey"]["chapters"]) {
            for (const auto& n : c.value("nodes", json::array())) {
                if (n.is_string()) chaptersOf[n.get<std::string>()].insert(c.value("name", std::string()));
            }
        }
    }
    // the chapter the film is in at a time (the journey keys give the distance; the chapters' starts the chapter)
    std::vector<std::pair<double, std::string>> starts;
    if (scene.contains("camera") && scene["camera"].is_object() && scene["camera"].contains("journey") &&
        scene["camera"]["journey"].contains("chapters")) {
        for (const auto& c : scene["camera"]["journey"]["chapters"]) starts.emplace_back(c.value("start", 0.0), c.value("name", std::string()));
        std::sort(starts.begin(), starts.end());
    }
    const auto& keys = ctx.options.journeyKeys;
    auto chapterAt = [&](double t) -> std::string {
        if (keys.empty() || starts.empty()) return {};
        double d = keys.front().second;
        for (std::size_t k = 0; k + 1 < keys.size(); ++k) {
            if (t >= keys[k].first && t <= keys[k + 1].first) {
                const double span = keys[k + 1].first - keys[k].first;
                const double u = span > 1e-9 ? (t - keys[k].first) / span : 0.0;
                d = keys[k].second + u * (keys[k + 1].second - keys[k].second);
                break;
            }
            if (t > keys[k + 1].first) d = keys[k + 1].second;
        }
        std::string name;
        for (const auto& [s0, n] : starts) {
            if (s0 <= d) name = n;
        }
        return name;
    };
    auto visibleDuring = [&](int oi, double t0, double t1) {
        const auto it = ctx.options.visibleSpans.find(ctx.objects[oi].name);
        if (it == ctx.options.visibleSpans.end() || t0 < 0.0) return true;
        for (const auto& [a, b] : it->second) {
            if (a < t1 && t0 < b) return true;
        }
        return false;
    };
    auto objectPresent = [&](int oi, const SpEntity& t) {
        if (!visibleDuring(oi, t.t0, t.t1)) return false;
        bool any = false;
        for (const SpEntity& e : ctx.entities) {
            if (e.object != oi) continue;
            if (e.t0 < 0.0) return true;
            any = true;
            if (coexist(e, t)) return true;
        }
        return !any;
    };
    // sample points on a text's face: a grid over its rectangle at half its extrusion
    auto facePoints = [](const SpEntity& t, int nu, int nv) {
        std::vector<glm::vec3> pts;
        const SpBox& l = t.geom.local;
        for (int i = 0; i < nu; ++i) {
            for (int j = 0; j < nv; ++j) {
                const glm::vec3 lp{l.lo.x + (l.hi.x - l.lo.x) * (static_cast<float>(i) + 0.5f) / static_cast<float>(nu),
                                   l.lo.y + (l.hi.y - l.lo.y) * (static_cast<float>(j) + 0.5f) / static_cast<float>(nv),
                                   (l.lo.z + l.hi.z) * 0.5f};
                pts.push_back(glm::vec3(t.geom.world * glm::vec4(lp, 1.0f)));
            }
        }
        return pts;
    };
    const float tracking = ctx.rules.value("lyric", json::object()).value("minTracking", -0.05f);
    for (std::size_t ti = 0; ti < ctx.entities.size(); ++ti) {
        const SpEntity& t = ctx.entities[ti];
        if (!t.text) continue;
        if (t.note.contains("tracking") && t.note["tracking"].is_number() && t.note["tracking"].get<float>() < tracking) {
            ctx.add({"WARNING", "textSelfOverlap", {t.id},
                     fmt::format("text {} overlaps itself: tracking {} em packs its glyphs into each other", t.id,
                                 t.note["tracking"].get<float>()),
                     "", "glyphs side by side"},
                    "");
        }
        // which objects it is shown with
        // Which chapters it is shown in: listed in a chapter's nodes, or (with the project's journey keys) the
        // chapters the film is in while its span lasts. Neither: only the objects every chapter shows.
        std::set<std::string> shownIn;
        if (const auto chs = chaptersOf.find(t.node); chs != chaptersOf.end()) shownIn = chs->second;
        if (shownIn.empty() && t.t0 >= 0.0) {
            for (double tt = t.t0; tt <= t.t1 + 1e-6; tt += std::max(0.05, (t.t1 - t.t0) / 20.0)) {
                const std::string c = chapterAt(tt);
                if (!c.empty()) shownIn.insert(c);
            }
        }
        std::set<int> objs;
        for (const SpGroup& g : ctx.groups) {
            bool member = g.chapters.empty();
            for (const auto& c : g.chapters) member = member || shownIn.count(c) > 0;
            if (member) objs.insert(g.objects.begin(), g.objects.end());
        }
        if (shownIn.empty() && !starts.empty()) {
            // every group's common objects only
            std::set<int> common;
            bool first = true;
            for (const SpGroup& g : ctx.groups) {
                if (first) common = g.objects;
                else {
                    std::set<int> keep;
                    for (int o : common) {
                        if (g.objects.count(o)) keep.insert(o);
                    }
                    common = keep;
                }
                first = false;
            }
            objs = common;
        }
        const auto pts = facePoints(t, 12, 4);
        std::map<std::string, int> hits;
        std::map<std::string, glm::vec3> where;
        for (int oi : objs) {
            const SpObject& o = ctx.objects[oi];
            if (!intersect(o.geom.worldBox, t.geom.worldBox).valid() && o.geom.worldBox.valid()) continue;
            if (!objectPresent(oi, t)) continue;
            for (const glm::vec3& p : pts) {
                const glm::vec3 tl = glm::vec3(glm::inverse(o.world) * glm::vec4(p, 1.0f));
                if (o.hasMarch && !o.march.contains(tl)) continue;
                if (o.geom.dist(p) >= -0.004f) continue;
                std::string who;
                float bestV = kInf;
                for (const SpEntity& e : ctx.entities) {
                    if (e.object != oi || !e.geom.ok || !coexist(e, t) || !e.geom.worldBox.contains(p, 0.01f)) continue;
                    if (e.geom.dist(p) >= 0.0f) continue;
                    const float v = e.category == "room" ? 1e9f : e.geom.worldBox.volume();
                    if (v < bestV) {
                        bestV = v;
                        who = e.category == "room" ? e.id + " (walls)" : e.id;
                    }
                }
                if (who.empty()) who = "'" + o.name + "'";
                ++hits[who];
                where.try_emplace(who, p);
            }
        }
        for (const auto& [who, n] : hits) {
            std::string bare = who;
            if (const auto sp = bare.find(" (walls)"); sp != std::string::npos) bare = bare.substr(0, sp);
            std::vector<std::string> ids{t.id, bare};
            std::sort(ids.begin(), ids.end());
            if (ctx.seen.count("lyricPlacement|" + ids[0] + "|" + ids[1])) continue; // already measured on its wall
            const float share = static_cast<float>(n) / static_cast<float>(pts.size());
            ctx.add({share > 0.25f ? "ERROR" : "WARNING", "textIntersection", {t.id, bare},
                     fmt::format("text {} intersects {}: {:.0f}% of its face is inside it (at {})", t.id, who, share * 100.0f,
                                 vecJson(where[who]).dump()),
                     "", "text clear of the architecture and the furniture", "move or resize the text, or move it in front of the surface",
                     json{{"share", r3(share)}}},
                    "");
        }
        if (hits.empty()) ctx.pass("lyric");
        // other text at the same time
        for (std::size_t oj = ti + 1; oj < ctx.entities.size(); ++oj) {
            const SpEntity& o = ctx.entities[oj];
            if (!o.text || !coexist(t, o)) continue;
            if (!intersect(t.geom.worldBox, o.geom.worldBox).valid()) continue;
            std::vector<std::string> ids{t.id, o.id};
            std::sort(ids.begin(), ids.end());
            if (ctx.seen.count("lyricPlacement|" + ids[0] + "|" + ids[1])) continue;
            const glm::mat4 inv = glm::inverse(o.geom.world);
            int in = 0;
            for (const glm::vec3& p : pts) {
                const glm::vec3 l = glm::vec3(inv * glm::vec4(p, 1.0f));
                const SpBox& ob = o.geom.local;
                if (l.x > ob.lo.x && l.x < ob.hi.x && l.y > ob.lo.y && l.y < ob.hi.y && l.z > ob.lo.z - 0.01f && l.z < ob.hi.z + 0.01f) ++in;
            }
            if (in > 0) {
                const float share = static_cast<float>(in) / static_cast<float>(pts.size());
                ctx.add({"WARNING", "textOverlap", {t.id, o.id},
                         fmt::format("text {} and text {} occupy the same space while both are shown ({:.0f}% of {})", t.id, o.id,
                                     share * 100.0f, t.id),
                         "", "one word per place at a time", "", json{{"share", r3(share)}}},
                        "");
            }
        }
    }
}

// ---- ADR-1056: structural relationships -----------------------------------------------------------------
//
// Architecture is checked against the geometry it is cut from: a window or a door is measured against the
// actual opening in its room's wall (scanned from the room's SDF, cuts included), not against the numbers
// the generator meant to use. That is what catches a cut made with one size and a frame made with another.

struct SpRect2 {
    float u0 = 0, u1 = 0, v0 = 0, v1 = 0;
    [[nodiscard]] bool valid() const { return u1 > u0 && v1 > v0; }
};

// The wall of `room` an opening entity sits in (the nearest wall plane whose inward normal it faces).
const SpWall* wallFor(const std::vector<SpWall>& walls, const SpEntity& e, float maxDepth) {
    const glm::vec3 facing = e.normal.value_or(e.front);
    const glm::vec3 ref = e.geom.worldBox.centre();
    const SpWall* best = nullptr;
    float bestD = kInf;
    for (const SpWall& w : walls) {
        const float d = std::abs(w.depth(ref));
        const float a = angleBetween(facing, w.inward());
        if ((a < 45.0f || a > 135.0f) && d < bestD && d < maxDepth) {
            best = &w;
            bestD = d;
        }
    }
    return best;
}

// +1 when increasing u is to the right of someone inside the room facing the wall.
float rightSign(const SpWall& w) {
    const glm::vec3 view = -w.inward();
    const glm::vec3 right = glm::cross(view, glm::vec3(0.0f, 1.0f, 0.0f));
    return right[w.uAxis] >= 0.0f ? 1.0f : -1.0f;
}

// The opening in the wall around (u, v): scanned in the room's own geometry at mid-wall depth.
// Returns an invalid rect when the wall is solid at (u, v).
SpRect2 openingAt(const SpGeom& room, const SpWall& w, float wall, float u, float v) {
    const float d = -wall * 0.5f;
    auto solid = [&](float uu, float vv) { return room.dist(w.point(uu, vv, d)) < 0.0f; };
    if (solid(u, v)) return {};
    const float step = 0.005f;
    SpRect2 r;
    auto scan = [&](float du, float dv, float limit) {
        float s = 0.0f;
        while (s < limit && !solid(u + du * (s + step), v + dv * (s + step))) s += step;
        return s;
    };
    r.u0 = u - scan(-1.0f, 0.0f, 4.0f);
    r.u1 = u + scan(1.0f, 0.0f, 4.0f);
    r.v0 = v - scan(0.0f, -1.0f, 4.0f);
    r.v1 = v + scan(0.0f, 1.0f, 4.0f);
    return r;
}

// Row profile of an entity's geometry on the wall: for each 1 cm row, the u extent the geometry occupies in the
// depth slab [d0, d1] (wall coordinates). Rows with nothing have u0 > u1.
struct SpRow {
    float v = 0.0f, u0 = kInf, u1 = -kInf;
    [[nodiscard]] bool any() const { return u0 <= u1; }
    [[nodiscard]] float width() const { return any() ? u1 - u0 : 0.0f; }
};

std::vector<SpRow> rowProfile(const SpGeom& g, const SpWall& w, float d0, float d1, float step = 0.01f) {
    std::vector<SpRow> rows;
    const SpBox& b = g.worldBox;
    const float uLo = b.lo[w.uAxis] - 0.02f, uHi = b.hi[w.uAxis] + 0.02f;
    for (float v = b.lo.y + step * 0.5f; v < b.hi.y; v += step) {
        SpRow r;
        r.v = v;
        for (float u = uLo; u <= uHi; u += step) {
            for (int k = 0; k < 5; ++k) {
                const float d = d0 + (d1 - d0) * static_cast<float>(k) / 4.0f;
                if (g.dist(w.point(u, v, d)) < 0.0f) {
                    r.u0 = std::min(r.u0, u);
                    r.u1 = std::max(r.u1, u);
                    break;
                }
            }
        }
        rows.push_back(r);
    }
    return rows;
}

std::string edgeList(const std::vector<std::pair<std::string, float>>& edges, float tol) {
    std::string out;
    for (const auto& [name, off] : edges) {
        if (std::abs(off) <= tol) continue;
        out += (out.empty() ? "" : ", ") +
               fmt::format("{} edge {} {}", name, fmtM(std::abs(off)), off > 0.0f ? "past the opening (over the wall)" : "short of the opening (a gap)");
    }
    return out;
}

// The top-level pieces of an object (what `checkObjectPieces` walks), each with its own geometry: the thing to
// name when something unannotated (a skirting, a dado band) is found where it should not be.
struct SpPiece {
    std::string label;
    SpGeom geom;
};

std::vector<SpPiece> piecesOf(const SpObject& obj) {
    std::vector<SpPiece> out;
    std::vector<std::pair<const json*, glm::mat4>> stack{{&obj.treeJson, glm::mat4(1.0f)}};
    int piece = 0;
    while (!stack.empty()) {
        auto [n, m] = stack.back();
        stack.pop_back();
        const std::string k = kindOf(*n);
        if ((k == "union" || k == "translate" || k == "rotate") && n->contains("children") && !n->contains("entity")) {
            glm::mat4 mm = m;
            if (k == "translate") mm = m * glm::translate(glm::mat4(1.0f), vec3Of(n->value("translation", json()), glm::vec3(0.0f)));
            if (k == "rotate") mm = m * eulerMatrix(vec3Of(n->value("rotation", json()), glm::vec3(0.0f)));
            if (k == "union") {
                for (const auto& c : (*n)["children"]) stack.emplace_back(&c, mm);
                continue;
            }
            const json& c = (*n)["children"][0];
            const std::string ck = kindOf(c);
            if ((ck == "union" || ck == "translate" || ck == "rotate") && !c.contains("entity")) {
                stack.emplace_back(&c, mm);
                continue;
            }
        }
        ++piece;
        SpPiece p;
        std::string label = n->value("name", std::string());
        if (label.empty() && n->contains("entity")) label = (*n)["entity"].value("id", std::string());
        p.label = label.empty() ? fmt::format("{}#{}", obj.name, piece) : obj.name + "/" + label;
        const SpBox clip = transformBox(obj.march, glm::inverse(m));
        if (!buildGeom(*n, obj.world * m, 1.0f, 12, p.geom, &clip) || !p.geom.ok) continue;
        out.push_back(std::move(p));
    }
    return out;
}

std::string describePiece(const SpPiece& p) {
    const glm::vec3 s = p.geom.worldBox.size();
    const float along = std::max(s.x, s.z);
    if (s.y < 0.25f && along > 1.5f) {
        return fmt::format("{} (a band at y {:.2f}-{:.2f}m: trim)", p.label, p.geom.worldBox.lo.y, p.geom.worldBox.hi.y);
    }
    return p.label;
}

void checkStructure(SpCtx& ctx, const SpGroup& g, const std::string& gname,
                    std::map<int, std::vector<SpPiece>>& pieceCache) {
    const json& sr = ctx.rules.value("structure", json::object());
    const float alignWarn = sr.value("alignWarn", 0.015f);
    const float alignError = sr.value("alignError", 0.05f);
    const float sillWarn = sr.value("sillWarn", 0.02f);
    for (int i : g.entities) {
        const SpEntity& e = ctx.entities[i];
        const std::string opening = ctx.cat(e.category).value("opening", std::string());
        if (opening.empty() || !e.geom.ok || !e.frameOk) continue;
        // A leaf inside another opening's frame (a door in its doorway) is part of that opening.
        bool leaf = false;
        for (int j : g.entities) {
            const SpEntity& x = ctx.entities[j];
            if (&x == &e || !x.geom.ok || ctx.cat(x.category).value("opening", std::string()).empty()) continue;
            if (x.geom.worldBox.contains(e.geom.worldBox.centre(), 0.02f) && x.geom.worldBox.volume() > e.geom.worldBox.volume() &&
                std::max(x.geom.worldBox.size().x, x.geom.worldBox.size().z) >= std::max(e.geom.worldBox.size().x, e.geom.worldBox.size().z)) {
                leaf = true;
            }
        }
        if (leaf) continue;
        const int r = roomFor(ctx, g, e);
        if (r < 0) continue;
        const SpEntity& room = ctx.entities[r];
        if (!room.geom.ok) continue;
        const float wall = room.note.value("wall", 0.15f);
        const auto walls = wallsOf(*room.interior);
        const SpWall* wp = wallFor(walls, e, ctx.tol("embedDepth"));
        if (wp == nullptr) continue;
        const SpWall& w = *wp;
        const float rs = rightSign(w);
        const SpBox& b = e.geom.worldBox;
        const float cu = (b.lo[w.uAxis] + b.hi[w.uAxis]) * 0.5f;
        const float cv = opening == "door" ? room.interior->lo.y + 1.0f : (b.lo.y + b.hi.y) * 0.5f;
        const SpRect2 o = openingAt(room.geom, w, wall, cu, cv);
        if (!o.valid()) {
            if (e.note.value("blind", false)) continue;
            ctx.add({"ERROR", "opening", {e.id, room.id},
                     fmt::format("{} has no opening behind it: wall {} of {} is solid at its centre", e.id, w.side, room.id),
                     opening == "door" ? "a door is contained by a wall opening" : "a window is contained by a wall opening",
                     "cut the opening (the generator's cut list), or mark the entity \"blind\": true",
                     fmt::format("cut an opening in wall {} at {} m along", w.side, r3(cu)), json{{"wall", w.side}}},
                    gname);
            continue;
        }
        // the frame's rectangle on the wall
        SpRect2 frame;
        float sillTop = -kInf;
        bool haveSill = false;
        const SpPart* framePart = nullptr;
        const SpPart* sillPart = nullptr;
        for (const SpPart& p : e.parts) {
            if (p.name == "frame" && p.geom.ok) framePart = &p;
            if (p.name == "sill" && p.geom.ok) sillPart = &p;
        }
        if (opening == "window") {
            const float d0 = std::min(w.depth(b.lo), w.depth(b.hi));
            const float d1 = std::max(w.depth(b.lo), w.depth(b.hi));
            if (framePart != nullptr) {
                const SpBox& fb = framePart->geom.worldBox;
                frame = {fb.lo[w.uAxis], fb.hi[w.uAxis], fb.lo.y, fb.hi.y};
            }
            if (sillPart != nullptr) {
                sillTop = sillPart->geom.worldBox.hi.y;
                haveSill = true;
            }
            if (framePart == nullptr || sillPart == nullptr) {
                // Untagged: the sill is the run of bottom rows wider than the frame above them.
                const auto rows = rowProfile(e.geom, w, std::max(d0, -wall), d1);
                std::vector<float> widths;
                for (const SpRow& row : rows) {
                    if (row.any()) widths.push_back(row.width());
                }
                if (widths.empty()) continue;
                std::vector<float> sorted = widths;
                std::sort(sorted.begin(), sorted.end());
                const float median = sorted[sorted.size() / 2];
                std::size_t k = 0;
                while (k < rows.size() && !rows[k].any()) ++k;
                std::size_t sillEnd = k;
                while (sillEnd < rows.size() && rows[sillEnd].any() && rows[sillEnd].width() > median + 0.03f) ++sillEnd;
                if (sillPart == nullptr && sillEnd > k) {
                    sillTop = rows[sillEnd - 1].v + 0.005f;
                    haveSill = true;
                }
                if (framePart == nullptr) {
                    SpRect2 f{kInf, -kInf, kInf, -kInf};
                    for (std::size_t q = sillEnd; q < rows.size(); ++q) {
                        if (!rows[q].any()) continue;
                        f.u0 = std::min(f.u0, rows[q].u0);
                        f.u1 = std::max(f.u1, rows[q].u1 + 0.01f);
                        f.v0 = std::min(f.v0, rows[q].v - 0.005f);
                        f.v1 = std::max(f.v1, rows[q].v + 0.005f);
                    }
                    frame = f;
                }
            }
        } else {
            // a door: the architrave's hole, scanned outwards from the opening's centre in the frame's plane
            const SpGeom& dg = framePart != nullptr ? framePart->geom : e.geom;
            const float fd0 = std::min(w.depth(dg.worldBox.lo), w.depth(dg.worldBox.hi));
            const float fd1 = std::max(w.depth(dg.worldBox.lo), w.depth(dg.worldBox.hi));
            auto solid = [&](float u, float v) {
                for (int k = 0; k < 7; ++k) {
                    const float d = fd0 + (fd1 - fd0) * (static_cast<float>(k) + 0.5f) / 7.0f;
                    if (dg.dist(w.point(u, v, d)) < 0.0f) return true;
                }
                return false;
            };
            const float u = (o.u0 + o.u1) * 0.5f;
            auto scan = [&](float du, float dv) {
                float s = 0.0f;
                while (s < 3.0f && !solid(u + du * (s + 0.005f), cv + dv * (s + 0.005f))) s += 0.005f;
                return s;
            };
            if (!solid(u, cv)) {
                frame = {u - scan(-1.0f, 0.0f), u + scan(1.0f, 0.0f), room.interior->lo.y, cv + scan(0.0f, 1.0f)};
            }
        }
        if (frame.valid()) {
            // Per edge, as seen from the room: + = the frame edge lies past the opening (over the wall).
            const float leftOff = rs > 0.0f ? o.u0 - frame.u0 : frame.u1 - o.u1;
            const float rightOff = rs > 0.0f ? frame.u1 - o.u1 : o.u0 - frame.u0;
            const float topOff = frame.v1 - o.v1;
            const float bottomOff = o.v0 - frame.v0;
            std::vector<std::pair<std::string, float>> edges{{"left", leftOff}, {"right", rightOff}, {"top", topOff}};
            if (opening == "window") edges.emplace_back("bottom", bottomOff);
            float worst = 0.0f;
            for (const auto& [n, v] : edges) worst = std::max(worst, std::abs(v));
            if (worst > alignWarn) {
                SpViolation v{worst > alignError ? "ERROR" : "WARNING", "openingAlignment", {e.id, room.id},
                              fmt::format("{} is misaligned with its opening in wall {} of {}: {}", e.id, w.side, room.id,
                                          edgeList(edges, alignWarn)),
                              "contained by its wall opening",
                              "the frame and the cut should have the same rectangle"};
                v.measured = json{{"left", r3(leftOff)}, {"right", r3(rightOff)}, {"top", r3(topOff)},
                                  {"opening", json::array({r3(o.u0), r3(o.u1), r3(o.v0), r3(o.v1)})},
                                  {"frame", json::array({r3(frame.u0), r3(frame.u1), r3(frame.v0), r3(frame.v1)})},
                                  {"wall", w.side}};
                if (opening == "window") v.measured["bottom"] = r3(bottomOff);
                const float du = ((o.u0 + o.u1) - (frame.u0 + frame.u1)) * 0.5f;
                const float dv = opening == "window" ? ((o.v0 + o.v1) - (frame.v0 + frame.v1)) * 0.5f : 0.0f;
                glm::vec3 delta{0.0f};
                delta[w.uAxis] = du;
                delta.y = dv;
                v.suggestion = fmt::format("move {} by {} to centre it on the opening, and size the frame and the cut alike "
                                           "(opening {:.2f} x {:.2f}m, frame {:.2f} x {:.2f}m)",
                                           e.id, vecJson(delta).dump(), o.u1 - o.u0, o.v1 - o.v0, frame.u1 - frame.u0,
                                           frame.v1 - frame.v0);
                if (glm::length(delta) > 0.005f) v.fix = json{{"id", e.id}, {"translate", vecJson(delta)}};
                ctx.add(std::move(v), gname);
            } else {
                ctx.pass("architecture");
            }
        }
        if (opening == "window") {
            if (!haveSill) {
                ctx.add({"INFO", "sill", {e.id}, fmt::format("{} has no sill (no ledge found below the frame)", e.id)}, gname);
            } else {
                const float toOpening = sillTop - o.v0;
                const float toFrame = frame.valid() ? sillTop - frame.v0 : 0.0f;
                const float worst = std::max(std::abs(toOpening), std::abs(toFrame));
                if (worst > sillWarn) {
                    SpViolation v{worst > alignError ? "ERROR" : "WARNING", "sill", {e.id, room.id},
                                  fmt::format("window sill of {} is offset from the window: its top is {} {} the opening's lower edge{}",
                                              e.id, fmtM(std::abs(toOpening)), toOpening > 0.0f ? "above" : "below",
                                              frame.valid() && std::abs(toFrame) > sillWarn
                                                  ? fmt::format(" and {} {} the frame's", fmtM(std::abs(toFrame)), toFrame > 0.0f ? "above" : "below")
                                                  : std::string()),
                                  "the sill's top on the window's lower edge"};
                    v.measured = json{{"sillToOpening", r3(toOpening)}, {"sillToFrame", r3(toFrame)}, {"sillTop", r3(sillTop)},
                                      {"openingBottom", r3(o.v0)}};
                    v.suggestion = fmt::format("move the sill by {} (or the cut's sill height to {:.2f}m)", fmtM(-toOpening), sillTop);
                    ctx.add(std::move(v), gname);
                } else {
                    ctx.pass("architecture");
                }
            }
        }
        // ---- the opening's passage must be clear of everything but its own frame (trim, decor, furniture)
        const SpObject& obj = ctx.objects[room.object];
        auto [it, fresh] = pieceCache.try_emplace(room.object);
        if (fresh) it->second = piecesOf(obj);
        const auto& pieces = it->second;
        std::map<std::string, float> crossing; // culprit -> metres of the opening's width it spans
        std::map<std::string, std::pair<float, float>> crossingSpan;
        const float du = 0.03f, dv = 0.03f;
        for (float u = o.u0 + 0.015f; u < o.u1 - 0.01f; u += du) {
            std::set<std::string> here;
            for (float v = o.v0 + 0.012f; v < o.v1 - 0.01f; v += dv) {
                for (float d : {-wall * 0.5f, -0.01f, 0.0f, 0.012f}) {
                    const glm::vec3 p = w.point(u, v, d);
                    if (e.geom.dist(p) < 0.004f) continue; // its own frame, mullions and glass
                    if (room.geom.dist(p) < 0.0f) continue; // the wall itself (the scan found its edge)
                    std::string who;
                    for (int j : g.entities) {
                        const SpEntity& x = ctx.entities[j];
                        if (&x == &e || &x == &room || !x.geom.ok || x.text || !coexist(e, x)) continue;
                        if (mayIntersect(ctx, e, x) && ctx.groupOf(x.category) != "architecture") continue;
                        if (!ctx.cat(x.category).value("opening", std::string()).empty()) continue; // a leaf in its frame
                        if (!x.geom.worldBox.contains(p) || x.geom.dist(p) >= 0.0f) continue;
                        who = x.id;
                        break;
                    }
                    if (who.empty() && obj.geom.dist(p) < 0.0f) {
                        for (const SpPiece& pc : pieces) {
                            if (pc.geom.worldBox.contains(p, 0.01f) && pc.geom.dist(p) < 0.0f) {
                                who = describePiece(pc);
                                break;
                            }
                        }
                        if (who.empty()) who = "'" + obj.name + "'";
                    }
                    if (!who.empty()) {
                        here.insert(who);
                        auto& span = crossingSpan.try_emplace(who, std::make_pair(kInf, -kInf)).first->second;
                        span.first = std::min(span.first, v);
                        span.second = std::max(span.second, v);
                        break;
                    }
                }
            }
            for (const auto& h : here) crossing[h] += du;
        }
        for (const auto& [who, width] : crossing) {
            const float share = width / std::max(1e-3f, o.u1 - o.u0);
            const auto& span = crossingSpan[who];
            ctx.add({share > 0.5f && opening == "door" ? "ERROR" : "WARNING", "openingCrossed", {who, e.id},
                     fmt::format("{} crosses the {} opening of {} (wall {} of {}): across {} of its {} width, at y {:.2f}-{:.2f}m",
                                 who, opening, e.id, w.side, room.id, fmtM(std::min(width, o.u1 - o.u0)), fmtM(o.u1 - o.u0),
                                 span.first, span.second),
                     "architectural trim and decoration stop at doors and windows",
                     opening == "door" ? "nothing runs across a doorway" : "nothing runs across a window",
                     "cut the trim at the opening (subtract the opening's cut from the band too), or move the piece",
                     json{{"share", r3(share)}, {"yFrom", r3(span.first)}, {"yTo", r3(span.second)}}},
                    gname);
        }
        if (crossing.empty()) ctx.pass("architecture");
    }
}

// The highest solid point of a geometry in the vertical column at (x, z) between y1 (top) and y0 (bottom).
std::optional<float> columnTop(const SpGeom& g, float x, float z, float y0, float y1) {
    float y = y1;
    for (int s = 0; s < 200 && y > y0; ++s) {
        const float d = g.dist({x, y, z});
        if (d < 1e-3f) return y;
        y -= std::max(d, 1e-3f);
    }
    return std::nullopt;
}

// Roofs on their buildings, stairs between floors, railings along their stairs.
void checkConnections(SpCtx& ctx, const SpGroup& g, const std::string& gname) {
    std::vector<float> floors{0.0f};
    for (int r : roomsIn(ctx, g)) floors.push_back(ctx.entities[r].interior->lo.y);
    for (int i : g.entities) {
        const SpEntity& e = ctx.entities[i];
        if ((e.category == "landing" || e.category == "floor" || e.category == "balcony") && e.geom.ok) {
            floors.push_back(e.geom.worldBox.hi.y);
        }
    }
    auto nearestFloor = [&](float y) {
        float best = kInf;
        for (float f : floors) {
            if (std::abs(f - y) < std::abs(best - y)) best = f;
        }
        return best;
    };
    auto byId = [&](const std::string& id) -> const SpEntity* {
        for (int j : g.entities) {
            if (ctx.entities[j].id == id) return &ctx.entities[j];
        }
        return nullptr;
    };
    for (int i : g.entities) {
        const SpEntity& e = ctx.entities[i];
        if (!e.geom.ok || !e.frameOk) continue;
        if (e.category == "roof") {
            const SpEntity* b = e.anchor.empty() ? nullptr : byId(e.anchor);
            if (b == nullptr) {
                // the building under it: the entity with the most footprint overlap below the roof
                float bestShare = 0.0f;
                for (int j : g.entities) {
                    const SpEntity& x = ctx.entities[j];
                    if (&x == &e || !x.geom.ok || (x.category != "building" && x.category != "room")) continue;
                    const glm::vec3 xs = x.geom.worldBox.size();
                    SpBox ov = intersect(x.geom.worldBox, e.geom.worldBox);
                    const float share = ov.lo.x < ov.hi.x && ov.lo.z < ov.hi.z ? (ov.hi.x - ov.lo.x) * (ov.hi.z - ov.lo.z) / std::max(1e-4f, xs.x * xs.z) : 0.0f;
                    if (share > bestShare && x.geom.worldBox.centre().y < e.geom.worldBox.centre().y) {
                        bestShare = share;
                        b = &x;
                    }
                }
            }
            if (b == nullptr) {
                ctx.add({"WARNING", "roof", {e.id}, fmt::format("{} has no building under it", e.id), "a roof connects to its building",
                         "", "set its \"anchor\" to the building entity"},
                        gname);
                continue;
            }
            const ColumnHit bottom = traceVertical(e.geom, +1);
            const float top = b->geom.worldBox.hi.y;
            const float gap = bottom.hit ? bottom.y - top : 0.0f;
            const glm::vec3 bs = b->geom.worldBox.size();
            SpBox ov = intersect(b->geom.worldBox, e.geom.worldBox);
            const float cover = ov.lo.x < ov.hi.x && ov.lo.z < ov.hi.z ? (ov.hi.x - ov.lo.x) * (ov.hi.z - ov.lo.z) / std::max(1e-4f, bs.x * bs.z) : 0.0f;
            if (gap > ctx.tol("supportGap")) {
                ctx.add({gap > 0.15f ? "ERROR" : "WARNING", "roof", {e.id, b->id},
                         fmt::format("{} floats {} above the top of {} (not connected)", e.id, fmtM(gap), b->id),
                         "a roof rests on its building's walls", "", fmt::format("lower {} by {}", e.id, fmtM(gap)),
                         json{{"gap", r3(gap)}}, json{{"id", e.id}, {"translate", json::array({0.0, r3(-gap), 0.0})}}},
                        gname);
            } else if (cover < 0.9f) {
                ctx.add({"WARNING", "roof", {e.id, b->id},
                         fmt::format("{} covers only {:.0f}% of {}'s footprint", e.id, cover * 100.0f, b->id), "", "", "",
                         json{{"cover", r3(cover)}}},
                        gname);
            } else {
                ctx.pass("architecture");
            }
        }
        if (e.category == "stairs" && !e.note.value("terminates", false)) {
            const ColumnHit top = traceVertical(e.geom, -1);
            const ColumnHit bottom = traceVertical(e.geom, +1);
            if (!top.hit || !bottom.hit) continue;
            const float lowF = nearestFloor(bottom.y);
            const float highF = nearestFloor(top.y);
            const float riser = e.note.value("rise", 0.2f);
            bool ok = true;
            if (std::abs(bottom.y - lowF) > 0.05f) {
                ctx.add({"WARNING", "stairs", {e.id},
                         fmt::format("{} starts {} {} the nearest floor (y {:.2f})", e.id, fmtM(std::abs(bottom.y - lowF)),
                                     bottom.y > lowF ? "above" : "below", lowF),
                         "stairs connect floors"},
                        gname);
                ok = false;
            }
            if (std::abs(top.y - highF) > riser + 0.05f || highF <= lowF + 0.5f) {
                ctx.add({"WARNING", "stairs", {e.id},
                         fmt::format("{} ends at y {:.2f} with no floor there (nearest floor {:.2f}): it terminates in empty space", e.id,
                                     top.y, highF),
                         "stairs connect floors", "",
                         "end it at a floor or a landing (tag a \"landing\"), or mark it \"terminates\": true if that is the image",
                         json{{"top", r3(top.y)}, {"nearestFloor", r3(highF)}}},
                        gname);
                ok = false;
            }
            if (ok) ctx.pass("architecture");
        }
        if (e.category == "railing") {
            const SpEntity* s = e.anchor.empty() ? nullptr : byId(e.anchor);
            if (s == nullptr) {
                for (int j : g.entities) {
                    const SpEntity& x = ctx.entities[j];
                    if (x.category == "stairs" && x.geom.ok && intersect(x.geom.worldBox, e.geom.worldBox).valid()) s = &x;
                }
            }
            if (s == nullptr) {
                ctx.add({"WARNING", "railing", {e.id}, fmt::format("{} follows no stairs or balcony", e.id), "railings follow stairs",
                         "", "set its \"anchor\""},
                        gname);
                continue;
            }
            const SpBox& rb = e.geom.worldBox;
            const bool alongX = rb.size().x >= rb.size().z;
            std::vector<float> heights;
            int off = 0;
            for (int k = 0; k < 12; ++k) {
                const float f = (static_cast<float>(k) + 0.5f) / 12.0f;
                float x = alongX ? rb.lo.x + rb.size().x * f : (rb.lo.x + rb.hi.x) * 0.5f;
                float z = alongX ? (rb.lo.z + rb.hi.z) * 0.5f : rb.lo.z + rb.size().z * f;
                const auto railTop = columnTop(e.geom, x, z, rb.lo.y - 0.01f, rb.hi.y + 0.01f);
                if (!railTop) continue;
                std::optional<float> stairTop;
                for (float side : {0.0f, 0.15f, -0.15f, 0.3f, -0.3f}) {
                    const float sx = alongX ? x : x + side;
                    const float sz = alongX ? z + side : z;
                    stairTop = columnTop(s->geom, sx, sz, s->geom.worldBox.lo.y - 0.01f, *railTop - 0.05f);
                    if (stairTop) break;
                }
                if (!stairTop) {
                    ++off;
                    continue;
                }
                heights.push_back(*railTop - *stairTop);
            }
            if (heights.size() < 3) continue;
            const auto [mn, mx] = std::minmax_element(heights.begin(), heights.end());
            if (off > 2) {
                ctx.add({"WARNING", "railing", {e.id, s->id},
                         fmt::format("{} runs off {}: {} of its 12 samples have no tread under them", e.id, s->id, off),
                         "railings follow stairs"},
                        gname);
            } else if (*mx - *mn > 0.15f) {
                ctx.add({"WARNING", "railing", {e.id, s->id},
                         fmt::format("{} does not follow {}: its height above the treads varies {:.2f}-{:.2f}m", e.id, s->id, *mn, *mx),
                         "a handrail parallel to the pitch line", "", "", json{{"min", r3(*mn)}, {"max", r3(*mx)}}},
                        gname);
            } else {
                ctx.pass("architecture");
            }
        }
    }
}

// ---- ADR-1056: containment ------------------------------------------------------------------------------

bool roomIsKind(const SpEntity& room, const json& kinds) {
    std::string id = room.id;
    std::transform(id.begin(), id.end(), id.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    const std::string kind = room.note.value("kind", std::string());
    for (const auto& k : kinds) {
        if (!k.is_string()) continue;
        const std::string s = k.get<std::string>();
        if (s == kind || id.find(s) != std::string::npos) return true;
    }
    return false;
}

void checkContainment(SpCtx& ctx, const SpGroup& g, const std::string& gname) {
    std::map<std::string, std::vector<const SpEntity*>> chairsByTable;
    for (int i : g.entities) {
        const SpEntity& e = ctx.entities[i];
        if (e.text || !e.geom.ok || !e.frameOk || e.category == "room") continue;
        const std::string group = ctx.groupOf(e.category);
        if (group != "furniture" && group != "decor" && group != "character") continue;
        const json& c = ctx.cat(e.category);
        const auto mounts = supportsOf(c, "mounts");
        int r = -1;
        for (int rr : roomsIn(ctx, g)) {
            if (!e.room.empty() && ctx.entities[rr].id == e.room) r = rr;
        }
        if (r < 0) r = roomFor(ctx, g, e);
        if (r < 0) continue;
        const SpEntity& room = ctx.entities[r];
        const SpBox& in = *room.interior;
        const SpBox& b = e.geom.worldBox;
        const glm::vec3 ctr = b.centre();
        if (!e.room.empty() && !in.contains(ctr, 0.0f)) {
            ctx.add({"ERROR", "containment", {e.id, room.id},
                     fmt::format("{} is outside its room {} (its centre is {} beyond the walls)", e.id, room.id,
                                 fmtM(std::max({in.lo.x - ctr.x, ctr.x - in.hi.x, in.lo.z - ctr.z, ctr.z - in.hi.z, 0.0f}))),
                     "inside the room it belongs to"},
                    gname);
            continue;
        }
        // overhang through a wall, per side (mounted things live on the wall: their depth is the wall's)
        if (mounts.empty() && supportsOf(c, "embeds").empty()) {
            const std::pair<const char*, float> sides[4] = {{"-x", in.lo.x - b.lo.x}, {"+x", b.hi.x - in.hi.x},
                                                           {"-z", in.lo.z - b.lo.z}, {"+z", b.hi.z - in.hi.z}};
            const auto worst = *std::max_element(std::begin(sides), std::end(sides),
                                                 [](const auto& a, const auto& bb) { return a.second < bb.second; });
            std::vector<std::string> sorted{e.id, room.id};
            std::sort(sorted.begin(), sorted.end());
            const bool already = ctx.seen.count("intersection|" + sorted[0] + "|" + sorted[1]) > 0;
            if (worst.second > 0.05f && !already && group != "character") {
                ctx.add({"WARNING", "containment", {e.id, room.id},
                         fmt::format("{} extends {} outside {} through wall {}", e.id, fmtM(worst.second), room.id, worst.first),
                         "inside the room it belongs to", "", fmt::format("move {} {} into the room", e.id, fmtM(worst.second))},
                        gname);
            }
        }
        // the right kind of room
        if (c.contains("rooms") && c["rooms"].is_array() && !roomIsKind(room, c["rooms"])) {
            ctx.add({"WARNING", "roomKind", {e.id, room.id},
                     fmt::format("{} ({}) is in {}, expected a room of kind {}", e.id, e.category, room.id, c["rooms"].dump()),
                     "", "", "move it, or give the room \"kind\""},
                    gname);
        }
        // against a wall
        if (c.value("againstWall", false) && !e.note.value("freestanding", false)) {
            float gap = kInf;
            std::string side;
            for (const SpWall& w : wallsOf(in)) {
                if (angleBetween(e.front, w.inward()) > 30.0f) continue;
                const float d = std::min(w.depth(b.lo), w.depth(b.hi));
                if (d < gap) {
                    gap = d;
                    side = w.side;
                }
            }
            if (gap < kInf && gap > ctx.rules.value("structure", json::object()).value("wallGap", 0.15f)) {
                ctx.add({"WARNING", "againstWall", {e.id, room.id},
                         fmt::format("{} stands {} off wall {} of {} behind it", e.id, fmtM(gap), side, room.id),
                         "a " + e.category + " stands against a wall", "",
                         fmt::format("move it {} back, or mark it \"freestanding\": true", fmtM(gap))},
                        gname);
            }
        }
        if (e.category == "chair" && !e.anchor.empty()) chairsByTable[e.anchor].push_back(&e);
    }
    const float minSpacing = ctx.rules.value("structure", json::object()).value("chairSpacing", 0.5f);
    for (const auto& [table, chairs] : chairsByTable) {
        for (std::size_t a = 0; a < chairs.size(); ++a) {
            for (std::size_t b = a + 1; b < chairs.size(); ++b) {
                if (!coexist(*chairs[a], *chairs[b])) continue;
                glm::vec3 d = chairs[a]->geom.worldBox.centre() - chairs[b]->geom.worldBox.centre();
                d.y = 0.0f;
                const float dist = glm::length(d);
                if (dist < minSpacing) {
                    ctx.add({"WARNING", "chairSpacing", {chairs[a]->id, chairs[b]->id},
                             fmt::format("{} and {} at {} are {} apart (under {}: crowded)", chairs[a]->id, chairs[b]->id, table,
                                         fmtM(dist), fmtM(minSpacing)),
                             "chairs around a table have room for a person each"},
                            gname);
                }
            }
        }
    }
}

// ADR-1056: the tiers the owner's report names. ERROR = critical, WARNING = warning, INFO = informational
// (expected or intended: a child overlapping its parent, decoration in the floor, motion that is the effect).
std::string tierOf(const std::string& severity) {
    return severity == "ERROR" ? "critical" : severity == "WARNING" ? "warning" : "info";
}

json violationJson(const SpViolation& v) {
    json j{{"severity", v.severity}, {"tier", tierOf(v.severity)}, {"rule", v.rule}, {"entities", v.ids}, {"message", v.message}};
    if (!v.relationship.empty()) j["relationship"] = v.relationship;
    if (!v.expected.empty()) j["expected"] = v.expected;
    if (!v.suggestion.empty()) j["suggestion"] = v.suggestion;
    if (!v.measured.empty()) j["measured"] = v.measured;
    if (!v.fix.is_null()) j["fix"] = v.fix;
    if (!v.groups.empty()) j["groups"] = v.groups;
    return j;
}

void deepMerge(json& base, const json& over) {
    if (!base.is_object() || !over.is_object()) {
        base = over;
        return;
    }
    for (auto it = over.begin(); it != over.end(); ++it) {
        if (base.contains(it.key()) && base[it.key()].is_object() && it.value().is_object()) {
            deepMerge(base[it.key()], it.value());
        } else {
            base[it.key()] = it.value();
        }
    }
}

} // namespace

json defaultSpaceRules() {
    return json::parse(kDefaultRules);
}

json mergeSpaceRules(const json& overrides) {
    json r = defaultSpaceRules();
    if (overrides.is_object()) deepMerge(r, overrides);
    return r;
}

Result<json> validateSpace(const json& scene, const json& rules, const SpaceValidateOptions& options) {
    if (!scene.is_object() || !scene.contains("nodes") || !scene["nodes"].is_array()) {
        return fail("space validator: not a scene document (no 'nodes' array)");
    }
    for (const char* key : {"tolerances", "categories", "poses", "lyric", "door", "window", "camera"}) {
        if (!rules.contains(key) || !rules[key].is_object()) {
            return fail("space validator: rules lack the '{}' object", key);
        }
    }
    SpCtx ctx{rules, options};
    // objects, then their entities
    for (const auto& node : scene["nodes"]) {
        if (!node.is_object()) continue;
        const std::string kind = node.value("kind", std::string());
        if (kind == "sdf" && node.contains("sdf") && node["sdf"].contains("tree")) {
            const json& tree = node["sdf"]["tree"];
            const json& root = tree.contains("root") ? tree["root"] : tree;
            SpObject o;
            o.name = node.value("name", std::string("sdf"));
            o.treeJson = root;
            float sc = 1.0f;
            o.world = nodeWorld(node, &sc, nullptr);
            if (node["sdf"].contains("boundsMin") && node["sdf"].contains("boundsMax")) {
                o.march.lo = vec3Of(node["sdf"]["boundsMin"], glm::vec3(-5.0f));
                o.march.hi = vec3Of(node["sdf"]["boundsMax"], glm::vec3(5.0f));
            } else {
                o.march = boxAround(5.0f, 5.0f, 5.0f); // the engine's default
            }
            o.hasMarch = true;
            buildGeom(root, o.world, sc, 8, o.geom, &o.march);
            o.geom.ok = true; // whole objects are queried for distance only
            ctx.objects.push_back(std::move(o));
        }
    }
    for (std::size_t i = 0; i < ctx.objects.size(); ++i) {
        const float sc = 1.0f;
        walkTree(ctx, static_cast<int>(i), ctx.objects[i].treeJson, nullptr, 0, glm::mat4(1.0f), sc, true, 0);
    }
    for (const auto& node : scene["nodes"]) {
        if (!node.is_object() || node.value("kind", std::string()) != "procedural") continue;
        if (!node.contains("procedural") || !node["procedural"].contains("source")) continue;
        if (node["procedural"]["source"].value("kind", std::string()) != "text") continue;
        readTextNode(ctx, node);
    }
    deriveFrames(ctx);
    buildGroups(ctx, scene);

    checkIntegrity(ctx);
    checkObjectPieces(ctx);
    std::map<int, std::vector<SpPiece>> pieceCache;
    for (const SpGroup& g : ctx.groups) {
        checkIntersections(ctx, g, g.name);
        checkPlacement(ctx, g, g.name);
        checkRelationships(ctx, g, g.name);
        checkOpenings(ctx, g, g.name);
        checkPoses(ctx, g, g.name);
        checkStructure(ctx, g, g.name, pieceCache);
        checkConnections(ctx, g, g.name);
        checkContainment(ctx, g, g.name);
    }
    checkLyrics(ctx);
    if (options.text) checkTextGeometry(ctx, scene);
    if (options.cameraPath) checkCameraPaths(ctx, scene);

    // the report
    int errors = 0, warnings = 0, infos = 0;
    json vs = json::array();
    std::stable_sort(ctx.violations.begin(), ctx.violations.end(), [](const SpViolation& a, const SpViolation& b) {
        auto rank = [](const std::string& s) { return s == "ERROR" ? 0 : s == "WARNING" ? 1 : 2; };
        return rank(a.severity) < rank(b.severity);
    });
    for (const SpViolation& v : ctx.violations) {
        if (v.severity == "ERROR") ++errors;
        else if (v.severity == "WARNING") ++warnings;
        else ++infos;
        vs.push_back(violationJson(v));
    }
    json ents = json::array();
    for (const SpEntity& e : ctx.entities) {
        json j{{"id", e.id}, {"category", e.category}, {"node", e.node}};
        if (!e.room.empty()) j["room"] = e.room;
        if (!e.anchor.empty()) j["anchor"] = e.anchor;
        if (!e.pose.empty()) j["pose"] = e.pose;
        if (e.geom.worldBox.valid()) {
            j["min"] = vecJson(e.geom.worldBox.lo);
            j["max"] = vecJson(e.geom.worldBox.hi);
        }
        j["origin"] = vecJson(e.origin);
        j["front"] = vecJson(e.front);
        if (e.interior) j["interior"] = json::array({vecJson(e.interior->lo), vecJson(e.interior->hi)});
        if (e.note.contains("opensAt") && e.note["opensAt"].is_number()) j["opensAt"] = e.note["opensAt"];
        if (e.t0 >= 0.0) {
            j["t0"] = e.t0;
            j["t1"] = e.t1;
        }
        if (!e.parts.empty()) {
            json parts = json::array();
            for (const SpPart& p : e.parts) parts.push_back(p.name);
            j["parts"] = parts;
        }
        ents.push_back(j);
    }
    json groups = json::array();
    for (const SpGroup& g : ctx.groups) {
        json names = json::array();
        for (int o : g.objects) names.push_back(ctx.objects[o].name);
        groups.push_back(json{{"name", g.name}, {"chapters", g.chapters}, {"objects", names}, {"entities", g.entities.size()}});
    }
    json passes = json::object();
    for (const auto& [k, v] : ctx.passes) passes[k] = v;
    json report{{"title", options.title},
                {"source", options.source},
                {"summary", {{"errors", errors}, {"warnings", warnings}, {"infos", infos}, {"pass", passes},
                             {"entities", ctx.entities.size()}, {"objects", ctx.objects.size()}}},
                {"violations", vs},
                {"entities", ents},
                {"groups", groups},
                {"notes", ctx.notes}};
    return report;
}

void mergeFilmReport(json& report, const json& film) {
    json all = report["violations"];
    for (json v : film.value("violations", json::array())) {
        v["tier"] = tierOf(v.value("severity", std::string("INFO")));
        all.push_back(v);
    }
    std::vector<json> sorted(all.begin(), all.end());
    auto rank = [](const json& v) {
        const std::string s = v.value("severity", std::string());
        return s == "ERROR" ? 0 : s == "WARNING" ? 1 : 2;
    };
    std::stable_sort(sorted.begin(), sorted.end(), [&](const json& a, const json& b) { return rank(a) < rank(b); });
    int errors = 0, warnings = 0, infos = 0;
    for (const json& v : sorted) {
        const int r = rank(v);
        (r == 0 ? errors : r == 1 ? warnings : infos)++;
    }
    report["violations"] = sorted;
    report["summary"]["errors"] = errors;
    report["summary"]["warnings"] = warnings;
    report["summary"]["infos"] = infos;
    json f = film;
    f.erase("violations");
    report["film"] = f;
}

std::string formatSceneValidationMarkdown(const json& report) {
    std::string out = "# Scene Validation Report\n\n";
    const json& s = report["summary"];
    out += fmt::format("Source: `{}`\n\n", report.value("source", std::string()));
    out += fmt::format("| tier | count |\n|---|---|\n| Critical | {} |\n| Warnings | {} |\n| Informational | {} |\n\n",
                       s.value("errors", 0), s.value("warnings", 0), s.value("infos", 0));
    // counts by rule and tier
    std::map<std::string, std::array<int, 3>> byRule;
    for (const auto& v : report["violations"]) {
        const std::string sev = v.value("severity", std::string());
        byRule[v.value("rule", std::string())][sev == "ERROR" ? 0 : sev == "WARNING" ? 1 : 2]++;
    }
    out += "| rule | critical | warning | info |\n|---|---|---|---|\n";
    for (const auto& [rule, c] : byRule) out += fmt::format("| {} | {} | {} | {} |\n", rule, c[0], c[1], c[2]);
    auto item = [](const json& v) {
        std::string line = "- **" + v.value("rule", std::string()) + "**: " + v.value("message", std::string());
        if (v.contains("groups") && !v["groups"].empty()) {
            std::string g;
            for (const auto& x : v["groups"]) g += (g.empty() ? "" : ", ") + x.get<std::string>();
            line += " *(in: " + g + ")*";
        }
        if (v.contains("suggestion")) line += "\n  - fix: " + v["suggestion"].get<std::string>();
        return line + "\n";
    };
    const char* heads[3] = {"Critical", "Warnings", "Informational"};
    const char* sevs[3] = {"ERROR", "WARNING", "INFO"};
    for (int t = 0; t < 3; ++t) {
        out += fmt::format("\n## {}\n\n", heads[t]);
        int n = 0, shown = 0;
        const int limit = t == 2 ? 40 : 100000;
        for (const auto& v : report["violations"]) {
            if (v.value("severity", std::string()) != sevs[t]) continue;
            ++n;
            if (shown < limit) {
                out += item(v);
                ++shown;
            }
        }
        if (n == 0) out += "None.\n";
        if (n > shown) out += fmt::format("\n...and {} more (the JSON report has every item).\n", n - shown);
    }
    if (report.contains("film")) {
        const json& f = report["film"];
        out += "\n## Film pass\n\n";
        if (f.contains("camera")) {
            const json& c = f["camera"];
            out += fmt::format("- camera: {} samples at {} fps; closest approach {} m at {} s\n", c.value("samples", 0),
                               c.value("fps", 0.0), c.contains("minClearance") ? c["minClearance"].dump() : std::string("-"),
                               c.value("minClearanceAt", 0.0));
        }
        if (f.contains("motion")) {
            const json& m = f["motion"];
            out += fmt::format("- structural transforms watched: {}; locked after construction: {}; twitching: {}; "
                               "moving continuously: {}; allowed to move (decor, characters, text): {}\n",
                               m.value("watched", 0), m.value("locked", 0), m.value("twitching", 0), m.value("animated", 0),
                               m.value("mayMove", 0));
        }
    }
    out += "\n## Passed\n\n";
    for (const auto& [k, v] : s["pass"].items()) out += fmt::format("- {}: {}\n", k, v.dump());
    for (const auto& n : report["notes"]) out += "\nnote: " + n.get<std::string>() + "\n";
    return out;
}

std::string formatSpaceReport(const json& report) {
    std::string out;
    const json& s = report["summary"];
    out += report.value("title", std::string("ROOM VALIDATION")) + "\n\n";
    out += fmt::format("Errors: {}\nWarnings: {}\n", s.value("errors", 0), s.value("warnings", 0));
    if (s.value("infos", 0) > 0) out += fmt::format("Notes: {}\n", s.value("infos", 0));
    for (const auto& v : report["violations"]) {
        out += "\n" + v.value("severity", std::string()) + "\n" + v.value("message", std::string()) + "\n";
        if (v.contains("measured")) {
            for (const auto& [k, m] : v["measured"].items()) {
                if (m.is_number()) out += fmt::format("{}: {}\n", k, m.dump());
            }
        }
        if (v.contains("expected")) out += "\nExpected relationship:\n" + v["expected"].get<std::string>() + "\n";
        if (v.contains("suggestion")) out += "\nSuggested correction:\n" + v["suggestion"].get<std::string>() + "\n";
        if (v.contains("groups") && v["groups"].size() > 0) {
            std::string g;
            for (const auto& x : v["groups"]) g += (g.empty() ? "" : ", ") + x.get<std::string>();
            out += "(in: " + g + ")\n";
        }
    }
    out += "\nPASS\n";
    const json& p = s["pass"];
    auto line = [&](const char* key, const char* label) {
        if (p.contains(key)) out += fmt::format("{} {}\n", p[key].get<int>(), label);
    };
    line("architecture", "architectural relationships");
    line("placement", "entity placements");
    line("relationship", "furniture relationships");
    line("character", "character poses");
    line("integrity", "entities complete");
    line("lyric", "lyric placements");
    line("camera", "camera paths clear");
    for (const auto& n : report["notes"]) out += "note: " + n.get<std::string>() + "\n";
    return out;
}

} // namespace avgen::scene
