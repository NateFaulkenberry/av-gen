#pragma once

// Skeletal animation (ADR-086): clips, clip sampling, a cross-fading state machine, and the
// SkinnedRig that carries all of it into a Scene.
//
// Determinism. Every time in this header is an absolute second on the engine's timeline
// (core/time.hpp), never an accumulated delta and never a frame count. The player stores *when* a
// state was entered, not how long it has been running, so the pose is a pure function of
//
//     (states, the times they were entered, the blend in flight, now)
//
// and a frame rendered live at a wobbling frame rate is byte-identical to the same frame rendered
// offline at a fixed one. The only thing that mutates the player is an outside decision -- play(),
// restart(), setSpeed() -- and each of those takes the timeline second it happened at.
//
// The seam: whatever drives a character (a behaviour, a timeline cue, a person clicking) calls
// `play(state, now)`; everything below that is arithmetic.

#include "core/time.hpp"
#include "scene/pose_layers.hpp"
#include "scene/root_motion.hpp"
#include "scene/skeleton.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::scene {

struct Scene;

enum class AnimationPath : std::uint8_t { Translation, Rotation, Scale };
// glTF 2.0's three sampler interpolations. CubicSpline keyframes carry three values each --
// in-tangent, value, out-tangent -- which is why `values` is three times `times` for that mode.
enum class Interpolation : std::uint8_t { Linear, Step, CubicSpline };

[[nodiscard]] const char* animationPathName(AnimationPath path);
[[nodiscard]] const char* interpolationName(Interpolation interpolation);

struct AnimationChannel {
    std::uint32_t joint = 0; // index into Skeleton::joints
    AnimationPath path = AnimationPath::Translation;
    Interpolation interpolation = Interpolation::Linear;
    std::vector<float> times;      // ascending, seconds
    std::vector<glm::vec4> values; // translation/scale in xyz; rotation as a quaternion in xyzw
    [[nodiscard]] bool valid() const;
    [[nodiscard]] std::size_t keyCount() const { return times.size(); }
};

struct AnimationClip {
    std::string name;
    // The range the keys actually cover. `start` is the *first* key time across every channel and
    // is not always zero: Blender's glTF exporter writes the frame range it was given, and a take
    // authored on frames 1..32 arrives as keys at 1/30 s .. 32/30 s. Assuming the range began at
    // zero cost every clip in the alien pack 1/30 s of held first pose at the top of every loop --
    // motion no animator authored, and a cycle 3.2% longer than the one in the file. The Mixamo
    // character this engine was built against starts at 0, which is why it was never noticed.
    //
    // `length()` is the playable span and the loop period; `duration` remains the last key time so
    // a clip whose keys start at zero is byte-for-byte what it always was.
    float start = 0.0f;
    float duration = 0.0f; // the last key time across every channel
    std::vector<AnimationChannel> channels;
    [[nodiscard]] float length() const { return duration > start ? duration - start : 0.0f; }
    [[nodiscard]] bool valid() const;
};

// `time` folded into [0, duration). Zero-length clips return 0.
[[nodiscard]] float wrapTime(float time, float duration);

// Samples every channel of `clip` at `time` into `pose`. Channels the clip does not carry leave
// `pose` untouched, so a caller seeds it with the rest pose once and re-samples in place -- which
// is what makes a clip that animates fifty of a rig's sixty joints leave the other ten alone
// instead of collapsing them to the origin.
//
// `time` is clamped to [start, duration]; wrap it yourself for a looping clip. Channels shorter
// than two keys hold their single value.
void sampleClip(const AnimationClip& clip, float time, Pose& pose);

// Index of the clip called `name` in `clips`, or -1. Exporters routinely prefix a clip with the rig
// it came off ("Alien_Low_Green|Walk"), so the part after the last '|' matches too -- an exact match
// first, across every clip, before any short name is considered. One copy of that rule, because
// `SkinnedRig::findClip` and the layer stack both have to obey it and two copies drift.
[[nodiscard]] int findClip(const std::vector<AnimationClip>& clips, std::string_view name);

// ---- the state machine -------------------------------------------------------------------------
//
// Deliberately small. Named states over clips, and how long it takes to get from one to another.
// It holds no conditions, no parameters and no graph: what a character *should* be doing is a
// behaviour's decision, and a system that tried to own both would be in the way of the first
// character that wanted to decide differently.

