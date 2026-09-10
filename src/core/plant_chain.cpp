#include "core/plant_chain.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace avgen::wind {
namespace {

constexpr float kSegment = 1.0f / static_cast<float>(kChainPoints); // rest length, plant heights

// The chain including its two anchors: index -1 is the pinned root at the origin and index -2 is a
// fixed point one segment *below* it, which is what makes the rest pose upright rather than merely
// straight -- without it the stalk could pivot freely at the soil line.
[[nodiscard]] inline glm::vec3 point(const std::array<glm::vec3, kChainPoints>& pos, int i) {
    if (i <= -2) {
        return {0.0f, -kSegment, 0.0f};
    }
    if (i == -1) {
        return {0.0f, 0.0f, 0.0f};
    }
    return pos[static_cast<std::size_t>(i)];
}

// Enforce the rest length exactly, root pinned. Length is a *constraint*, not a spring: a spring
// stiff enough not to stretch is a spring stiff enough to explode, and a stalk that stretches is
// the single tell that reads as rubber.
inline void project(std::array<glm::vec3, kChainPoints>& pos) {
    for (int iter = 0; iter < 2; ++iter) {
        for (int i = 0; i < kChainPoints; ++i) {
            const glm::vec3 prev = point(pos, i - 1);
            const glm::vec3 d = pos[static_cast<std::size_t>(i)] - prev;
            const float len = glm::length(d);
            pos[static_cast<std::size_t>(i)] =
                len > 1e-6f ? prev + d * (kSegment / len) : prev + glm::vec3(0.0f, kSegment, 0.0f);
        }
    }
}

// The bending force, and the part that took two attempts. The obvious form -- pull each point
// toward the straight continuation of the segment below it -- is a one-way spring: the joint at the
// soil only ever resists its own deflection, never the load of everything above it, so the base
// stays rigid and the last segment flails. That is a whip, not a stalk, and it is exactly the
// rubber the spec warns about. What is here instead is the discrete elastic rod: the energy is the
// squared curvature at each joint, `e = P(j-1) - 2 P(j) + P(j+1)`, and its gradient puts equal and
// opposite forces on the three points involved. The base joint then carries the reaction of every
// joint above it, which is what makes a cantilever bend in a smooth arc instead of cracking.
inline void bendForce(const std::array<glm::vec3, kChainPoints>& pos, float kBend,
                      std::array<glm::vec3, kChainPoints>& force) {
    for (int j = -1; j <= kChainPoints - 2; ++j) {
        const glm::vec3 f = (point(pos, j - 1) - 2.0f * point(pos, j) + point(pos, j + 1)) * kBend;
        if (j - 1 >= 0) {
            force[static_cast<std::size_t>(j - 1)] -= f;
        }
        if (j >= 0) {
            force[static_cast<std::size_t>(j)] += 2.0f * f;
        }
        force[static_cast<std::size_t>(j + 1)] -= f;
    }
}

// Bend the whole chain back toward upright by scaling every joint's angle from the vertical.
//
// Two things here were arrived at the hard way. Scaling the horizontal coordinates instead of the
// angles does not work: it shortens the segments, and putting their length back stands the chain up
// again by almost exactly as much as the scaling laid it down, so a chain that is nearly flat cannot
// be brought up at all. And the scale cannot be found by iterating -- the tip's offset is the sum of
// sines of the joint angles, which is flat in the scale exactly where the clamp matters -- so it is
// bisected, on a closed form that never touches the chain until the answer is known.
inline void clampLean(std::array<glm::vec3, kChainPoints>& pos, std::array<glm::vec3, kChainPoints>& vel,
                      float maxLean) {
    std::array<float, kChainPoints> theta{};
    std::array<glm::vec2, kChainPoints> dir{};
    for (int i = 0; i < kChainPoints; ++i) {
        const auto s = static_cast<std::size_t>(i);
        const glm::vec3 d = pos[s] - point(pos, i - 1);
        const float horiz = std::sqrt(d.x * d.x + d.z * d.z);
        theta[s] = std::atan2(horiz, d.y);
        dir[s] = horiz > 1e-7f ? glm::vec2(d.x / horiz, d.z / horiz) : glm::vec2(0.0f);
    }
    const auto leanAt = [&](float k) {
        glm::vec2 tip(0.0f);
        for (int i = 0; i < kChainPoints; ++i) {
            const auto s = static_cast<std::size_t>(i);
            tip += dir[s] * (kSegment * std::sin(theta[s] * k));
        }
        return glm::length(tip);
    };
    if (leanAt(1.0f) <= maxLean) {
        return;
    }
    float lo = 0.0f;
    float hi = 1.0f;
    for (int iter = 0; iter < 16; ++iter) {
        const float mid = 0.5f * (lo + hi);
        (leanAt(mid) > maxLean ? hi : lo) = mid;
    }
    const float k = lo;
    glm::vec3 at(0.0f);
    for (int i = 0; i < kChainPoints; ++i) {
        const auto s = static_cast<std::size_t>(i);
        const float t = theta[s] * k;
        at += glm::vec3(dir[s].x * kSegment * std::sin(t), kSegment * std::cos(t),
                        dir[s].y * kSegment * std::sin(t));
        pos[s] = at;
        vel[s] *= k;
    }
}

// One substep: semi-implicit Euler on the forces, then the length constraint, then velocities read
// back out of the constrained positions.
void substep(std::array<glm::vec3, kChainPoints>& pos, std::array<glm::vec3, kChainPoints>& vel, float kBend,
             float damp, const glm::vec3& accel, float dt) {
    std::array<glm::vec3, kChainPoints> force{};
    bendForce(pos, kBend, force);
    const std::array<glm::vec3, kChainPoints> before = pos;
    for (int i = 0; i < kChainPoints; ++i) {
        const auto s = static_cast<std::size_t>(i);
        vel[s] += (force[s] + accel - vel[s] * damp) * dt;
        pos[s] += vel[s] * dt;
    }
    project(pos);
    // Velocities come back out of the constrained positions rather than surviving the projection
    // untouched. Without this the length constraint keeps quietly injecting the energy it just
    // removed, and the chain never settles.
    const float inv = 1.0f / dt;
    for (int i = 0; i < kChainPoints; ++i) {
        const auto s = static_cast<std::size_t>(i);
        vel[s] = (pos[s] - before[s]) * inv;
    }
}

// ---- calibration -------------------------------------------------------------------------------
//
// The bending force is exactly linear in the point positions, so near the upright rest pose the
// horizontal dynamics is `a = K x` for a small constant matrix K. That is worth exploiting: every
// number the chain needs -- its static gain, its fundamental, how far above the fundamental its
// stiffest mode sits, and the self weight that would topple it -- is an exact property of K and of
// the geometric stiffness gravity adds, and an eigenvalue is a better thing to derive a time step
// from than a settling experiment. (The first version measured them by simulating, and quietly
// reported nonsense the moment the undamped free ring it was timing drifted.)

using Matrix = std::array<std::array<float, kChainPoints>, kChainPoints>;

// Gravity's contribution, per unit downward acceleration. Holding the segment lengths fixed makes
// the height of the chain a function of its lean -- y falls as the square of every joint's sideways
// offset -- so the potential is a negative definite quadratic form and gravity acts as a *negative*
// stiffness. It is the self weight of a standing column: below the buckling load it softens the
// plant and slows its recovery, and above it the plant lies down and never gets up.
[[nodiscard]] Matrix gravityStiffness() {
    Matrix g{};
    // V = -(1/2h) sum_m (N - m) (x_m - x_{m-1})^2, with x_-1 = 0 at the pinned root. Differentiate
    // twice: joint m couples x_m to x_{m-1} with weight (N - m) / h.
    for (int m = 0; m < kChainPoints; ++m) {
        const float w = static_cast<float>(kChainPoints - m) / kSegment;
        const auto a = static_cast<std::size_t>(m);
        g[a][a] += w;
        if (m >= 1) {
            const auto b = static_cast<std::size_t>(m - 1);
            g[b][b] += w;
            g[a][b] -= w;
            g[b][a] -= w;
        }
    }
    return g;
}

[[nodiscard]] Matrix bendStiffness() {
    Matrix k{};
    for (int col = 0; col < kChainPoints; ++col) {
        std::array<glm::vec3, kChainPoints> pos{};
        for (int i = 0; i < kChainPoints; ++i) {
            pos[static_cast<std::size_t>(i)] =
                glm::vec3(i == col ? 1.0f : 0.0f, kSegment * static_cast<float>(i + 1), 0.0f);
        }
        std::array<glm::vec3, kChainPoints> force{};
        bendForce(pos, 1.0f, force);
        for (int row = 0; row < kChainPoints; ++row) {
            k[static_cast<std::size_t>(row)][static_cast<std::size_t>(col)] =
                force[static_cast<std::size_t>(row)].x;
        }
    }
    return k;
}

// Gauss-Jordan on a matrix this small is exact and there is nothing to iterate.
[[nodiscard]] bool invert(const Matrix& in, Matrix& out) {
    constexpr int n = kChainPoints;
    float a[n][2 * n];
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            a[i][j] = in[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)];
            a[i][n + j] = i == j ? 1.0f : 0.0f;
        }
    }
    for (int col = 0; col < n; ++col) {
        int pivot = col;
        for (int r = col + 1; r < n; ++r) {
            if (std::fabs(a[r][col]) > std::fabs(a[pivot][col])) {
                pivot = r;
            }
        }
        if (std::fabs(a[pivot][col]) < 1e-12f) {
            return false;
        }
        if (pivot != col) {
            for (int j = 0; j < 2 * n; ++j) {
                std::swap(a[col][j], a[pivot][j]);
            }
        }
        const float scale = 1.0f / a[col][col];
        for (int j = 0; j < 2 * n; ++j) {
            a[col][j] *= scale;
        }
        for (int r = 0; r < n; ++r) {
            if (r == col || a[r][col] == 0.0f) {
                continue;
            }
            const float f = a[r][col];
            for (int j = 0; j < 2 * n; ++j) {
                a[r][j] -= f * a[col][j];
            }
        }
    }
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            out[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = a[i][n + j];
        }
    }
    return true;
}

