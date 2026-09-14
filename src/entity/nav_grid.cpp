#include "entity/nav_grid.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <queue>

namespace avgen::entity {
namespace {

constexpr float kSqrt2 = 1.41421356f;

std::uint8_t quantise(float v01) {
    const float c = std::clamp(v01, 0.0f, 1.0f);
    return static_cast<std::uint8_t>(c * 255.0f + 0.5f);
}

// A* frontier entry. `f` is the priority; `cell` breaks ties, so two routes of equal cost are
// always resolved the same way and a path is reproducible rather than merely usually the same.
struct Frontier {
    float f = 0.0f;
    std::int32_t cell = 0;
    bool operator>(const Frontier& other) const {
        return f != other.f ? f > other.f : cell > other.cell;
    }
};

// Whether a step from `c` to `c + d` is one A* would take. Kept as one function because the region
// flood fill and the search must agree exactly: if the fill is more generous than the search, two
// cells are called connected and no route is ever found between them, which is the worst of both.
template <typename Walkable>
bool stepAllowed(glm::ivec2 c, glm::ivec2 d, Walkable&& walkable) {
    if (!walkable(c + d)) {
        return false;
    }
    if (d.x != 0 && d.y != 0) {
        // No cutting a corner a body would not fit round.
        return walkable(glm::ivec2(c.x + d.x, c.y)) && walkable(glm::ivec2(c.x, c.y + d.y));
    }
    return true;
}

} // namespace

const char* pathStatusName(PathStatus status) {
    switch (status) {
    case PathStatus::Ok: return "ok";
    case PathStatus::AlreadyThere: return "already there";
    case PathStatus::NoGraph: return "no navigation graph";
    case PathStatus::NoStart: return "nowhere to start from";
    case PathStatus::NoGoal: return "nowhere to stand at the destination";
    case PathStatus::Unreachable: return "unreachable: a different region";
    case PathStatus::SearchExhausted: return "search budget exhausted";
    }
    return "unknown";
}

void NavGrid::clear() {
    cells_.clear();
    shore_.clear();
    vistas_.clear();
    gScore_.clear();
    cameFrom_.clear();
    visitStamp_.clear();
    cellPath_.clear();
    regions_.clear();
    regionSizes_.clear();
    floodStack_.clear();
    stamp_ = 0;
    lastExpansions_ = 0;
    stats_ = {};
}

void NavGrid::build(const Navigator& nav, float cellSize) {
    clear();
    const auto begin = std::chrono::steady_clock::now();
    const float cell = std::max(cellSize, 0.5f);
    // The walkable world, not the whole map: the navigator refuses to stand within
    // `boundaryMargin` of the edge, so cells out there would be built only to be rejected.
    const glm::vec2 lo = nav.worldMin();
    const glm::vec2 hi = nav.worldMax();
    if (hi.x <= lo.x || hi.y <= lo.y) {
        return;
    }
    origin_ = lo;
    const int width = std::max(1, static_cast<int>(std::ceil((hi.x - lo.x) / cell)));
    const int height = std::max(1, static_cast<int>(std::ceil((hi.y - lo.y) / cell)));
    // A ceiling, so a world authored at an absurd size fails loudly rather than allocating for an
    // hour. Two million cells is a 5,600 m square at four metres -- far past anything Glowmere is.
    constexpr std::size_t kMaxCells = 2u << 20;
    const auto total = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (total > kMaxCells) {
        log::warn("nav grid: {}x{} cells at {} m exceeds the {} cell ceiling; no graph was built",
                  width, height, cell, kMaxCells);
        return;
    }

    stats_.width = width;
    stats_.height = height;
    stats_.cellSize = cell;
    stats_.cells = total;
    cells_.assign(total, NavCell{});

    const float maxSlope = std::max(nav.settings().maxSlope, 1e-3f);
    // How much of a cell a solid covers, as the fraction of the cell's area its footprint claims.
    // Sampled from the obstacle field rather than from the point test, because a trunk half a metre
    // off the cell centre obstructs the cell and a point test at the centre says it does not.
    const spatial::ObstacleField* obstacles = nav.obstacles();
    const float cellArea = cell * cell;
    std::vector<std::uint32_t> hits; // hoisted: one allocation for the build, not one per cell

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const glm::vec2 p = centerOf(glm::ivec2(x, y));
            const NavSample s = nav.sample(p);
            NavCell& c = cells_[index(glm::ivec2(x, y))];
            c.ground = s.ground;
            c.slope = quantise(s.slope / maxSlope);
            if (std::isfinite(s.waterSurface) && s.ground < s.waterSurface) {
                c.flags |= NavWater;
                ++stats_.water;
            }
            switch (s.reject) {
            case NavReject::TooSteep: c.flags |= NavSteep; break;
            case NavReject::Submerged: c.flags |= NavWater; break;
            case NavReject::Obstructed:
            case NavReject::InsideHero: c.flags |= NavBlocked; break;
            default: break;
            }
            if (obstacles != nullptr) {
                // Area-weighted rather than counted: three saplings and one boulder should not
                // score the same, and a cell a walker can still thread should not score as full.
                float covered = 0.0f;
                float vaulted = 0.0f;
                const spatial::ObstacleFilter body = nav.filter(s.ground);
                obstacles->query(p, cell * 0.7071f, hits);
                for (const std::uint32_t i : hits) {
                    const spatial::NavigationObstacle& o = obstacles->obstacles()[i];
                    const float r = o.radius + nav.settings().bodyRadius;
                    const float area = 3.14159265f * r * r;
                    // Only what this body actually gets past by jumping leaves the obstruction
                    // total (ADR-194). Everything else -- including a solid it merely steps over,
                    // which this sum has always counted -- stays where it was, so a body that
                    // cannot jump scores every cell exactly as it did.
                    if (spatial::traversalFor(o, body) == spatial::Traversal::Jumpable) {
                        vaulted += area;
                    } else {
                        covered += area;
                    }
                }
                c.obstruction = quantise(covered / cellArea);
                c.vault = quantise(vaulted / cellArea);
                if (c.obstruction > 200) {
                    c.flags |= NavBlocked;
                }
            }
            if (s.navigable) {
                c.flags |= NavWalkable;
                ++stats_.walkable;
            } else if ((c.flags & NavBlocked) != 0) {
                ++stats_.blocked;
            }
        }
    }

    // Edge cells: walkable, and next to something that is not. Used by the interest extraction
    // below and useful on its own -- a character that only ever walks through the middle of open
    // ground never goes anywhere with a view.
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const glm::ivec2 c(x, y);
            if (!walkable(c)) {
                continue;
            }
            const bool edge = !walkable(glm::ivec2(x - 1, y)) || !walkable(glm::ivec2(x + 1, y)) ||
                              !walkable(glm::ivec2(x, y - 1)) || !walkable(glm::ivec2(x, y + 1));
            if (edge) {
                cells_[index(c)].flags |= NavEdge;
            }
        }
    }

    gScore_.assign(total, 0.0f);
    cameFrom_.assign(total, -1);
    visitStamp_.assign(total, 0u);
    buildRegions();
    extractInterestPoints();

    stats_.buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    log::info("nav grid: {}x{} at {:.1f} m ({} cells, {} walkable, {} water, {} blocked), "
              "{} region{} (largest {} cells), {} shore and {} vista points, built in {:.1f} ms",
              width, height, cell, total, stats_.walkable, stats_.water, stats_.blocked,
              stats_.regions, stats_.regions == 1 ? "" : "s", stats_.largestRegion, shore_.size(),
              vistas_.size(), stats_.buildMs);
    if (stats_.regions > 1) {
        // Worth saying out loud. An archipelago is a legitimate world and a character stranded on
        // an islet in one is not, and the difference is invisible until something fails to path.
        log::info("nav grid: the walkable ground is in {} disconnected pieces; {} of {} cells are "
                  "in the largest",
                  stats_.regions, stats_.largestRegion, stats_.walkable);
    }
}

