#pragma once

// XFORM: the render-transform offset layer (Effect Library Wave 2, shared-infrastructure.md "XFORM",
// rendering-architecture.md §3 and §7).
//
// **What XFORM is.** A visual-only offset on an owner node's transform -- a translation, and a
// rotation and non-uniform scale about a pivot -- produced by the Geometry-stage types (Orbit,
// Spiral, Float, Shake, Bounce) as a pure function of the transport second and this frame's
// parameter finals. The Composition composes it into the node's transform during its ordinary
// flatten, so the node's children, its attached lights, particle systems and meshes, every effect
// that reads the node's DRAWN view (a Glow's lanes, a Space Warp's proxy, a Trail's head) and the
// renderer's `prevModel` (hence the velocity target and motion blur) all follow the offset without a
// second, effect-aware traversal.
//
// **Visual only.** `EntityWorld` (position, travel, perception), the hero anchors, the camera rigs'
// follow targets and HIST all read `Composition::nodeWorldTransform`, which does NOT include the
// offset: a bobbing saucer's AI does not perceive its own bob, a camera framing it does not shake
// with it, and HIST records the pre-offset path (so it is exact under seek without re-running the
// effects in the replay -- the offset is a pure function of t and can be re-applied at any sample).
//
// **Why it runs before the scene.** The flatten needs the offset while it walks the nodes, so the
// Geometry prefix of `effectOrder_` is evaluated by `Engine::updateEffects(BeforeScene)`, ahead of
// `controller_->update`. Its inputs are only what exists at that point: this frame's parameter finals
// (the routes have run), pure functions of t, and the last completed step. It must NOT read the
// drawn transform of this frame, which does not exist yet -- so the builder's context carries no
// scene query at all, and a type cannot ask by accident.
//
// **Composition within an owner** (rendering-architecture §7). Translations are in the owner's
// PARENT space (world metres for a root node, whatever the node's own rotation and scale) and SUM, so
// two bobs commute. Rotations and scales are in the owner's LOCAL frame, about each instance's pivot,
// and compose in stack (evaluation) order, top of the stack outermost -- rotations do not commute, and
// which comes first is the author's choice, as it is for FXPOST passes. The flatten then draws the
// node at
//
//     T(sum of translations) * node's own transform * L1 * L2 * ... * Ln
//
// where Li = T(pivot_i) R_i S_i T(-pivot_i).
//
// **Adding a producer** is one file in `kinds/` plus one row in `transformProducers()` (in
// transform_frame.cpp): a function that turns a live instance into one `TransformContribution`. The
// builder owns activation, timing, the envelope, stacking, capacity and status for every producer.

#include "world/effects/effect_instance.hpp"
#include "world/effects/effect_timing.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace avgen::world {

// How many distinct owners one frame can offset. Each record is a few dozen bytes; the budget is
// about a person being told, not about memory. The 65th owner's instances are `Dropped` with a
// reason, lowest evaluation priority first.
inline constexpr std::size_t kMaxTransformOwners = 64;

// ONE instance's offset, as its producer resolves it at full strength. The builder applies the
// lifecycle envelope (translation and scale toward identity, rotation slerped toward identity), so a
// fading Shake settles smoothly rather than snapping.
struct TransformContribution {
    glm::vec3 translation{0.0f};                  // parent space, metres
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};   // local frame, about `pivot`
    glm::vec3 scale{1.0f};                        // local frame, about `pivot`
    glm::vec3 pivot{0.0f};                        // node-local metres
};

// ONE owner's composed offset: what the flatten applies. `local*` is the product L1 * ... * Ln as a
// TRS (position already carries the pivots).
struct TransformOffset {
    std::string node;                                // the owner node's name
    glm::vec3 translation{0.0f};                     // parent space: added to the node's position
    glm::vec3 localPosition{0.0f};                   // node-local: composed after the node's transform
    glm::quat localRotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 localScale{1.0f};
};

// The frame block the Composition reads. Plain data with a fixed capacity: the strings keep their
// storage from frame to frame, so a steady scene allocates nothing here.
struct TransformFrame {
    std::array<TransformOffset, kMaxTransformOwners> offsets{};
    std::size_t count = 0;
    std::uint32_t dropped = 0; // instances refused because the owner budget was full