// Largest eigenvalue by power iteration: a hundred passes over sixteen floats.
[[nodiscard]] float dominantEigenvalue(const Matrix& m) {
    std::array<float, kChainPoints> v{};
    v.fill(1.0f);
    v[0] = 0.37f; // anything not orthogonal to the dominant vector
    float lambda = 0.0f;
    for (int iter = 0; iter < 100; ++iter) {
        std::array<float, kChainPoints> w{};
        for (std::size_t i = 0; i < kChainPoints; ++i) {
            float sum = 0.0f;
            for (std::size_t j = 0; j < kChainPoints; ++j) {
                sum += m[i][j] * v[j];
            }
            w[i] = sum;
        }
        float norm = 0.0f;
        for (float x : w) {
            norm += x * x;
        }
        norm = std::sqrt(norm);
        if (norm < 1e-20f) {
            return 0.0f;
        }
        float dot = 0.0f;
        for (std::size_t i = 0; i < kChainPoints; ++i) {
            w[i] /= norm;
            dot += w[i] * v[i];
        }
        v = w;
        lambda = norm * std::fabs(dot);
    }
    return lambda;
}

// The chain's modes with `gFrac` of downward acceleration per unit joint spring already loading it.
// Everything scales with the joint spring, so this is a function of one number and the whole
// species model can be recovered from it by multiplying.
struct LoadedModes {
    float staticTip = 1.0f;
    float frequency = 1.0f;
    float stiffRatio = 1.0f;
    bool ok = false;
};

