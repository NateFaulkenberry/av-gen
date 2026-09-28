#pragma once

// The authored cut as a focus schedule (ADR-947).
//
// `HeroFocus` and `CameraTravel` effects gate on `world::ShotSpan`s: which subject the cut is holding
// and when the camera is on its way to another (ADR-207). Until ADR-947 the only thing that wrote
// those spans was the Auto-director's bake (`app::Sequence::shotSpans`, installed by
// `installSequence`), so a film cut by hand on the camera track -- `cameraDirection.shots`, cameras
// with aim and follow nodes, which is how Glowmere Valley 3 is cut -- gave both activations nothing
// and every Hero Pulse and Travel Beam in it stayed dark.
//
// This is the second source. It reads the camera track the way the director reads it
// (`resolveActiveCamera`: the last shot containing the instant whose camera exists, else the default
// camera; events aside) and says, for each stretch of the timeline, who the camera is about:
//
//   * **A shot's subject** is, first, the shot's own `subject` ("Focuses on" on the camera track),
//     then its camera's aim node, then its camera's follow node, then nobody. The aim-then-follow
//     order is the Creative Critic adapter's (`directing_evaluate.cpp`, `shotsDocument`) and GV3's
//     framing tool's (`tools/gv3/framing.py`: `rig.aim or rig.follow`), so the three agree on who a
//     shot is about. The main camera has no subject: its placement is the legacy `camera/*` block.
//   * **A shot with a subject is a hold** (`spotlight`) for its whole span, so the subject's Hero
//     Pulse fires from the moment the cut lands on it until the cut leaves.
//   * **A cut is a camera change.** Where the live camera changes and the incoming shot has a
//     subject, a travel span opens at the cut, from the outgoing subject to the incoming one (the
//     `handoff`), for `kCutTravelSeconds` or the shot's blend if that is longer, and never past the
//     incoming shot's end. It overlaps the incoming hold on purpose: the beam sweeps toward the hero
//     while that hero's pulse starts.
//
// Pure: a function of the camera track alone, so a seek lands on the same span -- and so the same
// pulse phase -- as a play (ADR-089/091). Event cameras (a scenario taking the frame) are not part
// of it, because which events have run is recorded state and the schedule has to be known before
// the frame is.

#include "scene/camera_rig.hpp"
#include "world/effects/effect_timing.hpp"

#include <functional>
#include <string_view>
#include <vector>

namespace avgen::scene {

// How long a cut counts as "the camera travelling" for a `CameraTravel` effect: long enough for the
// Travel Beam's default delay, fade in and fade out (0.15 + 0.5 + 1.1 s) to play out. A blend
// longer than this travels for the blend. The beam's own `lifetime` shortens it.
inline constexpr double kCutTravelSeconds = 2.5;

// Who a camera is about: its aim node, else its follow node, else nobody (and never the main camera).
[[nodiscard]] std::string_view cameraSubject(const CameraRig& rig);

// Who a shot is about: the shot's own `subject` when it names one, else its camera's subject.
[[nodiscard]] std::string_view shotSubject(const CameraDirection& direction, const CameraShot& shot);

// Where a subject stands and how big it is, for the span's fallback position (a `FocusHero` endpoint
// uses the live hero or node first). False leaves the position at the origin.
using SubjectLocator = std::function<bool(std::string_view name, glm::vec3& position, float& radius)>;

// The authored cut as `world::ShotSpan`s, in time order (holds and travels interleaved, travels
// overlapping the start of the hold they hand off to). Empty when the track has no shots.
[[nodiscard]] std::vector<world::ShotSpan> authoredShotSpans(const CameraDirection& direction,
                                                             const SubjectLocator& locate = {});

} // namespace avgen::scene
