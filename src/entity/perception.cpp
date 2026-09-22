#include "entity/perception.hpp"

#include "entity/entity.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace avgen::entity {
namespace {

// A seed-and-index hash, never a stream (D2). The model is `app/cinematic.cpp:1555-1587`: a PRNG
// stream would make every later answer depend on how many earlier ones were drawn, so one extra
// character in the scene would re-phase everybody else's senses.
[[nodiscard]] std::uint32_t mix(std::uint32_t h) {
    h ^= h >> 16;
    h *= 0x7FEB352Du;
    h ^= h >> 15;
    h *= 0x846CA68Bu;
    h ^= h >> 16;
    return h;
}

constexpr std::array<std::string_view, 5> kWeightNames{"landmark", "character", "glow", "water",
                                                       "vista"};

[[nodiscard]] float readFloat(const nlohmann::json& j, const char* key, float fallback) {
    return j.contains(key) && j[key].is_number() ? j[key].get<float>() : fallback;
}

} // namespace

std::span<const std::string_view> perceptionWeightNames() { return kWeightNames; }

std::uint64_t senseTick(double time, float hertz, std::uint32_t seed) {
    // A cadence at or below zero means "every step", and the tick is then the caller's own step
    // count in disguise: returning a monotone function of `time` at the finest resolution the
    // double carries would be a tick that never repeats and a character that senses every frame,
    // which is what `hertz = 0` should mean and does.
    if (!(hertz > 0.0f)) {
        return static_cast<std::uint64_t>(std::max(time, 0.0) * 1.0e6);
    }
    // The phase is in [0, 1) *ticks*, so it moves when a body senses and never how often. Without
    // it every character in the scene re-senses on the same frame -- one spike in sixteen rather
    // than a flat cost, and a crowd that all reacts on the same frame reads as a chorus line.
    const double phase = static_cast<double>(mix(seed * 0x9E3779B1u) & 0xFFFFFFu) / 16777216.0;
    return static_cast<std::uint64_t>(std::max(time, 0.0) * static_cast<double>(hertz) + phase);
}

