#include "crowd/crowd_world.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <thread>

namespace avgen::crowd {

const char* agentModeName(AgentMode mode) {
    switch (mode) {
    case AgentMode::Idle: return "idle";
    case AgentMode::Departing: return "departing";
    case AgentMode::Moving: return "moving";
    case AgentMode::Arrived: return "arrived";
    case AgentMode::Dwelling: return "dwelling";
    case AgentMode::Unreachable: return "unreachable";
    }
    return "?";
}

namespace {

constexpr double kDt = 1.0 / kStepsPerSecond;
constexpr float kDtF = 1.0f / kStepsPerSecond;
constexpr float kRateQuantum = 1.0f / 32.0f; // clip rates on a lattice: shared poses, fewer re-pushes

std::uint64_t mix64(std::uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
// A seeded number in [0, 1): a hash of its inputs, never a stream (ADR-2223).
float hash01(std::uint64_t a, std::uint64_t b, std::uint64_t c, std::uint64_t d = 0) {
    const std::uint64_t h = mix64(mix64(mix64(mix64(a) ^ b) ^ c) ^ d);
    return static_cast<float>(h >> 40) / static_cast<float>(1ull << 24);
}

std::uint64_t fnv(std::uint64_t h, const void* data, std::size_t n) {
    const auto* b = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < n; ++i) {
        h ^= b[i];
        h *= 1099511628211ull;
    }
    return h;
}
template <typename T> std::uint64_t fnvVec(std::uint64_t h, const std::vector<T>& v) {
    return v.empty() ? h : fnv(h, v.data(), v.size() * sizeof(T));
}

double threadMicros() {
    timespec t{};
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return static_cast<double>(t.tv_sec) * 1e6 + static_cast<double>(t.tv_nsec) * 1e-3;
}

float distToSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 d = b - a;
    const float l2 = glm::dot(d, d);
    const float t = l2 > 0.0f ? std::clamp(glm::dot(p - a, d) / l2, 0.0f, 1.0f) : 0.0f;
    return glm::length(p - (a + d * t));
}

float wrapPi(float a) {
    while (a > 3.14159265f) a -= 6.2831853f;
    while (a < -3.14159265f) a += 6.2831853f;
    return a;
}

// The slot member m of M takes in a destination.
glm::vec2 slotFor(const ScheduleEntry& e, std::size_t m, std::size_t count, std::uint32_t seed, std::size_t entry) {
    if (e.area) {
        const AreaDesc& a = *e.area;
        const float w = std::max(1e-3f, a.halfExtents.x), h = std::max(1e-3f, a.halfExtents.y);
        const std::size_t cols =
            std::max<std::size_t>(1, static_cast<std::size_t>(std::lround(std::sqrt(static_cast<float>(count) * w / h))));
        const std::size_t rows = (count + cols - 1) / cols;
        const float cw = 2.0f * w / static_cast<float>(cols), ch = 2.0f * h / static_cast<float>(rows);
        const std::size_t col = m % cols, row = m / cols;
        const float jx = (hash01(seed, entry, m, 1) - 0.5f) * 0.5f * cw;
        const float jz = (hash01(seed, entry, m, 2) - 0.5f) * 0.5f * ch;
        return a.center + glm::vec2(-w + (static_cast<float>(col) + 0.5f) * cw + jx,
                                    -h + (static_cast<float>(row) + 0.5f) * ch + jz);
    }
    const std::size_t n = e.points.size();
    const glm::vec2 base = e.points[m % n];
    const std::size_t k = m / n; // members sharing a point stand on a golden-angle spiral round it
    if (k == 0) {
        return base;
    }
    const float r = 0.8f * std::sqrt(static_cast<float>(k));
    const float ang = 2.39996323f * static_cast<float>(k);
    return base + r * glm::vec2(std::cos(ang), std::sin(ang));
}

glm::vec2 wanderPoint(const AreaDesc& a, std::uint32_t seed, std::size_t entry, std::size_t agent, std::uint32_t n) {
    return a.center + glm::vec2((hash01(seed, entry, agent, 1000u + n) * 2.0f - 1.0f) * a.halfExtents.x,
                                (hash01(seed, entry, agent, 2000u + n) * 2.0f - 1.0f) * a.halfExtents.y);
}

} // namespace

