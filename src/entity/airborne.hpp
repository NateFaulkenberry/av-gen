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
};

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

private:
    enum class Phase : std::uint8_t { Grounded, Rising, Falling, Landing };
    Phase phase_ = Phase::Grounded;
    glm::vec3 position_{0.0f};
    glm::vec3 velocity_{0.0f};
    double elapsed_ = 0.0;
    double landedFor_ = 0.0;
};

} // namespace avgen::entity
