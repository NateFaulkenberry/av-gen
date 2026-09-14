#pragma once

// The tree's reaction to music: registered parameters, and the routes that drive them.
//
// AUDIO DRIVES THE WIND, NOT THE TREE. This is the whole architectural decision and everything else
// follows from it. The only audio-to-vegetation link anywhere in this engine today is
// `music.build -> scene/windSpeed` in Glowmere -- audio moves a *field*, and the plants move because
// the field moved. Routing audio at a branch's transform instead would be a shorter path to a worse
// result: the branch would track the signal, which is a level meter with bark on it. Routing it at
// the wind means the signal moves the spring's TARGET and the spring decides how the joint gets
// there, so inertia, overshoot, damping and per-joint phase come free and cannot be switched off by
// a badly chosen route depth. Section 22 of the brief -- "audio should influence physical-looking
// motion rather than directly dictate position" -- is a property of this wiring rather than
// something anyone has to be careful about afterwards.
//
// Everything here is an ordinary `params::ModRoute` onto an ordinary registered parameter. There is
// deliberately no second reactivity system: a route that is not in the route list does not appear in
// the graph editor, the inspector's "why is this moving", or a saved project, and the codebase says
// so where `installHeroReactions` does the same thing.
//
// THE BAND VOCABULARY IS THE ONE THAT EXISTS. The brief's section 21 asks for sub-bass and highs;
// the analyser has five bands -- bass (20-150), lowMid (150-400), mid (400-2k), highMid (2k-6k),
// treble (6k-16k) -- and no sub-bass. The mapping below is written against what is actually
// published on the bus.

#include "params/modulation.hpp"
#include "params/parameter.hpp"
#include "scene/tree_rig.hpp"
#include "scene/tree_scene.hpp"

#include <string>
#include <vector>

namespace avgen::scene {

// Handles onto the registered parameters, so a per-frame read is a pointer dereference rather than
// a string lookup. The same shape `registerProceduralParameters` uses.
struct TreeParameters {
    params::Parameter<float>* windSpeed = nullptr;
    params::Parameter<float>* windGust = nullptr;
    params::Parameter<float>* windFlutter = nullptr;
    params::Parameter<float>* windImpulse = nullptr;
    params::Parameter<glm::vec3>* windDirection = nullptr;
    params::Parameter<float>* foliageEmissive = nullptr;
    params::Parameter<float>* veinEmissive = nullptr;
    params::Parameter<float>* foliageHueDrift = nullptr;
    std::vector<params::IParameter*> all;
    [[nodiscard]] bool bound() const { return windSpeed != nullptr; }
};

// Registers `tree/<name>/...`. Rest values come from `look`, so a scene with no audio and no routes
// renders exactly the authored pose -- which is what makes `op: add` routes safe.
[[nodiscard]] TreeParameters registerTreeParameters(params::ParameterSet& params, const std::string& prefix,
                                                    const TreeLook& look = {});

// The authored mapping, as data. Returned rather than installed so a scene file can override it,
// and so a test can read the depths without running an engine.
//
// Route depths follow the discipline in docs/glowmere-audio.md: every event route between 4% and
// 24% of its target's rest value, `op: add` throughout so silence is the authored pose, and the
// attack/decay times doing most of the work of keeping it musical.
[[nodiscard]] std::vector<params::ModRoute> defaultTreeRoutes(const std::string& prefix);

// Reads this frame's finals into the animator's inputs. `time` is the render clock, not the
// transport: the wind keeps blowing while the transport is parked, which is ADR-102's rule and the
// reason the tree is still alive in an editor with nothing playing.
[[nodiscard]] TreeMotionInputs readTreeMotion(const TreeParameters& parameters, double time);

// Pushes this frame's emissive finals onto the scene's materials. Separate from the motion because
// they are different rates: motion is integrated every frame, colour is a slow envelope, and the
// brief is explicit that the tree should breathe through colour rather than flash with it.
void applyTreeLook(const TreeParameters& parameters, const TreeLook& look, Scene& scene);

} // namespace avgen::scene
