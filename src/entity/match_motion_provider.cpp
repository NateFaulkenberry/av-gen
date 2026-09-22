#include "entity/match_motion_provider.hpp"
#include "entity/trajectory_prediction.hpp"

#include "scene/animation.hpp"
#include "scene/skeleton.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <optional>

namespace avgen::entity {
namespace {

// Where the intent block sits in the feature vector: after the per-joint block, the trajectory
// block, and then the root velocity. Derived from the config rather than assumed, because the
// config is data-driven (§8) and a hardcoded offset would be wrong the first time somebody adds a
// joint.
struct FeatureLayout {
    std::size_t trajectory = 0; // first trajectory dimension
    std::size_t rootVelocity = 0;
    std::size_t total = 0;
};

FeatureLayout layoutOf(const scene::MotionFeatureConfig& config) {
    FeatureLayout out;
    out.trajectory = config.joints.size() * 6u;
    out.rootVelocity = out.trajectory + (config.trajectoryTimes.size() * 4u);
    out.total = config.dimension();
    return out;
}

// Where carrying on for `dt` seconds lands, from `sample` at clip time `time`.
//
// **By time, not by one sample per call.** The continuation used to follow `sampleNext` once per
// `advance`, whatever `dt` was. The database is sampled at 30 Hz and the entity steps at 60 Hz, and
// seeking replays at a fixed 1/60 step, so every continuation played at twice its authored speed,
// and at a speed that changed with the frame rate. That broke ADR-086: the pose must be a function
// of time, not of how many frames it took to get there.
//
// The walk follows `sampleNext`, so it goes wherever the database says the clip goes: a looping
// clip wraps into its own start (whose first and last samples are the same instant, so the wrap
// costs no time), and a clip that ends reports `ended` so a search can be forced.
// The clip time a memory is showing: its own `localTime` when that lies within the stretch of clip
// its sample stands for, and the sample's time otherwise. A memory built by hand, or by an older
// provider, may carry a time that does not belong to its sample, and posing that time would show
// some other part of the clip.
float shownTime(const scene::MotionDatabase& db, std::uint32_t sample, float localTime) {
    const float from = db.sampleTime[sample];
    const std::uint32_t after = db.sampleNext[sample];
    const float to = (after != scene::MotionDatabase::kInvalid && after > sample)
                         ? db.sampleTime[after]
                         : from; // a clip's last sample stands for its own instant only
    return (localTime >= from - 1e-6f && localTime <= to + 1e-6f) ? localTime : from;
}

struct Carried {
    std::uint32_t sample = scene::MotionDatabase::kInvalid;
    float time = 0.0f;
    bool ended = false;
};

Carried carryOn(const scene::MotionDatabase& db, std::uint32_t sample, float time, float dt) {
    Carried out;
    out.sample = sample;
    float target = shownTime(db, sample, time) + std::max(dt, 0.0f);
    // The samples of one clip are in order, so the walk visits each at most once per wrap. The
    // bound stops a malformed chain looping forever.
    for (std::uint32_t guard = 0; guard <= db.sampleCount(); ++guard) {
        const std::uint32_t next = db.sampleNext[out.sample];
        if (next == scene::MotionDatabase::kInvalid) {
            // The clip ends here. Hold its last frame, and say so.
            out.ended = target > db.sampleTime[out.sample] + 1e-6f;
            out.time = db.sampleTime[out.sample];
            return out;
        }
        if (next <= out.sample) {
            // The loop's wrap: the last sample and the first are the same instant, one period
            // apart on the clock.
            const float period = db.sampleTime[out.sample] - db.sampleTime[next];
            if (period <= 0.0f) {
                break;
            }
            target -= period;
            out.sample = next;
            continue;
        }
        if (db.sampleTime[next] > target + 1e-6f) {
            break;
        }
        out.sample = next;
    }
    out.time = std::max(target, db.sampleTime[out.sample]);
    return out;
}


// §71: the body frame a clip's pose is expressed in at `time`, for a clip whose travel is real:
// the travel joint's horizontal position and facing, interpolated between the sample and the one
// after it in the same clip. Returns false for an in-place clip, or a database that does not carry
// root frames, which is posed as authored.
bool rootFrameAt(const scene::MotionDatabase& db, std::uint32_t sample, float time, glm::vec3& at, float& yaw) {
    if (db.sampleRoot.size() != 3u * db.sampleCount() || sample >= db.sampleCount()) {
        return false;
    }
    const std::uint32_t clip = db.sampleClip[sample];
    if (clip >= db.clipTravels.size() || db.clipTravels[clip] == 0u) {
        return false;
    }
    const float* a = db.sampleRoot.data() + (3u * static_cast<std::size_t>(sample));
    at = glm::vec3(a[0], 0.0f, a[1]);
    yaw = a[2];
    const std::uint32_t next = db.sampleNext[sample];
    if (next != scene::MotionDatabase::kInvalid && next > sample) {
        const float span = db.sampleTime[next] - db.sampleTime[sample];
        const float s = span > 0.0f ? std::clamp((time - db.sampleTime[sample]) / span, 0.0f, 1.0f) : 0.0f;
        const float* b = db.sampleRoot.data() + (3u * static_cast<std::size_t>(next));
        at = glm::mix(at, glm::vec3(b[0], 0.0f, b[1]), s);
        float dYaw = b[2] - a[2];
        while (dYaw > 3.14159265f) { dYaw -= 6.28318531f; }
        while (dYaw < -3.14159265f) { dYaw += 6.28318531f; }
        yaw = a[2] + (dYaw * s);
    }
    return true;
}

// Re-express `pose` in its body frame: the travel joint moved to the origin horizontally and turned
// to face +Z. Applied to the top-level joints, so everything under them follows.
void toBodyFrame(const scene::Skeleton& skeleton, scene::Pose& pose, const glm::vec3& at, float yaw) {
    const glm::mat4 w = glm::rotate(glm::mat4(1.0f), -yaw, glm::vec3(0.0f, 1.0f, 0.0f)) *
                        glm::translate(glm::mat4(1.0f), -at);
    for (std::size_t j = 0; j < skeleton.joints.size() && j < pose.local.size(); ++j) {
        if (skeleton.joints[j].parent < 0) {
            pose.local[j] = scene::Transform::fromMatrix(w * pose.local[j].matrix());
        }
    }
}

// Sample `sample`'s clip at `time`, in its body frame when its travel is real (§71).
void poseSample(const scene::MotionDatabase& db, const std::vector<scene::AnimationClip>& clips,
                const scene::Skeleton& skeleton, std::uint32_t sample, float time, scene::Pose& out) {
    scene::setRestPose(skeleton, out);
    scene::sampleClip(clips[db.sampleClip[sample]], time, out);
    glm::vec3 at;
    float yaw = 0.0f;
    if (rootFrameAt(db, sample, time, at, yaw)) {
        toBodyFrame(skeleton, out, at, yaw);
    }
}
} // namespace

void MatchMotionProvider::resolveStyle() {
    styleClipCost_.clear();
    clipsInStyle_ = 0;
    if (db_ == nullptr || style_.empty()) {
        return;
    }
    const std::size_t n = db_->clipNames.size();
    std::vector<std::uint8_t> in(n, 0u);
    for (const StyleRule& rule : styleRules_) {
        if (rule.style != style_) {
            continue;
        }
        for (std::size_t c = 0; c < n; ++c) {
            for (const std::string& prefix : rule.prefixes) {
                if (db_->clipNames[c].rfind(prefix, 0) == 0) {
                    in[c] = 1u;
                }
            }
        }
    }
    for (const std::uint8_t v : in) {
        clipsInStyle_ += v;
    }
    // A style nothing carries costs every clip the same, which changes no choice: leave it empty
    // rather than add a constant to every candidate.
    if (clipsInStyle_ == 0 || clipsInStyle_ == n) {
        return;
    }
    const float penalty = std::max(settings_.styleWeight, 0.0f) * db_->stats.costSpread;
    if (penalty <= 0.0f) {
        return;
    }
    styleClipCost_.resize(n);
    for (std::size_t c = 0; c < n; ++c) {
        styleClipCost_[c] = in[c] != 0u ? 0.0f : penalty;
    }
}

void MatchMotionProvider::fillQuery(const MotionRequest& request, std::uint32_t current,
                                    scene::MotionQuery& query, std::vector<float>& raw) const {
    const FeatureLayout layout = layoutOf(db_->config);
    const bool haveCurrent = current < db_->sampleCount();
    // **The pose half is a lookup.** The character is playing a database sample, so what it looks
    // like right now is that sample's own feature vector -- already extracted and already
    // standardised. Only the intent half is computed, and it is computed from the request.
    std::vector<float>& features = query.features;
    features.assign(db_->dimension, 0.0f);
    if (haveCurrent) {
        const float* sample = db_->featuresFor(current);
        std::copy(sample, sample + db_->dimension, features.begin());
    }

    // The intent half, in raw units, standardised the same way the database was: the trajectory
    // block (where the body will be at each horizon) and the root velocity.
    raw.assign(db_->dimension, 0.0f);
    // **In the body's own frame, because that is the frame the database is in (§7), and in the
    // asset's own units.** A request is world space. Compared as-is with features extracted facing
    // +Z, a body facing east that asked to walk forward was scored against sideways motion.
    const auto toBody = [&](const glm::vec3& world) {
        return scene::toFacingFrame(world, request.bodyFacing) / worldScale_;
    };
    const glm::vec3 want = toBody(request.desiredVelocity + request.steering);
    const std::vector<float>& horizons = db_->config.trajectoryTimes;
    if (settings_.predictTrajectory && request.bodyVelocityKnown && !horizons.empty()) {
        // §25: where the body will actually be, stepping the motion controller from how it moves
        // now toward what it was asked. A standing body asked to walk ramps up; a walking body
        // asked to face somewhere else curves. The prediction runs in world units and is
        // expressed in the body's frame, as every database feature is.
        MotionState state;
        state.velocity = request.bodyVelocity;
        state.previousVelocity = request.bodyVelocity;
        state.facing = request.bodyFacing;
        state.started = true;
        const TrajectoryPrediction prediction = predictTrajectory(request, state, settings_.limits, horizons);
        for (std::size_t t = 0; t < horizons.size() && t < prediction.points.size(); ++t) {
            const glm::vec3 p = toBody(prediction.points[t].position);
            // The future facing, in the body's frame (a direction: no unit scaling).
            const glm::vec3 face = scene::toFacingFrame(prediction.points[t].facing, request.bodyFacing);
            const std::size_t base = layout.trajectory + (t * 4u);
            raw[base + 0] = p.x;
            raw[base + 1] = p.z;
            raw[base + 2] = face.x;
            raw[base + 3] = face.z;
        }
        const glm::vec3 now = toBody(request.bodyVelocity);
        raw[layout.rootVelocity + 0] = now.x;
        raw[layout.rootVelocity + 1] = now.y;
        raw[layout.rootVelocity + 2] = now.z;
    } else {
        // The asked-for velocity, held: where it wants to be at each horizon, for a body already
        // moving as asked.
        for (std::size_t t = 0; t < horizons.size(); ++t) {
            const float ahead = horizons[t];
            const std::size_t base = layout.trajectory + (t * 4u);
            raw[base + 0] = want.x * ahead;
            raw[base + 1] = want.z * ahead;
            // The facing asked for, in the body's frame.
            const glm::vec3 face = scene::toFacingFrame(request.desiredFacing, request.bodyFacing);
            raw[base + 2] = face.x;
            raw[base + 3] = face.z;
        }
        raw[layout.rootVelocity + 0] = want.x;
        raw[layout.rootVelocity + 1] = want.y;
        raw[layout.rootVelocity + 2] = want.z;
    }
    scene::normaliseQuery(*db_, raw);

    // Overwrite the intent dimensions of the pose-derived query with what the character wants.
    // Blended by `intentWeight` against what it is already doing, so a body already moving the
    // right way is not yanked toward an idealised trajectory it is fractionally off.
    const float w = std::clamp(settings_.intentWeight, 0.0f, 1.0f);
    for (std::size_t d = layout.trajectory; d < layout.total && d < layout.rootVelocity + 3; ++d) {
        features[d] = (features[d] * (1.0f - w)) + (raw[d] * w);
    }

    query.requireTags = 0;
    query.current = haveCurrent ? current : scene::MotionDatabase::kInvalid;
    // §44: the style term. Empty unless a style is set and some clip carries it.
    query.clipCost = styleClipCost_;
    // §14: a body on the ground never wants an airborne sample. One AND per sample, and it removes
    // whole clips before anything is scored.
    query.rejectTags = static_cast<std::uint32_t>(scene::MotionTag::Airborne) |
                       static_cast<std::uint32_t>(scene::MotionTag::Terminal);
}

std::optional<scene::MotionQuery> MatchMotionProvider::queryFor(const MotionRequest& request,
                                                                const MotionMemory& in) const {
    if (db_ == nullptr || db_->sampleCount() == 0) {
        return std::nullopt;
    }
    const bool haveCurrent =
        in.generation > 0 && in.database == db_->identity && in.selection < db_->sampleCount();
    scene::MotionQuery query;
    std::vector<float> raw;
    fillQuery(request, haveCurrent ? in.selection : scene::MotionDatabase::kInvalid, query, raw);
    return query;
}

MotionResult MatchMotionProvider::advance(const MotionRequest& request, const MotionMemory& in,
                                          double time, float dt, MotionMemory& next) const {
    MotionResult result;
    next = in;
    if (db_ == nullptr || clips_ == nullptr || db_->sampleCount() == 0) {
        result.status = MotionStatus::NotReady;
        return result;
    }
    if (!expectedSkeleton_.empty() && db_->skeletonDigest != expectedSkeleton_) {
        // §35 "skeleton mismatch". Not a pose to adapt: every sample indexes another rig's joints.
        result.status = MotionStatus::NotReady;
        return result;
    }
    // §35 "invalid query". A non-finite intent would standardise to NaN in every intent dimension
    // and every candidate would score NaN, so the search would return whatever it compared first.
    const glm::vec3 intent = request.desiredVelocity + request.steering;
    if (!std::isfinite(intent.x) || !std::isfinite(intent.y) || !std::isfinite(intent.z) ||
        !std::isfinite(request.bodyFacing.x) || !std::isfinite(request.bodyFacing.z)) {
        result.status = MotionStatus::Unsupported;
        return result;
    }
    if (request.mode != MovementMode::Ground) {
        // §35: decline rather than guess. A matcher whose database is all ground locomotion has
        // nothing to say about a body in the air, and the chain's clip provider does.
        result.status = MotionStatus::Unsupported;
        return result;
    }

    // §40/§76: a memory settled against another database -- the one this provider held before a
    // hot swap -- is not a current sample here, however in-range its index is. It is migrated the
    // only safe way: as a first selection, with no blend from a frame that no longer exists.
    const bool sameDatabase = in.database == db_->identity;
    const bool haveCurrent =
        in.generation > 0 && sameDatabase && in.selection < db_->sampleCount();
    if (!sameDatabase) {
        next.clearBlends();
    }
    next.database = db_->identity;

    // ---- continue, if it is not time to search (§27/§29) --------------------------------------
    //
    // Following `sampleNext` is an array read. That is the whole reason motion matching is
    // affordable: the scan happens ten times a second, not sixty.
    const double sinceSearch = time - in.decisionTime;
    const bool due = !haveCurrent || sinceSearch >= static_cast<double>(settings_.searchInterval);
    const bool locked = haveCurrent && sinceSearch < static_cast<double>(settings_.minimumContinuation);

    // Where carrying on would put the body now. Both branches below need it: the continuation
    // itself, and the §28 comparison of a search's winner against it.
    const Carried carried = haveCurrent ? carryOn(*db_, in.selection, in.localTime, dt) : Carried{};

    if (haveCurrent && (!due || locked) && !carried.ended) {
        next.selection = carried.sample;
        next.localTime = carried.time;
        next.phase = db_->samplePhase[carried.sample];
        next.hasPhase = true;
        // §32: the blends' own clocks run on every step, including the ones that do not
        // search.
        next.tickBlends(dt);
        ++counters_.continued;
        result.status = MotionStatus::Produced;
        result.content = db_->clipNames[db_->sampleClip[carried.sample]];
        return result;
    }
    // A clip that has ended and does not loop falls through: a search is due whatever the clock
    // says.

    // ---- build the query -----------------------------------------------------------------------
    //
    // §75: every vector on this path is per-thread scratch, reused. A search allocated three
    // vectors per call before -- the query, the intent block and the margin's weights -- and at ten
    // searches a second per character that is thousands of allocations a second on a path that
    // needs none. `thread_local` rather than members for the reason `pose` gives below: one
    // provider serves every character, possibly from several threads (§74).
    thread_local scene::MotionQuery query;
    thread_local std::vector<float> raw;
    thread_local std::vector<float> dimWeight;
    fillQuery(request, haveCurrent ? in.selection : scene::MotionDatabase::kInvalid, query, raw);

    const scene::MotionMatch match = scene::searchMotion(*db_, query, settings_.weights);
    ++counters_.searches;
    counters_.scored += match.considered;
    if (!match.found()) {
        result.status = MotionStatus::NoContent;
        return result;
    }

    // §28: a candidate must beat carrying on by a margin, or the motion is held. Without this a
    // body sitting between two samples re-selects on every search and commits to neither.
    std::uint32_t chosen = match.sample;
    const std::uint32_t follow = haveCurrent && !carried.ended ? carried.sample
                                                                : scene::MotionDatabase::kInvalid;
    if (haveCurrent) {
        if (follow != scene::MotionDatabase::kInvalid && follow != match.sample) {
            // The continuation's own cost, under the same query, so the comparison is like for
            // like rather than the winner against a remembered number.
            const float* f = db_->featuresFor(follow);
            // **Weighted, because `match.cost` is.** This summed raw squared deltas while the
            // search it is compared against applies per-dimension weights (§10) -- so the two were
            // in different scales and the comparison was invalid. §10 made the weights live for
            // the first time and nothing here was updated to match; at the default weight vector
            // the discrepancy is small, which is exactly why it survived.
            //
            // A comparison between two costs computed by different formulas is worse than no
            // comparison: it has a defensible-looking number on both sides.
            scene::motionFeatureWeightsInto(db_->config, dimWeight);
            const bool weighted = dimWeight.size() == db_->dimension;
            float continueCost = 0.0f;
            for (std::size_t d = 0; d < db_->dimension; ++d) {
                const float delta = query.features[d] - f[d];
                continueCost += scene::motionFeatureTerm(delta, weighted ? dimWeight[d] : 1.0f);
            }
            // §44: and the same style term the search added, or an out-of-style continuation
            // would be compared as if it were free.
            continueCost += scene::motionClipCost(*db_, query, follow);
            // §28: the margin is a fraction of the measured spread, so it is in the cost
            // function's own scale rather than in raw units that mean nothing without it.
            const float margin = settings_.switchMargin * db_->stats.costSpread;
            if (match.cost > continueCost - margin) {
                chosen = follow;
                ++counters_.heldByMargin;
            }
        }
    }

    const bool switched = !haveCurrent || chosen != follow;
    if (switched) {
        ++counters_.switches;
    }
    // §32/ADR-613. **A blend begins where the motion jumps, and nowhere else.** A search whose
    // winner was the continuation changed nothing, so restarting the decay there would throw away
    // a running blend for a transition that did not happen -- and would do it on the majority of
    // searches, because most searches continue. This is the same distinction `counters_.switches`
    // already draws, reused rather than re-derived so the two cannot disagree.
    if (switched && haveCurrent) {
        next.tickBlends(dt);
        // From where the outgoing motion was actually showing, which while carrying on lies
        // between samples.
        next.pushBlend(in.selection, shownTime(*db_, in.selection, in.localTime), chosen,
                       db_->sampleTime[chosen], settings_.blendSlots);
    } else if (switched) {
        // The first selection of a character's life is not a transition: there is nothing to
        // blend from, and pretending otherwise would decay an offset against a bind pose.
        next.clearBlends();
    } else {
        next.tickBlends(dt);
    }
    next.selection = chosen;
    // Carrying on keeps its time between samples. A switch starts at the chosen sample's own time.
    next.localTime = (!switched && haveCurrent) ? carried.time : db_->sampleTime[chosen];
    next.phase = db_->samplePhase[chosen];
    next.hasPhase = true;
    // **This counts SEARCHES, not selection changes, and `MotionMemory::generation` is documented
    // as the latter** -- "bumped whenever `selection` changes, so a consumer can tell 'still
    // playing the same thing' from 'playing the same thing again'". `ClipMotionProvider` honours
    // that; this bumps unconditionally, including on the majority of searches whose winner was the
    // continuation and which changed nothing.
    //
    // **One field, two writers, two meanings** -- the same defect as the field three lines above
    // it, which was renamed `decisionTime` for exactly this reason. It is harmless only because
    // every live reader tests `> 0` as a presence check; the first consumer to use it the way the
    // header describes gets a wrong answer under this provider. Left as-is rather than changed
    // inside a documentation pass, because `switched` is right there and the fix is a behaviour
    // change that wants its own before-and-after (ADR-615).
    next.generation = in.generation + 1;
    next.decisionTime = time;
    result.status = MotionStatus::Produced;
    result.content = db_->clipNames[db_->sampleClip[chosen]];
    return result;
}

MotionResult MatchMotionProvider::pose(const MotionMemory& memory, const scene::Skeleton& skeleton,
                                       scene::Pose& out) const {
    MotionResult result;
    if (db_ == nullptr || clips_ == nullptr) {
        result.status = MotionStatus::NotReady;
        return result;
    }
    // `selection` is a SAMPLE index in this provider's space. Validated rather than trusted: the
    // clip provider reads the same field as a clip index, and ADR-556's `MotionMemory::provider`
    // is what stops them being confused -- this is the second lock on that door.
    //
    // §40/§76: and it must be a sample of THIS database. After a hot swap an old index is usually
    // still in range and names an unrelated frame; declining is what lets the chain fall through
    // for the one frame until `advance` has migrated the memory.
    if (memory.database != db_->identity || memory.selection >= db_->sampleCount()) {
        result.status = MotionStatus::NoContent;
        return result;
    }
    const std::uint32_t clipIndex = db_->sampleClip[memory.selection];
    if (clipIndex >= clips_->size()) {
        result.status = MotionStatus::NoContent;
        return result;
    }
    const scene::AnimationClip& clip = (*clips_)[clipIndex];
    // At the memory's own time, which lies between samples while carrying on (`shownTime`), and
    // in the body's frame for a clip that travels (§71: the simulation owns where the body is).
    poseSample(*db_, *clips_, skeleton, memory.selection, shownTime(*db_, memory.selection, memory.localTime), out);
    result.status = MotionStatus::Produced;
    result.content = clip.name;

    // ---- §32: inertialize across the seam (ADR-613) --------------------------------------------
    //
    // The pose above is the incoming motion, alone. What is added here is **the difference the
    // switch introduced**, decaying to nothing -- so at the instant of the switch the output is
    // exactly the outgoing pose, and from there it converges on the incoming one without ever
    // teleporting. That is ADR-547's inertialization on `AnimationPlayer`, applied at the provider
    // seam, and it is deliberately the same arithmetic rather than a second scheme: the bar for
    // switching `proceduralMotion` on is parity with the player it replaces, and a provider that
    // blended *differently* would be a change of look as well as of architecture.
    //
    // **The offset is recomputed, never remembered.** Both ends are sampled from the two database
    // frames the memory names, so a scrub that lands mid-transition reconstructs the same offset
    // instead of inheriting one from wherever the playhead came from -- the property ADR-360
    // requires and the reason `MotionMemory` can hold two integers where an engine would hold a
    // pose.
    if (settings_.inertializeHalflife <= 0.0f || !memory.blending()) {
        return result;
    }
    // Scratch for the two ends of one offset. `thread_local` rather than a member:
    // `IMotionProvider` is const because one instance serves every character in a scene, so a
    // mutable member would be shared state between bodies the moment anything poses two of them at
    // once.
    thread_local scene::Pose was;
    thread_local scene::Pose became;

    // **Oldest first.** The offsets were introduced in order, so they are re-applied in order;
    // translations add either way, and rotations do not, so the order is the one that matches how
    // the pose was actually built up.
    const std::size_t slots =
        std::min(std::max<std::size_t>(settings_.blendSlots, 1), MotionMemory::kBlendSlots);
    for (std::size_t s = slots; s-- > 0;) {
        const MotionMemory::Blend& blend = memory.blends[s];
        if (!blend.live() || blend.from >= db_->sampleCount() || blend.to >= db_->sampleCount()) {
            continue; // an empty slot, or a memory from another database
        }
        const float decay = inertializationDecay(settings_.inertializeHalflife, blend.elapsed);
        if (decay <= kInertializationFloor) {
            continue; // what is left of this offset is below the floor
        }
        const std::uint32_t fromClip = db_->sampleClip[blend.from];
        const std::uint32_t toClip = db_->sampleClip[blend.to];
        if (fromClip >= clips_->size() || toClip >= clips_->size()) {
            continue;
        }
        // Both ends in their own body frames, as the pose above is: an offset between two poses
        // expressed in different frames would carry their travel into the blend.
        poseSample(*db_, *clips_, skeleton, blend.from, blend.fromTime, was);
        poseSample(*db_, *clips_, skeleton, blend.to, blend.toTime, became);
        applyInertializedOffset(was, became, decay, out);
    }
    return result;
}

} // namespace avgen::entity
