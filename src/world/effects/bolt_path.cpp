#include "world/effects/bolt_path.hpp"

#include "core/noise.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace avgen::world {

namespace {

// The finest grid a path is refined on: 2^kBoltMaxDepth segments. Every depth indexes the SAME grid
// (a coarser depth visits every 2^(8-depth)-th point), which is what makes a lower depth a prefix of
// a higher one.
constexpr int kGrid = 1 << kBoltMaxDepth;

// tan(15 deg) and tan(45 deg): the range a branch leaves its heading by. A tangent rather than an
// angle so the generator needs no trigonometry.
constexpr float kTanMin = 0.26794919f;
constexpr float kTanMax = 1.0f;

std::uint32_t mix32(std::uint32_t a, std::uint32_t b) {
    const noise::U3 h = noise::pcg3d({a, b, 0x9e3779b9u});
    return h.x ^ h.z;
}

// Two unit vectors across `t` (unit). Chosen from the axis least aligned with it, which is a fixed
// function of `t` in canonical space.
void across(const glm::vec3& t, glm::vec3& e1, glm::vec3& e2) {
    const glm::vec3 ref = std::abs(t.x) < 0.9f ? glm::vec3(1.0f, 0.0f, 0.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
    e1 = glm::normalize(glm::cross(t, ref));
    e2 = glm::cross(t, e1);
}

// How steeply any segment may lean off its path's chord: 35 degrees. Two consecutive segments then
// turn by at most 70 degrees, which is what separates a lightning channel (sharp kinks, always
// going somewhere) from a random walk (hairpins, loops, doubling back).
constexpr float kMaxLean = 0.70020754f; // tan(35 deg)
// The top level's displacement is `jaggedness` times the chord; each level below is this much of
// the one above -- a little more than the halving of the segment, so fine detail stays visible.
constexpr float kLevelDecay = 0.55f;

// Midpoint displacement of one path from `p0` to `p1` on the fixed grid, refined `depth` times.
// Writes the grid points the depth visits into `grid` (the rest are left alone).
//
// Every point is `p0 + chord * (i / 256)` plus a LATERAL offset in the plane across the chord: a
// point's position along the chord is fixed by its grid index, so the path advances strictly from
// start to end (no backtracking, no loops) by construction. A midpoint's offset is uniform in
// [-1, 1] on each lateral axis times `jag * |chord| * 0.55^level`, and then held so that neither
// half of the segment it splits leans off the chord by more than 35 degrees. That is always
// possible (the parent segment leaned at most 35 degrees over twice the length, so its midpoint
// satisfies both halves), and it is found by a fixed 16-step bisection on the offset's scale -- the
// same arithmetic on every platform.
void refine(const glm::vec3& p0, const glm::vec3& p1, int depth, float jag, std::uint64_t seed,
            std::uint32_t index, std::uint32_t pathId, std::array<glm::vec3, kGrid + 1>& grid) {
    const glm::vec3 chord = p1 - p0;
    const float length = glm::length(chord);
    grid[0] = p0;
    grid[kGrid] = p1;
    if (!(length > 1e-9f)) {
        for (int i = 1; i < kGrid; ++i) {
            grid[static_cast<std::size_t>(i)] = p0;
        }
        return;
    }
    const glm::vec3 dir = chord / length;
    glm::vec3 e1;
    glm::vec3 e2;
    across(dir, e1, e2);
    std::array<glm::vec2, kGrid + 1> lateral{};
    float amplitude = jag * length;
    for (int level = 0; level < depth; ++level, amplitude *= kLevelDecay) {
        const int half = kGrid >> (level + 1);
        const float reach = kMaxLean * length * static_cast<float>(half) / static_cast<float>(kGrid);
        for (int i = half; i < kGrid; i += 2 * half) {
            const glm::vec2 a = lateral[static_cast<std::size_t>(i - half)];
            const glm::vec2 b = lateral[static_cast<std::size_t>(i + half)];
            const glm::vec2 centre = (a + b) * 0.5f;
            glm::vec2 w(0.0f);
            if (amplitude > 0.0f) {
                const auto key = static_cast<std::uint32_t>((level << 16) | i);
                w = glm::vec2(boltHash(seed, index, pathId, key, 0u) * 2.0f - 1.0f,
                              boltHash(seed, index, pathId, key, 1u) * 2.0f - 1.0f) *
                    amplitude;
            }
            const auto fits = [&](float k) {
                const glm::vec2 m = centre + w * k;
                return glm::length(m - a) <= reach && glm::length(b - m) <= reach;
            };
            float scale = 1.0f;
            if (!fits(1.0f)) {
                float lo = 0.0f; // fits: the centre always does
                float hi = 1.0f;
                for (int step = 0; step < 16; ++step) {
                    const float midK = 0.5f * (lo + hi);
                    (fits(midK) ? lo : hi) = midK;
                }
                scale = lo;
            }
            lateral[static_cast<std::size_t>(i)] = centre + w * scale;
        }
    }
    const int stride = kGrid >> depth;
    for (int i = stride; i < kGrid; i += stride) {
        const glm::vec2 l = lateral[static_cast<std::size_t>(i)];
        grid[static_cast<std::size_t>(i)] =
            p0 + dir * (length * static_cast<float>(i) / static_cast<float>(kGrid)) + e1 * l.x + e2 * l.y;
    }
}

// Appends the path `grid` holds at `depth` to `out` as vertices, with arc length from `s0`,
// intensity falling from `intensity0` by `fade` along it and width from `width0` by `taper`.
void emitPath(const std::array<glm::vec3, kGrid + 1>& grid, int depth, float s0, float intensity0, float fade,
              float width0, float taper, BoltPath& out) {
    const int stride = kGrid >> depth;
    const int n = (1 << depth) + 1;
    float s = s0;
    glm::vec3 prev = grid[0];
    for (int k = 0; k < n; ++k) {
        const glm::vec3 p = grid[static_cast<std::size_t>(k * stride)];
        s += glm::length(p - prev);
        prev = p;
        const float u = static_cast<float>(k) / static_cast<float>(n - 1);
        BoltVertex v;
        v.position = p;
        v.s = s;
        v.intensity = intensity0 * (1.0f - fade * u);
        v.width = width0 * (1.0f - taper * u);
        out.vertices.push_back(v);
        out.maxS = std::max(out.maxS, s);
    }
}

BoltParams sanitised(const BoltParams& in) {
    BoltParams p = in;
    p.depth = std::clamp(p.depth, 1, kBoltMaxDepth);
    p.jaggedness = std::isfinite(p.jaggedness) ? std::clamp(p.jaggedness, 0.0f, 1.0f) : 0.0f;
    p.branchProbability = std::isfinite(p.branchProbability) ? std::clamp(p.branchProbability, 0.0f, 1.0f) : 0.0f;
    p.branchDecay = std::isfinite(p.branchDecay) ? std::clamp(p.branchDecay, 0.05f, 0.95f) : 0.5f;
    p.generations = std::clamp(p.generations, 0, kBoltMaxGenerations);
    p.maxVertices = std::clamp<std::uint32_t>(p.maxVertices, (1u << p.depth) + 1u, kBoltMaxVertices);
    return p;
}

} // namespace

float boltHash(std::uint64_t seed, std::uint32_t a, std::uint32_t b, std::uint32_t c, std::uint32_t channel) {
    const auto lo = static_cast<std::uint32_t>(seed);
    const auto hi = static_cast<std::uint32_t>(seed >> 32);
    const noise::U3 first = noise::pcg3d({a ^ lo, b + 0x632be5abu * (channel + 1u), c ^ hi});
    const noise::U3 second = noise::pcg3d({first.x ^ channel, first.y + lo, first.z ^ (hi * 0x85ebca6bu)});
    // 24 bits, so the result is exactly representable and strictly below 1.
    return static_cast<float>(second.x >> 8) * (1.0f / 16777216.0f);
}

std::uint32_t boltEventIndex(double t0) {
    if (!std::isfinite(t0)) {
        return 0;
    }
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(std::llround(t0 * 960.0)));
}