std::size_t NavGrid::index(glm::ivec2 c) const {
    return static_cast<std::size_t>(c.y) * static_cast<std::size_t>(stats_.width) +
           static_cast<std::size_t>(c.x);
}

glm::ivec2 NavGrid::cellOf(glm::vec2 p) const {
    const glm::vec2 local = (p - origin_) / std::max(stats_.cellSize, 1e-3f);
    return glm::ivec2(static_cast<int>(std::floor(local.x)), static_cast<int>(std::floor(local.y)));
}

glm::vec2 NavGrid::centerOf(glm::ivec2 c) const {
    return origin_ + (glm::vec2(c) + 0.5f) * stats_.cellSize;
}

bool NavGrid::inside(glm::ivec2 c) const {
    return c.x >= 0 && c.y >= 0 && c.x < stats_.width && c.y < stats_.height;
}

const NavCell& NavGrid::at(glm::ivec2 c) const {
    static const NavCell kOutside{};
    return inside(c) ? cells_[index(c)] : kOutside;
}

bool NavGrid::walkable(glm::ivec2 c) const {
    return inside(c) && (cells_[index(c)].flags & NavWalkable) != 0;
}

bool NavGrid::walkable(glm::vec2 p) const { return walkable(cellOf(p)); }

bool NavGrid::nearestWalkable(glm::vec2 p, float maxRange, glm::vec2& out) const {
    if (!valid()) {
        return false;
    }
    const glm::ivec2 start = cellOf(p);
    if (walkable(start)) {
        out = centerOf(start);
        return true;
    }
    const int rings = std::max(1, static_cast<int>(std::ceil(maxRange / stats_.cellSize)));
    // Rings outward, and the best cell *within* a ring chosen by true distance rather than by
    // scan order: a square ring is not a circle, and taking the first hit biases every answer
    // toward whichever corner the loop starts in.
    for (int r = 1; r <= rings; ++r) {
        float bestDistSq = std::numeric_limits<float>::max();
        glm::vec2 best(0.0f);
        bool found = false;
        for (int dy = -r; dy <= r; ++dy) {
            for (int dx = -r; dx <= r; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) {
                    continue; // interior: already covered by a smaller ring
                }
                const glm::ivec2 c = start + glm::ivec2(dx, dy);
                if (!walkable(c)) {
                    continue;
                }
                const glm::vec2 centre = centerOf(c);
                const float d = glm::dot(centre - p, centre - p);
                if (d < bestDistSq) {
                    bestDistSq = d;
                    best = centre;
                    found = true;
                }
            }
        }
        if (found) {
            out = best;
            return true;
        }
    }
    return false;
}