struct AnimationState {
    std::string name;
    std::uint32_t clip = 0; // index into SkinnedRig::clips
    float speed = 1.0f;     // clip seconds per timeline second
    bool loop = true;
    float blendIn = 0.2f;   // default cross-fade, in seconds, when something enters this state
};

struct AnimationTransition {
    std::string from;   // "" = from any state
    std::string to;
    float blend = 0.2f; // seconds
};

class AnimationPlayer {
public:
    // ---- authoring ----
    void addState(AnimationState state);
    void addTransition(AnimationTransition transition);
    void clear();
    [[nodiscard]] const std::vector<AnimationState>& states() const { return states_; }
    [[nodiscard]] const std::vector<AnimationTransition>& transitions() const { return transitions_; }
    [[nodiscard]] const AnimationState* findState(std::string_view name) const;
    [[nodiscard]] int stateIndex(std::string_view name) const;
    // How long a change from `from` to `to` takes: the first transition that matches (an exact
    // `from`, then a wildcard one), else the target state's own blendIn.
    [[nodiscard]] float blendTimeFor(std::string_view from, std::string_view to) const;

    // ---- driving: the four calls a behaviour makes ----
    // Enters `name` at timeline second `now`, cross-fading from whatever is playing over the
    // transition's blend time. Asking for the state already current is a no-op -- the clip is *not*
    // restarted -- so a behaviour may call this unconditionally every frame. Returns false when
    // there is no state by that name, and changes nothing.
    bool play(std::string_view name, double now);
    // As above with an explicit cross-fade; 0 snaps.
    bool play(std::string_view name, double now, float blendSeconds);
    // Restarts the current state's clock at `now` (a one-shot played again).
    void restart(double now);
    // Changes the current state's rate without a jump: the clock is rebased so the local clip time
    // is continuous across the change.
    void setSpeed(float speed, double now);

    // ---- reading: pure functions of (the above, now) ----
    [[nodiscard]] bool active() const { return current_.state >= 0; }
    [[nodiscard]] std::string_view currentState() const;
    // Index of the state being played, or -1. The name is what a person asks with; an index is
    // what a clip lookup needs, and resolving the name back through `stateIndex` every frame is a
    // string compare per state for an answer the player already has.
    [[nodiscard]] int currentStateIndex() const { return current_.state; }
    // The timeline second the current state was entered at. Not "how long it has been running":
    // this player stores *when*, which is what makes a scrubbed frame reproduce (ADR-086). Root
    // motion reads it to tell "the same clip, later" from "the clip was restarted", which are the
    // same clip index and must not be the same displacement.
    [[nodiscard]] double currentStart() const { return current_.start; }
    // The state being faded out, or "" when the player has settled.
    [[nodiscard]] std::string_view fadingState() const;
    // 0 at the instant of the change, 1 once the cross-fade is over.
    [[nodiscard]] float blendWeight(double now) const;
    [[nodiscard]] bool blending(double now) const { return blendWeight(now) < 1.0f; }
    // Local clip seconds of the current state, wrapped when it loops and clamped when it does not.
    [[nodiscard]] float stateTime(double now) const;
    [[nodiscard]] float stateTime(const std::vector<AnimationClip>& clips, double now) const;
    // True once a non-looping state has run past the end of its clip.
    [[nodiscard]] bool finished(const std::vector<AnimationClip>& clips, double now) const;

    // Writes the pose at `now` into `pose`: the rest pose, then the current state's clip, then --
    // while a cross-fade is in flight -- the outgoing state's clip blended underneath it. `scratch`
    // holds the outgoing pose between calls so a settled player allocates nothing.
    void evaluate(const std::vector<AnimationClip>& clips, const Skeleton& skeleton, double now, Pose& pose,
                  Pose& scratch) const;

private:
    struct Playing {
        int state = -1;
        double start = 0.0; // timeline second this state was entered
        float speed = 1.0f;
    };
    [[nodiscard]] float localTime(const Playing& playing, const std::vector<AnimationClip>& clips,
                                  double now) const;
    void sampleInto(const Playing& playing, const std::vector<AnimationClip>& clips, const Skeleton& skeleton,
                    double now, Pose& pose) const;

    std::vector<AnimationState> states_;
    std::vector<AnimationTransition> transitions_;
    Playing current_;
    Playing previous_;
    double blendStart_ = 0.0;
    float blendDuration_ = 0.0f;
};