void generateBolt(const BoltParams& raw, std::uint64_t seed, std::uint32_t index, BoltPath& out) {
    const BoltParams p = sanitised(raw);
    out.vertices.clear();
    out.paths.clear();
    out.maxS = 0.0f;
    if (out.vertices.capacity() < kBoltMaxVertices) {
        out.vertices.reserve(kBoltMaxVertices);
    }
    if (out.paths.capacity() < kBoltMaxBranches + 1) {
        out.paths.reserve(kBoltMaxBranches + 1);
    }

    std::array<glm::vec3, kGrid + 1> grid{};
    // Per path: its depth and its id (for hashing its own midpoints and its own branches).
    std::array<int, kBoltMaxBranches + 1> depthOf{};
    std::array<std::uint32_t, kBoltMaxBranches + 1> idOf{};

    // ---- the main channel ----
    constexpr std::uint32_t kMainId = 0x5eed0001u;
    refine(glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f), p.depth, p.jaggedness, seed, index, kMainId, grid);
    BoltBranch main;
    main.first = 0;
    main.count = static_cast<std::uint32_t>((1 << p.depth) + 1);
    main.generation = 0;
    main.parent = 0;
    main.root = 0;
    emitPath(grid, p.depth, 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, out);
    out.paths.push_back(main);
    depthOf[0] = p.depth;
    idOf[0] = kMainId;

    // ---- branches, breadth first: every branch of the channel, then theirs ----
    for (std::size_t at = 0; at < out.paths.size(); ++at) {
        const BoltBranch parent = out.paths[at];
        if (static_cast<int>(parent.generation) >= p.generations) {
            continue;
        }
        const int n = static_cast<int>(parent.count);
        // Candidates at fixed fractions of the parent: sixteenths of the channel, eighths of a branch
        // (or every vertex of a path too coarse for that). Keyed by the FRACTION, so a coarser depth
        // offers the same candidates at the same places.
        const int wanted = parent.generation == 0 ? 16 : 8;
        const int slots = std::min(wanted, n - 1);
        if (slots < 2) {
            continue;
        }
        const int step = (n - 1) / slots;
        const glm::vec3 chord = out.vertices[parent.first + parent.count - 1].position - out.vertices[parent.first].position;
        const float chordLength = glm::length(chord);
        for (int j = 1; j < slots; ++j) {
            if (out.paths.size() >= kBoltMaxBranches + 1) {
                break;
            }
            const auto key = static_cast<std::uint32_t>(j * (64 / slots));
            const float q = static_cast<float>(j) / static_cast<float>(slots);
            const float probability = p.branchProbability * (1.0f - 0.6f * q) *
                                      (parent.generation == 0 ? 1.0f : p.branchDecay);
            if (boltHash(seed, index, idOf[at], key, 10u) >= probability) {
                continue;
            }
            const int depth = std::clamp(depthOf[at] - 2, 2, kBoltMaxDepth);
            const std::uint32_t need = (1u << depth) + 1u;
            if (out.vertices.size() + need > p.maxVertices) {
                continue; // no room for this one; a smaller one later may still fit
            }
            const std::uint32_t rootVertex = parent.first + static_cast<std::uint32_t>(j * step);
            const BoltVertex root = out.vertices[rootVertex];
            // The heading: half-way between where the parent is going here (over the neighbouring
            // candidates, so it is the same at every depth) and where the whole bolt is going (the
            // channel's +Z: the target, downward for a sky strike). A branch then leaves that heading
            // at a shallow 15-45 degrees about a random axis across it -- forking forward, never back.
            const glm::vec3 ahead = out.vertices[parent.first + static_cast<std::uint32_t>(std::min(n - 1, (j + 1) * step))].position;
            const glm::vec3 behind = out.vertices[parent.first + static_cast<std::uint32_t>((j - 1) * step)].position;
            glm::vec3 t = ahead - behind;
            if (!(glm::length(t) > 1e-9f)) {
                t = chordLength > 1e-9f ? chord : glm::vec3(0.0f, 0.0f, 1.0f);
            }
            t = glm::normalize(t) + glm::vec3(0.0f, 0.0f, 1.0f);
            t = glm::length(t) > 1e-6f ? glm::normalize(t) : glm::vec3(0.0f, 0.0f, 1.0f);
            glm::vec3 e1;
            glm::vec3 e2;
            across(t, e1, e2);
            glm::vec2 turn(boltHash(seed, index, idOf[at], key, 11u) * 2.0f - 1.0f,
                           boltHash(seed, index, idOf[at], key, 12u) * 2.0f - 1.0f);
            const float turnLength = glm::length(turn);
            turn = turnLength > 0.05f ? turn / turnLength : glm::vec2(1.0f, 0.0f);
            const float tanAngle = kTanMin + (kTanMax - kTanMin) * boltHash(seed, index, idOf[at], key, 13u);
            const glm::vec3 dir = glm::normalize(t + (e1 * turn.x + e2 * turn.y) * tanAngle);
            const float length = chordLength * p.branchDecay * (0.45f + 0.55f * boltHash(seed, index, idOf[at], key, 14u)) *
                                 (1.0f - 0.5f * q);
            if (!(length > 1e-6f)) {
                continue;
            }
            const std::uint32_t id = mix32(idOf[at], key + 0x100u * (parent.generation + 1u));
            refine(root.position, root.position + dir * length, depth, p.jaggedness, seed, index, id, grid);
            BoltBranch branch;
            branch.first = static_cast<std::uint32_t>(out.vertices.size());
            branch.count = need;
            branch.generation = parent.generation + 1;
            branch.parent = static_cast<std::uint32_t>(at);
            branch.root = rootVertex;
            emitPath(grid, depth, root.s, root.intensity * p.branchDecay, 0.6f, root.width * 0.6f, 0.75f, out);
            depthOf[out.paths.size()] = depth;
            idOf[out.paths.size()] = id;
            out.paths.push_back(branch);
        }
    }
}