std::size_t GridPerception::perceive(const EntityWorld& world, std::size_t self,
                                     const PerceptionSettings& settings, std::uint32_t seed,
                                     double time, std::span<Percept> out) const {
    ++counts_.calls;
    const auto capacity =
        std::min<std::size_t>(out.size(), std::max<std::size_t>(settings.capacity, 0));
    if (capacity == 0 || self >= world.entities().size() || !(settings.range > 0.0f)) {
        return 0;
    }
    const Entity& me = *world.entities()[self];
    // R1: the simulation's answer, which is what navigation reasons in and therefore what a
    // decision fed by these percepts will act on.
    const glm::vec3 eye = me.state().position();
    const glm::vec2 forward(std::sin(me.state().yaw), std::cos(me.state().yaw));
    // Half-angle of the horizontal field of view. 360 or more is a body that notices what is behind
    // it, which is right for a herd animal and wrong for one an author wants to surprise.
    const float halfFov = std::min(settings.fieldOfView, 360.0f) * 0.5f * 0.01745329252f;
    const float cosHalf = std::cos(halfFov);
    const bool allRound = settings.fieldOfView >= 360.0f;
    const float range = settings.range;
    const float invRange = 1.0f / range;

    // Taste, normalised by its own largest term so salience lands in [0, 1] and still orders. The
    // alternative -- clamping a weight above 1 -- makes every nearby thing report exactly 1 and
    // destroys the ranking the capacity cut depends on.
    float maxWeight = 0.0f;
    for (const float w : settings.weight) {
        maxWeight = std::max(maxWeight, w);
    }
    // Phase D §9: per-tag taste, when this body declared any. Empty for every body written before
    // Phase D, which leaves `maxWeight` and every salience below exactly as they were.
    const std::span<const std::pair<std::uint64_t, float>> tagWeights = me.perceptionTagWeights();
    for (const auto& [bit, w] : tagWeights) {
        (void)bit;
        maxWeight = std::max(maxWeight, w);
    }
    const float invWeight = maxWeight > 0.0f ? 1.0f / maxWeight : 0.0f;

    candidates_.clear();

    // `scanIndex` is what breaks a tie in salience, and it is a single sequence across both scans:
    // bodies first in entity order, then interest points in list order. A tie broken on the lower
    // index is the same rule `NavGrid`'s A* breaks a tie on cell index with, and it is what stops
    // the answer depending on the order the grid happened to visit its cells.
    const auto add = [&](InterestKind kind, std::size_t scanIndex, const glm::vec3& at,
                         std::uint64_t tags) {
        const glm::vec3 delta = at - eye;
        const glm::vec2 flat(delta.x, delta.z);
        const float distance = glm::length(delta);
        if (distance > range) {
            return;
        }
        // Facing, except within arm's reach. A thing standing next to you is noticed whichever way
        // you are turned; that is what `proximityRange` is for, and it is why a character cannot be
        // walked into without knowing it.
        if (!allRound && distance > settings.proximityRange) {
            const float flatLength = glm::length(flat);
            if (flatLength > 1e-4f) {
                const float cosAngle = glm::dot(flat / flatLength, forward);
                if (cosAngle < cosHalf) {
                    return;
                }
            }
        }
        Percept p;
        p.kind = kind;
        p.position = at;
        p.distance = distance;
        p.visibility = 1.0f;
        p.tested = false;
        p.seenAt = time;
        p.tags = tags; // Phase D §25: what the thing is, carried rather than looked up later
        const auto slot = static_cast<std::size_t>(kind);
        float weight = slot < 5 ? settings.weight[slot] : 1.0f;
        for (const auto& [bit, w] : tagWeights) {
            if ((tags & bit) != 0) {
                weight = std::max(weight, w);
            }
        }
        // **Salience deliberately does not include `visibility`.** It is taste times nearness and
        // nothing else. Folding in a visibility that only a round-robin subset of the percepts ever
        // measured would make the ranking -- and so the capacity cut, and so what the character
        // knows -- depend on how many other characters were competing for the occlusion budget.
        // That is precisely the failure ADR-270's budget rule exists to prevent, arriving through
        // the back door. What was seen is reported; what it is worth is the decider's to weigh.
        p.salience = std::clamp(weight * invWeight * (1.0f - distance * invRange), 0.0f, 1.0f);
        // The scan index rides in `source` while the percept is a candidate -- it is the tie-break
        // key -- and the copy-out below turns it back into the real `source`.
        p.source = scanIndex;
        candidates_.push_back(p);
    };

    if (index_.bodies != nullptr && !index_.bodyPositions.empty()) {
        index_.bodies->query(eye, range, hits_);
        counts_.candidates += hits_.size();
        for (const std::uint32_t hit : hits_) {
            const auto body = static_cast<std::size_t>(hit);
            if (body == self || body >= index_.bodyPositions.size()) {
                continue; // a character does not perceive itself
            }
            add(InterestKind::Character, body, index_.bodyPositions[body],
                body < world.entities().size() ? world.entities()[body]->tagMask() : 0);
        }
    }
    const std::span<const InterestPoint> interests = world.interestPoints();
    if (index_.interests != nullptr && !interests.empty()) {
        index_.interests->query(eye, range, hits_);
        counts_.candidates += hits_.size();
        const std::size_t bodyCount = index_.bodyPositions.size();
        for (const std::uint32_t hit : hits_) {
            if (hit >= index_.interestSource.size()) {
                continue;
            }
            const auto source = static_cast<std::size_t>(index_.interestSource[hit]);
            if (source >= interests.size()) {
                continue;
            }
            add(interests[source].kind, bodyCount + source, interests[source].position,
                interests[source].tags);
        }
    }

    std::stable_sort(candidates_.begin(), candidates_.end(),
                     [](const Percept& a, const Percept& b) {
                         if (a.salience != b.salience) {
                             return a.salience > b.salience;
                         }
                         return a.source < b.source; // still the scan index here
                     });

    const std::size_t kept = std::min(candidates_.size(), capacity);
    counts_.dropped += candidates_.size() - kept;
    const std::size_t bodyCount = index_.bodyPositions.size();
    for (std::size_t i = 0; i < kept; ++i) {
        out[i] = candidates_[i];
        const std::size_t scanIndex = candidates_[i].source;
        out[i].source = scanIndex < bodyCount ? scanIndex : scanIndex - bodyCount;
    }
    counts_.percepts += kept;

    // ---- the occlusion budget (ADR-270 §3) ---------------------------------------------------
    //
    // `world::heroSightline` measures **1800.6 us at 20 m and 5876.0 us at 60 m** on
    // `glowmere-valley-2` (minima of 5, load average 6.6-7.9), which confirms ADR-270's 1720.779.
    // One per character per frame at a hundred characters is 180 ms a frame -- four orders of
    // magnitude off, which is why this is a budget rather than a tuning knob.
    //
    // And the cost is a property of the **world function**, not of the nine rays: the same call on
    // the Character Intelligence Lab's flat fixture is 8 us, two hundred times cheaper, because a
    // world with no ecology makes the march's per-sample lookup nearly free. A fixture cannot prove
    // this budget is necessary; the shipped world is what justifies it (ADR-290 section 5.1).
    //
    // How many tests this tick is a **function of the tick index**, not an accumulator:
    //
    //     allowed(n) = floor(n * occlusionTestsPerSecond / tickRate)   tests in ticks [0, n)
    //     tests      = allowed(tick + 1) - allowed(lastTick + 1)
    //
    // so over any T seconds exactly floor(T * occlusionTestsPerSecond) tests are performed, the
    // rate is respected to the test rather than on average, and a replay reproduces the same
    // schedule because the only thing carried is the tick the body last sensed at, which the
    // cadence already had to carry. `allowed(lastTick + 1) % kept` is the round-robin cursor: each
    // tick resumes where the last one stopped, so every percept is tested in turn instead of the
    // first one being tested forever.
    //
    // **`lastTick + 1` and not `tick`, because sense ticks are not consecutive.** They skip when
    // the cadence is above the step rate, when a frame is long enough to cross two of them, and by
    // a whole frame of microseconds when `hertz` is zero and every step senses. Pricing `tick`
    // against `tick + 1` silently bought nothing in every one of those cases: measured, a crowd at
    // `hertz = 0` and two tests a second performed **zero** tests in three seconds where it was
    // owed 120. The first version of this budget was stateless and wrong, and the fix was to use
    // the one piece of state the cadence already keeps.
    //
    // **When the budget is spent, `tested` stays false and the percept stays.** A percept dropped
    // because a budget ran out is a character whose behaviour depends on how many other characters
    // exist -- the bug that only appears in the crowd scene and reproduces nowhere.
    const bool canTest = clearance_ != nullptr && clearance_->map != nullptr &&
                         settings.occlusionTestsPerSecond > 0.0f && kept > 0;
    if (!canTest) {
        counts_.occlusionDeferred += settings.occlusionTestsPerSecond > 0.0f ? kept : 0;
        return kept;
    }
    // Ticks per second, which is `hertz` except at `hertz <= 0` -- "sense every step" -- where
    // `senseTick` counts microseconds. The budget divides by whatever the tick rate actually is, or
    // a body told to sense every step would be granted sixteen thousand times its rate.
    const double tickRate = settings.hertz > 0.0f ? static_cast<double>(settings.hertz) : 1.0e6;
    const double perTick = static_cast<double>(settings.occlusionTestsPerSecond) / tickRate;
    const auto tick = senseTick(time, settings.hertz, seed);
    const std::uint64_t last = me.lastSenseTick();
    const auto allowed = [&](std::uint64_t n) {
        return static_cast<std::uint64_t>(static_cast<double>(n) * perTick);
    };
    // A body that has never sensed is owed this tick's share and not the whole history before it:
    // it was not sensing, so it did not accrue.
    const std::uint64_t before = last == Entity::kNoSenseTick ? allowed(tick) : allowed(last + 1);
    const std::uint64_t after = allowed(tick + 1);
    const auto budget =
        std::min<std::size_t>(static_cast<std::size_t>(after > before ? after - before : 0), kept);
    // The eye, and it is a stand-in rather than a measurement: `EntityState` carries a radius and
    // no height, and the crowd field already derives a body height from the radius the same way.
    // ADR-274 made a real joint reachable -- `socketTransform("eye", ...)` now distinguishes a
    // posed joint from the entity frame -- and the day a character declares an eye socket this is
    // the line that reads it.
    const float myHeight = std::max(me.state().radius * 2.0f, 1.0f);
    const glm::vec3 from = eye + glm::vec3(0.0f, myHeight * 0.9f, 0.0f);
    for (std::size_t i = 0; i < kept; ++i) {
        const std::size_t slot = (static_cast<std::size_t>(before) + i) % kept;
        if (i >= budget) {
            ++counts_.occlusionDeferred;
            continue;
        }
        Percept& p = out[slot];
        world::SubjectCapsule subject;
        // The base, as `HeroPoint::position` is: a percept's position is the body's feet.
        subject.position = p.position;
        if (p.kind == InterestKind::Character && p.source < world.entities().size()) {
            const Entity& them = *world.entities()[p.source];
            subject.radius = std::max(them.state().radius, 0.35f);
            subject.height = std::max(them.state().radius * 2.0f, 1.0f);
            subject.name = them.name();
        } else {
            subject.radius = 0.5f;
            subject.height = 1.0f;
        }
        const world::Sightline line = world::heroSightline(*clearance_, from, subject);
        p.visibility = line.visible;
        p.tested = true;
        ++counts_.occlusionTests;
    }
    return kept;
}