[[nodiscard]] LoadedModes loadedModes(float gFrac) {
    static const Matrix kBendMatrix = bendStiffness();
    static const Matrix kGravMatrix = gravityStiffness();
    LoadedModes out;
    Matrix s{};
    for (std::size_t i = 0; i < kChainPoints; ++i) {
        for (std::size_t j = 0; j < kChainPoints; ++j) {
            s[i][j] = -(kBendMatrix[i][j] + gFrac * kGravMatrix[i][j]);
        }
    }
    Matrix inv{};
    if (!invert(s, inv)) {
        return out;
    }
    float tip = 0.0f;
    for (std::size_t j = 0; j < kChainPoints; ++j) {
        tip += inv[kChainPoints - 1][j];
    }
    if (!(tip > 0.0f)) {
        return out; // past buckling: the "equilibrium" is on the wrong side of upright
    }
    out.staticTip = tip;
    const float lambdaLow = 1.0f / std::max(dominantEigenvalue(inv), 1e-12f);
    const float lambdaHigh = dominantEigenvalue(s);
    out.frequency = std::sqrt(std::max(lambdaLow, 1e-12f));
    out.stiffRatio = std::max(std::sqrt(std::max(lambdaHigh, 0.0f)) / out.frequency, 1.0f);
    out.ok = true;
    return out;
}