void blendBolts(const BoltPath& a, const BoltPath& b, float f, BoltPath& out) {
    f = std::clamp(f, 0.0f, 1.0f);
    out.vertices.clear();
    out.paths.clear();
    out.maxS = 0.0f;
    if (a.paths.empty() || b.paths.empty() || a.paths[0].count != b.paths[0].count) {
        const BoltPath& src = f < 0.5f ? a : b;
        out.vertices.assign(src.vertices.begin(), src.vertices.end());
        out.paths.assign(src.paths.begin(), src.paths.end());
        out.maxS = src.maxS;
        return;
    }
    const std::uint32_t n = a.paths[0].count;
    // Displacements from the chord, blended so their size holds: independent displacements lerped
    // shrink by sqrt((1-f)^2 + f^2), which is 0.71 half-way -- a visibly straighter arc mid-writhe.
    // Both sources lean at most 35 degrees per segment, so their plain blend does too; the size-holding
    // scale is then capped so the result still does (it is a uniform scale of the lateral offsets,
    // which keeps the endpoints and the strictly increasing progress along the chord).
    float norm = 1.0f / std::sqrt((1.0f - f) * (1.0f - f) + f * f);
    float steepest = 0.0f;
    const float h = 1.0f / static_cast<float>(n - 1);
    for (std::uint32_t k = 0; k + 1 < n; ++k) {
        const glm::vec3 l0 = a.vertices[k].position * (1.0f - f) + b.vertices[k].position * f;
        const glm::vec3 l1 = a.vertices[k + 1].position * (1.0f - f) + b.vertices[k + 1].position * f;
        steepest = std::max(steepest, glm::length(glm::vec2(l1.x - l0.x, l1.y - l0.y)) / h);
    }
    if (steepest * norm > kMaxLean) {
        norm = std::max(kMaxLean / std::max(steepest, 1e-9f), 0.0f);
    }
    float s = 0.0f;
    glm::vec3 prev(0.0f);
    for (std::uint32_t k = 0; k < n; ++k) {
        const float u = static_cast<float>(k) / static_cast<float>(n - 1);
        const glm::vec3 chord(0.0f, 0.0f, u);
        const glm::vec3 da = a.vertices[k].position - chord;
        const glm::vec3 db = b.vertices[k].position - chord;
        BoltVertex v = a.vertices[k];
        v.position = chord + (da * (1.0f - f) + db * f) * norm;
        v.position.z = u; // on the chord's grid exactly, as both sources are
        s += glm::length(v.position - prev);
        prev = v.position;
        v.s = s;
        out.vertices.push_back(v);
        out.maxS = std::max(out.maxS, s);
    }
    BoltBranch main = a.paths[0];
    out.paths.push_back(main);

    // Each source's branches, moved with the channel they hang from and faded by their weight. A
    // branch of a branch moves with its parent.
    std::array<glm::vec3, kBoltMaxBranches + 1> shift{};
    const auto carry = [&](const BoltPath& src, float weight) {
        if (weight <= 1e-4f) {
            return;
        }
        const auto base = static_cast<std::uint32_t>(out.paths.size()) - 1u; // src path i -> out base + i
        for (std::size_t i = 1; i < src.paths.size() && i < shift.size(); ++i) {
            const BoltBranch& br = src.paths[i];
            glm::vec3 d(0.0f);
            if (br.parent == 0) {
                d = out.vertices[br.root].position - src.vertices[br.root].position;
            } else if (br.parent < shift.size()) {
                d = shift[br.parent];
            }
            shift[i] = d;
            BoltBranch copy = br;
            copy.first = static_cast<std::uint32_t>(out.vertices.size());
            copy.parent = br.parent == 0 ? 0u : base + br.parent;
            const float rootS = out.vertices[br.root].s;
            const float srcRootS = src.vertices[br.root].s;
            copy.root = br.parent == 0 ? br.root : base + br.root; // only meaningful for the channel
            for (std::uint32_t k = 0; k < br.count; ++k) {
                BoltVertex v = src.vertices[br.first + k];
                v.position += d;
                v.intensity *= weight;
                v.s = v.s - srcRootS + rootS;
                out.vertices.push_back(v);
                out.maxS = std::max(out.maxS, v.s);
            }
            out.paths.push_back(copy);
        }
    };
    carry(a, 1.0f - f);
    carry(b, f);
}