void ScriptedPerception::set(std::size_t self, std::vector<Percept> percepts) {
    for (auto& entry : scripted_) {
        if (entry.first == self) {
            entry.second = std::move(percepts);
            return;
        }
    }
    scripted_.emplace_back(self, std::move(percepts));
}

std::size_t ScriptedPerception::perceive(const EntityWorld& world, std::size_t self,
                                         const PerceptionSettings& settings, std::uint32_t seed,
                                         double time, std::span<Percept> out) const {
    (void)world;
    (void)seed;
    (void)time;
    for (const auto& entry : scripted_) {
        if (entry.first != self) {
            continue;
        }
        const std::size_t kept =
            std::min({entry.second.size(), out.size(), static_cast<std::size_t>(settings.capacity)});
        std::copy_n(entry.second.begin(), kept, out.begin());
        return kept;
    }
    return 0;
}

Result<PerceptionSettings> perceptionFromJson(const nlohmann::json& j) {
    if (!j.is_object()) {
        return fail("'perception' must be an object");
    }
    PerceptionSettings settings;
    settings.range = readFloat(j, "range", settings.range);
    settings.fieldOfView = readFloat(j, "fieldOfView", settings.fieldOfView);
    settings.proximityRange = readFloat(j, "proximityRange", settings.proximityRange);
    settings.hertz = readFloat(j, "hertz", settings.hertz);
    settings.occlusionTestsPerSecond =
        readFloat(j, "occlusionTestsPerSecond", settings.occlusionTestsPerSecond);
    if (j.contains("capacity") && j["capacity"].is_number()) {
        const double capacity = j["capacity"].get<double>();
        if (capacity < 1.0 || capacity > 64.0) {
            return fail("perception: 'capacity' ({}) must be between 1 and 64", capacity);
        }
        settings.capacity = static_cast<std::uint16_t>(capacity);
    }
    if (j.contains("weights")) {
        if (!j["weights"].is_object()) {
            return fail("perception: 'weights' must be an object keyed by interest kind");
        }
        for (const auto& [key, value] : j["weights"].items()) {
            const auto found = std::find(kWeightNames.begin(), kWeightNames.end(), key);
            if (found == kWeightNames.end()) {
                return fail("perception: unknown interest kind '{}' in 'weights'", key);
            }
            if (!value.is_number()) {
                return fail("perception: weight '{}' must be a number", key);
            }
            settings.weight[static_cast<std::size_t>(found - kWeightNames.begin())] =
                value.get<float>();
        }
    }
    // A range of zero is a character that perceives nothing, which is what *not declaring*
    // perception already says and says more clearly. Refused here rather than discovered as a
    // character that never notices anything.
    if (!(settings.range > 0.0f)) {
        return fail("perception: 'range' ({}) must be above zero", settings.range);
    }
    if (settings.hertz < 0.0f) {
        return fail("perception: 'hertz' ({}) must not be negative", settings.hertz);
    }
    if (settings.occlusionTestsPerSecond < 0.0f) {
        return fail("perception: 'occlusionTestsPerSecond' ({}) must not be negative",
                    settings.occlusionTestsPerSecond);
    }
    return settings;
}

nlohmann::json perceptionToJson(const PerceptionSettings& settings) {
    nlohmann::json j = nlohmann::json::object();
    j["range"] = settings.range;
    j["fieldOfView"] = settings.fieldOfView;
    j["proximityRange"] = settings.proximityRange;
    j["capacity"] = settings.capacity;
    j["hertz"] = settings.hertz;
    j["occlusionTestsPerSecond"] = settings.occlusionTestsPerSecond;
    // Written only when it is not the default, so an entity that took the stock taste model
    // round-trips as the two lines the author wrote rather than as seven.
    const PerceptionSettings defaults;
    nlohmann::json weights = nlohmann::json::object();
    for (std::size_t i = 0; i < kWeightNames.size(); ++i) {
        if (settings.weight[i] != defaults.weight[i]) {
            weights[std::string(kWeightNames[i])] = settings.weight[i];
        }
    }
    if (!weights.empty()) {
        j["weights"] = std::move(weights);
    }
    return j;
}

} // namespace avgen::entity