void CrowdWorld::clear() {
    desc_ = {};
    agents_.clear();
    members_.clear();
    scheduleStep_.clear();
    walls_.clear();
    surface_ = nullptr;
    state_ = {};
    checkpoints_.clear();
    inputKey_ = 0;
}

void CrowdWorld::configure(CrowdDesc desc, std::vector<AgentInit> agents, const nav::INavSurface* surface) {
    desc_ = std::move(desc);
    agents_ = std::move(agents);
    surface_ = surface;
    threads_ = desc_.threads;
    members_.assign(desc_.groups.size(), {});
    for (std::size_t i = 0; i < agents_.size(); ++i) {
        const int g = std::clamp(agents_[i].group, 0, static_cast<int>(desc_.groups.size()) - 1);
        agents_[i].group = g;
        members_[static_cast<std::size_t>(g)].push_back(i);
    }
    scheduleStep_.clear();
    for (const ScheduleEntry& e : desc_.schedule) {
        scheduleStep_.push_back(std::max<std::int64_t>(0, static_cast<std::int64_t>(std::ceil(e.at * kStepsPerSecond - 1e-9))));
    }
    walls_.clear();
    for (const WallDesc& w : desc_.walls) {
        for (std::size_t k = 1; k < w.points.size(); ++k) {
            walls_.push_back(OrcaSegment{w.points[k - 1], w.points[k], 0.5f * w.thickness});
        }
    }
    std::uint64_t h = 1469598103934665603ull;
    const std::string block = crowdToJson(desc_).dump();
    h = fnv(h, block.data(), block.size());
    for (const AgentInit& a : agents_) {
        h = fnv(h, a.node.data(), a.node.size());
        h = fnv(h, &a.group, sizeof a.group);
        h = fnv(h, &a.position, sizeof a.position);
        h = fnv(h, &a.yaw, sizeof a.yaw);
    }
    const std::uint64_t sk = surface_ != nullptr ? surface_->contentKey() : 0;
    inputKey_ = fnv(h, &sk, sizeof sk);
    checkpoints_.clear();
    reset();
}

void CrowdWorld::reset() {
    const std::size_t n = agents_.size();
    CrowdState s;
    s.pos.resize(n);
    s.vel.assign(n, glm::vec2(0.0f));
    s.goal.resize(n);
    s.face.assign(n, glm::vec2(0.0f));
    s.y.assign(n, 0.0f);
    s.yaw.resize(n);
    s.cruise.resize(n);
    s.slowFor.assign(n, 0.0f);
    s.anchor.resize(n);
    s.anchorStep.assign(n, 0);
    s.mode.assign(n, AgentMode::Idle);
    s.hasFace.assign(n, 0);
    s.needPath.assign(n, 0);
    s.departAt.assign(n, 0);
    s.dwellUntil.assign(n, 0);
    s.command.assign(n, -1);
    s.wanderCount.assign(n, 0);
    s.path.assign(n, {});
    s.cursor.assign(n, 0);
    s.pathStatus.assign(n, nav::NavStatus::Ok);
    s.gait.assign(n, entity::Gait{});
    s.activity.assign(n, entity::Activity::Idle);
    s.activityStep.assign(n, 0);
    s.clipPhase.assign(n, 0.0);
    s.clipRate.assign(n, 1.0f);
    for (std::size_t i = 0; i < n; ++i) {
        const AgentInit& a = agents_[i];
        const GroupDesc& g = desc_.groups[static_cast<std::size_t>(a.group)];
        s.pos[i] = a.position;
        s.anchor[i] = a.position;
        s.goal[i] = a.position;
        s.yaw[i] = a.yaw;
        float y = 0.0f;
        if (surface_ != nullptr && surface_->ground(a.position, y)) {
            s.y[i] = y;
        }
        // The cruise speed: a seeded draw, quantised to the group's buckets (ADR-2226).
        const int buckets = std::max(1, g.speedBuckets);
        const int b = std::min(buckets - 1, static_cast<int>(hash01(g.seed, i, 7) * static_cast<float>(buckets)));
        s.cruise[i] = buckets == 1 ? 0.5f * (g.speedMin + g.speedMax)
                                   : g.speedMin + (g.speedMax - g.speedMin) * static_cast<float>(b) /
                                                      static_cast<float>(buckets - 1);
        // Idle clips start at a seeded second so a standing crowd does not breathe in step.
        s.clipPhase[i] = static_cast<double>(hash01(g.seed, i, 11)) * 4.0;
    }
    state_ = std::move(s);
    applySchedule(0);
    if (checkpointsOn_ && checkpoints_.find(0) == checkpoints_.end()) {
        checkpoints_.emplace(0, state_);
    }
}