bool NavGrid::lineOfSight(glm::ivec2 a, glm::ivec2 b) const {
    // A supercover walk: every cell the segment touches, not merely the ones a Bresenham line
    // lands on. The difference matters exactly where it always does -- a diagonal squeeze between
    // two blocked cells looks clear to a thin line and is not a gap anything can walk through.
    int x = a.x;
    int y = a.y;
    const int dx = std::abs(b.x - a.x);
    const int dy = std::abs(b.y - a.y);
    const int sx = b.x > a.x ? 1 : -1;
    const int sy = b.y > a.y ? 1 : -1;
    int err = dx - dy;
    for (int guard = 0; guard <= dx + dy + 2; ++guard) {
        if (!walkable(glm::ivec2(x, y))) {
            return false;
        }
        if (x == b.x && y == b.y) {
            return true;
        }
        const int e2 = err * 2;
        if (e2 > -dy && e2 < dx) {
            // A true diagonal step. Both orthogonal neighbours must be open or the walker is being
            // asked to pass through a corner it cannot fit through.
            if (!walkable(glm::ivec2(x + sx, y)) || !walkable(glm::ivec2(x, y + sy))) {
                return false;
            }
        }
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
    return false;
}

bool NavGrid::findPath(glm::vec2 from, glm::vec2 to, std::vector<glm::vec2>& out,
                       const NavPathCost& cost) const {
    out.clear();
    if (!valid()) {
        return false;
    }
    glm::ivec2 start = cellOf(from);
    if (!walkable(start)) {
        glm::vec2 snapped;
        // A walker standing somewhere the grid calls unwalkable is normal, not an error: the grid
        // samples cell centres and a character stands between them. Start from the nearest cell it
        // could be on rather than refusing to plan.
        if (!nearestWalkable(from, stats_.cellSize * 3.0f, snapped)) {
            return false;
        }
        start = cellOf(snapped);
    }
    glm::ivec2 goal = cellOf(to);
    // Where the route actually ends. Not always `to`: a destination inside a rock, under water or
    // outside the world is answered with the nearest place a body could stand, and handing back
    // `to` regardless would put a character inside the thing it was walking towards and call that
    // arrival.
    glm::vec2 goalPoint = to;
    if (!walkable(goal)) {
        glm::vec2 snapped;
        if (!nearestWalkable(to, stats_.cellSize * 4.0f, snapped)) {
            return false;
        }
        goal = cellOf(snapped);
        goalPoint = snapped;
    }
    if (start == goal) {
        out.push_back(goalPoint);
        return true;
    }
    // The lookup that saves the search. Two points in different connected regions are not reachable
    // from each other, full stop, and A* would only discover it by opening every cell on this side
    // of the divide first -- nine and a half thousand of them in Glowmere, for a fact the flood fill
    // settled when the grid was built. The fill uses `stepAllowed`, the same rule the search below
    // uses, so the two can never disagree about what "connected" means.
    if (!regions_.empty() && regions_[index(start)] != regions_[index(goal)]) {
        lastExpansions_ = 0;
        return false;
    }

    ++stamp_;
    const auto startIndex = static_cast<std::int32_t>(index(start));
    const auto goalIndex = static_cast<std::int32_t>(index(goal));
    const float cell = stats_.cellSize;
    const glm::vec2 goalCentre = centerOf(goal);
    const auto heuristic = [&](glm::ivec2 c) {
        // Octile: exact for eight-connected movement, so A* expands the fewest cells it can
        // without ever returning a path that is not the cheapest.
        const glm::vec2 d = glm::abs(centerOf(c) - goalCentre);
        const float lo = std::min(d.x, d.y);
        const float hi = std::max(d.x, d.y);
        return hi + (kSqrt2 - 1.0f) * lo;
    };

    std::priority_queue<Frontier, std::vector<Frontier>, std::greater<>> open;
    gScore_[static_cast<std::size_t>(startIndex)] = 0.0f;
    cameFrom_[static_cast<std::size_t>(startIndex)] = startIndex;
    visitStamp_[static_cast<std::size_t>(startIndex)] = stamp_;
    open.push({heuristic(start), startIndex});

    std::size_t expansions = 0;
    bool reached = false;
    const int budget = std::max(cost.maxExpansions, 16);
    while (!open.empty() && expansions < static_cast<std::size_t>(budget)) {
        const Frontier current = open.top();
        open.pop();
        if (current.cell == goalIndex) {
            reached = true;
            break;
        }
        const auto ci = static_cast<std::size_t>(current.cell);
        // Stale entry: this cell was reached more cheaply after it was queued. A lazy-deletion
        // heap is the right trade here -- a decrease-key structure costs more than the few extra
        // entries a grid of this branching factor produces.
        if (current.f > gScore_[ci] + heuristic(glm::ivec2(current.cell % stats_.width,
                                                           current.cell / stats_.width)) + 1e-4f) {
            continue;
        }
        ++expansions;
        const glm::ivec2 c(current.cell % stats_.width, current.cell / stats_.width);
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                if (dx == 0 && dy == 0) {
                    continue;
                }
                const glm::ivec2 d(dx, dy);
                if (!stepAllowed(c, d, [&](glm::ivec2 at) { return walkable(at); })) {
                    continue;
                }
                const glm::ivec2 n = c + d;
                const NavCell& target = cells_[index(n)];
                const float step = (dx != 0 && dy != 0 ? kSqrt2 : 1.0f) * cell;
                // Distance is not the only cost. A character that minimised it alone would cross
                // every scree slope and thread every thicket on the way, which is efficient and
                // reads as a machine looking for the shortest line.
                const float penalty = 1.0f +
                                      cost.obstructionPenalty * (static_cast<float>(target.obstruction) / 255.0f) +
                                      cost.vaultPenalty * (static_cast<float>(target.vault) / 255.0f) +
                                      cost.slopePenalty * (static_cast<float>(target.slope) / 255.0f);
                const float tentative = gScore_[ci] + step * penalty;
                const std::size_t ni = index(n);
                if (visitStamp_[ni] == stamp_ && tentative >= gScore_[ni]) {
                    continue;
                }
                visitStamp_[ni] = stamp_;
                gScore_[ni] = tentative;
                cameFrom_[ni] = current.cell;
                open.push({tentative + heuristic(n), static_cast<std::int32_t>(ni)});
            }
        }
    }
    lastExpansions_ = expansions;
    if (!reached) {
        return false;
    }

    // Walk the parents back, then forward-smooth. The raw path is a staircase of cell centres and
    // nothing should ever walk one: it turns forty-five degrees every few metres for no reason a
    // viewer can see.
    cellPath_.clear();
    for (std::int32_t at = goalIndex; at != startIndex; at = cameFrom_[static_cast<std::size_t>(at)]) {
        cellPath_.push_back(glm::ivec2(at % stats_.width, at / stats_.width));
        if (cellPath_.size() > stats_.cells) {
            return false; // a cycle in the parent links; refuse rather than loop
        }
    }
    cellPath_.push_back(start);
    std::reverse(cellPath_.begin(), cellPath_.end());

    // String-pull over the grid rather than over the world. The continuous layer is the authority
    // on whether a line is walkable, but asking it here would be O(n^2) world samples for a path
    // that is about to be re-checked a metre at a time by the steerer anyway.
    std::size_t anchor = 0;
    while (anchor + 1 < cellPath_.size()) {
        std::size_t furthest = anchor + 1;
        for (std::size_t probe = cellPath_.size() - 1; probe > anchor + 1; --probe) {
            if (lineOfSight(cellPath_[anchor], cellPath_[probe])) {
                furthest = probe;
                break;
            }
        }
        out.push_back(centerOf(cellPath_[furthest]));
        anchor = furthest;
    }
    // Finish at the point that was actually asked for, not at the centre of the cell containing it.
    if (!out.empty()) {
        out.back() = goalPoint;
    } else {
        out.push_back(goalPoint);
    }
    return true;
}