ChainCalibration measureCalibration() {
    ChainCalibration out;
    const LoadedModes bare = loadedModes(0.0f);
    out.staticTip = bare.staticTip;
    out.frequency = bare.frequency;
    out.stiffRatio = bare.stiffRatio;
    // The buckling load: the smallest downward acceleration whose geometric softening cancels the
    // bending stiffness. (K + lambda G) x = 0 is x = lambda (-K)^-1 G x, so it is one more power
    // iteration. Bisection would also work and would be less obviously right.
    static const Matrix kBendMatrix = bendStiffness();
    static const Matrix kGravMatrix = gravityStiffness();
    Matrix negK{};
    for (std::size_t i = 0; i < kChainPoints; ++i) {
        for (std::size_t j = 0; j < kChainPoints; ++j) {
            negK[i][j] = -kBendMatrix[i][j];
        }
    }
    Matrix invK{};
    if (invert(negK, invK)) {
        Matrix product{};
        for (std::size_t i = 0; i < kChainPoints; ++i) {
            for (std::size_t j = 0; j < kChainPoints; ++j) {
                float sum = 0.0f;
                for (std::size_t m = 0; m < kChainPoints; ++m) {
                    sum += invK[i][m] * kGravMatrix[m][j];
                }
                product[i][j] = sum;
            }
        }
        out.buckling = 1.0f / std::max(dominantEigenvalue(product), 1e-9f);
    }
    return out;
}

} // namespace

const ChainCalibration& chainCalibration(int) {
    // The point count is a compile-time constant, so this is one measurement for the life of the
    // process: a few milliseconds once, and thereafter two floats.
    static const ChainCalibration cal = measureCalibration();
    return cal;
}

// ---- the chain ---------------------------------------------------------------------------------

void PlantChain::reset() {
    for (int i = 0; i < kChainPoints; ++i) {
        pos[static_cast<std::size_t>(i)] = glm::vec3(0.0f, kSegment * static_cast<float>(i + 1), 0.0f);
        vel[static_cast<std::size_t>(i)] = glm::vec3(0.0f);
    }
    quiet = 0.0f;
    asleep = false;
}

void PlantChain::setFromBend(const glm::vec2& b, const glm::vec2& bendVel) {
    // The tip has to land exactly on `b` -- that is the number the shader reads and the number the
    // other tier would have produced -- and every segment has to keep its rest length. Both at once
    // is a small allocation problem: the tip offset is the sum of the joints' horizontal steps, so
    // hand each joint a share of it, cap any share a segment cannot physically reach, and give the
    // remainder to the joints that have room. The shares follow a quadratic profile, which is the
    // shape a uniformly loaded cantilever takes and close to the shape the chain settles into.
    const float want = glm::length(b);
    const glm::vec2 dir = want > 1e-6f ? b / want : glm::vec2(0.0f);
    constexpr float kCap = 0.97f * kSegment; // a segment lying flat still has to have a length
    const float reach = std::min(want, kCap * static_cast<float>(kChainPoints));

    std::array<float, kChainPoints> step{};
    float previousProfile = 0.0f;
    for (int i = 0; i < kChainPoints; ++i) {
        const float u = static_cast<float>(i + 1) / static_cast<float>(kChainPoints);
        const float profile = u * u; // 0 at the root, 1 at the tip, tangent to vertical at the base
        step[static_cast<std::size_t>(i)] = (profile - previousProfile) * reach;
        previousProfile = profile;
    }
    for (int pass = 0; pass < 4; ++pass) {
        float excess = 0.0f;
        float room = 0.0f;
        for (float& d : step) {
            if (d > kCap) {
                excess += d - kCap;
                d = kCap;
            } else {
                room += kCap - d;
            }
        }
        if (excess <= 1e-6f || room <= 1e-6f) {
            break;
        }
        for (float& d : step) {
            if (d < kCap) {
                d += excess * (kCap - d) / room;
            }
        }
    }

    glm::vec3 at(0.0f);
    for (int i = 0; i < kChainPoints; ++i) {
        const auto s = static_cast<std::size_t>(i);
        const float rise = std::sqrt(std::max(kSegment * kSegment - step[s] * step[s], 0.0f));
        at += glm::vec3(dir.x * step[s], rise, dir.y * step[s]);
        pos[s] = at;
        const float u = static_cast<float>(i + 1) / static_cast<float>(kChainPoints);
        vel[s] = glm::vec3(bendVel.x * u * u, 0.0f, bendVel.y * u * u);
    }
    quiet = 0.0f;
    asleep = false;
}

float PlantChain::speed() const {
    float s = 0.0f;
    for (int i = 0; i < kChainPoints; ++i) {
        s += glm::length(vel[static_cast<std::size_t>(i)]);
    }
    return s;
}

