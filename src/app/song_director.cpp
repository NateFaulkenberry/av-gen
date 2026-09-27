#include "app/song_director.hpp"

#include "core/log.hpp"

#include <fmt/format.h>
#include <glm/gtc/constants.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>

namespace avgen::app {
namespace {

// The closest and widest a Song Mode shot stands, in subject radii. The intent's `distance` axis is
// a position in this band, which is what makes an intent portable between a two-metre artefact and
// a forty-metre tree: the band is in radii, as every other distance in the director is.
constexpr float kCloseRadii = 2.6f;
constexpr float kWideRadii = 14.0f;
// Below this the subject is a speck and a spotlight on it is a claim the picture does not support.
// The same number `Sequence::validate()` enforces; duplicated as a constant rather than reached for,
// because the alternative is exporting a private threshold from cinematic.cpp.
constexpr float kMinHeroCoverage = 0.06f;

// ---- determinism ---------------------------------------------------------------------------------
//
// A *function* of the decision's coordinates, never a draw from a stream -- the same argument
// `directFromStructure` makes and for the same reason: a stream makes every later choice depend on
// how many earlier ones were made, so adding one section would re-cast the whole rest of the film.
//
// The coordinates are (seed, section, occurrence, shot). `occurrence` is the one that matters and
// the one ADR-249 is about: two passes over the same material carry the same intent and differ only
// here, so they come out as different films of the same section.
std::uint32_t mix(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

std::uint32_t decisionHash(std::uint32_t seed, std::size_t section, int occurrence, std::size_t shot,
                           std::uint32_t salt) {
    std::uint32_t h = mix(seed * 0x9E3779B1u + salt);
    h = mix(h ^ (static_cast<std::uint32_t>(section) * 0x85EBCA6Bu));
    h = mix(h ^ (static_cast<std::uint32_t>(occurrence) * 0xC2B2AE35u));
    h = mix(h ^ (static_cast<std::uint32_t>(shot) * 0x27D4EB2Fu));
    return h;
}

// The shape of an intent, as one number. **Not its name** -- the six axes and the camera count,
// quantised so two intents a person would call the same are the same here.
//
// It exists so the camera *set* is a function of the intent and the world rather than of where in
// the piece the section happens to sit: two sections carrying the same intent then score the
// cameras identically, which is what makes the phase rule below a guarantee instead of a coin toss.
std::uint32_t intentShape(const ShotIntentProfile& intent) {
    const auto q = [](float v) { return static_cast<std::uint32_t>(std::lround(v * 1000.0f)); };
    std::uint32_t h = 0x9E3779B1u;
    for (const std::uint32_t part : {q(intent.heroEmphasis), q(intent.distance), q(intent.movement),
                                     q(intent.variation), q(intent.cutRate),
                                     static_cast<std::uint32_t>(intent.cameras)}) {
        h = mix(h ^ part);
    }
    return h;
}

// 0..1, and -1..1.
float unitOf(std::uint32_t h) { return static_cast<float>(h & 0xFFFFFFu) / 16777216.0f; }
float signedOf(std::uint32_t h) { return unitOf(h) * 2.0f - 1.0f; }

float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// How wide a camera is, 0..1, from whatever optical identity it states. A 24 mm camera is a wide
// camera and an 85 mm one is not, which is the whole of what a lens tells an intent.
//
// Falls back to the field of view when no focal length is stated, because `focalLength == 0` means
// "no opinion, use `fovDegrees`" (ADR-245) and a camera with an opinion expressed the other way is
// still a camera with an opinion.
float widenessOf(const scene::CameraRig& camera) {
    if (camera.focalLength > 0.0f) {
        // 18 mm and below is as wide as this scale goes; 85 mm and above as long. Linear in
        // millimetres rather than in angle, because millimetres is how the author chose it.
        return std::clamp((85.0f - camera.focalLength) / (85.0f - 18.0f), 0.0f, 1.0f);
    }
    if (camera.fovDegrees > 0.0f) {
        return std::clamp((camera.fovDegrees - 20.0f) / (80.0f - 20.0f), 0.0f, 1.0f);
    }
    return 0.5f;
}

// How much a camera is *about a subject*, 0..1.
//
// A camera that rides or aims at a composition node is watching something; the Auto-director's own
// camera is, by construction, always framing whatever the shot is of; everything else is a placed
// viewpoint, which is a shot of a world. Three states and no more, because the camera model does
// not carry a fourth.
float subjectnessOf(const scene::CameraRig& camera) {
    if (!camera.followNode.empty() || !camera.aimNode.empty()) {
        return 1.0f;
    }
    if (camera.id == scene::kMainCamera) {
        // The director bakes the framing onto this one, so it is whatever the intent asked for. A
        // fraction above neutral rather than 1.0: a camera composed to watch something is a better
        // answer for a hero intent than a camera that merely can be.
        return 0.65f;
    }
    return 0.2f;
}

struct Candidate {
    const scene::CameraRig* camera = nullptr;
    float score = 0.0f;
};

// The cast rotation, lifted in shape from `directFromStructure` (ADR-202/203) and given one extra
// input: the intent's `heroEmphasis` leans the weights towards or away from the film's subject.
//
// Smooth weighted round-robin rather than a proportional draw, for ADR-202's reason: the turns come
// out *spread* rather than clumped. What is new is that the lean is per section, so a run of
// environment-intent sections genuinely stops being about the star and a run of hero-intent ones
// genuinely is -- without any section kind being consulted.
class Cast {
public:
    Cast(const DirectionBrief& brief) {
        targets_.push_back(brief.hero);
        targets_.insert(targets_.end(), brief.supporting.begin(), brief.supporting.end());
        credit_.assign(targets_.size(), 0.0);
    }

    [[nodiscard]] bool empty() const { return targets_.empty(); }
    [[nodiscard]] const FocalTarget& at(std::size_t i) const { return targets_[i]; }
    [[nodiscard]] std::size_t size() const { return targets_.size(); }

    // Seeds the starting credit so a different seed opens the film on a different subject without
    // changing anybody's share -- the phase of the rotation, not its weights.
    void seed(std::uint32_t h) {
        double total = 0.0;
        for (const FocalTarget& t : targets_) {
            total += std::max(static_cast<double>(t.importance), 0.05);
        }
        for (std::size_t i = 0; i < targets_.size(); ++i) {
            credit_[i] = total * static_cast<double>(unitOf(mix(h + static_cast<std::uint32_t>(i) *
                                                                       0x9E3779B1u)));
        }
    }

    std::size_t next(float heroEmphasis) {
        // The lean. At emphasis 1 the film's subject accrues three times its own importance and
        // everybody else two fifths of theirs; at 0 the reverse. At 0.5 nothing moves, which is the
        // property that keeps this a bias rather than a second casting rule.
        const double heroScale = static_cast<double>(lerpf(0.30f, 3.0f, heroEmphasis));
        const double restScale = static_cast<double>(lerpf(1.7f, 0.40f, heroEmphasis));
        double total = 0.0;
        std::vector<double> weight(targets_.size(), 0.0);
        for (std::size_t i = 0; i < targets_.size(); ++i) {
            const double base = std::max(static_cast<double>(targets_[i].importance), 0.05);
            weight[i] = base * (i == 0 ? heroScale : restScale);
            total += weight[i];
        }
        std::size_t best = 0;
        for (std::size_t i = 0; i < targets_.size(); ++i) {
            credit_[i] += weight[i];
            // Strictly greater, so a tie goes to the more important one and two runs cast alike.
            if (credit_[i] > credit_[best]) {
                best = i;
            }
        }
        credit_[best] -= total;
        return best;
    }

private:
    std::vector<FocalTarget> targets_;
    std::vector<double> credit_;
};

// Which way a move travels in distance: negative closes in, positive opens out, zero holds. The
// second half of the kind's identity, and the reason an `Approach` and a `Reveal` at the same
// nominal distance are different shots.
float openingOf(ShotKind kind) {
    switch (kind) {
    case ShotKind::Approach:
        return -1.0f;
    case ShotKind::Discovery:
        return -0.65f;
    case ShotKind::Reveal:
        return 1.0f;
    case ShotKind::HeroReveal:
        return 0.70f;
    case ShotKind::Establish:
    case ShotKind::Drift:
    case ShotKind::Track:
    case ShotKind::Orbit:
    case ShotKind::Transition:
    case ShotKind::Entry:
    case ShotKind::Passage:
    case ShotKind::Descent:
    case ShotKind::Ascent:
    case ShotKind::Flyby:
        return 0.0f;
    }
    return 0.0f;
}

float bestCoverage(const Shot& shot) {
    float best = 0.0f;
    for (int i = 0; i <= 8; ++i) {
        best = std::max(best, shot.subjectCoverageAt(static_cast<float>(i) / 8.0f));
    }
    return best;
}

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

// =================================================================================================
// ADR-921: the duration rule, and the grid it lands on
// =================================================================================================
//
// Every number below is a named part of the rule the ADR writes down. None of them reads a label.

// How far the section's level-free density moves the cut rate, at Expressive: a section at density
// 1 cuts 0.30 of the band faster than one at 0.5, and one at 0 that much slower.
constexpr float kDensityWeight = 0.30f;
// How far the intent's visual density moves it: a full frame asks for shorter shots.
constexpr float kVisualDensityWeight = 0.10f;
// Onsets per second read as density 0 and 1. A four-on-the-floor groove with hats sits near 5 --
// the middle -- a drum-less passage near 0, a busy break with sixteenth hats and fills near 9.
constexpr float kOnsetsSparse = 1.0f;
constexpr float kOnsetsDense = 9.0f;
// **An accelerating arc halves its shot length this many times across its section at a cut rate
// of 1** (and proportionally fewer below): a Rising treatment opens at its steady pace and closes
// `2^(4 * rate)` times faster, a Burst the mirror. Four halvings is 2 bars -> 1 -> 2 beats -> 1:
// how a riser's roll subdivides.
constexpr double kArcHalvings = 4.0;
// `song::ShotIntent::atProgress`'s burst decay, in section progress (shot_intent.cpp kBurstDecay).
constexpr double kBurstDecay = 0.22;
// A subject that moves on its own holds a shot longer than one that stands still, relative to the
// cast's mean: `1 + 0.35 * (motion - mean)`. Relative, so a cast of statues cuts at the band.
constexpr float kMotionGain = 0.35f;
// The intent's `variation` -- "how different successive shots should be" -- varies their length as
// well as their framing: shot k aims at `2^(variation * s_k)` of the pace, s_k in [-1, 1] from the
// decision hash. At variation 1 one shot may aim at half the pace's length and the next at twice it
// -- a bar, then four -- which is what it takes for a length to move off a bar line; at 0 every shot
// aims at the pace. Not in an accelerating section, whose arc is its shape.
constexpr double kVariationOctaves = 1.0;
// A section's wide opener that establishes scale holds up to 60% longer, and may run that far past
// the longest shot.
constexpr float kScaleGain = 0.6f;
// An opener arriving after a section denser by more than this holds longer by the difference (at
// most half again): a long shot after short ones is contrast.
constexpr float kContrastThreshold = 0.15f;
constexpr float kContrastMax = 0.5f;
// The opener's ceiling never exceeds this multiple of the longest shot, whatever scale and contrast
// ask for.
constexpr double kOpenerCeiling = 1.8;
// A Suspended section holds as one shot until it is longer than this multiple of the longest shot.
constexpr double kSuspendedHold = 2.0;
// Below this a shot is a flash of a composition, not a move: it does not travel from one subject to
// another, and its camera travels in proportion to its length.
constexpr double kMinTravelSeconds = 1.5;
constexpr double kFullTravelSeconds = 2.5;
// ADR-922: a section is one of the film's peaks when its push times its music energy is within this
// share of the film's largest, and at least `kPeakFloor`; a film in which more than half the sections
// qualify has no peaks.
constexpr float kPeakShare = 0.85f;
constexpr float kPeakFloor = 0.5f;
// What a cut position costs on the grid, by strength (beat, half-bar, bar, half-phrase, phrase), in
// the same units as |ln(length / aim)|: a bar line is worth landing 35% off the aim for, a phrase line
// more. Scaled down for short aims, where a beat is the natural unit (`gridCost`).
constexpr std::array<double, 5> kGridCost{0.9, 0.6, 0.3, 0.1, 0.0};
// A section boundary snaps to the nearest bar line within half a bar, else to the nearest beat
// within half a beat, else stays where it was authored.

// The section's level-free density, 0..1 (ADR-897's measures): half its energy composite as a share
// of the piece's largest, half its percussive onset rate. The authored energy and density when the
// plan carries no measurement.
float densityOf(const SongPlanSection& s, float maxComposite) {
    if (s.audio.measured() && maxComposite > 0.0f) {
        const float share = clamp01(s.audio.energy / maxComposite);
        const float onsets = clamp01((s.audio.onsetRate - kOnsetsSparse) / (kOnsetsDense - kOnsetsSparse));
        return 0.5f * share + 0.5f * onsets;
    }
    return clamp01(0.5f * (s.energy + s.density));
}

// The section's music energy as a share of the piece's, 0..1: the composite when measured, else its own.
float musicEnergyOf(const SongPlanSection& s, float maxComposite) {
    if (s.audio.measured() && maxComposite > 0.0f) {
        return clamp01(s.audio.energy / maxComposite);
    }
    return clamp01(s.energy);
}

// How much a shot with this intent establishes scale, 0..1: wide, and about the world.
float scaleOf(const ShotIntentProfile& intent) {
    return clamp01((intent.distance - 0.5f) / 0.5f) * clamp01(1.0f - intent.heroEmphasis);
}

bool accelerates(song::Arc arc) { return arc == song::Arc::Rising || arc == song::Arc::Burst; }

// The pace of one section: the length a shot aims at, as a function of how far through the section
// it is. The whole of the duration rule's music half.
class SectionPace {
public:
    SectionPace(const SongPlanSection& section, const SongDirectorOptions& options, bool mayChoose,
                bool expressive, float density, double floorSeconds)
        : section_(section), options_(options), mayChoose_(mayChoose), expressive_(expressive),
          density_(density), floor_(floorSeconds) {
        const song::Arc arc = section.intent.arc;
        if (arc == song::Arc::Rising) {
            // Opens at the steady pace of the arc's own opening dial; closes faster by the arc's
            // acceleration at its closing dial, never below the build floor.
            open_ = band(rateAt(0.0f));
            close_ = std::max(floor_, open_ / std::pow(2.0, kArcHalvings * static_cast<double>(rateAt(1.0f))));
        } else if (arc == song::Arc::Burst) {
            close_ = band(rateAt(1.0f));
            open_ = std::max(floor_, close_ / std::pow(2.0, kArcHalvings * static_cast<double>(rateAt(0.0f))));
        }
    }

    // The cut rate `progress` of the way through: the treatment's dial with its arc applied
    // (`SongPlanSection::intentAtProgress`, ADR-920), moved by visual density when the director may
    // choose and by the measured density when it may also read the music.
    [[nodiscard]] float rateAt(float progress) const {
        float r = section_.intentAtProgress(progress).cutRate;
        if (mayChoose_) {
            r += kVisualDensityWeight * (2.0f * section_.intent.visualDensity - 1.0f);
        }
        if (expressive_) {
            r += kDensityWeight * (2.0f * density_ - 1.0f);
        }
        return clamp01(r);
    }

    // The band: a rate of 0 aims at the longest shot, 1 at the shortest, geometrically between.
    [[nodiscard]] double band(float rate) const {
        const double lo = options_.minShotSeconds;
        const double hi = options_.maxShotSeconds;
        return hi * std::pow(lo / hi, static_cast<double>(clamp01(rate)));
    }

    // The length a shot aims at, `progress` of the way through the section.
    [[nodiscard]] double lengthAt(float progress) const {
        const float p = clamp01(progress);
        switch (section_.intent.arc) {
        case song::Arc::Rising:
            return open_ * std::pow(close_ / open_, static_cast<double>(p));
        case song::Arc::Burst:
            return close_ + (open_ - close_) * std::exp(-static_cast<double>(p) / kBurstDecay);
        case song::Arc::Steady:
        case song::Arc::Falling:
        case song::Arc::Suspended:
            break;
        }
        return band(rateAt(p));
    }

    // The length whose pace integral from `t` is one shot, over [a, b]: what a shot starting at `t`
    // aims at. Past `b` the closing pace continues, so a shot near the end aims past it and the
    // layout gives it the rest of the section.
    [[nodiscard]] double aimFrom(double t, double a, double b) const {
        const double span = b - a;
        if (!(span > 0.0)) {
            return lengthAt(0.0f);
        }
        const double step = std::max(span / 2048.0, 1e-3);
        double tau = std::max(t, a);
        double acc = 0.0;
        while (tau < b) {
            const double dt = std::min(step, b - tau);
            const double length = lengthAt(static_cast<float>((tau + 0.5 * dt - a) / span));
            if (acc + dt / length >= 1.0) {
                return (tau - t) + (1.0 - acc) * length;
            }
            acc += dt / length;
            tau += dt;
        }
        return (b - t) + (1.0 - acc) * lengthAt(1.0f);
    }

    // How many shots the pace asks for across [a, b]: its integral.
    [[nodiscard]] double shotsAsked(double a, double b) const {
        const double span = b - a;
        if (!(span > 0.0)) {
            return 1.0;
        }
        constexpr int kSteps = 1024;
        double acc = 0.0;
        for (int i = 0; i < kSteps; ++i) {
            acc += (span / kSteps) / lengthAt((static_cast<float>(i) + 0.5f) / static_cast<float>(kSteps));
        }
        return acc;
    }

private:
    const SongPlanSection& section_;
    const SongDirectorOptions& options_;
    bool mayChoose_ = false;
    bool expressive_ = false;
    float density_ = 0.5f;
    double floor_ = 0.0;
    double open_ = 0.0;
    double close_ = 0.0;
};

// What landing a cut on a grid position costs: the strength's price, cheaper for short aims (a beat
// is the natural unit of a half-second shot and a poor one for a six-second shot).
double gridCost(int strength, double aimSeconds, double beatSeconds) {
    const double scale = std::clamp(aimSeconds / std::max(4.0 * beatSeconds, 1e-6), 0.25, 1.0);
    return kGridCost[static_cast<std::size_t>(std::clamp(strength, 0, 4))] * scale;
}

// One section's layout: its cut positions (including both ends) and, for each shot, what its end
// landed on and what it aimed at.
struct Layout {
    std::vector<double> cuts;          // size = shots + 1
    std::vector<int> endStrength;      // per shot: grid strength of its end, -1 = the section's end
    std::vector<double> aims;          // per shot: the length it aimed at
    bool gridded = false;
};

// The positions a cut may land on inside [a, b], with their strengths. Both ends are included; an end
// is strength 5 (free: the section's own boundary). Long sections keep only their stronger positions
// so the layout stays cheap.
struct Positions {
    std::vector<double> t;
    std::vector<int> strength;
};

Positions positionsFor(const MusicalGrid& grid, double a, double b) {
    Positions p;
    p.t.push_back(a);
    p.strength.push_back(5);
    const double guard = std::max(1e-3, 0.05 * grid.beatSeconds());
    std::vector<std::size_t> inner = grid.beatsBetween(a + guard, b - guard);
    int minStrength = 0;
    if (inner.size() > 480) {
        minStrength = 2; // bar lines only past four minutes of beats in one section
    } else if (inner.size() > 200) {
        minStrength = 1;
    }
    for (const std::size_t k : inner) {
        const int s = grid.strength(k);
        if (s >= minStrength) {
            p.t.push_back(grid.beatTimes[k]);
            p.strength.push_back(s);
        }
    }
    p.t.push_back(b);
    p.strength.push_back(5);
    return p;
}

// The layout's one rule, as a cost: how far a shot of `length` misses its `aim`, in log ratio.
double lengthCost(double length, double aim) { return std::abs(std::log(length / std::max(aim, 1e-6))); }

// Where the cuts go on a grid (ADR-921), by dynamic programming over the grid's positions: the path
// from the section's start to its end minimising the sum over shots of |ln(length / aim)| plus the
// grid cost of where each shot ends. `aimOf(k, i)` is what shot `k` starting at position `i` aims
// at, and `ceilingOf(k)` the longest it may run.
//
// `fixedCount` > 0 asks for exactly that many shots (the second pass, once the cast is known);
// otherwise the count falls out of the cost. `monotone` is +1 for "never longer than the shot
// before" (Rising), -1 for "never shorter" (Burst), 0 for neither. A shot may not be shorter than
// `floor` unless the whole section is.
template <typename AimFn, typename CeilingFn>
std::optional<Layout> layoutOnGrid(const Positions& pos, double beatSeconds, double floor, AimFn aimOf,
                                   CeilingFn ceilingOf, int monotone, std::size_t fixedCount) {
    const std::size_t n = pos.t.size();
    if (n < 2) {
        return std::nullopt;
    }
    const std::size_t last = n - 1;
    const double span = pos.t[last] - pos.t[0];
    const double tolerance = 0.25 * beatSeconds;
    // The floor and the ceiling are the panel's own numbers and are held -- with half a beat of
    // grace for the tracked grid's own jitter (ADR-896: within 19 ms of the true beats), so a
    // four-bar shot is not refused for being a millisecond long.
    const double grace = std::max(0.5 * beatSeconds, 1e-6);
    const double floorGrace = std::min(grace, 0.01 * floor);
    constexpr double kInf = std::numeric_limits<double>::infinity();
    const auto edgeCost = [&](std::size_t k, std::size_t i, std::size_t j, double& aimOut) -> double {
        const double length = pos.t[j] - pos.t[i];
        if (!(length > 0.0)) {
            return kInf;
        }
        const bool whole = i == 0 && j == last;
        if (length < floor - floorGrace && !(whole && span < floor)) {
            return kInf;
        }
        // A section that cannot be divided inside the band is one shot, rather than no film.
        if (length > ceilingOf(k) + grace && !(whole && k == 0 && fixedCount <= 1)) {
            return kInf;
        }
        aimOut = aimOf(k, i);
        const double landing = j == last ? 0.0 : gridCost(pos.strength[j], aimOut, beatSeconds);
        // Weighted by the shot's share of the section: the objective is the time-average of how far
        // the picture runs from its pace. Summed per shot instead, every extra cut paid a landing
        // cost and one forty-second shot came out cheaper than twenty-four aimed ones.
        return (length / span) * (lengthCost(length, aimOut) + landing);
    };
    const auto build = [&](const std::vector<std::size_t>& path) {
        Layout out;
        out.gridded = true;
        for (std::size_t idx = 0; idx < path.size(); ++idx) {
            out.cuts.push_back(pos.t[path[idx]]);
        }
        for (std::size_t s = 0; s + 1 < path.size(); ++s) {
            out.endStrength.push_back(path[s + 1] == last ? -1 : pos.strength[path[s + 1]]);
            out.aims.push_back(aimOf(s, path[s]));
        }
        return out;
    };

    if (monotone != 0) {
        // The state is the last edge, because the constraint is on consecutive lengths. O(n^3), and
        // only accelerating sections -- short, as risers and impacts are -- come here.
        std::vector<double> best(n * n, kInf);
        std::vector<std::size_t> from(n * n, n);
        std::vector<int> count(n * n, 0);
        for (std::size_t j = 1; j < n; ++j) {
            double aim = 0.0;
            best[j] = edgeCost(0, 0, j, aim); // edge (0, j) at index 0 * n + j
            count[j] = 1;
        }
        for (std::size_t i = 1; i < last; ++i) {
            for (std::size_t j = i + 1; j < n; ++j) {
                const double lij = pos.t[j] - pos.t[i];
                double bestHere = kInf;
                std::size_t bestFrom = n;
                int bestCount = 0;
                for (std::size_t h = 0; h < i; ++h) {
                    const double prior = best[h * n + i];
                    if (prior == kInf) {
                        continue;
                    }
                    const double lhi = pos.t[i] - pos.t[h];
                    if (monotone > 0 && lij > lhi + tolerance) {
                        continue;
                    }
                    if (monotone < 0 && lij < lhi - tolerance) {
                        continue;
                    }
                    const int k = count[h * n + i];
                    double aim = 0.0;
                    const double c = edgeCost(static_cast<std::size_t>(k), i, j, aim);
                    if (c == kInf) {
                        continue;
                    }
                    if (prior + c < bestHere) {
                        bestHere = prior + c;
                        bestFrom = h;
                        bestCount = k + 1;
                    }
                }
                best[i * n + j] = bestHere;
                from[i * n + j] = bestFrom;
                count[i * n + j] = bestCount;
            }
        }
        double total = kInf;
        std::size_t endI = n;
        for (std::size_t i = 0; i < last; ++i) {
            if (best[i * n + last] < total) {
                total = best[i * n + last];
                endI = i;
            }
        }
        if (endI == n) {
            return std::nullopt;
        }
        std::vector<std::size_t> path{last};
        std::size_t i = endI;
        std::size_t j = last;
        while (true) {
            path.push_back(i);
            if (i == 0) {
                break;
            }
            const std::size_t h = from[i * n + j];
            j = i;
            i = h;
        }
        std::reverse(path.begin(), path.end());
        return build(path);
    }

    // No ordering between lengths: the state is (shots so far, position). With a fixed count this is
    // exact; without one, the count is whatever minimises the cost, capped at what the floor allows.
    const std::size_t maxShots = fixedCount > 0
                                     ? fixedCount
                                     : std::max<std::size_t>(1, static_cast<std::size_t>(span / std::max(floor, 1e-3)) + 1);
    std::vector<std::vector<double>> best(maxShots + 1, std::vector<double>(n, kInf));
    std::vector<std::vector<std::size_t>> from(maxShots + 1, std::vector<std::size_t>(n, n));
    best[0][0] = 0.0;
    for (std::size_t k = 1; k <= maxShots; ++k) {
        for (std::size_t j = 1; j < n; ++j) {
            for (std::size_t i = 0; i < j; ++i) {
                if (best[k - 1][i] == kInf) {
                    continue;
                }
                double aim = 0.0;
                const double c = edgeCost(k - 1, i, j, aim);
                if (c == kInf) {
                    continue;
                }
                if (best[k - 1][i] + c < best[k][j]) {
                    best[k][j] = best[k - 1][i] + c;
                    from[k][j] = i;
                }
            }
        }
    }
    std::size_t bestK = 0;
    double total = kInf;
    for (std::size_t k = fixedCount > 0 ? fixedCount : 1; k <= maxShots; ++k) {
        if (best[k][last] < total) {
            total = best[k][last];
            bestK = k;
        }
    }
    if (bestK == 0) {
        return std::nullopt;
    }
    std::vector<std::size_t> path{last};
    std::size_t j = last;
    for (std::size_t k = bestK; k > 0; --k) {
        j = from[k][j];
        path.push_back(j);
    }
    std::reverse(path.begin(), path.end());
    return build(path);
}

// Where the cuts go with no grid: the pace integral says how many shots, and the shots share the
// section in proportion to the pace -- a Steady section divides evenly, as Song Mode always did; an
// accelerating one shortens as its pace climbs. `weights` scales each shot's share (the cast's
// motion, the opener's scale and contrast); empty is all ones. A count the floor cannot hold is
// reduced until it can. [a, b] may be the tail of a section `[sectionA, sectionB]` -- the pace is the
// section's, read at the section's own progress.
Layout layoutFree(const SectionPace& pace, double a, double b, double floor, std::size_t count,
                  const std::vector<double>& weights, double sectionA, double sectionB) {
    const double span = b - a;
    const double sectionSpan = sectionB - sectionA;
    const auto progressOf = [&](double t) {
        return sectionSpan > 0.0 ? static_cast<float>((t - sectionA) / sectionSpan) : 0.0f;
    };
    Layout out;
    std::size_t shots = std::max<std::size_t>(1, count);
    // The cumulative pace, sampled, so shot boundaries sit at equal steps of it.
    constexpr int kSteps = 2048;
    std::vector<double> cumulative(kSteps + 1, 0.0);
    for (int i = 0; i < kSteps; ++i) {
        const double dt = span / kSteps;
        cumulative[static_cast<std::size_t>(i) + 1] =
            cumulative[static_cast<std::size_t>(i)] +
            dt / pace.lengthAt(progressOf(a + dt * (static_cast<double>(i) + 0.5)));
    }
    const double total = cumulative.back();
    const auto timeAtPace = [&](double target) {
        const auto it = std::lower_bound(cumulative.begin(), cumulative.end(), target);
        const auto i = static_cast<std::size_t>(std::distance(cumulative.begin(), it));
        if (i == 0) {
            return a;
        }
        if (i > static_cast<std::size_t>(kSteps)) {
            return b;
        }
        const double c0 = cumulative[i - 1];
        const double c1 = cumulative[i];
        const double f = c1 > c0 ? (target - c0) / (c1 - c0) : 0.0;
        return a + span * (static_cast<double>(i - 1) + f) / kSteps;
    };
    while (true) {
        std::vector<double> w(shots, 1.0);
        for (std::size_t k = 0; k < shots && k < weights.size(); ++k) {
            w[k] = std::max(weights[k], 0.05);
        }
        double sum = 0.0;
        for (const double x : w) {
            sum += x;
        }
        out.cuts.assign(1, a);
        double acc = 0.0;
        for (std::size_t k = 0; k + 1 < shots; ++k) {
            acc += w[k] / sum;
            out.cuts.push_back(timeAtPace(acc * total));
        }
        out.cuts.push_back(b);
        bool fits = true;
        for (std::size_t k = 0; k < shots; ++k) {
            if (out.cuts[k + 1] - out.cuts[k] < floor - 1e-6) {
                fits = false;
                break;
            }
        }
        if (fits || shots == 1) {
            break;
        }
        --shots;
    }
    out.endStrength.assign(shots, -1);
    out.aims.clear();
    for (std::size_t k = 0; k < shots; ++k) {
        out.aims.push_back(pace.aimFrom(out.cuts[k], sectionA, sectionB));
    }
    return out;
}

} // namespace

// ---- the grid ------------------------------------------------------------------------------------

double MusicalGrid::beatSeconds() const {
    if (beatTimes.size() < 2) {
        return 0.0;
    }
    std::vector<double> gaps;
    gaps.reserve(beatTimes.size() - 1);
    for (std::size_t i = 1; i < beatTimes.size(); ++i) {
        gaps.push_back(beatTimes[i] - beatTimes[i - 1]);
    }
    std::nth_element(gaps.begin(), gaps.begin() + static_cast<std::ptrdiff_t>(gaps.size() / 2), gaps.end());
    return gaps[gaps.size() / 2];
}

int MusicalGrid::strength(std::size_t k) const {
    const std::int64_t beat = musicalBeat(k);
    const auto mod = [](std::int64_t v, std::int64_t m) { return ((v % m) + m) % m; };
    const std::int64_t bar = std::max(1, beatsPerBar);
    const std::int64_t phrase = bar * std::max(1, phraseBars);
    if (mod(beat, phrase) == 0) {
        return 4;
    }
    if (phraseBars % 2 == 0 && mod(beat, phrase / 2) == 0) {
        return 3;
    }
    if (mod(beat, bar) == 0) {
        return 2;
    }
    if (bar % 2 == 0 && mod(beat, bar / 2) == 0) {
        return 1;
    }
    return 0;
}

const char* MusicalGrid::strengthName(int strength) {
    switch (strength) {
    case 4:
        return "phrase";
    case 3:
        return "half-phrase";
    case 2:
        return "bar";
    case 1:
        return "half-bar";
    case 0:
        return "beat";
    default:
        return "section";
    }
}

std::optional<std::size_t> MusicalGrid::nearestBeat(double seconds, double within) const {
    if (beatTimes.empty()) {
        return std::nullopt;
    }
    const auto it = std::lower_bound(beatTimes.begin(), beatTimes.end(), seconds);
    std::optional<std::size_t> best;
    double bestGap = within;
    for (const auto candidate : {it, it == beatTimes.begin() ? it : std::prev(it)}) {
        if (candidate == beatTimes.end()) {
            continue;
        }
        const double gap = std::abs(*candidate - seconds);
        if (gap <= bestGap) {
            bestGap = gap;
            best = static_cast<std::size_t>(std::distance(beatTimes.begin(), candidate));
        }
    }
    return best;
}

std::optional<std::size_t> MusicalGrid::nearestDownbeat(double seconds, double within) const {
    const auto near = nearestBeat(seconds, within + barSeconds());
    if (!near) {
        return std::nullopt;
    }
    std::optional<std::size_t> best;
    double bestGap = within;
    const std::size_t lo = *near >= static_cast<std::size_t>(beatsPerBar) ? *near - static_cast<std::size_t>(beatsPerBar) : 0;
    const std::size_t hi = std::min(beatTimes.size() - 1, *near + static_cast<std::size_t>(beatsPerBar));
    for (std::size_t k = lo; k <= hi; ++k) {
        if (strength(k) < 2) {
            continue;
        }
        const double gap = std::abs(beatTimes[k] - seconds);
        if (gap <= bestGap) {
            bestGap = gap;
            best = k;
        }
    }
    return best;
}

std::int64_t MusicalGrid::barNumber(std::size_t k) const {
    const std::int64_t bar = std::max(1, beatsPerBar);
    const std::int64_t beat = musicalBeat(k);
    const std::int64_t floorDiv = beat >= 0 ? beat / bar : -((-beat + bar - 1) / bar);
    return floorDiv + 1;
}

int MusicalGrid::beatInBar(std::size_t k) const {
    const std::int64_t bar = std::max(1, beatsPerBar);
    const std::int64_t beat = musicalBeat(k);
    return static_cast<int>(((beat % bar) + bar) % bar) + 1;
}

std::vector<std::size_t> MusicalGrid::beatsBetween(double a, double b) const {
    std::vector<std::size_t> out;
    const auto lo = std::upper_bound(beatTimes.begin(), beatTimes.end(), a);
    for (auto it = lo; it != beatTimes.end() && *it < b; ++it) {
        out.push_back(static_cast<std::size_t>(std::distance(beatTimes.begin(), it)));
    }
    return out;
}

MusicalGrid musicalGridFrom(std::span<const double> beatTimes, const analysis::Meter& meter) {
    MusicalGrid grid;
    grid.beatTimes.assign(beatTimes.begin(), beatTimes.end());
    const analysis::Meter m = meter.sanitized();
    grid.downbeat = m.downbeat;
    grid.beatsPerBar = m.beatsPerBar;
    grid.phraseBars = m.phraseBars;
    return grid;
}

// ---- the two exposed tables ----------------------------------------------------------------------

ShotKind shotKindForIntent(const ShotIntentProfile& intent) {
    const bool moving = intent.movement > 0.45f;
    const bool wide = intent.distance > 0.5f;
    if (intent.heroEmphasis < 0.35f) {
        // The world's shot. It is not about anybody, so it either holds or it travels past.
        if (!moving) {
            return ShotKind::Establish; // hold wide; the world, not the subject
        }
        return ShotKind::Drift; // lateral travel with the aim held: parallax, not a pan
    }
    if (intent.heroEmphasis > 0.70f) {
        // The subject's shot.
        if (!moving) {
            return wide ? ShotKind::Establish  // held and wide is still an establishing frame
                        : ShotKind::Track;     // held and close is a follow
        }
        return wide ? ShotKind::HeroReveal   // round it *while* opening out: silhouette and scale
                    : ShotKind::Approach;    // close the gap; its scale becomes apparent
    }
    // Between the two. A shot with a subject that is not the point of the film: it finds one, leaves
    // one, or circles one.
    if (!moving) {
        return wide ? ShotKind::Establish : ShotKind::Orbit;
    }
    return wide ? ShotKind::Discovery : ShotKind::Transition;
}

float cameraMatch(const scene::CameraRig& camera, const ShotIntentProfile& intent) {
    // Two axes, weighted equally, because they are the two things a camera can be wrong about for
    // an intent: it is at the wrong distance, or it is pointed at the wrong kind of thing.
    const float lens = 1.0f - std::abs(widenessOf(camera) - intent.distance);
    const float subject = 1.0f - std::abs(subjectnessOf(camera) - intent.heroEmphasis);
    return std::clamp(0.5f * lens + 0.5f * subject, 0.0f, 1.0f);
}

const char* arcCutNote(song::Arc arc) {
    switch (arc) {
    case song::Arc::Steady:
        return "steady -- one pace for the whole section";
    case song::Arc::Rising:
        return "rising -- shots shorten toward the section's end, below 'shortest shot' if they must";
    case song::Arc::Falling:
        return "falling -- shots lengthen toward the section's end";
    case song::Arc::Suspended:
        return "suspended -- held as one shot, up to twice 'longest shot'";
    case song::Arc::Burst:
        return "burst -- opens on short cuts on its downbeat, then settles";
    }
    return "steady -- one pace for the whole section";
}

std::vector<scene::CameraRig> eligibleCameras(const scene::CameraDirection& direction) {
    std::vector<scene::CameraRig> out;
    for (const scene::CameraRig& rig : direction.cameras) {
        if (rig.autoDirectorEligible) {
            out.push_back(rig);
        }
    }
    return out;
}

std::string SongDecision::line() const {
    return fmt::format("{:7.2f}-{:7.2f} {:5.2f}s {:<9} {:<18} [{:<26}] x{}  {:<16} {:<11} {}  {:.1f}->{:.1f}r  {}{}",
                       startSeconds, endSeconds, endSeconds - startSeconds, song::arcName(arc), sectionLabel,
                       intentId, occurrence + 1, cameraName, shotKindName(kind), autonomyName(autonomy),
                       static_cast<double>(startDistance), static_cast<double>(endDistance), subject,
                       timing.endsOn.empty() ? std::string() : " -> " + timing.endsOn);
}

std::size_t SongDirection::camerasUsed() const {
    std::set<scene::CameraId> seen;
    for (const scene::CameraShot& shot : shots) {
        seen.insert(shot.camera);
    }
    return seen.size();
}

// ---- the cut report (ADR-923) --------------------------------------------------------------------

namespace {

struct Spread {
    double min = 0.0;
    double mean = 0.0;
    double median = 0.0;
    double max = 0.0;
    double cv = 0.0; // standard deviation over the mean
};

Spread spreadOf(std::vector<double> v) {
    Spread s;
    if (v.empty()) {
        return s;
    }
    std::sort(v.begin(), v.end());
    s.min = v.front();
    s.max = v.back();
    s.median = v.size() % 2 == 1 ? v[v.size() / 2] : 0.5 * (v[v.size() / 2 - 1] + v[v.size() / 2]);
    double sum = 0.0;
    for (const double x : v) {
        sum += x;
    }
    s.mean = sum / static_cast<double>(v.size());
    double var = 0.0;
    for (const double x : v) {
        var += (x - s.mean) * (x - s.mean);
    }
    s.cv = s.mean > 0.0 ? std::sqrt(var / static_cast<double>(v.size())) / s.mean : 0.0;
    return s;
}

nlohmann::json spreadJson(const Spread& s) {
    return nlohmann::json{{"min", s.min}, {"mean", s.mean}, {"median", s.median}, {"max", s.max}, {"cv", s.cv}};
}

} // namespace

nlohmann::json SongDirection::report() const {
    using nlohmann::json;
    const MusicalGrid& grid = options.grid;
    const bool gridded = !grid.empty();
    json doc = json::object();
    doc["format"] = "avgen-song-cut";
    doc["version"] = 1;
    doc["plan"] = planName;
    doc["settings"] = json{{"shortestShot", options.minShotSeconds},
                           {"longestShot", options.maxShotSeconds},
                           {"shortestBuild", options.minBuildShotSeconds},
                           {"seed", options.seed},
                           {"autonomy", autonomyName(options.autonomy)}};
    if (gridded) {
        const double beat = grid.beatSeconds();
        const std::size_t first = static_cast<std::size_t>(std::clamp(grid.downbeat, 0, static_cast<int>(grid.beatTimes.size()) - 1));
        doc["grid"] = json{{"beats", grid.beatTimes.size()},
                           {"beatSeconds", beat},
                           {"tempoBpm", beat > 0.0 ? 60.0 / beat : 0.0},
                           {"beatsPerBar", grid.beatsPerBar},
                           {"phraseBars", grid.phraseBars},
                           {"downbeatBeat", grid.downbeat},
                           {"firstDownbeatSeconds", grid.beatTimes[first]}};
    } else {
        doc["grid"] = nullptr;
    }

    // Where a second sits on the grid: the nearest tracked beat, and whether it is a bar line.
    const auto placeOf = [&](double seconds, json& into) {
        if (!gridded) {
            return;
        }
        const auto k = grid.nearestBeat(seconds, 0.5 * grid.beatSeconds());
        if (!k) {
            into["onBeat"] = false;
            into["onDownbeat"] = false;
            return;
        }
        const double error = seconds - grid.beatTimes[*k];
        const bool on = std::abs(error) < 1e-3;
        into["bar"] = grid.barNumber(*k);
        into["beat"] = grid.beatInBar(*k);
        into["onBeat"] = on;
        into["onDownbeat"] = on && grid.strength(*k) >= 2;
        into["gridErrorMs"] = error * 1000.0;
    };

    json sectionList = json::array();
    for (const SongSectionCut& c : sections) {
        std::vector<double> lengths;
        for (std::size_t i = c.firstShot; i < c.firstShot + c.shotCount && i < decisions.size(); ++i) {
            lengths.push_back(decisions[i].endSeconds - decisions[i].startSeconds);
        }
        json j{{"index", c.index},
               {"label", c.label},
               {"intent", c.intentId},
               {"arc", song::arcName(c.arc)},
               {"authored", json::array({c.authoredStart, c.authoredEnd})},
               {"start", c.startSeconds},
               {"end", c.endSeconds},
               {"density", c.density},
               {"musicEnergy", c.musicEnergy},
               {"push", c.push},
               {"cutRate", json::array({c.rateOpen, c.rateClose})},
               {"peak", c.peak},
               {"firstShot", c.firstShot},
               {"shots", c.shotCount},
               {"durations", spreadJson(spreadOf(lengths))},
               {"lengths", lengths}};
        if (!c.eventSubject.empty()) {
            j["peakSubject"] = c.eventSubject;
        }
        if (!c.eventName.empty()) {
            j["peakEvent"] = c.eventName;
        }
        sectionList.push_back(std::move(j));
    }
    doc["sections"] = std::move(sectionList);

    json shotList = json::array();
    std::vector<double> lengths;
    for (std::size_t i = 0; i < decisions.size(); ++i) {
        const SongDecision& d = decisions[i];
        const ShotTiming& t = d.timing;
        const double length = d.endSeconds - d.startSeconds;
        lengths.push_back(length);
        json j{{"index", i},
               {"section", d.sectionIndex},
               {"start", d.startSeconds},
               {"end", d.endSeconds},
               {"duration", length},
               {"subject", d.subject},
               {"handoff", d.handoff},
               {"subjectReason", d.subjectReason},
               {"camera", d.cameraName},
               {"cameraId", d.camera},
               {"kind", shotKindName(d.kind)},
               {"transition", scene::shotTransitionName(d.transition)},
               {"visible", t.visible},
               {"arc", song::arcName(d.arc)},
               {"intent", d.intentId},
               {"peak", d.peak},
               {"aim", t.aimSeconds},
               {"rate", t.rate},
               {"density", t.density},
               {"motion", t.motion},
               {"scale", t.scale},
               {"contrast", t.contrast},
               {"endsOn", t.endsOn},
               {"why", t.why}};
        if (gridded) {
            j["beats"] = t.beats;
            j["startBar"] = t.startBar;
            j["startBeat"] = t.startBeat;
        }
        shotList.push_back(std::move(j));
    }
    doc["shots"] = std::move(shotList);

    // Every cut: the start of every shot but the first.
    json cutList = json::array();
    std::size_t onBeat = 0;
    std::size_t onDownbeat = 0;
    std::size_t visible = 0;
    for (std::size_t i = 1; i < decisions.size(); ++i) {
        json j{{"seconds", decisions[i].startSeconds}, {"visible", decisions[i].timing.visible},
               {"sectionBoundary", decisions[i].sectionIndex != decisions[i - 1].sectionIndex}};
        placeOf(decisions[i].startSeconds, j);
        onBeat += j.value("onBeat", false) ? 1 : 0;
        onDownbeat += j.value("onDownbeat", false) ? 1 : 0;
        visible += decisions[i].timing.visible ? 1 : 0;
        cutList.push_back(std::move(j));
    }
    doc["cuts"] = std::move(cutList);

    // The audit's measure of sameness: the largest share of shots within 50 ms of one length.
    std::size_t modal = 0;
    for (const double x : lengths) {
        std::size_t n = 0;
        for (const double y : lengths) {
            n += std::abs(x - y) <= 0.05 ? 1 : 0;
        }
        modal = std::max(modal, n);
    }
    const Spread all = spreadOf(lengths);
    json stats = spreadJson(all);
    stats["shots"] = lengths.size();
    stats["cuts"] = decisions.empty() ? 0 : decisions.size() - 1;
    stats["visibleCuts"] = visible;
    stats["cutsOnBeat"] = onBeat;
    stats["cutsOnDownbeat"] = onDownbeat;
    stats["modalShare"] = lengths.empty() ? 0.0 : static_cast<double>(modal) / static_cast<double>(lengths.size());
    stats["camerasUsed"] = camerasUsed();
    doc["stats"] = std::move(stats);
    doc["warnings"] = warnings;
    return doc;
}

// ---- the director --------------------------------------------------------------------------------

namespace {

// Snaps one section boundary to the grid (ADR-921): the nearest bar line within half a bar, else the
// nearest beat within half a beat, else where it was authored.
double snapBoundary(const MusicalGrid& grid, double t) {
    if (grid.empty()) {
        return t;
    }
    if (const auto k = grid.nearestDownbeat(t, 0.5 * grid.barSeconds())) {
        return grid.beatTimes[*k];
    }
    if (const auto k = grid.nearestBeat(t, 0.5 * grid.beatSeconds())) {
        return grid.beatTimes[*k];
    }
    return t;
}

struct SnappedSpan {
    double start = 0.0;
    double end = 0.0;
};

// Every section boundary on its downbeat. The film's own ends stay where they are -- the silence
// before the first beat belongs to the first shot, and the last section ends where the piece does --
// and a snap that would empty a section, or run it into its neighbour, is refused.
std::vector<SnappedSpan> snapSections(const SongPlan& plan, const MusicalGrid& grid) {
    std::vector<SnappedSpan> out;
    out.reserve(plan.sections.size());
    const double halfBeat = 0.5 * grid.beatSeconds();
    for (std::size_t i = 0; i < plan.sections.size(); ++i) {
        const SongPlanSection& s = plan.sections[i];
        SnappedSpan span{s.startSeconds, s.endSeconds};
        if (!grid.empty()) {
            const bool beforeGrid = s.startSeconds < grid.beatTimes.front() - halfBeat;
            const bool afterGrid = s.endSeconds > grid.beatTimes.back() + halfBeat;
            if (!beforeGrid) {
                span.start = snapBoundary(grid, s.startSeconds);
            }
            if (!afterGrid) {
                span.end = snapBoundary(grid, s.endSeconds);
            }
        }
        if (i > 0) {
            const SongPlanSection& previous = plan.sections[i - 1];
            if (std::abs(previous.endSeconds - s.startSeconds) < 1e-6) {
                span.start = out.back().end; // one boundary, one place
            } else {
                span.start = std::max(span.start, out.back().end);
            }
        }
        if (!(span.end > span.start + 1e-3)) {
            span = {std::max(s.startSeconds, i > 0 ? out.back().end : s.startSeconds), s.endSeconds};
        }
        out.push_back(span);
    }
    return out;
}

// What a musical position is called in a sentence: "bar 97", "bar 95 beat 3".
std::string whereOnGrid(const MusicalGrid& grid, double seconds) {
    if (grid.empty()) {
        return fmt::format("{:.2f} s", seconds);
    }
    const auto k = grid.nearestBeat(seconds, 1e-3);
    if (!k) {
        return fmt::format("{:.2f} s", seconds);
    }
    const int beat = grid.beatInBar(*k);
    return beat == 1 ? fmt::format("bar {}", grid.barNumber(*k))
                     : fmt::format("bar {} beat {}", grid.barNumber(*k), beat);
}

} // namespace

Result<SongDirection> directSong(const SongPlan& plan, const DirectionBrief& brief,
                                 std::span<const scene::CameraRig> eligible,
                                 const SongDirectorOptions& options) {
    if (plan.sections.empty()) {
        return fail("Song Mode needs a song: this plan has no sections. Analyze a track, or author "
                    "a plan, before directing to it");
    }
    if (auto ok = plan.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    if (eligible.empty()) {
        return fail("Song Mode has no camera to cut to: no camera in this scene is available to the "
                    "Auto-director. Tick 'available to the Auto-director' on at least one in the "
                    "Cameras panel");
    }
    if (!(brief.hero.radius > 0.0f)) {
        return fail("the hero '{}' has a radius of {}; distances are in radii, so a hero with no "
                    "size has no film",
                    brief.hero.name, brief.hero.radius);
    }
    if (!(options.maxShotSeconds >= options.minShotSeconds) || !(options.minShotSeconds > 0.0)) {
        return fail("Song Mode's shot band is {:.2f}..{:.2f} s, which is not a band",
                    options.minShotSeconds, options.maxShotSeconds);
    }
    if (!(options.minBuildShotSeconds > 0.0)) {
        return fail("Song Mode's shortest build is {:.2f} s; it must be positive", options.minBuildShotSeconds);
    }

    SongDirection out;
    out.sequence.name = plan.name.empty() ? "song" : plan.name;
    out.options = options;
    out.planName = plan.name;

    const MusicalGrid& grid = options.grid;
    const bool gridded = !grid.empty();
    const double beatSeconds = gridded ? grid.beatSeconds() : 0.0;
    const double phraseSeconds = gridded ? grid.barSeconds() * static_cast<double>(std::max(1, grid.phraseBars)) : 0.0;
    // How short an accelerating section may cut: the build floor, never above the ordinary floor (a
    // build may go below it, not above it) and never below one beat of the grid.
    const double buildFloor = std::max(std::min(options.minBuildShotSeconds, options.minShotSeconds), beatSeconds);

    const std::vector<SnappedSpan> spans = snapSections(plan, grid);
    const std::size_t sectionCount = plan.sections.size();

    // ---- what the piece measures, once ------------------------------------------------------------
    float maxComposite = 0.0f;
    for (const SongPlanSection& s : plan.sections) {
        if (s.audio.measured()) {
            maxComposite = std::max(maxComposite, s.audio.energy);
        }
    }
    std::vector<float> density(sectionCount, 0.5f);
    std::vector<float> musicEnergy(sectionCount, 0.5f);
    std::vector<float> peakScore(sectionCount, 0.0f);
    float topScore = 0.0f;
    for (std::size_t i = 0; i < sectionCount; ++i) {
        density[i] = densityOf(plan.sections[i], maxComposite);
        musicEnergy[i] = musicEnergyOf(plan.sections[i], maxComposite);
        peakScore[i] = plan.sections[i].intent.energy * musicEnergy[i];
        topScore = std::max(topScore, peakScore[i]);
    }
    // ADR-922: the film's peaks -- the author's push times the music's energy, within a share of the
    // largest. A film where most sections qualify has no hierarchy to honour, so it has no peaks.
    std::vector<bool> peak(sectionCount, false);
    {
        std::size_t qualifying = 0;
        for (std::size_t i = 0; i < sectionCount; ++i) {
            peak[i] = topScore >= kPeakFloor && peakScore[i] >= kPeakShare * topScore;
            qualifying += peak[i] ? 1 : 0;
        }
        if (qualifying * 2 > sectionCount) {
            std::fill(peak.begin(), peak.end(), false);
        }
    }

    Cast cast(brief);
    cast.seed(mix(options.seed));
    // The cast's mean motion, so a subject's hold is relative: a cast of statues cuts at the band.
    float meanMotion = 0.0f;
    for (std::size_t i = 0; i < cast.size(); ++i) {
        meanMotion += cast.at(i).motion;
    }
    meanMotion = cast.empty() ? 0.0f : meanMotion / static_cast<float>(cast.size());

    // Who the camera is on now, as opposed to what the last shot was *about*. The two differ for a
    // transition, and reading the second for the first is how a run of transitions never advances
    // -- the same trap `directFromStructure` documents, and the same answer.
    FocalTarget current = brief.hero;
    bool haveCurrent = false;
    std::optional<scene::CameraId> previousCamera;

    for (std::size_t si = 0; si < sectionCount; ++si) {
        const SongPlanSection& section = plan.sections[si];
        const ShotIntentProfile& intent = section.intent;
        const double a = spans[si].start;
        const double b = spans[si].end;
        const double span = b - a;
        // The ceiling, not the setting. A section may ask for less freedom than the film allows; it
        // may not ask for more.
        const Autonomy autonomy = std::min(section.autonomy, options.autonomy);
        const std::uint32_t shape = intentShape(intent);
        const bool mayChoose = autonomy >= Autonomy::Guided;
        const bool expressive = autonomy == Autonomy::Expressive;
        const song::Arc arc = intent.arc;
        const bool accelerating = mayChoose && accelerates(arc);
        const bool suspended = mayChoose && arc == song::Arc::Suspended;
        const double floor = accelerating ? buildFloor : options.minShotSeconds;
        const SectionPace pace(section, options, mayChoose, expressive, density[si], buildFloor);

        // The opener's hold, for a section that neither accelerates nor holds: a wide shot of the
        // world establishes scale, and one arriving after a busier section is contrast.
        float scale = 0.0f;
        float contrast = 1.0f;
        if (mayChoose && !accelerating && !suspended) {
            scale = scaleOf(intent);
            if (si > 0) {
                const float drop = density[si - 1] - density[si];
                if (drop > kContrastThreshold) {
                    contrast = 1.0f + std::min(kContrastMax, drop);
                }
            }
        }
        const double openerFactor = (1.0 + static_cast<double>(kScaleGain * scale)) * static_cast<double>(contrast);
        const double openerCeiling = options.maxShotSeconds * std::min(kOpenerCeiling, openerFactor);

        // ---- how many cuts, and where (ADR-921) -----------------------------------------------
        const double asked = pace.shotsAsked(a, b);
        const auto oneShot = [&] {
            Layout l;
            l.cuts = {a, b};
            l.endStrength = {-1};
            l.aims = {span};
            l.gridded = gridded;
            return l;
        };
        std::vector<double> aimAt;
        Positions positions;
        if (gridded) {
            positions = positionsFor(grid, a, b);
            aimAt.reserve(positions.t.size());
            for (const double t : positions.t) {
                aimAt.push_back(pace.aimFrom(t, a, b));
            }
        }
        const int monotone = !accelerating ? 0 : (arc == song::Arc::Rising ? 1 : -1);
        // How far the intent's variation moves shot k's aim (ADR-921): a function of the shot's
        // coordinates, like every other decision here, so the same plan is the same film.
        const auto variationOf = [&](std::size_t k) {
            if (!mayChoose || accelerating || suspended) {
                return 1.0;
            }
            const float s01 = signedOf(decisionHash(options.seed, si, section.occurrence, k, 0x5A1Eu));
            return std::pow(2.0, kVariationOctaves * static_cast<double>(intent.variation) * static_cast<double>(s01));
        };
        const auto gridLayout = [&](std::size_t fixedCount, const std::vector<double>& factors) {
            const auto aimOf = [&](std::size_t k, std::size_t i) {
                const double f = k < factors.size() ? factors[k] : 1.0;
                return aimAt[i] * f * variationOf(k) * (k == 0 && !accelerating ? openerFactor : 1.0);
            };
            const auto ceilingOf = [&](std::size_t k) {
                return (k == 0 && !accelerating) ? openerCeiling : options.maxShotSeconds;
            };
            return layoutOnGrid(positions, beatSeconds, floor, aimOf, ceilingOf, monotone, fixedCount);
        };
        const auto freeCount = [&] {
            std::size_t count = static_cast<std::size_t>(std::max(1.0, std::round(asked)));
            if (!accelerating) {
                count = std::min(count, static_cast<std::size_t>(std::max(1.0, std::floor(span / floor))));
            }
            return count;
        };
        Layout layout;
        if (!mayChoose) {
            layout = oneShot(); // Locked: one shot, framed as the section asked
        } else if (suspended) {
            // Suspended holds: one shot, until the section is longer than a hold can be.
            const auto count = static_cast<std::size_t>(
                std::max(1.0, std::ceil(span / (kSuspendedHold * options.maxShotSeconds) - 1e-9)));
            if (count == 1) {
                layout = oneShot();
            } else if (gridded) {
                std::fill(aimAt.begin(), aimAt.end(), span / static_cast<double>(count));
                const auto aimOf = [&](std::size_t, std::size_t i) { return aimAt[i]; };
                const auto ceilingOf = [&](std::size_t) { return kSuspendedHold * options.maxShotSeconds; };
                auto l = layoutOnGrid(positions, beatSeconds, options.minShotSeconds, aimOf, ceilingOf, 0, count);
                layout = l ? *l : layoutFree(pace, a, b, options.minShotSeconds, count, {}, a, b);
            } else {
                layout = layoutFree(pace, a, b, options.minShotSeconds, count, {}, a, b);
            }
        } else if (gridded) {
            auto l = gridLayout(0, {});
            if (l) {
                layout = *l;
            } else {
                layout = layoutFree(pace, a, b, floor, freeCount(), {openerFactor}, a, b);
                out.warnings.push_back(fmt::format(
                    "'{}' could not be laid on the beat grid inside {:.2f} s with a {:.2f} s floor; its "
                    "cuts fall where the pace puts them",
                    section.label, span, floor));
            }
        } else {
            std::vector<double> weights;
            if (!accelerating) {
                const std::size_t count = freeCount();
                for (std::size_t k = 0; k < count; ++k) {
                    weights.push_back(variationOf(k) * (k == 0 ? openerFactor : 1.0));
                }
            }
            layout = layoutFree(pace, a, b, floor, freeCount(), weights, a, b);
        }
        std::size_t shotCount = layout.cuts.size() - 1;
        if (mayChoose && !suspended) {
            const auto askedCount = static_cast<std::size_t>(std::max(1.0, std::round(asked)));
            const auto allowed = static_cast<std::size_t>(std::max(1.0, std::floor(span / floor)));
            // Only a real shortfall: a pace that sits exactly on the floor asks for a fraction of a
            // shot more than fits, and saying so about every such section is noise.
            if (askedCount > allowed && static_cast<double>(askedCount) >= 1.15 * static_cast<double>(allowed)) {
                // Said rather than silently granted in part: the fix is the director's own timing
                // floor and nothing in the plan, and an author staring at a plan that asks for
                // rapid coverage has no way to know that from the film alone.
                out.warnings.push_back(fmt::format("'{}' asks for {} shot(s) across {:.1f}s; a {:.1f}s floor allows {}",
                                                   section.label, askedCount, span, floor, allowed));
            }
        }

        // ---- which cameras --------------------------------------------------------------------
        //
        // Every eligible camera is scored against the intent, the scores are jittered by the
        // intent's own `variation` (and by nothing at all when the section is Locked), and the top
        // few are taken. The jitter is a function of the section *and its occurrence*, so the
        // second pass over the same material genuinely draws a different set.
        std::vector<Candidate> candidates;
        candidates.reserve(eligible.size());
        for (std::size_t ci = 0; ci < eligible.size(); ++ci) {
            Candidate c;
            c.camera = &eligible[ci];
            c.score = cameraMatch(eligible[ci], intent);
            if (mayChoose) {
                // Salted by the *intent's shape*, not by where the section sits or which pass it
                // is. Two sections carrying the same intent therefore score the cameras the same,
                // and what distinguishes their passes is the phase rule below -- which is a
                // guarantee, where a hash would be a tendency.
                c.score += intent.variation * 0.45f *
                           unitOf(mix(shape ^ (static_cast<std::uint32_t>(ci) * 0x27D4EB2Fu)));
            }
            // The author's own tie-break, kept as a small bias rather than as a sort key: a camera
            // the author ranked higher wins a near-tie without overruling a real mismatch.
            c.score += static_cast<float>(std::clamp(c.camera->priority, -10, 10)) * 0.005f;
            candidates.push_back(c);
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& x, const Candidate& y) {
            if (x.score != y.score) {
                return x.score > y.score;
            }
            return x.camera->id < y.camera->id; // never vector order
        });
        std::size_t wanted = mayChoose ? static_cast<std::size_t>(std::max(1, intent.cameras)) : 1u;
        if (wanted > candidates.size()) {
            out.warnings.push_back(fmt::format("'{}' asks for {} camera(s); this scene offers the Auto-director {}",
                                               section.label, intent.cameras, candidates.size()));
            wanted = candidates.size();
        }
        const Candidate* director = nullptr; // the director's own camera, when it is eligible
        for (const Candidate& c : candidates) {
            if (c.camera->id == scene::kMainCamera) {
                director = &c;
            }
        }
        // **A cut the viewer can see** (ADR-921). The framing bake re-frames only the director's own
        // camera, so two shots in a row on one *placed* camera are one picture and the cut between
        // them does not exist. A section that wants one camera and cuts more than once is shot on
        // the director's camera -- the one camera on which a cut is visible -- and, with no such
        // camera eligible, is held as one shot rather than claiming cuts nobody sees.
        bool onDirectorCamera = false;
        if (wanted == 1 && shotCount > 1 && candidates.front().camera->id != scene::kMainCamera) {
            if (director != nullptr) {
                onDirectorCamera = true;
            } else {
                out.warnings.push_back(fmt::format(
                    "'{}' cuts {} time(s) on one camera, '{}', which the director does not frame, so "
                    "the cuts would not be seen; it is held as one shot. Tick the main camera "
                    "'available to the Auto-director' to let it cut",
                    section.label, shotCount - 1, candidates.front().camera->name));
                layout = oneShot();
                shotCount = 1;
            }
        }
        wanted = std::min(wanted, shotCount); // no point drawing a camera no shot will use

        // Where in the chosen set the section opens, and the one rule here that is a *guarantee*
        // rather than a tendency: the seed picks the phase, and the occurrence advances it, so
        // **two passes over the same material never open on the same camera** when the section has
        // more than one to open on.
        //
        // A guarantee rather than more hashing, because the property it protects is the one the
        // whole mode is judged on -- "the same intent, twice, is not the same film" -- and leaving
        // it to a hash means it holds about half the time. It costs nothing: `occurrence` is part
        // of the plan, so this is still a pure function of the section's coordinates and still
        // decided once, at bake time.
        const std::uint32_t rotation = mix(shape ^ mix(options.seed * 0x5EEDu));
        std::size_t first = wanted > 0 ? (rotation + static_cast<std::uint32_t>(section.occurrence)) % wanted : 0;
        const auto cameraFor = [&](std::size_t k) -> const scene::CameraRig& {
            if (onDirectorCamera) {
                return *director->camera;
            }
            return *candidates[wanted == 0 ? 0 : (first + k) % wanted].camera;
        };
        // ...and the section's own boundary must be visible too: opening on the placed camera the
        // last section closed on is no cut at all, which is how a drop gets buried.
        bool openerHidden = false;
        if (previousCamera && cameraFor(0).id == *previousCamera && *previousCamera != scene::kMainCamera) {
            if (wanted >= 2) {
                first = (first + 1) % wanted;
            } else if (director != nullptr) {
                onDirectorCamera = true;
            } else {
                openerHidden = true;
                out.warnings.push_back(fmt::format(
                    "'{}' opens on '{}', the camera the section before it closed on, so its first cut "
                    "is not seen",
                    section.label, cameraFor(0).name));
            }
        }

        // ---- who each shot is of --------------------------------------------------------------
        //
        // How long one subject is kept. Low variation holds; high variation moves on every shot.
        // Song Mode's own answer to what `dwellShots` is for in the other two modes, derived from
        // the intent rather than from a global slider -- because it is a property of the section.
        const int dwell = std::max(1, static_cast<int>(std::lround(lerpf(3.0f, 1.0f, intent.variation))));
        // ADR-922: a peak section opens on the subject of its event -- a set piece, a scenario, a
        // watched play's world event -- and failing that on the film's hero, and holds it through the
        // event and at least the section's first phrase, instead of taking whoever's turn it is.
        std::optional<std::size_t> peakSubject;
        const SongEvent* peakEvent = nullptr;
        double holdUntil = a;
        if (peak[si] && mayChoose && !cast.empty()) {
            const double lead = gridded ? grid.barSeconds() : 0.0;
            for (const SongEvent& e : plan.events) {
                const bool during = e.seconds >= a - lead && e.seconds < b;
                const bool reaching = e.span() && e.seconds < a && e.endSeconds > a;
                if (!during && !reaching) {
                    continue;
                }
                std::optional<std::size_t> who;
                for (std::size_t c = 0; c < cast.size(); ++c) {
                    if (!e.subject.empty() && cast.at(c).name == e.subject) {
                        who = c;
                        break;
                    }
                }
                if (!who) {
                    if (!e.subject.empty()) {
                        out.warnings.push_back(fmt::format(
                            "'{}' is a peak and '{}' is the subject of its event '{}' at {:.2f} s, but it "
                            "is not a hero; star it in the World window for the director to give it "
                            "the peak",
                            section.label, e.subject, e.name, e.seconds));
                    }
                    continue;
                }
                peakSubject = who;
                peakEvent = &e;
                break;
            }
            if (!peakSubject) {
                peakSubject = 0; // the film's hero: the brief's first, the most important
            }
            const double eventEnd = peakEvent == nullptr ? a : std::max(peakEvent->seconds, peakEvent->endSeconds);
            holdUntil = std::min(b, std::max(eventEnd, a + phraseSeconds));
            // On the grid, the phrase ends on a bar line, not on eight median bars from the start:
            // those land a few milliseconds past the tracked line, and a shot starting *on* the next
            // phrase would count as inside this one.
            if (gridded) {
                if (const auto k = grid.nearestDownbeat(holdUntil, 0.5 * grid.barSeconds())) {
                    holdUntil = std::min(b, grid.beatTimes[*k]);
                }
            }
        }
        std::vector<std::size_t> subjectIndex(shotCount, 0);
        std::vector<std::string> subjectReason(shotCount);
        {
            std::size_t heldIndex = 0;
            int heldFor = 0;
            bool holding = false;
            for (std::size_t k = 0; k < shotCount; ++k) {
                if (peakSubject && (k == 0 || layout.cuts[k] < holdUntil - std::max(0.25 * beatSeconds, 1e-6))) {
                    subjectIndex[k] = *peakSubject;
                    subjectReason[k] = peakEvent != nullptr
                                           ? fmt::format("event '{}' at {:.2f} s (peak)", peakEvent->name, peakEvent->seconds)
                                           : std::string("the film's hero (peak)");
                    continue;
                }
                if (!holding || heldFor >= dwell) {
                    heldIndex = cast.next(intent.heroEmphasis);
                    heldFor = 0;
                    holding = true;
                }
                ++heldFor;
                subjectIndex[k] = heldIndex;
                subjectReason[k] = "rotation";
            }
        }

        // ---- the cast's motion reaches the layout (ADR-921) -----------------------------------
        //
        // Now that the subjects are known: a subject that moves on its own holds its shot longer
        // than the cast's average, one that stands still is left sooner. Only where lengths are
        // free to differ -- an accelerating section keeps its arc's shape and a hold is a hold.
        //
        // A peak's hold is a prefix of the section whose end the rule placed (the first phrase line,
        // or the event's end); the pass re-lays only the shots after it, so a moving subject's longer
        // shots cannot carry the hold past its line.
        std::size_t held = 0;
        for (std::size_t k = 0; k < shotCount; ++k) {
            if (subjectReason[k] != "rotation") {
                held = k + 1;
            }
        }
        std::vector<double> motionFactor(shotCount, 1.0);
        if (mayChoose && !accelerating && !suspended && shotCount > held + 1 && !cast.empty()) {
            bool varies = false;
            for (std::size_t k = held; k < shotCount; ++k) {
                motionFactor[k] = 1.0 + static_cast<double>(kMotionGain) *
                                            static_cast<double>(cast.at(subjectIndex[k]).motion - meanMotion);
                varies = varies || std::abs(motionFactor[k] - 1.0) > 1e-3;
            }
            const std::size_t rest = shotCount - held;
            const double from = layout.cuts[held];
            if (varies) {
                if (gridded) {
                    Positions tail = positionsFor(grid, from, b);
                    std::vector<double> tailAim;
                    tailAim.reserve(tail.t.size());
                    for (const double t : tail.t) {
                        tailAim.push_back(pace.aimFrom(t, a, b));
                    }
                    const auto aimOf = [&](std::size_t k, std::size_t i) {
                        const std::size_t shot = held + k;
                        return tailAim[i] * motionFactor[shot] * variationOf(shot) * (shot == 0 ? openerFactor : 1.0);
                    };
                    const auto ceilingOf = [&](std::size_t k) {
                        return held + k == 0 ? openerCeiling : options.maxShotSeconds;
                    };
                    if (auto l = layoutOnGrid(tail, beatSeconds, floor, aimOf, ceilingOf, 0, rest)) {
                        layout.cuts.resize(held + 1);
                        layout.cuts.insert(layout.cuts.end(), l->cuts.begin() + 1, l->cuts.end());
                        layout.endStrength.resize(held);
                        layout.endStrength.insert(layout.endStrength.end(), l->endStrength.begin(), l->endStrength.end());
                        layout.aims.resize(held);
                        layout.aims.insert(layout.aims.end(), l->aims.begin(), l->aims.end());
                    }
                } else {
                    std::vector<double> weights;
                    for (std::size_t k = held; k < shotCount; ++k) {
                        weights.push_back(motionFactor[k] * variationOf(k) * (k == 0 ? openerFactor : 1.0));
                    }
                    Layout l = layoutFree(pace, from, b, floor, rest, weights, a, b);
                    if (l.cuts.size() == rest + 1) {
                        layout.cuts.resize(held + 1);
                        layout.cuts.insert(layout.cuts.end(), l.cuts.begin() + 1, l.cuts.end());
                        layout.endStrength.resize(held);
                        layout.endStrength.insert(layout.endStrength.end(), l.endStrength.begin(), l.endStrength.end());
                        layout.aims.resize(held);
                        layout.aims.insert(layout.aims.end(), l.aims.begin(), l.aims.end());
                    }
                }
            }
        }

        // ---- the shots ------------------------------------------------------------------------
        SongSectionCut cut;
        cut.index = si;
        cut.label = section.label;
        cut.intentId = intent.id;
        cut.authoredStart = section.startSeconds;
        cut.authoredEnd = section.endSeconds;
        cut.startSeconds = a;
        cut.endSeconds = b;
        cut.arc = arc;
        cut.density = density[si];
        cut.musicEnergy = musicEnergy[si];
        cut.push = intent.energy;
        cut.rateOpen = pace.rateAt(0.0f);
        cut.rateClose = pace.rateAt(1.0f);
        cut.peak = peak[si];
        if (peakEvent != nullptr) {
            cut.eventSubject = peakEvent->subject;
            cut.eventName = peakEvent->name;
        } else if (peakSubject) {
            cut.eventSubject = cast.at(*peakSubject).name;
        }
        cut.firstShot = out.decisions.size();
        cut.shotCount = shotCount;

        for (std::size_t k = 0; k < shotCount; ++k) {
            const double start = layout.cuts[k];
            const double end = layout.cuts[k + 1];
            const double length = end - start;
            const std::uint32_t h = decisionHash(options.seed, si, section.occurrence, k, 0xA17u);
            const FocalTarget subject = cast.empty() ? brief.hero : cast.at(subjectIndex[k]);
            const scene::CameraRig& camera = cameraFor(k);
            // The treatment where this shot sits in its section, arc applied: a Rising section's
            // camera settles at the start and travels by the end, a Suspended one barely moves.
            const float middle = span > 0.0 ? static_cast<float>((0.5 * (start + end) - a) / span) : 0.0f;
            const ShotIntentProfile at = mayChoose ? section.intentAtProgress(middle) : intent;

            // Move.
            Shot shot;
            shot.name = fmt::format("{}-{}", section.label.empty() ? "section" : section.label, k + 1);
            shot.kind = shotKindForIntent(at);
            shot.startSeconds = start;
            shot.durationSeconds = length;
            shot.subject = subject;
            shot.composition.focalLength = options.focalLength;

            // A flash of a composition does not travel between two subjects, and its camera travels
            // only as far as its length allows (ADR-921): an accelerating cut is a cut, not a whip.
            const double travelScale = std::clamp(length / kFullTravelSeconds, 0.25, 1.0);
            if (shot.kind == ShotKind::Transition && length < kMinTravelSeconds) {
                shot.kind = ShotKind::Track;
            }
            // ADR-922: a peak's payoff lands *on* its subject. A transition would open on whoever the
            // last shot held and travel to the event -- the drop spent getting there.
            const bool heldForPeak = peakSubject && subjectIndex[k] == *peakSubject && subjectReason[k] != "rotation";
            if (shot.kind == ShotKind::Transition && heldForPeak) {
                shot.kind = ShotKind::Track;
            }
            if (shot.kind == ShotKind::Transition) {
                // A transition needs somewhere to come from. With nowhere, it is the follow shot it
                // would have ended as -- resolved before the framing, so the shot gets the geometry
                // of the kind it actually is.
                if (haveCurrent && current.name != subject.name) {
                    shot.handoff = subject;
                    shot.subject = current;
                } else {
                    shot.kind = ShotKind::Track;
                }
            }

            // Framing, from the intent rather than from the kind's own table: the intent is what
            // said how far away this shot stands, and a kind that overrode it would make the
            // `distance` axis decorative.
            const float nominal = lerpf(kCloseRadii, kWideRadii, at.distance);
            const float opening = openingOf(shot.kind);
            const float travel = at.movement * (mayChoose ? 1.0f : 0.5f) * static_cast<float>(travelScale);
            // The variation term, and the only place a shot's framing differs from its neighbour's
            // inside one section. Zero when the section is Locked, which is what Locked means.
            const float jitter = mayChoose ? at.variation * 0.22f * signedOf(mix(h ^ 0x9E37u)) : 0.0f;
            const float centre = std::max(1.2f, nominal * (1.0f + jitter));
            shot.startDistance = std::max(0.8f, centre * (1.0f - 0.40f * opening * travel));
            shot.endDistance = std::max(0.8f, centre * (1.0f + 0.40f * opening * travel));

            // Elevation: a wide shot looks slightly down on the world, a close one is nearly level.
            // Biased by whatever the subject says it reads best from, exactly as
            // `directFromStructure` does -- degrees in, orbit-radius ratio out.
            float elevation = lerpf(0.06f, 0.30f, at.distance);
            if (subject.preferredElevationDegrees != 0.0f) {
                elevation = std::tan(glm::radians(std::clamp(subject.preferredElevationDegrees, -60.0f, 60.0f)));
            }
            shot.startElevation = elevation;
            shot.endElevation = elevation + 0.12f * opening * travel;

            // The approach bearing: the subject's own preferred one, spread by the golden angle so
            // consecutive shots are not nine views down one axis, and offset by the decision hash so
            // the second pass over this material comes at it from somewhere else.
            const float golden = static_cast<float>(out.sequence.shots.size()) * 2.39996f;
            const float offset = mayChoose ? at.variation * signedOf(h) * 1.4f : 0.0f;
            shot.startAzimuth = subject.preferredAzimuth + golden + offset;
            // How far round it goes: the movement axis, signed by the hash so a film does not
            // always circle the same way.
            const float sweep = at.movement * 0.85f * static_cast<float>(travelScale) *
                                (signedOf(mix(h ^ 0x51F3u)) < 0.0f ? -1.0f : 1.0f);
            shot.endAzimuth = shot.startAzimuth + sweep;

            // Who the shot is *for*, as a share of the frame's attention. The hero emphasis axis,
            // scaled by how much is happening -- a quiet hero shot is still a hero shot, but it is
            // not the loudest moment in the film. It gates the coverage check `Sequence::validate`
            // makes and nothing else (ADR-922 cut the track that carried it on).
            shot.spotlight.emphasis = std::clamp(intent.heroEmphasis * (0.45f + 0.55f * section.energy), 0.0f, 1.0f);
            shot.spotlight.active = shot.spotlight.emphasis > 0.05f;
            if (shot.spotlight.active && bestCoverage(shot) < kMinHeroCoverage) {
                // A spotlight on a subject that never spans a sixteenth of the frame is a claim the
                // picture does not support. Dropped rather than allowed to fail validation later,
                // and said out loud, because the cause is the intent's own distance axis.
                shot.spotlight.active = false;
                shot.spotlight.emphasis = 0.0f;
                out.warnings.push_back(fmt::format("'{}' shot {}: '{}' is too far away at {:.1f} radii to carry the "
                                                   "frame, so its emphasis was dropped",
                                                   section.label, k + 1, shot.subject.name,
                                                   static_cast<double>(shot.startDistance)));
            }

            current = shot.handoff.has_value() ? *shot.handoff : shot.subject;
            haveCurrent = true;

            // ---- how it is entered -------------------------------------------------------------
            //
            // A section opener where the music changes hard -- a peak, an impact, a step in energy
            // or in measured density -- is a cut; a gentler one is a blend. Inside a section, an
            // accelerating arc cuts (a blend would smear the very acceleration it is for); anything
            // else is a short blend, a coverage change between two angles of one moment.
            const bool sectionOpener = k == 0;
            const float densityStep = si > 0 ? std::abs(density[si] - density[si - 1]) : 0.0f;
            scene::ShotTransition transition = scene::ShotTransition::Cut;
            double blendSeconds = 0.0;
            if (sectionOpener) {
                const bool hard = peak[si] || arc == song::Arc::Burst || section.transition > 0.18f || densityStep > 0.18f;
                if (!hard) {
                    transition = scene::ShotTransition::Blend;
                    blendSeconds = std::min(0.8, 0.4 * length);
                }
            } else if (!accelerating) {
                transition = scene::ShotTransition::Blend;
                blendSeconds = std::min(0.35, 0.3 * length);
            }

            // ---- why it is this long -----------------------------------------------------------
            ShotTiming timing;
            timing.aimSeconds = k < layout.aims.size() ? layout.aims[k] : length;
            timing.rate = pace.rateAt(middle);
            timing.density = density[si];
            timing.motion = subject.motion;
            timing.scale = k == 0 ? scale : 0.0f;
            timing.contrast = k == 0 ? contrast : 1.0f;
            const bool lastOfFilm = si + 1 == sectionCount && k + 1 == shotCount;
            if (!gridded) {
                timing.endsOn = "free";
            } else if (k + 1 == shotCount) {
                timing.endsOn = lastOfFilm ? "end" : "section";
            } else {
                timing.endsOn = MusicalGrid::strengthName(k < layout.endStrength.size() ? layout.endStrength[k] : -1);
            }
            if (gridded) {
                if (const auto kb = grid.nearestBeat(start, 1e-3)) {
                    timing.startBar = grid.barNumber(*kb);
                    timing.startBeat = grid.beatInBar(*kb);
                }
                timing.beats = length / beatSeconds;
            }
            timing.visible = !(sectionOpener && openerHidden);
            {
                std::string why;
                if (!mayChoose) {
                    why = "locked: one shot for the section";
                } else if (suspended) {
                    why = shotCount == 1 ? "suspended: held for the whole section"
                                         : fmt::format("suspended: held as long as a hold may run ({:.1f} s)",
                                                       kSuspendedHold * options.maxShotSeconds);
                } else if (accelerating) {
                    why = fmt::format("{}: shot {} of {}, the pace running from {:.2f} s to {:.2f} s shots "
                                      "(cut rate {:.2f} -> {:.2f}, floor {:.2f} s)",
                                      song::arcName(arc), k + 1, shotCount, pace.lengthAt(0.0f), pace.lengthAt(1.0f),
                                      static_cast<double>(pace.rateAt(0.0f)), static_cast<double>(pace.rateAt(1.0f)),
                                      floor);
                } else {
                    why = fmt::format("{}: cut rate {:.2f} (treatment {:.2f}, density {:.2f}, frame {:.2f}) aims "
                                      "at {:.2f} s",
                                      song::arcName(arc), static_cast<double>(timing.rate),
                                      static_cast<double>(at.cutRate), static_cast<double>(density[si]),
                                      static_cast<double>(intent.visualDensity), pace.lengthAt(middle));
                    if (k == 0 && scale > 0.0f) {
                        why += fmt::format("; held x{:.2f} to establish scale", 1.0 + static_cast<double>(kScaleGain * scale));
                    }
                    if (k == 0 && contrast > 1.0f) {
                        why += fmt::format("; held x{:.2f} after a busier section", static_cast<double>(contrast));
                    }
                    if (std::abs(motionFactor[k] - 1.0) > 1e-3) {
                        why += fmt::format("; x{:.2f} for how much '{}' moves", motionFactor[k], subject.name);
                    }
                }
                if (gridded) {
                    why += k + 1 == shotCount ? fmt::format("; ends with the section at {}", whereOnGrid(grid, end))
                                              : fmt::format("; ends on the {} line at {}", timing.endsOn, whereOnGrid(grid, end));
                }
                timing.why = std::move(why);
            }

            SongDecision decision;
            decision.startSeconds = start;
            decision.endSeconds = end;
            decision.sectionIndex = si;
            decision.sectionLabel = section.label;
            decision.intentId = intent.id;
            decision.occurrence = section.occurrence;
            decision.autonomy = autonomy;
            decision.camera = camera.id;
            decision.cameraName = camera.name;
            decision.kind = shot.kind;
            decision.subject = shot.subject.name;
            decision.handoff = shot.handoff ? shot.handoff->name : std::string();
            decision.startDistance = shot.startDistance;
            decision.endDistance = shot.endDistance;
            decision.arc = arc;
            decision.peak = peak[si];
            decision.subjectReason = subjectReason[k];
            decision.transition = transition;
            decision.timing = std::move(timing);
            out.decisions.push_back(std::move(decision));

            out.sequence.shots.push_back(std::move(shot));

            // The camera track. Adjacent spans on the same camera are one shot, because a cut from a
            // camera to itself is not a cut -- what changed is the framing underneath, and the shot
            // track is not where framing lives.
            if (!out.shots.empty() && out.shots.back().camera == camera.id &&
                std::abs(out.shots.back().endSeconds - start) < 1e-6) {
                out.shots.back().endSeconds = end;
                // A placed camera held across the boundary shows no cut there; the director's own
                // camera re-frames, which is a cut.
                if (camera.id != scene::kMainCamera) {
                    out.decisions.back().timing.visible = false;
                }
                out.decisions.back().transition = scene::ShotTransition::Cut;
            } else {
                scene::CameraShot entry;
                entry.camera = camera.id;
                entry.startSeconds = start;
                entry.endSeconds = end;
                entry.transition = transition;
                entry.blendSeconds = blendSeconds;
                // Locked in the shot-track sense: an authored moment an event camera may not take.
                // A section the author locked is a moment the author chose, so it is protected;
                // everything else stays available to the world's own events, which is what keeps
                // Song Mode compatible with ADR-245's event cameras rather than fighting them.
                entry.locked = autonomy == Autonomy::Locked;
                entry.label = section.label;
                out.shots.push_back(std::move(entry));
            }
            previousCamera = camera.id;
        }
        out.sections.push_back(std::move(cut));
    }

    if (auto ok = out.sequence.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    log::info("song director: {} section(s) -> {} shot(s) on {} camera(s), {} cut(s) on the shot "
              "track{}",
              plan.sections.size(), out.sequence.shots.size(), out.camerasUsed(), out.shots.size(),
              gridded ? fmt::format(", on a {:.2f}-BPM grid", 60.0 / beatSeconds) : std::string(" (no beat grid)"));
    for (const std::string& warning : out.warnings) {
        log::info("song director: {}", warning);
    }
    for (const SongDecision& d : out.decisions) {
        log::debug("song director: {}", d.line());
    }
    return out;
}

} // namespace avgen::app