// ---- the cache ------------------------------------------------------------------------------------

std::uint32_t boltParamsKey(const BoltParams& params) {
    std::uint32_t h = mix32(static_cast<std::uint32_t>(params.depth), std::bit_cast<std::uint32_t>(params.jaggedness));
    h = mix32(h, std::bit_cast<std::uint32_t>(params.branchProbability));
    h = mix32(h, std::bit_cast<std::uint32_t>(params.branchDecay));
    h = mix32(h, static_cast<std::uint32_t>(params.generations));
    return mix32(h, params.maxVertices);
}

namespace {
bool sameParams(const BoltParams& a, const BoltParams& b) {
    return a.depth == b.depth && std::bit_cast<std::uint32_t>(a.jaggedness) == std::bit_cast<std::uint32_t>(b.jaggedness) &&
           std::bit_cast<std::uint32_t>(a.branchProbability) == std::bit_cast<std::uint32_t>(b.branchProbability) &&
           std::bit_cast<std::uint32_t>(a.branchDecay) == std::bit_cast<std::uint32_t>(b.branchDecay) &&
           a.generations == b.generations && a.maxVertices == b.maxVertices;
}
} // namespace

const BoltPath& BoltCache::get(const BoltParams& params, std::uint64_t seed, std::uint32_t index) {
    const std::uint32_t key = boltParamsKey(params);
    ++clock_;
    Slot* oldest = &slots_[0];
    for (Slot& slot : slots_) {
        // The key is a hash; the parameters it stands for are compared in full, so two parameter sets
        // that collide are two entries, never one path returned for the other.
        if (slot.used && slot.seed == seed && slot.index == index && slot.paramsKey == key &&
            sameParams(slot.params, params)) {
            slot.lastUse = clock_;
            ++hits_;
            return slot.path;
        }
        if (!slot.used || (oldest->used && slot.lastUse < oldest->lastUse)) {
            oldest = &slot;
        }
    }
    ++misses_;
    Slot& slot = *oldest;
    slot.seed = seed;
    slot.index = index;
    slot.paramsKey = key;
    slot.lastUse = clock_;
    slot.used = true;
    slot.params = params;
    generateBolt(params, seed, index, slot.path);
    return slot.path;
}

