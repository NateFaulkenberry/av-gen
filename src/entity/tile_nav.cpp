#include "entity/tile_nav.hpp"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_map>

namespace avgen::entity {

namespace {

std::uint64_t key(glm::ivec2 t) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(t.x)) << 32) | static_cast<std::uint32_t>(t.y);
}

} // namespace

bool tileWalkAllowed(const world::GroundSample& from, const world::GroundSample& to, const TileNavRules& r) {
    return !tileWet(to, r) && std::abs(to.height - from.height) <= r.stepHeight;
}

bool tileJumpAllowed(const world::GroundSample& from, const world::GroundSample& to, int tiles, const TileNavRules& r) {
    if (tiles < 1 || tiles > r.jumpTiles) {
        return false;
    }
    if (!r.landOnWater && tileWet(to, r)) {
        return false; // ADR-1997
    }
    const float dh = to.height - from.height;
    return dh <= r.jumpRise && -dh <= r.jumpDrop;
}

TilePath planTiles(const TileQuery& query, glm::ivec2 from, glm::ivec2 to, const TileNavRules& rules) {
    TilePath out;
    if (!query) {
        return out;
    }
    std::unordered_map<std::uint64_t, world::GroundSample> heights;
    const auto at = [&](glm::ivec2 t) -> const world::GroundSample& {
        auto it = heights.find(key(t));
        if (it == heights.end()) {
            it = heights.emplace(key(t), query(t)).first;
        }
        return it->second;
    };
    struct Node {
        float g = 1e30f;
        glm::ivec2 parent{0};
        TileEdge edge = TileEdge::Walk;
        bool closed = false;
        bool hasParent = false;
    };
    std::unordered_map<std::uint64_t, Node> nodes;
    using Item = std::pair<float, glm::ivec2>;
    const auto cmp = [](const Item& a, const Item& b) { return a.first > b.first; };
    std::priority_queue<Item, std::vector<Item>, decltype(cmp)> open(cmp);
    const auto h = [&](glm::ivec2 t) { return glm::length(glm::vec2(t - to)); };
    nodes[key(from)].g = 0.0f;
    open.push({h(from), from});
    static const glm::ivec2 kDirs[8] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
    while (!open.empty()) {
        const glm::ivec2 cur = open.top().second;
        open.pop();
        Node& cn = nodes[key(cur)];
        if (cn.closed) continue;
        cn.closed = true;
        ++out.expanded;
        if (cur == to) {
            out.found = true;
            break;
        }
        const world::GroundSample& here = at(cur);
        const float gHere = cn.g;
        const auto relax = [&](glm::ivec2 next, float cost, TileEdge edge) {
            if (std::abs(next.x - from.x) > rules.window || std::abs(next.y - from.y) > rules.window) return;
            Node& nn = nodes[key(next)];
            if (nn.closed || gHere + cost >= nn.g) return;
            nn.g = gHere + cost;
            nn.parent = cur;
            nn.edge = edge;
            nn.hasParent = true;
            open.push({nn.g + h(next), next});
        };
        for (int d = 0; d < 4; ++d) {
            const glm::ivec2 n = cur + kDirs[d];
            if (tileWalkAllowed(here, at(n), rules)) relax(n, 1.0f, TileEdge::Walk);
        }
        for (const glm::ivec2 dir : kDirs) {
            for (int k = 1; k <= rules.jumpTiles; ++k) {
                const glm::ivec2 n = cur + dir * k;
                const world::GroundSample& there = at(n);
                // a hop is for what a walk cannot do: a one-tile hop only over a step or onto water
                if (k == 1 && dir.x != 0 && dir.y != 0 && tileWalkAllowed(here, there, rules)) continue;
                if (k == 1 && (dir.x == 0 || dir.y == 0) && tileWalkAllowed(here, there, rules)) continue;
                if (tileJumpAllowed(here, there, k, rules)) {
                    relax(n, glm::length(glm::vec2(dir * k)) * 1.3f + rules.jumpCost, TileEdge::Jump);
                }
            }
        }
    }
    if (!out.found) {
        return out;
    }
    for (glm::ivec2 t = to; t != from;) {
        const Node& n = nodes[key(t)];
        out.steps.push_back({t, n.edge});
        t = n.parent;
    }
    std::reverse(out.steps.begin(), out.steps.end());
    return out;
}

} // namespace avgen::entity
