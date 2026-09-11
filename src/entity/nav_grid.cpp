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

} // namespace

void NavGrid::clear() {
    cells_.clear();
    shore_.clear();
    vistas_.clear();
    gScore_.clear();
    cameFrom_.clear();
    visitStamp_.clear();
    cellPath_.clear();
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
                obstacles->query(p, cell * 0.7071f, hits);
                for (const std::uint32_t i : hits) {
                    const spatial::NavigationObstacle& o = obstacles->obstacles()[i];
                    const float r = o.radius + nav.settings().bodyRadius;
                    covered += 3.14159265f * r * r;
                }
                c.obstruction = quantise(covered / cellArea);
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
    extractInterestPoints();

    stats_.buildMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin).count();
    log::info("nav grid: {}x{} at {:.1f} m ({} cells, {} walkable, {} water, {} blocked), "
              "{} shore and {} vista points, built in {:.1f} ms",
              width, height, cell, total, stats_.walkable, stats_.water, stats_.blocked,
              shore_.size(), vistas_.size(), stats_.buildMs);
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
                const glm::ivec2 n = c + glm::ivec2(dx, dy);
                if (!walkable(n)) {
                    continue;
                }
                if (dx != 0 && dy != 0 &&
                    (!walkable(glm::ivec2(c.x + dx, c.y)) || !walkable(glm::ivec2(c.x, c.y + dy)))) {
                    continue; // no cutting a corner a body would not fit round
                }
                const NavCell& target = cells_[index(n)];
                const float step = (dx != 0 && dy != 0 ? kSqrt2 : 1.0f) * cell;
                // Distance is not the only cost. A character that minimised it alone would cross
                // every scree slope and thread every thicket on the way, which is efficient and
                // reads as a machine looking for the shortest line.
                const float penalty = 1.0f +
                                      cost.obstructionPenalty * (static_cast<float>(target.obstruction) / 255.0f) +
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

    // A vista is a walkable cell higher than everything within `reach` of it. Sampled on a stride
    // over the grid rather than tested everywhere, because a local maximum is a property of a
    // neighbourhood and testing every cell finds the same handful of hills many times over.
    const int reach = std::max(3, static_cast<int>(std::lround(24.0f / stats_.cellSize)));
    const int stride = std::max(2, reach / 2);
    struct Candidate {
        glm::vec3 position;
        float height;
    };
    std::vector<Candidate> candidates;
    for (int y = reach; y + reach < stats_.height; y += stride) {
        for (int x = reach; x + reach < stats_.width; x += stride) {
            const glm::ivec2 c(x, y);
            if (!walkable(c)) {
                continue;
            }
            const float h = at(c).ground;
            bool highest = true;
            for (int dy = -reach; dy <= reach && highest; ++dy) {
                for (int dx = -reach; dx <= reach; ++dx) {
                    if (at(glm::ivec2(x + dx, y + dy)).ground > h) {
                        highest = false;
                        break;
                    }
                }
            }
            if (highest) {
                const glm::vec2 p = centerOf(c);
                candidates.push_back({glm::vec3(p.x, h, p.y), h});
            }
        }
    }
    // The highest ones first, so a world with more prominences than the cap keeps the ones worth
    // standing on. Ties broken by position, so the list does not depend on scan order.
    std::sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
        if (a.height != b.height) {
            return a.height > b.height;
        }
        if (a.position.x != b.position.x) {
            return a.position.x < b.position.x;
        }
        return a.position.z < b.position.z;
    });
    for (std::size_t i = 0; i < candidates.size() && i < kMaxVistas; ++i) {
        vistas_.push_back(candidates[i].position);
    }
}

} // namespace avgen::entity