void BoltCache::clear() {
    for (Slot& slot : slots_) {
        slot.used = false;
    }
    hits_ = 0;
    misses_ = 0;
}

BoltCache& boltCache() {
    thread_local BoltCache cache;
    return cache;
}

// ---- placing ---------------------------------------------------------------------------------------

glm::mat3 BoltPlacement::rotation() const {
    const glm::vec3 chord = end - start;
    const float len = glm::length(chord);
    if (!(len > 1e-9f)) {
        return glm::mat3(1.0f);
    }
    const glm::vec3 d = chord / len;
    const float c = d.z;
    if (c < -0.99999f) {
        // Straight down -Z: half a turn about X (the one place the minimal rotation is undefined).
        return glm::mat3(glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f));
    }
    // Rodrigues for the rotation of +Z onto d: I + [v]x + [v]x^2 / (1 + c), v = z x d.
    const glm::vec3 v(-d.y, d.x, 0.0f);
    const float k = 1.0f / (1.0f + c);
    // Column-major: m[col][row].
    glm::mat3 m(1.0f);
    m[0][0] = 1.0f - k * (v.y * v.y + v.z * v.z);
    m[0][1] = v.z + k * v.x * v.y;
    m[0][2] = -v.y + k * v.x * v.z;
    m[1][0] = -v.z + k * v.x * v.y;
    m[1][1] = 1.0f - k * (v.x * v.x + v.z * v.z);
    m[1][2] = v.x + k * v.y * v.z;
    m[2][0] = v.y + k * v.x * v.z;
    m[2][1] = -v.x + k * v.y * v.z;
    m[2][2] = 1.0f - k * (v.x * v.x + v.y * v.y);
    return m;
}