void NavGrid::buildRegions() {
    regions_.assign(stats_.cells, 0u);
    regionSizes_.assign(1, stats_.cells - stats_.walkable); // region 0 is everything unwalkable
    stats_.regions = 0;
    stats_.largestRegion = 0;
    if (cells_.empty()) {
        return;
    }
    const auto walkableAt = [&](glm::ivec2 c) { return walkable(c); };
    // An explicit stack rather than recursion: a walkable set of twenty thousand cells is twenty
    // thousand frames deep in the worst case, and a stack overflow in a world build is not a
    // failure mode anyone would diagnose quickly.
    floodStack_.clear();
    std::uint16_t next = 1;
    for (int y = 0; y < stats_.height; ++y) {
        for (int x = 0; x < stats_.width; ++x) {
            const glm::ivec2 seed(x, y);
            if (!walkable(seed) || regions_[index(seed)] != 0) {
                continue;
            }
            if (next == 0xFFFFu) {
                // 65,534 disconnected pieces is not a world, it is a bug in whatever made it. Stop
                // labelling rather than wrap the counter and silently merge two regions into one.
                log::warn("nav grid: more than {} disconnected regions; the rest are unlabelled",
                          next - 1);
                return;
            }
            std::size_t size = 0;
            floodStack_.push_back(static_cast<std::int32_t>(index(seed)));
            regions_[index(seed)] = next;
            while (!floodStack_.empty()) {
                const std::int32_t at = floodStack_.back();
                floodStack_.pop_back();
                ++size;
                const glm::ivec2 c(at % stats_.width, at / stats_.width);
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) {
                            continue;
                        }
                        const glm::ivec2 d(dx, dy);
                        if (!stepAllowed(c, d, walkableAt)) {
                            continue;
                        }
                        const std::size_t ni = index(c + d);
                        if (regions_[ni] != 0) {
                            continue;
                        }
                        regions_[ni] = next;
                        floodStack_.push_back(static_cast<std::int32_t>(ni));
                    }
                }
            }
            regionSizes_.push_back(size);
            stats_.largestRegion = std::max(stats_.largestRegion, size);
            ++stats_.regions;
            ++next;
        }
    }
}