// ---- a rig in a scene ---------------------------------------------------------------------------

// RigId / kInvalidRig live in scene_types.hpp, beside the Entity that names one.

// A skinned character: the rig, the clips it can play, the machine driving it, and the joint
// matrices the renderer uploads. Scene::rigs holds these; Entity::rig names one.
struct SkinnedRig {
    std::string name;
    Skeleton skeleton;
    std::vector<AnimationClip> clips;
    AnimationPlayer player;

    // ---- pose rate -------------------------------------------------------------------------
    // Re-posing costs joints x instances of CPU, every frame, for characters that may be forty
    // metres away. `updateHz` samples the player on a fixed grid -- floor(t * hz) / hz -- rather
    // than at the frame's own time, so the rate is a property of the *timeline*: the same rig at
    // 20 Hz produces identical matrices in a 60 fps window and in a 24 fps offline render, which a
    // frame-skip counter could never promise.
    float updateHz = 0.0f;  // 0 = every frame
    bool enabled = true;    // false = the palette is left exactly as it is

    // Distance policy, applied by updateRigs() from the scene camera to the rig's own entities.
    // Set `cullDistance` to 0 to opt a hero out of all of it.
    float nearDistance = 15.0f;   // nearer than this: posed every frame
    float farHz = 20.0f;          // between nearDistance and cullDistance: this rate
    float cullDistance = 120.0f;  // beyond this, or with no authored-visible entity: not posed

    // ---- layers (ADR-300) ------------------------------------------------------------------
    // What goes on top of the clip the player is playing: a head turn, a masked additive reaction.
    // Evaluated inside `evaluate()`, between the player and the palette, so `pose` is always the
    // *final* pose -- which is what makes `ISkeletonQuery::jointTransform` (ADR-274) answer with a
    // joint the layers have moved rather than one they have not. A socket on a head that is looking
    // somewhere follows the look.
    PoseLayerStack layers;
    // What the last `evaluate()` spent on them. Structural quantities, never a millisecond
    // (ADR-170): a layer that reported 0 joints is a layer whose mask missed.
    PoseLayerStats layerStats;

    // ---- root motion (ADR-335) -------------------------------------------------------------
    // Which of this rig's clips hand their root displacement to the simulation instead of drawing
    // it. Empty on every rig that does not author one, and an empty set is checked with one
    // `bindings_.empty()` before anything else happens -- which is how the other 163 clips stay
    // bit-identical rather than being told they are.
    //
    // The seam runs the *opposite* way to the layer stack (ADR-300 §8). A layer writes the pose
    // and can reach nothing else; root motion writes the pose **and** is read back out into the
    // entity, which adds it to `EntityState::travel`. That is `MotionAuthority::Simulation`, and
    // it is the only place in the animation system that has it.
    RootMotionSet rootMotion;
    // Whether the last `evaluate()` actually took a displacement out of the pose. False on a rig
    // with an opt-in that is playing something else, which is the case worth being able to tell
    // apart from "no opt-in at all" -- the same distinction `SocketResolution` exists for.
    bool rootMotionApplied = false;

    // ---- evaluated -------------------------------------------------------------------------
    Pose pose;                              // the local pose the player produced, then layered
    std::vector<glm::mat4> palette;         // paletteSize() joint matrices; what the GPU reads
    std::vector<glm::mat4> previousPalette; // the palette this rig was drawn with last frame
    double paletteTime = -1.0;              // the timeline second `palette` was evaluated at
    std::uint64_t paletteVersion = 0;       // bumped whenever `palette` changes
    // Set by a transport discontinuity, consumed by the next `evaluate`.
    //
    // `previousPalette` means "where these joints were a frame ago", and the velocity target and
    // motion blur believe it. Across a seek that sentence is false: the joints were not anywhere a
    // frame ago, because there was no previous frame at this position. Without this flag a scrub to
    // a new second left all 49 joints holding the pose from before the jump -- measured at 71.5
    // model units on a 100-unit character -- and `pbr_skinned.wgsl` dutifully drew a motion vector
    // for a movement nobody made, smearing the first frame after every scrub.
    //
    // A flag rather than a collapse at seek time, because at seek time the rig has not been re-posed
    // yet: setting `previousPalette = palette` there would pin the *old* pose, and the next evaluate
    // would copy it forward and reintroduce exactly the jump. The next evaluate has to take its
    // "previous" from the pose it lands on rather than the one it left.
    bool reseedPrevious = false;