void CrowdWorld::applySchedule(std::int64_t k) {
    while (state_.scheduleCursor < desc_.schedule.size() && scheduleStep_[state_.scheduleCursor] <= k) {
        assignCommand(state_.scheduleCursor, k);
        ++state_.scheduleCursor;
    }
}

void CrowdWorld::assignCommand(std::size_t entryIndex, std::int64_t k) {
    const ScheduleEntry& e = desc_.schedule[entryIndex];
    const auto git = std::find_if(desc_.groups.begin(), desc_.groups.end(),
                                  [&](const GroupDesc& g) { return g.name == e.group; });
    if (git == desc_.groups.end()) {
        return;
    }
    const std::size_t gi = static_cast<std::size_t>(git - desc_.groups.begin());
    const std::vector<std::size_t>& members = members_[gi];
    // Which slot each member takes. For an area: the members nearest it take the slots deepest into
    // it, so the first to arrive walk to the back and nobody has to squeeze past a standing crowd.
    // A pure function of the state at the command (positions), ties broken by member order.
    std::vector<std::size_t> slotOf(members.size());
    for (std::size_t m = 0; m < members.size(); ++m) {
        slotOf[m] = m;
    }
    if (e.area && e.policy == SlotPolicy::Spread && !members.empty()) {
        glm::vec2 centroid(0.0f);
        for (const std::size_t i : members) {
            centroid += state_.pos[i];
        }
        centroid /= static_cast<float>(members.size());
        glm::vec2 dir = e.area->center - centroid;
        dir = glm::dot(dir, dir) > 1e-6f ? glm::normalize(dir) : glm::vec2(1.0f, 0.0f);
        std::vector<std::size_t> byNear(members.size()), byDeep(members.size());
        std::vector<float> near(members.size()), deep(members.size());
        for (std::size_t m = 0; m < members.size(); ++m) {
            byNear[m] = byDeep[m] = m;
            near[m] = glm::length(e.area->center - state_.pos[members[m]]);
            deep[m] = glm::dot(slotFor(e, m, members.size(), git->seed, entryIndex) - e.area->center, dir);
        }
        std::stable_sort(byNear.begin(), byNear.end(), [&](std::size_t a, std::size_t b) { return near[a] < near[b]; });
        std::stable_sort(byDeep.begin(), byDeep.end(), [&](std::size_t a, std::size_t b) { return deep[a] > deep[b]; });
        for (std::size_t r = 0; r < members.size(); ++r) {
            slotOf[byNear[r]] = byDeep[r];
        }
    }
    for (std::size_t m = 0; m < members.size(); ++m) {
        const std::size_t i = members[m];
        state_.command[i] = static_cast<std::int32_t>(entryIndex);
        state_.hasFace[i] = e.face ? 1 : 0;
        if (e.face) {
            state_.face[i] = *e.face;
        }
        state_.path[i].clear();
        state_.cursor[i] = 0;
        state_.needPath[i] = 0;
        state_.slowFor[i] = 0.0f;
        state_.anchor[i] = state_.pos[i];
        state_.anchorStep[i] = k;
        if (e.command == CommandKind::Hold) {
            state_.mode[i] = AgentMode::Idle;
            state_.goal[i] = state_.pos[i];
            continue;
        }
        if (e.command == CommandKind::Wander) {
            state_.wanderCount[i] = 0;
            state_.goal[i] = e.area ? wanderPoint(*e.area, git->seed, entryIndex, i, 0)
                                    : e.points[(m + i) % e.points.size()];
        } else {
            state_.goal[i] = slotFor(e, slotOf[m], members.size(), git->seed, entryIndex);
        }
        state_.mode[i] = AgentMode::Departing;
        state_.departAt[i] =
            k + static_cast<std::int64_t>(std::floor(hash01(git->seed, entryIndex, m, 3) * e.stagger * kStepsPerSecond));
    }
}

