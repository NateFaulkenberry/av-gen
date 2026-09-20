#pragma once

// The Lights panel's decisions, separated from its drawing.
//
// Where a new light goes, what a duplicate is called, and which parameter paths a light of a given
// type actually has are all questions with exact answers that do not need a window to ask. They are
// asked here and checked in tests/unit/test_lights_panel.cpp; `lights_panel.cpp` draws the answers.
//
// The path list is the important one. A hand-written panel asks for a parameter **by string**, so a
// wrong path does not fail to compile and does not throw -- the row simply does not draw, and the
// section degrades into an empty box indistinguishable from "this light has no such control"
// (ADR-375). Having the panel and the test read the same list is what makes the test able to fail.

#include "scene/composition.hpp"
#include "scene/scene_types.hpp"

#include <glm/glm.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace avgen::ui {

// The parameter path prefix the panel asks under, ending in '/'.
//
// **This exists so the panel and its test can do the same arithmetic.** A hand-written panel asks
// for a parameter by string, so a prefix computed one way here and asserted another way in a test
// proves only that the parameters exist -- it says nothing about whether the UI can reach them, and
// both would keep passing while every row silently failed to draw. That is exactly the defect the
// owner found in the Tree panel, where a wrongly computed prefix made two whole sections render
// as an empty box indistinguishable from a scene without the feature.
//
// So there is one function, the panel calls it, and the test calls it.
[[nodiscard]] std::string lightParameterBase(const scene::Composition::AuthoredLight& light);

// Every parameter leaf a light of `type` registers, in the order the panel shows them. Mirrors
// `Composition::registerAuthoredLightParameters`: a directional light has no `range`, only a spot
// has cone angles, only an area kind has an extent.
[[nodiscard]] std::vector<std::string_view> lightParameterLeaves(scene::PunctualLight::Type type);

// Where a new light goes.
//
// Not the world origin. A light created at zero in a scene the camera is looking at from 200 m away
// is a light the user has to go and find, and the brief says so in as many words. It is placed a
// little in front of the editor camera and, for the kinds that aim, pointed the way the camera is
// looking -- so "add a spot" puts a spot on whatever you were looking at when you asked for one.
//
// `forward` need not be normalised; a degenerate one falls back to -Z rather than producing a NaN
// direction that `setAuthoredLights` would then refuse.
struct LightPlacement {
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};
[[nodiscard]] LightPlacement placeNewLight(const glm::vec3& cameraPosition, const glm::vec3& cameraTarget,
                                           scene::PunctualLight::Type type);

// A display name not already in `taken`. "Key Light", then "Key Light 2". Distinct from
// `Composition::uniqueAuthoredLightId`, which suffixes an id: a user sees the name and the file
// carries the id, and the two only coincide until somebody renames one.
[[nodiscard]] std::string uniqueLightName(std::string_view desired, const std::vector<std::string>& taken);

// The name a duplicate takes: "Key Light" -> "Key Light Copy", then "Key Light Copy 2".
[[nodiscard]] std::string duplicateLightName(std::string_view source, const std::vector<std::string>& taken);

// A sensible new light of `type`, placed and named. The defaults are the ones that make a light
// visible the moment it is created -- a spot at intensity 1 aimed into fog is a button that appears
// to do nothing, which ADR-375 calls out as worse than no button.
[[nodiscard]] scene::Composition::AuthoredLight makeLight(scene::PunctualLight::Type type,
                                                          const LightPlacement& placement,
                                                          std::string name, std::string id);

// The direction a light at `from` must travel to point at `at`, or nullopt when they coincide.
// Shared by "Aim at Selection" and the same question asked of a camera.
[[nodiscard]] bool aimDirection(const glm::vec3& from, const glm::vec3& at, glm::vec3& out);

// The light types the panel offers, in menu order.
[[nodiscard]] std::vector<scene::PunctualLight::Type> creatableLightTypes();

} // namespace avgen::ui
