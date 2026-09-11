#pragma once

// The tools (ADR-094). Every one of them drives a real engine system; there is no shim, no fake
// object model and no operation that pretends to work.
//
// ## Why this pass is built on `params::ParameterSet`
//
// Because that is where the engine's semantics already live. A composition node's position,
// rotation, scale, visibility, emissive boost and roughness are parameters. So are the camera's
// pose, mode and field of view; the sky's zenith, horizon, sun colour, sun intensity and haze;
// fog density, height and scattering; wind speed and direction; a light rig's per-light intensity,
// azimuth, elevation and colour temperature; every procedural material part's tint, emissive
// colour and gain. They have paths, types, hard ranges, soft ranges, groups and labels, and they
// are already keyframeable by the timeline, already modulatable by audio, already serialised, and
// already the thing the UI edits.
//
// So a parameter-level tool API is not a lowest common denominator. It is the *actual* semantic
// interface of this engine, and reaching most of §14-§19's intent through it costs no new engine
// abstraction and depends on nothing currently in flux.
//
// ## The three failure modes this file is written against
//
// This repository has shipped features that were correct in every respect except that they did
// nothing. Three of those failures are reachable from here, and each has a guard:
//
//   1. **A timeline track naming an unknown parameter binds to nothing, silently.**
//      `sequencer.add_keyframe` refuses a target that does not exist, and reports the timeline's
//      `unboundTargets()` on every read of sequencer state.
//
//   2. **A `Replace`-mode track or route overwrites a base value every frame**, so setting the
//      base appears to do nothing. `parameter.set` checks for that and returns the change *plus*
//      a conflict note naming what is overriding it.
//
//   3. **`camera/position` is ignored in orbit mode.** `camera.set` switches the mode and says so,
//      rather than writing two vectors nothing reads.
//
// Every mutating tool reports the value the engine *actually holds afterwards*, read back, not the
// value it was asked for.

#include "ai/tool_api.hpp"

namespace avgen::ai {

// Registers the whole tool surface. One call, so a caller cannot get half of it, and so the list
// has exactly one definition site (§24: do not duplicate tool descriptions).
void registerEngineTools(ToolRegistry& registry);

} // namespace avgen::ai