glm::vec2 CrowdWorld::clearOfStatics(glm::vec2 p, float body) const {
    std::vector<nav::NavDisc> discs;
    std::vector<nav::NavSegment> segs;
    if (surface_ != nullptr) {
        surface_->staticObstacles(p, body + 1.0f, discs, segs);
    }
    for (int iteration = 0; iteration < 4; ++iteration) {
        glm::vec2 push(0.0f);
        const auto away = [&](glm::vec2 c, float reach) {
            const glm::vec2 d = p - c;
            const float dist = glm::length(d);
            if (dist < reach && dist > 1e-5f) {
                push += d / dist * (reach - dist);
            }
        };
        for (const OrcaSegment& w : walls_) {
            const glm::vec2 ab = w.b - w.a;
            const float l2 = glm::dot(ab, ab);
            const float t = l2 > 0.0f ? std::clamp(glm::dot(p - w.a, ab) / l2, 0.0f, 1.0f) : 0.0f;
            away(w.a + ab * t, body + w.radius);
        }
        for (const nav::NavDisc& d : discs) {
            away(d.center, body + d.radius);
        }
        if (glm::dot(push, push) < 1e-8f) {
            break;
        }
        p += push;
    }
    return p;
}

void CrowdWorld::servePaths() {
    const std::size_t n = agents_.size();
    if (n == 0) {
        return;
    }
    std::uint32_t served = 0;
    for (std::size_t j = 0; j < n; ++j) {
        const std::size_t i = (state_.pathCursor + j) % n;
        if (state_.needPath[i] == 0) {
            continue;
        }
        state_.needPath[i] = 0;
        const ScheduleEntry* e = state_.command[i] >= 0 ? &desc_.schedule[static_cast<std::size_t>(state_.command[i])] : nullptr;
        const float tolerance = e != nullptr ? e->arriveRadius : 0.35f;
        state_.path[i].clear();
        state_.cursor[i] = 0;
        if (surface_ == nullptr || !surface_->valid()) {
            state_.path[i].push_back(state_.goal[i]);
            state_.pathStatus[i] = nav::NavStatus::Ok;
        } else {
            nav::NavPath p = surface_->findPath(state_.pos[i], state_.goal[i], tolerance);
            lastStep_.pathExpansions += p.expansions;
            state_.pathStatus[i] = p.status;
            if (p.ok()) {
                state_.path[i] = std::move(p.points);
                // A grid corner can sit closer to a wall than this body fits (a 0.5 m cell centre in a
                // 2 m doorway): move every corner but the last out of reach of the static solids.
                const float body = desc_.groups[static_cast<std::size_t>(agents_[i].group)].radius + 0.05f;
                for (std::size_t w = 0; w + 1 < state_.path[i].size(); ++w) {
                    state_.path[i][w] = clearOfStatics(state_.path[i][w], body);
                }
                // The route ends where the slot is, not at the cell centre the grid stopped at,
                // whenever the slot itself is standable.
                if (state_.path[i].empty() || glm::length(state_.path[i].back() - state_.goal[i]) > 1e-3f) {
                    state_.path[i].push_back(state_.goal[i]);
                }
            } else {
                state_.mode[i] = AgentMode::Unreachable;
            }
        }
        ++lastStep_.pathQueries;
        if (++served >= static_cast<std::uint32_t>(desc_.pathsPerStep)) {
            state_.pathCursor = (i + 1) % n;
            return;
        }
    }
}

glm::vec2 CrowdWorld::preferredVelocity(std::size_t i) {
    if (state_.mode[i] != AgentMode::Moving || state_.path[i].empty()) {
        return glm::vec2(0.0f);
    }
    const GroupDesc& g = desc_.groups[static_cast<std::size_t>(agents_[i].group)];
    std::vector<glm::vec2>& path = state_.path[i];
    std::uint16_t& c = state_.cursor[i];
    const glm::vec2 p = state_.pos[i];
    const std::size_t last = path.size() - 1;
    // Reached a corner: move on -- but only once the next corner can be seen. A body pressed against
    // a wall beside a doorway is within reach of the doorway's corner and still on the wrong side of it.
    while (c < last) {
        const float d = glm::length(path[c] - p);
        if (d >= std::max(0.6f, 2.0f * g.radius)) {
            break;
        }
        if (d > 0.2f && surface_ != nullptr && !surface_->lineClear(p, path[c + 1], 0.9f * g.radius)) {
            break;
        }
        ++c;
    }
    // A shortcut to the next corner when the straight line is clear -- tried on one step in four
    // per agent, staggered by index, so the cost is spread and still a function of the step.
    if (c < last && surface_ != nullptr && ((state_.step + static_cast<std::int64_t>(i)) & 3) == 0 &&
        surface_->lineClear(p, path[c + 1], 0.9f * g.radius)) {
        ++c;
    }
    const glm::vec2 to = path[c] - p;
    const float d = glm::length(to);
    if (d < 1e-4f) {
        return glm::vec2(0.0f);
    }
    float speed = state_.cruise[i];
    if (c == last) {
        speed *= std::clamp(d / 1.5f, 0.15f, 1.0f); // arrive (Reynolds)
    } else {
        speed = std::min(speed, d / kDtF); // never overshoot a corner in one step
    }
    return to * (speed / d);
}

