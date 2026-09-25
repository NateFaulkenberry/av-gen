#pragma once

// Everything the validator and the compiler read about the scene, as one value (ADR-756).
//
// It is a **staging copy** (spec §20): the sequence and the camera collection are copies, so a dry
// run compiles into them and the person's project is untouched until an apply installs them. It is
// built by the host (`app::sceneFactsFor`); this module never reads the engine (ADR-750).

#include "directing/capabilities.hpp"
#include "directing/plan.hpp"
#include "directing/resolver.hpp"
#include "directing/time_ref.hpp"
#include "scene/camera_rig.hpp"
#include "seq/sequence.hpp"
#include "world/effects/effect_instance.hpp"

#include <glm/glm.hpp>

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace avgen::directing {

// Where a subject is, when that is a fact rather than a simulation's current state. Heroes and nodes
// have authored positions; an entity's position is wherever its simulation has taken it, so it has
// none here -- anything that needs one must go through the entity (a follow rig, a performance).
struct Place {
    SubjectKind kind = SubjectKind::Unresolved;
    std::string id;
    glm::vec3 position{0.0f};
    float radius = 1.0f;  // how big it is, for framing
    float height = 0.0f;  // how tall, for clearance; 0 = unknown
    float preferredCameraDistance = 0.0f;
};

// The content a plan compiles into: copies of the sequence, the camera collection and the effect
// list. What `produced` refers to, and what an apply installs.
struct Staging {
    seq::Sequence sequence;
    scene::CameraDirection cameras;
    std::vector<world::EffectInstance> effects; // captured: every slider's base in the instance
};

// A character's authored mark: where the scene file put it (its node's BASE position), never where
// its simulation has taken it. The only character position a plan may use (ADR-758): a performance's
// stage mark is placed from this and from places, so it is the same whenever it is compiled.
struct CharacterMark {
    std::string id;   // the entity
    std::string node; // the node it drives
    glm::vec3 anchor{0.0f};
};

struct SceneFacts {
    CapabilityRegistry capabilities;
    // The ground's height at a world x, z: the terrain's surface (water included, as the performer
    // stands on it). Authored geometry, so a plan-time fact. Null when the scene has no terrain; a
    // jump then cannot be checked or compiled, and says so. Holds a copy of the terrain query, so it
    // is valid only while the composition it came from is.
    std::function<float(float x, float z)> groundAt;
    // Whether a walker can get from one ground point to another (the entity world's own path
    // provider: what a goal's walk will ask). Nothing when it cannot say yet (a planner still
    // working) or the scene has no navigation.
    std::function<std::optional<bool>(glm::vec2 from, glm::vec2 to)> walkable;
    SubjectIndex subjects;
    MusicalContext music;
    std::vector<Place> places;
    std::vector<CharacterMark> characters;
    Staging staged;                   // copies of what exists now; a dry run compiles into these
    std::vector<Plan> plans;          // the project's plans, for revisions and provenance
    // Parameters the author has keys on (timeline tracks the sequence does not own). A baked cue
    // takes ownership of its parameter at install and would erase them (ADR-752's finding).
    std::vector<std::string> authorTrackTargets;
    // Every parameter's BASE value, by path. A cue that sets a value compiles as an Add of the
    // difference from this, because an Add's identity is known and an absolute Replace has nothing to
    // ramp from or return to (the bake says so, and it held the value from t = 0).
    std::vector<std::pair<std::string, std::vector<float>>> parameterBases;
    [[nodiscard]] const std::vector<float>* base(std::string_view path) const;

    [[nodiscard]] const Place* place(std::string_view id) const;
    [[nodiscard]] const CharacterMark* character(std::string_view id) const;
    [[nodiscard]] bool hasParameter(std::string_view path) const;
    [[nodiscard]] const Plan* plan(std::string_view id) const;
};

} // namespace avgen::directing