    [[nodiscard]] std::span<const TransformOffset> live() const { return std::span(offsets).first(count); }
    // The offset for `node`, or null. Linear: a frame has a handful of offset owners.
    [[nodiscard]] const TransformOffset* find(std::string_view node) const;
};

// A producer: this live instance's contribution at full strength. `age` is the seconds since the
// instance's current activation pass began (the window's start, or the last `repeatSeconds`
// restart) -- the `t - t0` a triggered Shake or Bounce decays over. Written as a function of `age`
// so that when TRIGGER lands, its `t0` replaces the window's by changing only where `age` comes from.
// Returns false for "nothing this frame" (a zero amplitude).
using TransformProduce = bool (*)(const EffectInstance&, const EffectContext&, double age,
                                  TransformContribution&);

struct TransformProducer {
    EffectKind kind;
    TransformProduce produce;
};

[[nodiscard]] std::span<const TransformProducer> transformProducers();
[[nodiscard]] const TransformProducer* transformProducer(EffectKind kind);

// The instance's lifecycle at `ctx.seconds`: its envelope (0 when it is not live) and the age of its
// current pass. The same gating every builder applies -- enabled, activation window (per-owner
// under `HeroFocus`), delay, repeat restarts, fades, lifetime.
struct TransformGate {
    float envelope = 0.0f;
    double age = 0.0;
};
[[nodiscard]] TransformGate transformGate(const EffectInstance& e, const EffectContext& ctx);

// `a` composed with `b` by the XFORM rule: translations add, locals multiply (`a`'s outermost).
// Exposed so a test can hold the builder to the rule it documents.
void composeTransformOffset(TransformOffset& into, const TransformContribution& c);

// The conformance probe's `resolve.records`: 1 when one live instance contributes an offset.
[[nodiscard]] std::size_t transformRecords(const EffectInstance& instance, const EffectContext& ctx);

// The Geometry-stage builder. Walks `order` (evaluation order: stage, priority, stack position),
// gates each Transform-bucket instance, asks its producer, composes per owner, writes every such
// instance's status. Called by `Engine::updateEffects(BeforeScene)`, before the flatten.
void buildTransformFrame(std::span<const EffectInstance> effects, const EffectContext& ctx,
                         TransformFrame& out, std::span<const std::uint32_t> order,
                         std::span<EffectStatus> status, std::span<std::string> reasons);

// The composed offset `node` has at `ctx.seconds`, from the same producers, gates and stacking rule
// as `buildTransformFrame`, without touching any status: for re-applying the offset at a PAST
// instant (a Trail's body, a velocity over the drawn path). The instant is `ctx.seconds`; the
// parameters are this frame's finals, so a row a route is moving is exact only for the instants in
// which it did not move -- an unrouted Orbit or Float is exact at every sample. False when no live
// instance offsets `node` then (`out` is the identity offset).
[[nodiscard]] bool transformOffsetAt(std::span<const EffectInstance> effects, std::span<const std::uint32_t> order,
                                     const EffectContext& ctx, std::string_view node, TransformOffset& out);
// Does any Transform-bucket instance name `node` as its owner at all (live or not)? The cheap test
// that lets a reader keep its pre-XFORM path, bit for bit, for every owner nothing offsets.
[[nodiscard]] bool hasTransformProducer(std::span<const EffectInstance> effects, std::string_view node);
// The drawn origin of a root node whose simulated transform is (position, rotation, scale) under
// `offset`: T(translation) * M * L applied to the local origin. A nested node's parent-space
// translation is treated as world-space here (the history holds world transforms, not the parent's).
[[nodiscard]] glm::vec3 drawnOrigin(const TransformOffset& offset, const glm::vec3& position,
                                    const glm::quat& rotation, const glm::vec3& scale);

// ---- shared motion maths (pure, deterministic) -----------------------------------------------------

// FNV-1a of an instance id folded to [0, 1): a per-instance phase seed that is the same on every run
// and machine, so two floaters side by side never bob in lockstep unless their seeds say so.
[[nodiscard]] float transformSeed(std::string_view id, float authoredSeed);

// Smooth 1-D gradient noise in [-1, 1] (quintic fade, hashed lattice gradients): C2-continuous in `x`,
// so a shake sampled at 30 or 120 fps traces the same curve. `channel` decorrelates axes.
[[nodiscard]] float motionNoise(double x, std::uint32_t channel, float seed);

} // namespace avgen::world