void CrowdWorld::stepOnce() {
    const double start = threadMicros();
    lastStep_.pathQueries = 0;
    lastStep_.pathExpansions = 0;
    const std::int64_t k = state_.step + 1;
    const std::size_t n = agents_.size();
    applySchedule(k);
    for (std::size_t i = 0; i < n; ++i) {
        if (state_.mode[i] == AgentMode::Departing && state_.departAt[i] <= k) {
            state_.mode[i] = AgentMode::Moving;
            state_.needPath[i] = 1;
        } else if (state_.mode[i] == AgentMode::Dwelling && state_.dwellUntil[i] <= k) {
            const ScheduleEntry& e = desc_.schedule[static_cast<std::size_t>(state_.command[i])];
            const GroupDesc& g = desc_.groups[static_cast<std::size_t>(agents_[i].group)];
            const std::uint32_t w = ++state_.wanderCount[i];
            state_.goal[i] = e.area ? wanderPoint(*e.area, g.seed, static_cast<std::size_t>(state_.command[i]), i, w)
                                    : e.points[(i + w) % e.points.size()];
            state_.mode[i] = AgentMode::Moving;
            state_.needPath[i] = 1;
        }
    }
    servePaths();

    // Preferred velocities (serial: they may ask the surface).
    std::vector<glm::vec2> pref(n);
    for (std::size_t i = 0; i < n; ++i) {
        pref[i] = preferredVelocity(i);
    }
    // The neighbour grid, rebuilt from this state.
    constexpr float cell = 2.0f;
    glm::vec2 lo(1e30f), hi(-1e30f);
    for (const glm::vec2& p : state_.pos) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    }
    const int gw = n > 0 ? static_cast<int>((hi.x - lo.x) / cell) + 1 : 1;
    const int gh = n > 0 ? static_cast<int>((hi.y - lo.y) / cell) + 1 : 1;
    std::vector<std::int32_t> head(static_cast<std::size_t>(gw) * static_cast<std::size_t>(gh), -1);
    std::vector<std::int32_t> next(n, -1);
    const auto cellOf = [&](glm::vec2 p) {
        return glm::ivec2(std::clamp(static_cast<int>((p.x - lo.x) / cell), 0, gw - 1),
                          std::clamp(static_cast<int>((p.y - lo.y) / cell), 0, gh - 1));
    };
    for (std::size_t i = n; i-- > 0;) {
        const glm::ivec2 c = cellOf(state_.pos[i]);
        const std::size_t idx = static_cast<std::size_t>(c.x + c.y * gw);
        next[i] = head[idx];
        head[idx] = static_cast<std::int32_t>(i);
    }
    // Static obstacles per agent, gathered serially (the surface keeps scratch), in a fixed order.
    std::vector<std::uint32_t> discStart(n + 1, 0), segStart(n + 1, 0);
    std::vector<nav::NavDisc> discs;
    std::vector<nav::NavSegment> segs;
    std::vector<nav::NavDisc> tmpDiscs;
    std::vector<nav::NavSegment> tmpSegs;
    for (std::size_t i = 0; i < n; ++i) {
        discStart[i] = static_cast<std::uint32_t>(discs.size());
        segStart[i] = static_cast<std::uint32_t>(segs.size());
        if (surface_ == nullptr) {
            continue;
        }
        const GroupDesc& g = desc_.groups[static_cast<std::size_t>(agents_[i].group)];
        const float range = g.radius + g.maxSpeed * 1.0f + 0.5f;
        tmpDiscs.clear();
        tmpSegs.clear();
        surface_->staticObstacles(state_.pos[i], range, tmpDiscs, tmpSegs);
        for (const nav::NavDisc& d : tmpDiscs) {
            // An authored wall is in the obstacle field as a chain of discs (so the planner sees it)
            // and here as segments (so avoidance sees one smooth face): drop its discs.
            bool onWall = false;
            for (const OrcaSegment& w : walls_) {
                if (distToSegment(d.center, w.a, w.b) < 1e-3f) {
                    onWall = true;
                    break;
                }
            }
            if (!onWall && glm::length(d.center - state_.pos[i]) < range + d.radius) {
                discs.push_back(d);
            }
        }
        segs.insert(segs.end(), tmpSegs.begin(), tmpSegs.end());
    }
    discStart[n] = static_cast<std::uint32_t>(discs.size());
    segStart[n] = static_cast<std::uint32_t>(segs.size());

    // Jacobi ORCA: every agent reads this state; nothing is written until all have chosen.
    std::vector<glm::vec2> newVel(n, glm::vec2(0.0f));
    const auto solve = [&](std::size_t first, std::size_t stride) {
        std::vector<std::pair<float, std::int32_t>> near;
        std::vector<OrcaNeighbour> nb;
        std::vector<OrcaSegment> walls;
        std::vector<OrcaLine> lines, scratch;
        for (std::size_t i = first; i < n; i += stride) {
            const GroupDesc& g = desc_.groups[static_cast<std::size_t>(agents_[i].group)];
            const glm::vec2 p = state_.pos[i];
            near.clear();
            const glm::ivec2 c0 = cellOf(p - glm::vec2(g.neighbourDist));
            const glm::ivec2 c1 = cellOf(p + glm::vec2(g.neighbourDist));
            const float r2 = g.neighbourDist * g.neighbourDist;
            for (int z = c0.y; z <= c1.y; ++z) {
                for (int x = c0.x; x <= c1.x; ++x) {
                    for (std::int32_t j = head[static_cast<std::size_t>(x + z * gw)]; j >= 0;
                         j = next[static_cast<std::size_t>(j)]) {
                        if (static_cast<std::size_t>(j) == i) {
                            continue;
                        }
                        const glm::vec2 d = state_.pos[static_cast<std::size_t>(j)] - p;
                        const float d2 = glm::dot(d, d);
                        if (d2 < r2) {
                            near.emplace_back(d2, j);
                        }
                    }
                }
            }
            std::sort(near.begin(), near.end());
            if (near.size() > static_cast<std::size_t>(g.maxNeighbours)) {
                near.resize(static_cast<std::size_t>(g.maxNeighbours));
            }
            nb.clear();
            for (const auto& [d2, j] : near) {
                const std::size_t js = static_cast<std::size_t>(j);
                nb.push_back(OrcaNeighbour{state_.pos[js], state_.vel[js],
                                           desc_.groups[static_cast<std::size_t>(agents_[js].group)].radius, 0.5f});
            }
            for (std::uint32_t d = discStart[i]; d < discStart[i + 1]; ++d) {
                nb.push_back(OrcaNeighbour{discs[d].center, glm::vec2(0.0f), discs[d].radius, 1.0f});
            }
            walls.clear();
            walls.insert(walls.end(), walls_.begin(), walls_.end());
            for (std::uint32_t s = segStart[i]; s < segStart[i + 1]; ++s) {
                walls.push_back(OrcaSegment{segs[s].a, segs[s].b, 0.0f});
            }
            OrcaParams op;
            op.radius = g.radius;
            op.maxSpeed = g.maxSpeed;
            op.timeHorizon = g.timeHorizon;
            op.dt = kDtF;
            newVel[i] = orcaVelocity(p, state_.vel[i], pref[i], nb, walls, op, lines, scratch);
        }
    };
    const int threads = (threads_ > 1 && n >= 32) ? threads_ : 1;
    if (threads > 1) {
        std::vector<std::thread> pool;
        for (int t = 1; t < threads; ++t) {
            pool.emplace_back(solve, static_cast<std::size_t>(t), static_cast<std::size_t>(threads));
        }
        solve(0, static_cast<std::size_t>(threads));
        for (std::thread& t : pool) {
            t.join();
        }
    } else {
        solve(0, 1);
    }

    // Integrate, arrive, face, gait.
    for (std::size_t i = 0; i < n; ++i) {
        const GroupDesc& g = desc_.groups[static_cast<std::size_t>(agents_[i].group)];
        state_.vel[i] = newVel[i];
        state_.pos[i] += newVel[i] * kDtF;
        float y = state_.y[i];
        if (surface_ != nullptr && surface_->ground(state_.pos[i], y)) {
            state_.y[i] = y;
        }
        const float speed = glm::length(newVel[i]);
        const float prevYaw = state_.yaw[i];
        float want = prevYaw;
        if (speed > 0.15f) {
            want = std::atan2(newVel[i].x, newVel[i].y);
        } else if (state_.hasFace[i] != 0 &&
                   (state_.mode[i] == AgentMode::Arrived || state_.mode[i] == AgentMode::Dwelling)) {
            const glm::vec2 f = state_.face[i] - state_.pos[i];
            if (glm::dot(f, f) > 1e-6f) {
                want = std::atan2(f.x, f.y);
            }
        }
        const float maxTurn = g.turnRate * kDtF;
        state_.yaw[i] = wrapPi(prevYaw + std::clamp(wrapPi(want - prevYaw), -maxTurn, maxTurn));
        const float turnRate = wrapPi(state_.yaw[i] - prevYaw) / kDtF;

        if (state_.mode[i] == AgentMode::Moving && !state_.path[i].empty()) {
            const ScheduleEntry& e = desc_.schedule[static_cast<std::size_t>(state_.command[i])];
            const bool onLast = state_.cursor[i] + 1u >= state_.path[i].size();
            const float toGoal = glm::length(state_.goal[i] - state_.pos[i]);
            // Arrived: at the slot, or wedged within a body length of it (other bodies stand where it
            // would have to go; a person stops there too).
            const bool wedgedNear = toGoal < std::max(e.arriveRadius, 8.0f * g.radius) && state_.slowFor[i] > 2.0f;
            if (onLast && (toGoal < e.arriveRadius || wedgedNear)) {
                if (e.command == CommandKind::Wander) {
                    state_.mode[i] = AgentMode::Dwelling;
                    const float dwell = e.dwellMin + (e.dwellMax - e.dwellMin) *
                                                         hash01(g.seed, static_cast<std::uint64_t>(state_.command[i]), i,
                                                                5000u + state_.wanderCount[i]);
                    state_.dwellUntil[i] = k + static_cast<std::int64_t>(std::ceil(dwell * kStepsPerSecond));
                } else {
                    state_.mode[i] = AgentMode::Arrived;
                }
                state_.slowFor[i] = 0.0f;
            } else if (speed < 0.2f) {
                // Wedged (barely moving): after four seconds, ask for a new route from here (counted, not timed).
                state_.slowFor[i] += kDtF;
                if (state_.slowFor[i] > 4.0f) {
                    state_.slowFor[i] = 0.0f;
                    state_.needPath[i] = 1;
                    ++state_.replans;
                }
            } else {
                state_.slowFor[i] = 0.0f;
            }
            // Busy but going nowhere (circling a corner, shuffling in a press): every two seconds,
            // counted, compare with where it was; under 0.3 m of progress asks for a new route.
            if (k - state_.anchorStep[i] >= 2 * kStepsPerSecond) {
                if (glm::length(state_.pos[i] - state_.anchor[i]) < 0.3f && state_.mode[i] == AgentMode::Moving) {
                    state_.needPath[i] = 1;
                    ++state_.replans;
                    state_.slowFor[i] += 2.0f; // and it counts as wedged for arrival
                }
                state_.anchor[i] = state_.pos[i];
                state_.anchorStep[i] = k;
            }
        }

        // The gait picks the state; the clip's phase advances at the rate the speed asks for.
        const entity::Activity proposed = speed > g.gait.moveExit ? entity::Activity::Walk : entity::Activity::Idle;
        const entity::Activity act = state_.gait[i].select(g.gait, proposed, speed, turnRate, kDt);
        float rate = entity::Gait::playbackRate(g.gait, act, speed, turnRate);
        rate = std::max(kRateQuantum, std::round(rate / kRateQuantum) * kRateQuantum);
        if (act != state_.activity[i]) {
            state_.activity[i] = act;
            state_.activityStep[i] = k;
            state_.clipPhase[i] = 0.0;
        }
        state_.clipPhase[i] += static_cast<double>(rate) * kDt;
        state_.clipRate[i] = rate;
    }
    state_.step = k;
    if (checkpointsOn_ && k % kStepsPerSecond == 0) {
        recordCheckpoint();
    }
    lastStep_.microseconds = threadMicros() - start;
}