ChainTuning tuneChain(const ChainParams& p, float dt) {
    const ChainCalibration& cal = chainCalibration();
    ChainTuning t;
    const float omega0 = std::clamp(p.omega0, 0.05f, 60.0f);

    // Substeps first, because they can change the species. An explicit integrator survives a mode
    // only while `omega * h < 2`, and the mode that matters is not the one the plant is tuned to:
    // the chain's stiffest mode sits `stiffRatio` above its fundamental, so a 2.4 Hz grass blade is
    // really a 20 Hz problem. The step is chosen from the stiffest mode with a margin and capped at
    // kMaxChainSubsteps, so a hitch cannot turn into a spiral -- and if even that is not enough the
    // species is *softened* to what the step can carry rather than being allowed to explode.
    // Softening moves the resonance and leaves the settled lean alone, so the worst it can do is
    // make a very stiff plant ring a little slower than its transfer function says.
    const float safeStep = 1.6f / std::max(cal.stiffRatio * omega0, 1e-3f);
    const float accurateStep = kTau / (kChainStepsPerPeriod * std::max(omega0, 1e-3f));
    int steps = static_cast<int>(std::ceil(std::max(dt, 1e-6f) / std::min(accurateStep, safeStep)));
    steps = std::clamp(steps, 1, kMaxChainSubsteps);
    const float h = std::max(dt, 1e-6f) / static_cast<float>(steps);
    t.substep = h;
    t.omega = h > safeStep ? 1.6f / (cal.stiffRatio * h) : omega0;

    // Gravity, and the reason it is not simply `g / height`. The chain is dimensionless, so gravity
    // enters divided by the plant's height -- but the species numbers are not SI (a "mass" of 40 for
    // a tree is a fitting constant, not kilograms), so the raw ratio topples exactly the species
    // whose stiffness is smallest in those invented units. It is therefore admitted up to a fixed
    // fraction of the chain's own *measured* buckling load, which is the only threshold that means
    // anything here. At 0.4 of it gravity is doing real work -- the plant is visibly softer and
    // recovers more slowly from a shove -- and the loaded modes below put the settled lean back
    // exactly where Tier 0 has it, so the two tiers still agree.
    const float kBare = (t.omega / cal.frequency) * (t.omega / cal.frequency);
    const float wanted = std::max(p.gravity, 0.0f) * std::max(p.gravitySag, 0.0f) / std::max(p.height, 1e-3f);
    float gFrac = std::min(wanted / std::max(kBare, 1e-9f), 0.4f * cal.buckling);
    LoadedModes modes = loadedModes(gFrac);
    if (!modes.ok) {
        gFrac = 0.0f;
        modes = loadedModes(0.0f);
    }
    // Gravity softened the chain, so the joint spring has to come back up for the loaded
    // fundamental -- the frequency the plant is actually seen to ring at -- to be the species'.
    t.kBend = (t.omega / modes.frequency) * (t.omega / modes.frequency);
    t.gravity = gFrac * t.kBend;
    t.staticGain = modes.staticTip / t.kBend;
    // ...and the drive that makes the chain's steady lean equal Tier 0's, so a promoted plant
    // settles where its unpromoted neighbours stand.
    t.drive = 1.0f / std::max(t.staticGain, 1e-9f);
    t.damp = 2.0f * std::clamp(p.zeta, 0.02f, 4.0f) * t.omega;
    return t;
}

void stepChain(PlantChain& chain, const ChainTuning& t, const glm::vec2& driveBend, const glm::vec3& worldAccel,
               float height, float maxLean, float dt) {
    if (dt <= 0.0f) {
        return;
    }
    // The wind enters as a uniform acceleration whose size is set so the chain *settles* at exactly
    // the lean Tier 0 would have drawn. Everything Tier 1 adds is therefore transient: the tip
    // arrives late, goes past, and rings back, and it does that around a mean the other tier agrees
    // with. Disturbances arrive in metres per second squared and the chain thinks in plant heights.
    const glm::vec3 accel = glm::vec3(driveBend.x, 0.0f, driveBend.y) * t.drive +
                            worldAccel / std::max(height, 1e-3f) + glm::vec3(0.0f, -t.gravity, 0.0f);
    int steps = static_cast<int>(std::ceil(dt / t.substep));
    steps = std::clamp(steps, 1, kMaxChainSubsteps);
    const float h = dt / static_cast<float>(steps);
    for (int s = 0; s < steps; ++s) {
        substep(chain.pos, chain.vel, t.kBend, t.damp, accel, h);
    }
    // A stalk may not lie down flat: past `maxLean` the whole chain is scaled back horizontally.
    // The shader's soft ceiling is what a viewer sees; this one only keeps the integrator inside the
    // small-angle regime the spring was calibrated in, and is the reason a disturbance ten times too
    // strong bends a plant over instead of turning it into a NaN.
    clampLean(chain.pos, chain.vel, maxLean);
}

