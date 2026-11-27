#include "crowd/orca.hpp"

#include <algorithm>
#include <cmath>

namespace avgen::crowd {

namespace {

constexpr float kEps = 1e-5f;

float det(glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; }
float absSq(glm::vec2 a) { return glm::dot(a, a); }
glm::vec2 normalised(glm::vec2 a) {
    const float l = std::sqrt(absSq(a));
    return l > 0.0f ? a / l : glm::vec2(0.0f);
}

glm::vec2 closestOnSegment(glm::vec2 p, glm::vec2 a, glm::vec2 b) {
    const glm::vec2 d = b - a;
    const float l2 = absSq(d);
    const float t = l2 > 0.0f ? std::clamp(glm::dot(p - a, d) / l2, 0.0f, 1.0f) : 0.0f;
    return a + d * t;
}

// One constraint line, solved against the ones before it (the paper's 1-D LP).
bool lp1(const std::vector<OrcaLine>& lines, std::size_t no, float radius, glm::vec2 opt, bool dirOpt,
         glm::vec2& result) {
    const float dp = glm::dot(lines[no].point, lines[no].direction);
    const float disc = dp * dp + radius * radius - absSq(lines[no].point);
    if (disc < 0.0f) {
        return false;
    }
    const float sd = std::sqrt(disc);
    float tl = -dp - sd;
    float tr = -dp + sd;
    for (std::size_t i = 0; i < no; ++i) {
        const float den = det(lines[no].direction, lines[i].direction);
        const float num = det(lines[i].direction, lines[no].point - lines[i].point);
        if (std::fabs(den) <= kEps) {
            if (num < 0.0f) {
                return false;
            }
            continue;
        }
        const float t = num / den;
        if (den >= 0.0f) {
            tr = std::min(tr, t);
        } else {
            tl = std::max(tl, t);
        }
        if (tl > tr) {
            return false;
        }
    }
    if (dirOpt) {
        result = glm::dot(opt, lines[no].direction) > 0.0f ? lines[no].point + tr * lines[no].direction
                                                           : lines[no].point + tl * lines[no].direction;
    } else {
        const float t = glm::dot(lines[no].direction, opt - lines[no].point);
        result = lines[no].point + std::clamp(t, tl, tr) * lines[no].direction;
    }
    return true;
}

std::size_t lp2(const std::vector<OrcaLine>& lines, float radius, glm::vec2 opt, bool dirOpt, glm::vec2& result) {
    if (dirOpt) {
        result = opt * radius;
    } else if (absSq(opt) > radius * radius) {
        result = normalised(opt) * radius;
    } else {
        result = opt;
    }
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (det(lines[i].direction, lines[i].point - result) > 0.0f) {
            const glm::vec2 keep = result;
            if (!lp1(lines, i, radius, opt, dirOpt, result)) {
                result = keep;
                return i;
            }
        }
    }
    return lines.size();
}

// Infeasible: minimise the largest violation of the agent lines, keeping the hard wall lines.
void lp3(const std::vector<OrcaLine>& lines, std::size_t hard, std::size_t begin, float radius, glm::vec2& result,
         std::vector<OrcaLine>& projected) {
    float distance = 0.0f;
    for (std::size_t i = begin; i < lines.size(); ++i) {
        if (det(lines[i].direction, lines[i].point - result) <= distance) {
            continue;
        }
        projected.assign(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(hard));
        for (std::size_t j = hard; j < i; ++j) {
            OrcaLine line;
            const float d = det(lines[i].direction, lines[j].direction);
            if (std::fabs(d) <= kEps) {
                if (glm::dot(lines[i].direction, lines[j].direction) > 0.0f) {
                    continue;
                }
                line.point = 0.5f * (lines[i].point + lines[j].point);
            } else {
                line.point = lines[i].point +
                             (det(lines[j].direction, lines[i].point - lines[j].point) / d) * lines[i].direction;
            }
            line.direction = normalised(lines[j].direction - lines[i].direction);
            projected.push_back(line);
        }
        const glm::vec2 keep = result;
        if (lp2(projected, radius, glm::vec2(-lines[i].direction.y, lines[i].direction.x), true, result) <
            projected.size()) {
            result = keep;
        }
        distance = det(lines[i].direction, lines[i].point - result);
    }
}

} // namespace

glm::vec2 orcaVelocity(glm::vec2 position, glm::vec2 velocity, glm::vec2 preferred,
                       const std::vector<OrcaNeighbour>& neighbours, const std::vector<OrcaSegment>& walls,
                       const OrcaParams& p, std::vector<OrcaLine>& lines, std::vector<OrcaLine>& scratch) {
    lines.clear();
    // Walls first, as hard constraints: "do not close on the wall faster than the gap allows within
    // the obstacle horizon"; already inside, "be out within one step".
    for (const OrcaSegment& w : walls) {
        const glm::vec2 c = closestOnSegment(position, w.a, w.b);
        const glm::vec2 d = position - c;
        const float dist = std::sqrt(absSq(d));
        const float reach = p.radius + w.radius;
        if (dist > reach + p.maxSpeed * p.timeHorizonObstacle) {
            continue;
        }
        const glm::vec2 n = dist > 1e-6f ? d / dist : glm::vec2(1.0f, 0.0f);
        const float b = dist < reach ? (reach - dist) / p.dt : -(dist - reach) / p.timeHorizonObstacle;
        lines.push_back(OrcaLine{n * b, glm::vec2(n.y, -n.x)});
    }
    const std::size_t hard = lines.size();
    const float invTau = 1.0f / p.timeHorizon;
    for (const OrcaNeighbour& o : neighbours) {
        const glm::vec2 relPos = o.position - position;
        const glm::vec2 relVel = velocity - o.velocity;
        const float distSq = absSq(relPos);
        const float cr = p.radius + o.radius;
        const float crSq = cr * cr;
        OrcaLine line;
        glm::vec2 u;
        if (distSq > crSq) {
            const glm::vec2 w = relVel - invTau * relPos;
            const float wl2 = absSq(w);
            const float dp1 = glm::dot(w, relPos);
            if (dp1 < 0.0f && dp1 * dp1 > crSq * wl2) {
                const float wl = std::sqrt(wl2);
                const glm::vec2 uw = w / wl;
                line.direction = glm::vec2(uw.y, -uw.x);
                u = (cr * invTau - wl) * uw;
            } else {
                const float leg = std::sqrt(distSq - crSq);
                if (det(relPos, w) > 0.0f) {
                    line.direction =
                        glm::vec2(relPos.x * leg - relPos.y * cr, relPos.x * cr + relPos.y * leg) / distSq;
                } else {
                    line.direction =
                        -glm::vec2(relPos.x * leg + relPos.y * cr, -relPos.x * cr + relPos.y * leg) / distSq;
                }
                u = glm::dot(relVel, line.direction) * line.direction - relVel;
            }
        } else {
            const float invDt = 1.0f / p.dt;
            const glm::vec2 w = relVel - invDt * relPos;
            const float wl = std::sqrt(absSq(w));
            const glm::vec2 uw = wl > 0.0f ? w / wl : glm::vec2(1.0f, 0.0f);
            line.direction = glm::vec2(uw.y, -uw.x);
            u = (cr * invDt - wl) * uw;
        }
        line.point = velocity + o.responsibility * u;
        lines.push_back(line);
    }
    glm::vec2 result{0.0f};
    const std::size_t fail = lp2(lines, p.maxSpeed, preferred, false, result);
    if (fail < lines.size()) {
        lp3(lines, hard, fail, p.maxSpeed, result, scratch);
    }
    return result;
}

} // namespace avgen::crowd