    // Scratch, kept so a per-frame evaluation allocates nothing.
    Pose scratchPose;
    std::vector<glm::mat4> scratchModel;
    // And root motion's, which is `mutable` because `rootMotionAt` is a question about the clips
    // rather than about the rig's current pose and has to be answerable through a `const&` -- the
    // animation sink holds the scene const while an entity asks it where its own body went.
    mutable Pose rootMotionScratch;

    // Index of the clip called `name`, or -1. Exporters routinely prefix a clip with the rig it
    // came off ("Alien_Low_Green|Walk"), so the part after the last '|' matches too: the file keeps
    // its own name and a scene may ask for "Walk".
    [[nodiscard]] int findClip(std::string_view clipName) const;
    // The root displacement this rig's current clip has produced by timeline second `now`,
    // measured from that clip's own first key, in the rig's model space (ADR-274: that is the
    // entity's own frame, and the entity composes its placement and its scale on).
    //
    // A **pure function of (the player, the clips, now)** and deliberately not a read of `pose`.
    // That is what dissolves the ordering problem ADR-300 §8 left for this unit: `EntityWorld::
    // update` runs one whole stage before `Composition::updateCharacters` poses the rigs, so an
    // entity that had to read a posed rig would always be reading last frame's. It does not have
    // to. The pose is derived from the clip; so is this; both can be taken at the same second from
    // opposite sides of the frame and agree exactly.
    [[nodiscard]] RootMotionSample rootMotionAt(double now) const;
    [[nodiscard]] bool valid() const { return skeleton.valid(); }
    // Gives the rig one state per clip, named after the clip's short name, and makes the first the
    // current one. What an imported file gets before anybody says otherwise.
    void addDefaultStates(float blendSeconds = 0.2f);
    // Re-poses at `now` and refreshes the palette; `hz` <= 0 means every frame. `previousPalette`
    // always ends up holding what the rig was last drawn with, so a rig that did not move this
    // frame reports no motion rather than a stale frame of it. Returns true when it re-posed.
    bool evaluate(double now, float hz = 0.0f);
    // Declares a transport discontinuity: the next `evaluate` reports no motion rather than motion
    // across the jump. Idempotent, and cheap enough to call on every rig at every seek.
    void reseedAfterDiscontinuity() { reseedPrevious = true; }
    // Keeps the palette exactly as it is and marks it unmoved. What a culled rig gets.
    void hold();
    // The timeline second the player is sampled at for frame time `now` at rate `hz`: a fixed grid,
    // so the rate belongs to the timeline rather than to the frame rate.
    [[nodiscard]] static double sampleTime(double now, float hz);
    // The pose rate this rig wants at `distance` metres from the camera: 0 = every frame, negative
    // = do not pose at all. The whole distance policy, in one testable function.
    [[nodiscard]] float rateFor(float distance) const;
};

struct RigStats {
    std::uint32_t rigs = 0;      // rigs in the scene
    std::uint32_t posed = 0;     // re-posed this frame
    std::uint32_t rateLimited = 0; // enabled, in range, but not due a new pose this frame
    std::uint32_t culled = 0;    // too far away, or with no authored-visible entity
    std::uint32_t joints = 0;    // joint matrices recomputed this frame
    std::uint32_t layers = 0;      // ADR-300: layers evaluated this frame, over every posed rig
    std::uint32_t layerJoints = 0; // joints those layers wrote
    // ADR-335: posed rigs whose current clip was opted in to root motion and had its displacement
    // taken out of the pose this frame. Zero on every scene in this repository but the Character
    // Intelligence Lab, which is the number that says the opt-in is an opt-in.
    std::uint32_t rootMotion = 0;
    double cpuMs = 0.0;          // wall time spent posing (this is the only clock in here, and it
                                 // reports, it never drives)
};

// Advances every rig in `scene` to `time.renderTime`, applying each rig's distance policy against
// the scene camera and its authored-visible entities. Camera-frustum culling is deliberately not
// included: it suppresses drawing, but must not freeze a timeline pose. The scene renderer never calls this: posing is the
// controller's business, so the renderer keeps taking a const Scene& and a frame cannot be made to
// look different by rendering it twice.
RigStats updateRigs(Scene& scene, const FrameTime& time);

} // namespace avgen::scene
