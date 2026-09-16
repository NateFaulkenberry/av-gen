#include "app/song_director.hpp"

#include "core/log.hpp"

#include <fmt/format.h>
#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
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

} // namespace

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
    return fmt::format("{:7.2f}-{:7.2f}  {:<18} [{:<26}] x{}  {:<16} {:<11} {}  {:.1f}->{:.1f}r  {}",
                       startSeconds, endSeconds, sectionLabel, intentId, occurrence + 1, cameraName,
                       shotKindName(kind), autonomyName(autonomy), static_cast<double>(startDistance),
                       static_cast<double>(endDistance), subject);
}

std::size_t SongDirection::camerasUsed() const {
    std::set<scene::CameraId> seen;
    for (const scene::CameraShot& shot : shots) {
        seen.insert(shot.camera);
    }
    return seen.size();
}

// ---- the director --------------------------------------------------------------------------------

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

    SongDirection out;
    out.sequence.name = plan.name.empty() ? "song" : plan.name;

    Cast cast(brief);
    cast.seed(mix(options.seed));

    // Who the camera is on now, as opposed to what the last shot was *about*. The two differ for a
    // transition, and reading the second for the first is how a run of transitions never advances
    // -- the same trap `directFromStructure` documents, and the same answer.
    FocalTarget current = brief.hero;
    bool haveCurrent = false;

    for (std::size_t si = 0; si < plan.sections.size(); ++si) {
        const SongPlanSection& section = plan.sections[si];
        const ShotIntentProfile& intent = section.intent;
        // The ceiling, not the setting. A section may ask for less freedom than the film allows; it
        // may not ask for more.
        const Autonomy autonomy = std::min(section.autonomy, options.autonomy);
        const std::uint32_t shape = intentShape(intent);
        const bool mayChoose = autonomy >= Autonomy::Guided;
        const bool expressive = autonomy == Autonomy::Expressive;

        // ---- how many cuts --------------------------------------------------------------------
        //
        // The intent says how often; the section's own measured energy and density move it within
        // the band, but only when the autonomy allows the director to read them. That is the whole
        // of "music-aware camera behaviour" (the brief's section 13): two generic quantities
        // nudging one generic rate, rather than a table of rules about what a build is.
        float rate = intent.cutRate;
        if (expressive) {
            rate = std::clamp(rate + 0.30f * (section.energy - 0.5f) * 2.0f +
                                  0.15f * (section.density - 0.5f) * 2.0f,
                              0.0f, 1.0f);
        }
        const double hold = options.maxShotSeconds +
                            (options.minShotSeconds - options.maxShotSeconds) *
                                static_cast<double>(rate);
        const double span = section.durationSeconds();
        std::size_t shotCount = 1;
        if (mayChoose && hold > 0.0) {
            const auto asked = static_cast<std::size_t>(std::max(1.0, std::round(span / hold)));
            // ...and never so many that a shot falls below the floor. A section shorter than the
            // floor is one shot, which is the honest answer rather than a flash.
            const auto ceiling =
                static_cast<std::size_t>(std::max(1.0, std::floor(span / options.minShotSeconds)));
            shotCount = std::min(asked, ceiling);
            if (shotCount < asked) {
                // Said rather than silently granted in part: the fix is the director's own timing
                // floor and nothing in the plan, and an author staring at a plan that asks for
                // rapid coverage has no way to know that from the film alone.
                out.warnings.push_back(fmt::format(
                    "'{}' asks for {} shot(s) across {:.1f}s; a {:.1f}s floor allows {}",
                    section.label, asked, span, options.minShotSeconds, shotCount));
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
        std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.score != b.score) {
                return a.score > b.score;
            }
            return a.camera->id < b.camera->id; // never vector order
        });
        std::size_t wanted = mayChoose ? static_cast<std::size_t>(std::max(1, intent.cameras)) : 1u;
        if (wanted > candidates.size()) {
            out.warnings.push_back(fmt::format(
                "'{}' asks for {} camera(s); this scene offers the Auto-director {}",
                section.label, intent.cameras, candidates.size()));
            wanted = candidates.size();
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
        const std::size_t first =
            wanted > 0 ? (rotation + static_cast<std::uint32_t>(section.occurrence)) % wanted : 0;

        // ---- the shots ------------------------------------------------------------------------
        const double each = span / static_cast<double>(shotCount);
        // How long one subject is kept. Low variation holds; high variation moves on every shot.
        // Song Mode's own answer to what `dwellShots` is for in the other two modes, derived from
        // the intent rather than from a global slider -- because it is a property of the section.
        const int dwell = std::max(1, static_cast<int>(std::lround(lerpf(3.0f, 1.0f, intent.variation))));
        std::size_t heldIndex = 0;
        int heldFor = 0;
        bool holding = false;

        for (std::size_t k = 0; k < shotCount; ++k) {
            const double start = section.startSeconds + each * static_cast<double>(k);
            const double end = k + 1 == shotCount ? section.endSeconds : start + each;
            const std::uint32_t h = decisionHash(options.seed, si, section.occurrence, k, 0xA17u);

            // Cast.
            if (!holding || heldFor >= dwell) {
                heldIndex = cast.next(intent.heroEmphasis);
                heldFor = 0;
                holding = true;
            }
            ++heldFor;
            const FocalTarget subject = cast.empty() ? brief.hero : cast.at(heldIndex);

            // Camera.
            const scene::CameraRig& camera =
                *candidates[wanted == 0 ? 0 : (first + k) % wanted].camera;

            // Move.
            Shot shot;
            shot.name = fmt::format("{}-{}", section.label.empty() ? "section" : section.label, k + 1);
            shot.kind = shotKindForIntent(intent);
            shot.startSeconds = start;
            shot.durationSeconds = end - start;
            shot.subject = subject;
            shot.composition.focalLength = options.focalLength;

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
            const float nominal = lerpf(kCloseRadii, kWideRadii, intent.distance);
            const float opening = openingOf(shot.kind);
            const float travel =
                intent.movement * (mayChoose ? 1.0f : 0.5f); // Locked moves, but less
            // The variation term, and the only place a shot's framing differs from its neighbour's
            // inside one section. Zero when the section is Locked, which is what Locked means.
            const float jitter = mayChoose ? intent.variation * 0.22f * signedOf(mix(h ^ 0x9E37u))
                                           : 0.0f;
            const float centre = std::max(1.2f, nominal * (1.0f + jitter));
            shot.startDistance = std::max(0.8f, centre * (1.0f - 0.40f * opening * travel));
            shot.endDistance = std::max(0.8f, centre * (1.0f + 0.40f * opening * travel));

            // Elevation: a wide shot looks slightly down on the world, a close one is nearly level.
            // Biased by whatever the subject says it reads best from, exactly as
            // `directFromStructure` does -- degrees in, orbit-radius ratio out.
            float elevation = lerpf(0.06f, 0.30f, intent.distance);
            if (subject.preferredElevationDegrees != 0.0f) {
                elevation = std::tan(glm::radians(
                    std::clamp(subject.preferredElevationDegrees, -60.0f, 60.0f)));
            }
            shot.startElevation = elevation;
            shot.endElevation = elevation + 0.12f * opening * travel;

            // The approach bearing: the subject's own preferred one, spread by the golden angle so
            // consecutive shots are not nine views down one axis, and offset by the decision hash so
            // the second pass over this material comes at it from somewhere else.
            const float golden = static_cast<float>(out.sequence.shots.size()) * 2.39996f;
            const float offset = mayChoose ? intent.variation * signedOf(h) * 1.4f : 0.0f;
            shot.startAzimuth = subject.preferredAzimuth + golden + offset;
            // How far round it goes: the movement axis, signed by the hash so a film does not
            // always circle the same way.
            const float sweep = intent.movement * 0.85f * (signedOf(mix(h ^ 0x51F3u)) < 0.0f ? -1.0f : 1.0f);
            shot.endAzimuth = shot.startAzimuth + sweep;

            // Who the shot is *for*, as a share of the frame's attention. The hero emphasis axis,
            // scaled by how much is happening -- a quiet hero shot is still a hero shot, but it is
            // not the loudest moment in the film.
            shot.spotlight.emphasis =
                std::clamp(intent.heroEmphasis * (0.45f + 0.55f * section.energy), 0.0f, 1.0f);
            shot.spotlight.active = shot.spotlight.emphasis > 0.05f;
            if (shot.spotlight.active && bestCoverage(shot) < kMinHeroCoverage) {
                // A spotlight on a subject that never spans a sixteenth of the frame is a claim the
                // picture does not support. Dropped rather than allowed to fail validation later,
                // and said out loud, because the cause is the intent's own distance axis.
                shot.spotlight.active = false;
                shot.spotlight.emphasis = 0.0f;
                out.warnings.push_back(
                    fmt::format("'{}' shot {}: '{}' is too far away at {:.1f} radii to carry the "
                                "frame, so its emphasis was dropped",
                                section.label, k + 1, shot.subject.name,
                                static_cast<double>(shot.startDistance)));
            }

            current = shot.handoff.has_value() ? *shot.handoff : shot.subject;
            haveCurrent = true;

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
            decision.startDistance = shot.startDistance;
            decision.endDistance = shot.endDistance;
            out.decisions.push_back(std::move(decision));

            out.sequence.shots.push_back(std::move(shot));

            // The camera track. Adjacent spans on the same camera are one shot, because a cut from a
            // camera to itself is not a cut -- what changed is the framing underneath, and the shot
            // track is not where framing lives.
            if (!out.shots.empty() && out.shots.back().camera == camera.id &&
                std::abs(out.shots.back().endSeconds - start) < 1e-6) {
                out.shots.back().endSeconds = end;
            } else {
                scene::CameraShot cut;
                cut.camera = camera.id;
                cut.startSeconds = start;
                cut.endSeconds = end;
                // A transition that lands where a section begins and where the music changed hard is
                // a cut; one inside a section is a short blend, which is what a coverage change
                // between two angles of the same moment looks like when it is done well.
                const bool sectionOpener = k == 0;
                if (sectionOpener && section.transition > 0.18f) {
                    cut.transition = scene::ShotTransition::Cut;
                } else if (sectionOpener) {
                    cut.transition = scene::ShotTransition::Blend;
                    cut.blendSeconds = 0.8;
                } else {
                    cut.transition = scene::ShotTransition::Blend;
                    cut.blendSeconds = 0.35;
                }
                // Locked in the shot-track sense: an authored moment an event camera may not take.
                // A section the author locked is a moment the author chose, so it is protected;
                // everything else stays available to the world's own events, which is what keeps
                // Song Mode compatible with ADR-245's event cameras rather than fighting them.
                cut.locked = autonomy == Autonomy::Locked;
                cut.label = section.label;
                out.shots.push_back(std::move(cut));
            }
        }
    }

    if (auto ok = out.sequence.validate(); !ok) {
        return std::unexpected(ok.error());
    }
    log::info("song director: {} section(s) -> {} shot(s) on {} camera(s), {} cut(s) on the shot "
              "track",
              plan.sections.size(), out.sequence.shots.size(), out.camerasUsed(), out.shots.size());
    for (const std::string& warning : out.warnings) {
        log::info("song director: {}", warning);
    }
    for (const SongDecision& d : out.decisions) {
        log::debug("song director: {}", d.line());
    }
    return out;
}

} // namespace avgen::app