void CrowdWorld::recordCheckpoint() {
    if (checkpoints_.find(state_.step) == checkpoints_.end()) {
        checkpoints_.emplace(state_.step, state_);
    }
    // A ceiling on the set: past it every other checkpoint goes, so it thins evenly (ADR-700's rule).
    constexpr std::size_t kMaxCheckpoints = 2048;
    if (checkpoints_.size() > kMaxCheckpoints) {
        bool drop = false;
        for (auto it = checkpoints_.begin(); it != checkpoints_.end();) {
            if (drop && it->first != 0) {
                it = checkpoints_.erase(it);
            } else {
                ++it;
            }
            drop = !drop;
        }
    }
}

std::int64_t CrowdWorld::advanceTo(double seconds) {
    // The last grid instant at or before `seconds`, spelled as a play spells it (k / 60.0).
    std::int64_t k = static_cast<std::int64_t>(std::floor(seconds * kStepsPerSecond + 1e-6));
    if (k < 0) {
        k = 0;
    }
    return advanceToStep(k);
}

std::int64_t CrowdWorld::advanceToStep(std::int64_t target) {
    if (agents_.empty()) {
        state_.step = target;
        return 0;
    }
    if (target < state_.step) {
        auto it = checkpointsOn_ ? checkpoints_.upper_bound(target) : checkpoints_.end();
        if (checkpointsOn_ && it != checkpoints_.begin()) {
            --it;
            state_ = it->second;
        } else {
            reset();
        }
    }
    std::int64_t ran = 0;
    while (state_.step < target) {
        stepOnce();
        ++ran;
    }
    return ran;
}

