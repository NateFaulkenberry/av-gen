#pragma once

// Skeletal rigs (ADR-086): the joint hierarchy, a pose, and the joint-matrix palette the skinned
// vertex stage reads. Pure data and arithmetic -- no GPU, no clock, and no allocation per frame
// once the caller's buffers are sized.
//
// `joints` is the glTF node subtree that drives a skin: every joint the skin names *and every
// ancestor of one*, topologically ordered so a parent always precedes its children. The ancestor
// set is not decoration. A Mixamo-derived file animates a wrapper node sitting above the hips, and
// a rig holding only the skin's own joints drops that channel and leaves the character rooted to
// the origin while its limbs move.
//
// `palette` maps the mesh's JOINTS_0 indices into `joints`, with the matching inverse bind
// matrices beside it. Palette entry k is modelMatrix[palette[k]] * inverseBind[k], which is what
// the GPU multiplies a bind-pose vertex by.
//
// glTF puts the whole chain from the scene root into the joints' own transforms, so the model
// space these matrices land in *is* the file's scene space, and the entity that carries the skin
// contributes only its placement in the world (its own node transform is ignored on import, as
// the glTF specification requires).

#include "scene/scene_types.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace avgen::scene {

// The most joints one palette may hold. A slice of the joint buffer is sized from the largest rig
// in the scene, so this is a ceiling on that, not a per-rig cost: the alien uses 49.
constexpr std::uint32_t kMaxPaletteJoints = 256;
// Influences per vertex. glTF allows more through JOINTS_1/WEIGHTS_1; this engine takes the first
// set only and says so in the import warnings.
constexpr std::uint32_t kJointInfluences = 4;

struct Joint {
    std::string name;
    int parent = -1; // index into Skeleton::joints, always less than this joint's own index; -1 = root
    Transform rest;  // the node's local transform in the file's rest pose
};

struct Skeleton {
    std::string name;
    std::vector<Joint> joints;            // parents before children
    std::vector<std::uint32_t> palette;   // indices into `joints`, in the mesh's JOINTS_0 order
    std::vector<glm::mat4> inverseBind;   // parallel to `palette`

    // A skeleton is valid when every parent precedes its child, every palette entry names a real
    // joint, the inverse binds are parallel to the palette, and the palette fits kMaxPaletteJoints.
    [[nodiscard]] bool valid() const;
    [[nodiscard]] std::size_t jointCount() const { return joints.size(); }
    [[nodiscard]] std::size_t paletteSize() const { return palette.size(); }
    // Index of the joint named `name`, or -1. Linear: a rig has tens of joints, and this is called
    // when something is wired up, not once per frame.
    [[nodiscard]] int find(std::string_view jointName) const;
};

// One local transform per joint. Model-space matrices are derived from it, never stored in it.
struct Pose {
    std::vector<Transform> local;
    [[nodiscard]] bool empty() const { return local.empty(); }
    [[nodiscard]] std::size_t size() const { return local.size(); }
};

// The rest pose: every joint at its bind local transform.
[[nodiscard]] Pose restPose(const Skeleton& skeleton);
// Overwrites `pose` with the rest pose, resizing it. Allocates nothing when already the right size,
// which is the point: a per-frame evaluation re-seeds the same Pose object.
void setRestPose(const Skeleton& skeleton, Pose& pose);

// Local transforms -> model-space matrices, one per joint. `out` is resized to jointCount().
void poseToModel(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& out);

// The GPU palette from model-space matrices: out[k] = model[palette[k]] * inverseBind[k].
// `out` is resized to paletteSize().
void jointPalette(const Skeleton& skeleton, const std::vector<glm::mat4>& model, std::vector<glm::mat4>& out);

// pose -> palette in one call. `scratch` holds the model-space matrices; the caller keeps it so a
// per-frame update allocates nothing after the first.
void skinningPalette(const Skeleton& skeleton, const Pose& pose, std::vector<glm::mat4>& scratch,
                     std::vector<glm::mat4>& out);

// `a` moved towards `b` by `weight` (0 = a, 1 = b): positions and scales lerp, rotations slerp
// along the shorter arc so a cross-fade never takes the long way round.
[[nodiscard]] Transform blendTransform(const Transform& a, const Transform& b, float weight);
// The same over a whole pose. `out` may alias `a` or `b`; poses of different lengths blend over
// the shorter one and `out` takes that length.
void blendPose(const Pose& a, const Pose& b, float weight, Pose& out);

} // namespace avgen::scene