// ---- disturbances ------------------------------------------------------------------------------

void DisturbanceField::add(const Disturbance& d) {
    if (d.spent()) {
        return;
    }
    Disturbance e = d;
    const float len = glm::length(e.direction);
    e.direction = len > 1e-6f ? e.direction / len : glm::vec3(0.0f);
    if (count_ < items_.size()) {
        items_[count_++] = e;
        return;
    }
    // Full: the weakest live entry gives way, and if none is weaker the newcomer is dropped. Either
    // way the set stays bounded, which is what keeps the per-active-plant cost bounded.
    std::size_t weakest = 0;
    for (std::size_t i = 1; i < count_; ++i) {
        if (items_[i].strength < items_[weakest].strength) {
            weakest = i;
        }
    }
    if (items_[weakest].strength < e.strength) {
        items_[weakest] = e;
    }
}

void DisturbanceField::clear() {
    count_ = 0;
}

void DisturbanceField::advance(float dt) {
    std::size_t out = 0;
    for (std::size_t i = 0; i < count_; ++i) {
        items_[i].age += dt;
        if (!items_[i].spent()) {
            items_[out++] = items_[i];
        }
    }
    count_ = out;
}

void DisturbanceField::trackBody(const glm::vec3& position, const glm::vec3& velocity, const CameraWake& cfg) {
    if (!cfg.enabled) {
        return;
    }
    const float speed = glm::length(velocity);
    const glm::vec3 dir = speed > 1e-4f ? velocity / speed : glm::vec3(0.0f);
    const float strength = std::min(speed * cfg.strength, cfg.maxStrength);
    // Refresh a reserved slot rather than pushing a new impulse: a body moving through vegetation
    // is a continuous presence, and an impulse a frame would evict every other disturbance in as
    // many frames as the set is long.
    if (strength <= 0.0f && count_ == 0) {
        return; // a body standing still in an empty set raises nothing at all
    }
    Disturbance& wake = items_[0];
    if (count_ == 0) {
        count_ = 1;
        wake = Disturbance{};
    }
    wake.position = position + dir * cfg.lead;
    wake.direction = dir;
    wake.radius = cfg.radius;
    wake.strength = std::max(strength, 0.0f);
    wake.duration = std::max(cfg.decay, 1e-3f);
    // Held at zero while the body is moving, so the slot never retires under it; once it stops the
    // ageing in advance() runs the wake out over `decaySeconds`.
    if (strength > 0.0f) {
        wake.age = 0.0f;
    }
}

glm::vec3 DisturbanceField::accelerationAt(const glm::vec3& p) const {
    glm::vec3 sum(0.0f);
    for (std::size_t i = 0; i < count_; ++i) {
        const Disturbance& d = items_[i];
        const glm::vec3 delta = p - d.position;
        const float r2 = glm::dot(delta, delta);
        const float rr = d.radius * d.radius;
        if (r2 >= rr) {
            continue;
        }
        const float space = 1.0f - r2 / rr;
        const float time = std::max(1.0f - d.age / std::max(d.duration, 1e-4f), 0.0f);
        const float amount = d.strength * space * space * time * time;
        // A disturbance with no direction pushes radially out from its centre, which is what a
        // landing body or a downdraught does; one with a direction shoves that way.
        const glm::vec3 dir = glm::dot(d.direction, d.direction) > 0.5f
                                  ? d.direction
                                  : (r2 > 1e-8f ? delta / std::sqrt(r2) : glm::vec3(0.0f));
        sum += dir * amount;
    }
    return sum;
}

bool DisturbanceField::reaches(const glm::vec3& p) const {
    for (std::size_t i = 0; i < count_; ++i) {
        const Disturbance& d = items_[i];
        if (d.strength <= 0.0f || d.age >= d.duration) {
            continue;
        }
        const glm::vec3 delta = p - d.position;
        if (glm::dot(delta, delta) < d.radius * d.radius) {
            return true;
        }
    }
    return false;
}

} // namespace avgen::wind