glm::vec3 BoltPlacement::place(const glm::vec3& canonical, const glm::mat3& r) const {
    // The two ends of the main channel land EXACTLY on the endpoints: a bolt that must touch a hull
    // or a marked spot does, to the bit.
    if (canonical.x == 0.0f && canonical.y == 0.0f) {
        if (canonical.z == 0.0f) {
            return start;
        }
        if (canonical.z == 1.0f) {
            return end;
        }
    }
    const float len = glm::length(end - start);
    const float u = std::clamp(canonical.z, 0.0f, 1.0f);
    return start + (r * canonical) * len + bow * (4.0f * u * (1.0f - u));
}

int boltLodDepth(int authored, float length, float distance) {
    int depth = std::clamp(authored, 1, kBoltMaxDepth);
    // About 1,100 pixels per radian: a 1080-line frame at the engine's usual ~55 degree lens.
    constexpr float kPixelsPerRadian = 1100.0f;
    constexpr float kFinestPixels = 3.0f;
    if (!(length > 0.0f) || !std::isfinite(distance)) {
        return depth;
    }
    const float pixels = length / std::max(distance, 1e-3f) * kPixelsPerRadian;
    while (depth > 2 && pixels / static_cast<float>(1 << depth) < kFinestPixels) {
        --depth;
    }
    return depth;
}

// ---- drawing ---------------------------------------------------------------------------------------