std::uint16_t NavGrid::regionAt(glm::vec2 p) const {
    const glm::ivec2 c = cellOf(p);
    return inside(c) && index(c) < regions_.size() ? regions_[index(c)] : 0;
}

bool NavGrid::connected(glm::vec2 a, glm::vec2 b) const {
    const std::uint16_t ra = regionAt(a);
    return ra != 0 && ra == regionAt(b);
}

std::size_t NavGrid::regionSize(std::uint16_t region) const {
    return region < regionSizes_.size() ? regionSizes_[region] : 0;
}

PathResult NavGrid::path(const PathRequest& request, const NavPathCost& cost) const {
    PathResult out;
    out.goal = request.to;
    if (!valid()) {
        out.status = PathStatus::NoGraph;
        return out;
    }
    // Resolve both ends to somewhere a body could actually stand, and say which one failed. A
    // walker standing between cell centres is normal, not an error; a goal inside a rock is a
    // different problem from a goal across a lake, and an action layer wants to tell them apart.
    glm::vec2 start = request.from;
    if (!walkable(start) && !nearestWalkable(request.from, stats_.cellSize * 3.0f, start)) {
        out.status = PathStatus::NoStart;
        return out;
    }
    glm::vec2 goal = request.to;
    const float reach = std::max(request.goalTolerance, stats_.cellSize * 4.0f);
    if (!walkable(goal) && !nearestWalkable(request.to, reach, goal)) {
        out.status = PathStatus::NoGoal;
        return out;
    }
    out.goal = goal;
    // The lookup that saves the search. Two points in different regions are not reachable from each
    // other, full stop, and A* would only discover that by opening every cell on this side of the
    // divide first -- nine and a half thousand of them in Glowmere, for an answer already known.
    if (!connected(start, goal)) {
        out.status = PathStatus::Unreachable;
        return out;
    }
    if (cellOf(start) == cellOf(goal)) {
        out.status = PathStatus::AlreadyThere;
        return out;
    }
    if (!findPath(start, goal, out.waypoints, cost)) {
        // Both ends are standable and in the same region, so a route exists; the budget ran out.
        out.status = PathStatus::SearchExhausted;
        out.waypoints.clear();
        return out;
    }
    out.status = PathStatus::Ok;
    out.expansions = lastExpansions_;
    glm::vec2 previous = start;
    for (const glm::vec2& point : out.waypoints) {
        out.length += glm::length(point - previous);
        previous = point;
    }
    if (!out.waypoints.empty()) {
        out.goal = out.waypoints.back();
    }
    return out;
}

