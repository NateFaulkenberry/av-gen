#pragma once

// A body off the ground (ADR-194).
//
// Everything else in this layer assumes a walker is standing on something. `GroundFollower` exists
// to guarantee it -- "a body is never below the ground it stands on", `maxSink = 0` -- and the whole
// gait machine reads a horizontal speed and picks between standing, walking and running. There was
// no way to express *not standing on anything*, so a character could not cross a gap it could
// obviously clear, and `Jumping`, `Jump_running`, `Fall_loop` and `Landing` were clips nothing could
// ever ask for.
//
// A component rather than a behaviour, for the same reason `GroundFollower` is one and stated in the
// same words: `explore` uses it, and a future vehicle or a keyframed character can use it without
// inheriting a walk cycle.
//
// **Code-driven, not root motion.** The clips are authored in place -- measured, not assumed: the
// net root translation of `Walking`, `Running` and `Jumping` is exactly zero over the cycle, so
// there is no forward motion in them to extract. The arc here is the movement; the clip is what it
// looks like. `Jumping` does carry 0.87 of *vertical* root range, which is the crouch and the
// extension, and that rides on top of this arc rather than duplicating it.
//
// Deterministic: a pure function of the launch and the dt sequence. No clock, no rng, nothing
// remembered across a seek that `reset()` does not clear -- an offline render of the same second
// produces the same arc.

#include "entity/locomotion.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <optional>

namespace avgen::entity {

class Navigator;

struct JumpSettings {
    // Stylised, not physical. 9.81 gives a floaty, slow-motion hop at the scale characters are
    // built at here, and every game that has ever looked right about this uses a larger number.
    float gravity = 18.0f;
    // How high above the take-off point the arc peaks. The horizontal speed follows from this and
    // the distance, rather than being set independently -- two of the three are free and the third
    // is arithmetic, and letting an author set all three is letting them set an impossible hop.
    float apex = 1.1f;
    // The longest hop the body will attempt, in metres. A body that tries to clear anything can
    // launch itself across a lake and land in it.
    float maxDistance = 4.0f;
    // How long `Land` holds after touchdown before the gait resumes. The landing clip needs a
    // moment or the character snaps back to a run the instant its feet touch.
    float landSeconds = 0.3f;
    // Ceiling on air time, as a guard rather than a control: a launch that somehow never finds
    // ground must not leave the character falling for ever.
    float maxSeconds = 3.0f;
    // ADR-822: the highest this body can leap, above its take-off point. 0 means `apex` is also the
    // limit -- the character jumps one way. A capability, read by whoever plans a jump for the body
    // (a director's validator); an autonomous hop always uses `apex`.
    float maxApex = 0.0f;
    [[nodiscard]] float apexLimit() const { return maxApex > 0.0f ? maxApex : apex; }
};

// ---- the arc (ADR-822) --------------------------------------------------------------------------
//
// One pure function for every jump in the engine: an autonomous hop integrates it (`Airborne`), and
// a director plans with it and bakes it into keys. Closed form, not stepped, so a play, a scrub and
// a bake are the same curve to the last bit rather than three integrations that agree to a
// tolerance.
//
// The body travels at a constant horizontal speed, so height is a parabola in the fraction `s` of
// the way across: y(s) = y0 + d s + K s (1 - s), with d the landing's height over the take-off and K
// the arc's curvature. Gravity turns that into time: T = sqrt(2 K / g). The apex h above the take-off
// is (d + K)^2 / (4 K), so an apex and a landing fix K, and with gravity everything else.
struct JumpArc {
    glm::vec3 from{0.0f};
    glm::vec3 to{0.0f};
    float gravity = 18.0f;
    float apex = 0.0f;       // above `from`
    float curvature = 0.0f;  // K
    float duration = 0.0f;   // T: take-off to landing on `to`
    // Where the body is `t` seconds after take-off. Past `duration` the same parabola continues, so
    // a body that finds no ground at `to` keeps falling along the curve it was on.
    [[nodiscard]] glm::vec3 at(float t) const;
    [[nodiscard]] glm::vec3 velocityAt(float t) const;
    // Seconds after take-off at which the body is highest.
    [[nodiscard]] float apexTime() const;
    [[nodiscard]] float horizontalSpeed() const;
};

// The arc from `from` to `to` peaking `apex` above `from`. Nothing when it cannot exist: an apex
// below the landing (a body cannot land higher than it rose), a non-positive gravity, or no
// horizontal distance at all.
[[nodiscard]] std::optional<JumpArc> planJump(glm::vec3 from, glm::vec3 to, float apex, float gravity);

// The smallest apex above `from` at which the arc from `from` to `to` clears a round obstacle --
// centre `centre` (x and z read), footprint radius `radius`, top at world height `top` -- by
// `clearance`. The arc is concave, so it clears the whole footprint when it clears both edges of the
// stretch of path the footprint covers. Nothing when the path does not cross the footprint.
[[nodiscard]] std::optional<float> minimumApex(glm::vec3 from, glm::vec3 to, glm::vec3 centre, float radius,
                                               float top, float clearance);

// What the ground makes of an arc. `groundAt` answers terrain height at a world x, z.
struct ArcCheck {
    bool clear = true;           // nothing between take-off and landing is above the body
    float firstContact = -1.0f;  // seconds after take-off the body first meets the ground, if not clear
    float landingGround = 0.0f;  // the ground under `to`
    float landingError = 0.0f;   // `to.y` minus that ground: > 0 lands in the air, < 0 underground
};
[[nodiscard]] ArcCheck checkArc(const JumpArc& arc, const std::function<float(float x, float z)>& groundAt,
                                float sampleSeconds = 1.0f / 60.0f);

// One body's airborne state.
class Airborne {
public:
    // Clears everything. Call on a teleport or a timeline seek -- otherwise a body that was mid-arc
    // when the playhead moved carries that arc into a second it never jumped in.
    void reset();