namespace {

// A zero-width, zero-opacity point just past `p` along `dir`: where a bridge to the next path
// starts or ends. Far enough from `p` (4 mm) that the ribbon's coincident-point filter keeps it.
RibbonPoint bridgePoint(const glm::vec3& p, const glm::vec3& dir) {
    RibbonPoint r;
    const float len = glm::length(dir);
    r.position = p + (len > 1e-9f ? dir / len : glm::vec3(0.0f, 1.0f, 0.0f)) * 0.004f;
    r.width = 0.0f;
    r.color = glm::vec3(0.0f);
    r.opacity = 0.0f;
    return r;
}

// The points of one layer of the whole bolt: every path, revealed up to `look.reveal`, every
// `stride`-th vertex (and always the last), joined by bridges. `core` picks the layer.
void layerPoints(const BoltPath& path, const BoltPlacement& placement, const glm::mat3& r, const BoltLook& look,
                 bool core, int stride, std::vector<RibbonPoint>& out) {
    out.clear();
    stride = std::max(stride, 1);
    const glm::vec3 radiance = core ? look.core : look.glow;
    const float width = core ? look.coreWidth : look.glowWidth;
    const float opacity = std::clamp(look.opacity, 0.0f, 1.0f);
    const auto point = [&](const BoltVertex& v, float gain) {
        RibbonPoint pt;
        pt.position = placement.place(v.position, r);
        pt.width = width * v.width;
        pt.color = radiance * (v.intensity * gain);
        pt.opacity = opacity;
        return pt;
    };
    for (std::size_t i = 0; i < path.paths.size(); ++i) {
        const BoltBranch& br = path.paths[i];
        if (br.count < 2) {
            continue;
        }
        const float gain = br.generation == 0 ? 1.0f : std::max(look.branchGain, 0.0f);
        if (gain <= 0.0f) {
            continue;
        }
        const std::span<const BoltVertex> vs = path.path(i);
        if (vs.front().s > look.reveal) {
            continue; // the leader has not reached this branch yet
        }
        const std::size_t start = out.size();
        // Revealed vertices at the stride, then the reveal's cut (or the last vertex).
        std::size_t last = 0;
        for (std::size_t k = 0; k < vs.size(); ++k) {
            if (vs[k].s > look.reveal) {
                break;
            }
            last = k;
        }
        for (std::size_t k = 0; k <= last; k += static_cast<std::size_t>(stride)) {
            out.push_back(point(vs[k], gain));
        }
        if (last % static_cast<std::size_t>(stride) != 0) {
            out.push_back(point(vs[last], gain));
        }
        if (last + 1 < vs.size() && vs[last + 1].s > look.reveal) {
            // The leader's tip: interpolated to exactly the reveal, so it grows smoothly.
            const BoltVertex& a = vs[last];
            const BoltVertex& b = vs[last + 1];
            const float u = std::clamp((look.reveal - a.s) / std::max(b.s - a.s, 1e-9f), 0.0f, 1.0f);
            if (u > 1e-3f) {
                BoltVertex tip = a;
                tip.position = glm::mix(a.position, b.position, u);
                tip.intensity = a.intensity + (b.intensity - a.intensity) * u;
                tip.width = a.width + (b.width - a.width) * u;
                out.push_back(point(tip, gain));
            }
        }
        if (out.size() - start < 2) {
            out.resize(start);
            continue;
        }
        if (!core && look.hasEye) {
            // Fade the glow where it runs towards the camera: |t x v| is 1 side-on, 0 end-on.
            const std::size_t m = out.size() - start;
            for (std::size_t k = 0; k < m; ++k) {
                RibbonPoint& p = out[start + k];
                const glm::vec3 t = out[start + std::min(k + 1, m - 1)].position - out[start + (k == 0 ? 0 : k - 1)].position;
                const glm::vec3 v = look.eye - p.position;
                const float tl = glm::length(t);
                const float vl = glm::length(v);
                if (tl > 1e-6f && vl > 1e-6f) {
                    const float side = glm::length(glm::cross(t / tl, v / vl));
                    const float x = std::clamp((side - 0.2f) / 0.45f, 0.0f, 1.0f);
                    p.opacity *= x * x * (3.0f - 2.0f * x);
                    // And no wider on screen than about 55 px at the ~1,100 px per radian the level of
                    // detail assumes: a glow is a halo round a line, not a plank in front of the lens.
                    p.width = std::min(p.width, 0.05f * vl);
                }
            }
        }
        if (start > 0) {
            // Bridge from the previous path's end to this one's start, invisible both ways.
            const RibbonPoint firstReal = out[start];
            const RibbonPoint secondReal = out[start + 1];
            const RibbonPoint prevEnd = out[start - 1];
            const RibbonPoint prevBefore = out[start - 2];
            const RibbonPoint leave = bridgePoint(prevEnd.position, prevEnd.position - prevBefore.position);
            const RibbonPoint arrive = bridgePoint(firstReal.position, firstReal.position - secondReal.position);
            out.insert(out.begin() + static_cast<std::ptrdiff_t>(start), {leave, arrive});
        }
    }
}

} // namespace

std::uint32_t boltStripVertices(const BoltPath& path, int glowStride) {
    std::uint32_t core = 0;
    std::uint32_t glow = 0;
    glowStride = std::max(glowStride, 1); // 0 (automatic) is counted at its densest
    for (const BoltBranch& br : path.paths) {
        if (br.count < 2) {
            continue;
        }
        core += br.count + 2u;
        glow += (br.count - 1u) / static_cast<std::uint32_t>(glowStride) + 2u + 2u;
    }
    return (core + glow) * 2u;
}