void NavGrid::extractInterestPoints() {
    shore_.clear();
    vistas_.clear();
    if (!valid()) {
        return;
    }
    // Both lists are capped and sampled on a stride rather than truncated, so a big world gets a
    // spread of places rather than every candidate in its bottom-left corner.
    constexpr std::size_t kMaxShore = 220;
    constexpr std::size_t kMaxVistas = 80;

    std::vector<glm::ivec2> shoreCells;
    for (int y = 0; y < stats_.height; ++y) {
        for (int x = 0; x < stats_.width; ++x) {
            const glm::ivec2 c(x, y);
            if (!walkable(c)) {
                continue;
            }
            const bool wet = (at(glm::ivec2(x - 1, y)).flags & NavWater) != 0 ||
                             (at(glm::ivec2(x + 1, y)).flags & NavWater) != 0 ||
                             (at(glm::ivec2(x, y - 1)).flags & NavWater) != 0 ||
                             (at(glm::ivec2(x, y + 1)).flags & NavWater) != 0;
            if (wet) {
                shoreCells.push_back(c);
            }
        }
    }
    const std::size_t shoreStride = std::max<std::size_t>(1, shoreCells.size() / kMaxShore);
    for (std::size_t i = 0; i < shoreCells.size(); i += shoreStride) {
        const glm::vec2 p = centerOf(shoreCells[i]);
        shore_.emplace_back(p.x, at(shoreCells[i]).ground, p.y);
    }

    // A vista is high ground with a view, and the obvious test for one -- a cell strictly higher
    // than every cell within twenty-odd metres -- turns out to find almost nothing. On terrain this
    // smooth there is one summit per hill and a hill is far wider than the test window, so Glowmere
    // produced *two* vistas for a whole valley and "terrain features" was effectively absent from
    // the interest registry.
    //
    // Prominence and separation instead: how far a point stands above the ground around it, and
    // then a greedy spread so the list is a set of different hills rather than one hill sampled
    // repeatedly. Both are cheap, both are deterministic, and the result is the thing an author
    // means by "somewhere with a view".
    const int reach = std::max(3, static_cast<int>(std::lround(26.0f / stats_.cellSize)));
    const int stride = std::max(1, reach / 3);
    const float minSeparation = 42.0f;
    struct Candidate {
        glm::vec3 position;
        float prominence;
    };
    std::vector<Candidate> candidates;
    for (int y = reach; y + reach < stats_.height; y += stride) {
        for (int x = reach; x + reach < stats_.width; x += stride) {
            const glm::ivec2 c(x, y);
            if (!walkable(c)) {
                continue;
            }
            const float h = at(c).ground;
            // Eight probes on the ring rather than every cell in the box: a summit is a summit in
            // every direction, and this is 8 lookups where the box was 169.
            float around = 0.0f;
            int taken = 0;
            for (int k = 0; k < 8; ++k) {
                const float angle = static_cast<float>(k) * 0.7853982f;
                const glm::ivec2 probe(x + static_cast<int>(std::lround(std::cos(angle) * reach)),
                                       y + static_cast<int>(std::lround(std::sin(angle) * reach)));
                if (!inside(probe)) {
                    continue;
                }
                around += at(probe).ground;
                ++taken;
            }
            if (taken < 6) {
                continue; // too near the edge to judge
            }
            const float prominence = h - around / static_cast<float>(taken);
            if (prominence < 2.5f) {
                continue;
            }
            const glm::vec2 p = centerOf(c);
            candidates.push_back({glm::vec3(p.x, h, p.y), prominence});
        }
    }
    // Most prominent first, ties broken by position so the list does not depend on scan order.
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.prominence != b.prominence) {
            return a.prominence > b.prominence;
        }
        if (a.position.x != b.position.x) {
            return a.position.x < b.position.x;
        }
        return a.position.z < b.position.z;
    });
    for (const Candidate& candidate : candidates) {
        if (vistas_.size() >= kMaxVistas) {
            break;
        }
        const bool tooClose = std::any_of(vistas_.begin(), vistas_.end(), [&](const glm::vec3& taken) {
            return glm::length(glm::vec2(taken.x - candidate.position.x,
                                         taken.z - candidate.position.z)) < minSeparation;
        });
        if (!tooClose) {
            vistas_.push_back(candidate.position);
        }
    }
}

} // namespace avgen::entity