AgentPose CrowdWorld::pose(std::size_t i, double seconds) const {
    AgentPose p;
    const GroupDesc& g = desc_.groups[static_cast<std::size_t>(agents_[i].group)];
    const double at = static_cast<double>(state_.step) / kStepsPerSecond;
    const double dt = std::clamp(seconds - at, 0.0, kDt);
    const glm::vec2 xz = state_.pos[i] + state_.vel[i] * static_cast<float>(dt);
    p.position = glm::vec3(xz.x, state_.y[i], xz.y);
    p.yaw = state_.yaw[i];
    p.activity = state_.activity[i];
    switch (p.activity) {
    case entity::Activity::Walk: p.clip = &g.walkClip; break;
    case entity::Activity::Run: p.clip = &g.runClip; break;
    default: p.clip = &g.idleClip; break;
    }
    if (p.clip->empty()) {
        p.clip = nullptr;
    }
    p.clipRate = state_.clipRate[i];
    const double clipSecond = state_.clipPhase[i] + static_cast<double>(p.clipRate) * dt;
    p.clipOrigin = seconds - clipSecond / static_cast<double>(p.clipRate);
    p.activityStart = static_cast<double>(state_.activityStep[i]) / kStepsPerSecond;
    return p;
}

std::uint64_t CrowdWorld::stateHash() const {
    const CrowdState& s = state_;
    std::uint64_t h = 1469598103934665603ull;
    h = fnv(h, &s.step, sizeof s.step);
    h = fnvVec(h, s.pos);
    h = fnvVec(h, s.vel);
    h = fnvVec(h, s.y);
    h = fnvVec(h, s.yaw);
    h = fnvVec(h, s.goal);
    h = fnvVec(h, s.mode);
    h = fnvVec(h, s.cursor);
    h = fnvVec(h, s.activity);
    h = fnvVec(h, s.clipPhase);
    h = fnvVec(h, s.clipRate);
    h = fnvVec(h, s.anchor);
    h = fnvVec(h, s.slowFor);
    for (const auto& p : s.path) {
        h = fnvVec(h, p);
    }
    h = fnv(h, &s.pathCursor, sizeof s.pathCursor);
    h = fnv(h, &s.scheduleCursor, sizeof s.scheduleCursor);
    return h;
}

std::size_t CrowdWorld::checkpointBytes() const {
    std::size_t bytes = 0;
    for (const auto& [step, s] : checkpoints_) {
        bytes += s.pos.size() * (sizeof(glm::vec2) * 4 + sizeof(float) * 4 + sizeof(std::int64_t) * 3 +
                                 sizeof(double) + sizeof(entity::Gait) + 16);
        for (const auto& p : s.path) {
            bytes += p.size() * sizeof(glm::vec2);
        }
    }
    return bytes;
}

} // namespace avgen::crowd