BoltFit appendBoltStrips(RibbonSink& sink, const BoltPath& path, const BoltPlacement& placement,
                         const BoltLook& lookIn, std::vector<RibbonPoint>& scratch) {
    if (path.paths.empty() || !(placement.length() > 1e-4f) || !(lookIn.opacity > 1e-4f) || lookIn.reveal <= 0.0f) {
        return BoltFit::Nothing;
    }
    const glm::mat3 r = placement.rotation();
    BoltLook look = lookIn;
    if (look.glowStride <= 0) {
        const float segment = placement.length() / static_cast<float>(std::max(path.paths[0].count, 2u) - 1u);
        look.glowStride = std::clamp(static_cast<int>(look.glowWidth / std::max(segment, 1e-6f) + 0.5f), 1, 32);
    }
    // Both layers are measured before either is written, so a bolt is never half drawn for want of
    // vertices. The core is built second into the same scratch, so measure the glow first.
    layerPoints(path, placement, r, look, false, look.glowStride, scratch);
    const std::size_t glowPoints = scratch.size();
    layerPoints(path, placement, r, look, true, 1, scratch);
    const std::size_t corePoints = scratch.size();
    if (corePoints < 2) {
        return BoltFit::Nothing;
    }
    const std::uint32_t need = RibbonSink::verticesFor(corePoints, 0) + RibbonSink::verticesFor(glowPoints, 0);
    if (need > sink.remaining()) {
        return BoltFit::NoRoom;
    }
    RibbonStyle coreStyle;
    coreStyle.blend = RibbonBlend::Additive;
    coreStyle.coreFraction = 0.55f;
    coreStyle.glow = 0.0f;
    coreStyle.coreBoost = 1.0f;
    coreStyle.softEdge = true;
    const RibbonFit coreFit = sink.appendStrip(scratch, 0, coreStyle);
    if (coreFit == RibbonFit::NoFit) {
        return BoltFit::NoRoom;
    }
    if (coreFit == RibbonFit::Nothing) {
        return BoltFit::Nothing;
    }
    if (glowPoints < 2) {
        return BoltFit::Written;
    }
    layerPoints(path, placement, r, look, false, look.glowStride, scratch);
    RibbonStyle glowStyle;
    glowStyle.blend = RibbonBlend::Additive;
    // A glow-only profile: no core (a hair-thin hard one with no boost), the Gaussian across it all.
    glowStyle.coreFraction = 0.0f;
    glowStyle.glow = 1.0f;
    glowStyle.coreBoost = 0.0f;
    glowStyle.softEdge = false;
    const RibbonFit glowFit = sink.appendStrip(scratch, 0, glowStyle);
    return glowFit == RibbonFit::NoFit ? BoltFit::CoreOnly : BoltFit::Written;
}

BoltFit appendStreaks(RibbonSink& sink, std::span<const BoltStreak> streaks, std::vector<RibbonPoint>& scratch) {
    scratch.clear();
    for (const BoltStreak& s : streaks) {
        if (!(s.opacity > 1e-4f) || glm::length(s.head - s.tail) < 2e-3f) {
            continue;
        }
        RibbonPoint tail;
        tail.position = s.tail;
        tail.width = s.width * 0.4f;
        tail.color = s.color * 0.35f;
        tail.opacity = std::clamp(s.opacity, 0.0f, 1.0f);
        RibbonPoint head = tail;
        head.position = s.head;
        head.width = s.width;
        head.color = s.color;
        if (!scratch.empty()) {
            const RibbonPoint prevEnd = scratch.back();
            const RibbonPoint prevBefore = scratch[scratch.size() - 2];
            scratch.push_back(bridgePoint(prevEnd.position, prevEnd.position - prevBefore.position));
            scratch.push_back(bridgePoint(tail.position, tail.position - head.position));
        }
        scratch.push_back(tail);
        scratch.push_back(head);
    }
    if (scratch.size() < 2) {
        return BoltFit::Nothing;
    }
    if (RibbonSink::verticesFor(scratch.size(), 0) > sink.remaining()) {
        return BoltFit::NoRoom;
    }
    RibbonStyle style;
    style.blend = RibbonBlend::Additive;
    style.coreFraction = 0.3f;
    style.glow = 0.6f;
    style.coreBoost = 1.0f;
    style.softEdge = true;
    switch (sink.appendStrip(scratch, 0, style)) {
    case RibbonFit::Written:
    case RibbonFit::Reduced: return BoltFit::Written;
    case RibbonFit::NoFit: return BoltFit::NoRoom;
    case RibbonFit::Nothing: return BoltFit::Nothing;
    }
    return BoltFit::Nothing;
}

} // namespace avgen::world