    // Begins a hop from `from` towards `to`. False -- and nothing changes -- when the hop is longer
    // than `maxDistance`, when one is already in progress, or when the distance is too small to be
    // worth leaving the ground for.
    [[nodiscard]] bool launch(glm::vec3 from, glm::vec3 to, const JumpSettings& settings);

    // True from launch until the landing recovery has finished.
    [[nodiscard]] bool active() const { return phase_ != Phase::Grounded; }
    // True only while actually off the ground -- `Land` is active but not airborne, which is the
    // distinction grounding cares about: a landing body is standing again.
    [[nodiscard]] bool airborne() const { return phase_ == Phase::Rising || phase_ == Phase::Falling; }

    // What the animation layer should be showing. Meaningless unless `active()`.
    [[nodiscard]] Activity activity() const;

    // Integrates one frame. `nav` answers the ground height the arc lands on; `out` receives the
    // body's world position. Returns false once the hop is over, at which point the caller resumes
    // ordinary locomotion -- and must re-prime its `GroundFollower`, or the body glides to the
    // surface from wherever the arc left it.
    bool update(const Navigator& nav, double dt, glm::vec3& out, const JumpSettings& settings);

    // Where the body is now, valid while `active()`.
    [[nodiscard]] glm::vec3 position() const { return position_; }
    // Seconds since launch, for a diagnostic.
    [[nodiscard]] double elapsed() const { return elapsed_; }

    // The arc being flown, valid while `airborne()`.
    [[nodiscard]] const JumpArc& arc() const { return arc_; }

private:
    enum class Phase : std::uint8_t { Grounded, Rising, Falling, Landing };
    Phase phase_ = Phase::Grounded;
    JumpArc arc_{};
    glm::vec3 position_{0.0f};
    double elapsed_ = 0.0;
    double landedFor_ = 0.0;
};

} // namespace avgen::entity
