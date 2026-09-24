#pragma once

// When an effect exists, and what it can ask the world (ADR-207, ADR-230, ADR-702).
//
// Every effect type shares these, whatever technique draws it: the activation that gates it on the
// director's cut, the delay/fade/lifetime/repeat envelope, the shot schedule it gates against, and
// the one context the evaluator hands every type each frame. They lived in ADR-207's surface-wave
// header until ADR-702, and ADR-230's sky effects reached into that header for them -- which is
// the clearest sign they were never the waves' to own.

#include "core/error.hpp"
#include "world/hero.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace avgen::world::fields {
class FieldBus;
}

namespace avgen::world {

// When the effect exists at all. §15 of the brief: an effect that is permanently on is scenery, and
// the two shipped effects are both *events* -- one belongs to a camera move, one to a held subject.
enum class Activation : std::uint8_t {
    // The window is open for the whole timeline. Note what that does and does not mean: the *front*
    // still makes one pass from t = 0 and is then past its range, so a permanently-visible effect is
    // one with a `repeatSeconds`, not one with `Always`.
    Always,
    Window,       // an authored [start, start + seconds) on the transport clock
    CameraTravel, // while the director's cut says the camera is travelling between subjects
    HeroFocus,    // while the director's cut is spotlighting this effect's source
};
[[nodiscard]] const char* activationName(Activation a);
[[nodiscard]] std::optional<Activation> activationFromName(std::string_view name);

// Procedural sparkle around the leading edge. Cells are static in world space and each cell's
// brightness is a smooth function of how near the front is, so the pattern does not crawl when the
// camera moves -- which is the failure mode §8 names. The distance fade is the anti-aliasing: a cell
// smaller than a pixel is faded out rather than sampled.
struct Sparkle {
    bool enabled = false;
    float density = 1.1f;     // cells per metre; coarse cells read as blobs, not as sparkle
    float size = 0.22f;       // 0..1 of a cell
    float intensity = 1.6f;
    float speed = 0.6f;       // twinkle rate, in cycles per second, off the effect's own clock
    float fadeDistance = 85.0f; // metres at which sparkle is gone, so it cannot alias at range
    std::uint32_t seed = 1;
    [[nodiscard]] Result<void> validate() const;
};

struct Timing {
    double delay = 0.0;      // seconds after activation before the front starts
    double lifetime = 0.0;   // seconds the effect lives; 0 = as long as its activation lasts
    double fadeIn = 0.35;
    double fadeOut = 0.9;
    double windowStart = 0.0;    // Activation::Window
    double windowSeconds = 6.0;  // Activation::Window
    // Restart the front every this many seconds while the activation holds. 0 = one pass. What makes
    // a hero pulse a *pulse* rather than a single expanding ring -- and what a beat route modulates
    // when somebody wants one ring per bar.
    double repeatSeconds = 0.0;
    [[nodiscard]] Result<void> validate() const;
};


// One span of the director's cut, flattened to what an effect needs to know. Baked from an
// `app::Sequence` when the camera is directed (ADR-075) and empty otherwise, in which case
// `CameraTravel` and `HeroFocus` effects simply never activate -- which is the honest answer for a
// camera nobody is directing.
struct ShotSpan {
    double start = 0.0;
    double end = 0.0;
    bool travel = false;     // the camera is moving from one subject to another
    // The camera has landed: this shot is not travelling and it is about something. Deliberately a
    // geometric fact rather than the director's own `Spotlight::emphasis`, which is how much of the
    // *film* a subject owns and is zero for a whole intro. An effect gated on "the camera is on this
    // hero" wants the former; `emphasis` below is there for anything that wants the latter.
    bool spotlight = false;
    float emphasis = 0.0f;   // 0..1, the director's own weighting of this subject
    std::string subject;     // who the shot is about
    glm::vec3 subjectPosition{0.0f};
    float subjectRadius = 1.0f;
    std::string handoff;     // for a travel shot, who it is going to
    glm::vec3 handoffPosition{0.0f};
};

// The activation window an effect is inside at `seconds`, or nothing.
//
// Exported rather than kept private because ADR-230's atmospheric effects reuse `Activation` and
// `Timing` outright, and two copies of the gating rule would be two places for "why did my effect
// not fire" to have different answers.
struct ActivationWindow {
    double start = 0.0;
    double end = 0.0;
    const ShotSpan* span = nullptr;
};

// `followsFocus` is a source that rides whatever the cut is spotlighting (ADR-207's
// `SourceKind::FocusHero`); `subject` names the one hero a `HeroFocus` effect fires for, empty
// meaning any. Both are ignored by every activation except `HeroFocus`.
[[nodiscard]] std::optional<ActivationWindow> resolveActivationWindow(
    Activation activation, const Timing& timing, double seconds, std::span<const ShotSpan> shots,
    bool followsFocus = true, std::string_view subject = {});

// The ramp both effect families fade with: a smoothstep over `width` seconds, guarding the
// degenerate width that would otherwise divide by zero and put a hard edge exactly where §7 of
// ADR-207's brief forbids one.
[[nodiscard]] float envelopeRamp(float x, float width);

// Delay, fade-in, lifetime and fade-out multiplied together; 0 when the effect is not alive at
// `local` seconds into its window. `windowLength` is the activation's own length, which is what a
// `lifetime` of 0 means "as long as".
[[nodiscard]] float timingEnvelope(const Timing& timing, double local, double windowLength);

// Where the world's nodes are. An interface rather than a std::function so resolution allocates
// nothing: the engine counts allocations per frame and a lambda capture in this path would show up.
class EffectSceneQuery {
public:
    virtual ~EffectSceneQuery() = default;
    // World position of the node named `name`, or false when there is no such node.
    [[nodiscard]] virtual bool nodePosition(std::string_view name, glm::vec3& out) const = 0;
    // The node's forward axis in world space, for DirectionMode::SourceForward. Optional: a scene
    // that cannot answer returns false and the direction falls back to the next mode in the chain.
    [[nodiscard]] virtual bool nodeForward(std::string_view name, glm::vec3& out) const { (void)name; (void)out; return false; }
};

// Everything an effect may read on a frame, for every type (ADR-702 merged ADR-207's and ADR-230's
// two contexts, which overlapped in the clock, the camera and the cut). All of it is a function of
// the transport second, which is what keeps an offline render of second N equal to a realtime
// playthrough of second N (ADR-091).
struct EffectContext {
    double seconds = 0.0;             // the transport clock, and the only clock
    glm::vec3 cameraPosition{0.0f};
    glm::vec3 cameraTarget{0.0f, 0.0f, -1.0f};
    glm::vec3 cameraForward{0.0f, 0.0f, -1.0f};
    // Metres per second, as a finite difference **in timeline seconds**: taking it from the frame
    // delta would make a travel beam point somewhere different at 30 fps than at 120.
    glm::vec3 cameraVelocity{0.0f};
    std::span<const ShotSpan> shots;
    std::span<const HeroPoint> heroes;
    const EffectSceneQuery* scene = nullptr;
    // ADR-230's one deliberate exception to "audio arrives through a route": an aurora's curtain is
    // shaped by a spectrum, which a scalar route cannot carry. Filled from the same analysis frame
    // every other consumer reads.
    std::span<const float> spectrum;
    const fields::FieldBus* fieldBus = nullptr; // ADR-420 §68
};

} // namespace avgen::world
