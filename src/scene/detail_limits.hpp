#pragma once

// Distance-based detail limits (ADR-186).
//
// Three subsystems reduce what they do to things that are far from the camera, so that a frame
// fits in a frame's budget: the procedural cull ladder stops drawing distant instances and demotes
// the ones it keeps to coarser meshes, `updateRigs` poses distant characters at a lower rate or not
// at all, and the entity world stops running behaviours past a radius. Each is authored per object
// -- `LodSettings::maxDistance`, `SkinnedRig::cullDistance`, `EntityDesc::cullDistance` -- and each
// is a *live playback* decision: it trades what the far field looks like for the time a real-time
// frame does not have.
//
// An offline render does have that time, and the trade is the wrong one there. Before this existed
// a batch render at the Offline tier still drew billboards past the LOD thresholds, still stepped
// distant characters at 20 Hz, and still froze anything past 120 m -- the tier table promised "no
// representation shortcut" (§5.9) and meant only the *shading* tiers by it, because those were the
// only shortcuts expressed as policy. These are the geometric and temporal ones, written the same
// way, so the offline path can lift them by asking rather than by each subsystem knowing which
// kind of render it is in.
//
// Every field defaults to `true` -- honour what the scene authored -- which is exactly the live
// behaviour, so a build that never sets this is byte-identical to one from before it existed.
//
// This is policy, not content. It rides on `Scene` because that is the one object all three readers
// already have, and it is deliberately not serialised: a scene file that could switch its own
// limits off would be a scene-specific quality decision, which §49 forbids.

#include <cstdint>
#include <string_view>

namespace avgen::scene {

struct DetailLimits {
    // Honour `LodSettings::maxDistance` and `minScreenRadius`. False keeps frustum culling -- which
    // removes only what is off screen -- and draws everything inside it however far away it is.
    bool proceduralDistanceCull = true;
    // Honour the LOD ladder's thresholds. False holds every instance at rung 0, the real mesh,
    // rather than letting distance or projected size demote it to a simplified one or a billboard.
    bool proceduralLodRungs = true;
    // Honour `SkinnedRig::nearDistance` / `farHz` / `cullDistance`. False poses every rig at its
    // authored `updateHz` whatever the distance, so a character forty metres out is sampled as
    // finely as one in front of the camera.
    bool rigDistanceRate = true;
    // Honour `EntityDesc::cullDistance` and `fullDetailDistance`. False updates every entity at the
    // full rate, so distant characters go on living rather than freezing where they stood.
    //
    // This one moves the simulation and not just the picture: an entity that keeps walking ends up
    // somewhere an entity that froze does not. That is the point -- a wide shot of a valley where
    // the far herd is motionless is the artifact this removes -- but it does mean a render with
    // this lifted is not frame-identical to a preview with it in force.
    bool entityDistanceCull = true;

    // Everything lifted. What `--render-limits unlimited` means, and not what the offline tier
    // takes -- see `offlineDefault` for why.
    [[nodiscard]] static DetailLimits unlimited() { return DetailLimits{false, false, false, false}; }

    // What an offline render takes by default (ADR-191): everything lifted **except the LOD ladder**.
    //
    // ADR-186 lifted all four, and that was one too many. The distance cull, the rig rate and the
    // entity bands all remove something a viewer would otherwise see -- scatter that vanishes, a
    // character stepping at 20 Hz, a herd frozen where it stood. The LOD ladder does not. It chooses
    // a *representation* by projected screen size, and at the sizes it acts on, the simpler mesh is
    // not a worse picture: it is the renderer's only prefilter for geometry smaller than the
    // sampling grid.
    //
    // Take it away and sub-pixel geometry is drawn at full frequency with one sample per pixel,
    // which is the definition of aliasing. Measured at a fixed view: lifting the rungs raised
    // flickering area from 2.842% to 4.453%, a 57% increase, and the report that followed the first
    // offline render under it was "small/distant objects look particularly aliased".
    //
    // Keeping it costs nothing a viewer can see, because the ladder is keyed to *projected size* --
    // so it is resolution-aware by construction. Render at 1920x1080 instead of 1280x720 and every
    // instance demotes later, in pixels, automatically. A billboard only appears once the object is
    // small enough on screen that a billboard is an honest description of it.
    //
    // `--render-limits unlimited` still lifts all four, for anyone who wants to see what the far
    // field looks like without the ladder at all.
    [[nodiscard]] static DetailLimits offlineDefault() {
        DetailLimits limits = unlimited();
        limits.proceduralLodRungs = true;
        return limits;
    }
    // True when anything is lifted, which is what a log line and a "this is not the live picture"
    // notice test.
    [[nodiscard]] bool anyLifted() const {
        return !proceduralDistanceCull || !proceduralLodRungs || !rigDistanceRate || !entityDistanceCull;
    }
    [[nodiscard]] bool operator==(const DetailLimits&) const = default;
};

// ---- the render setting's vocabulary ----------------------------------------------------------
//
// "live" is what playback does, "unlimited" lifts all four, and "tier" defers to the quality tier:
// the Offline tier is unlimited and every other tier is live. Three words rather than four flags,
// because the flags exist to dial *back* from unlimited when one of them is too expensive on a
// particular world, and the ordinary answer is one of these.
enum class DetailLimitMode : std::uint8_t { Tier, Live, Unlimited };

[[nodiscard]] constexpr const char* detailLimitModeName(DetailLimitMode mode) {
    switch (mode) {
    case DetailLimitMode::Tier: return "tier";
    case DetailLimitMode::Live: return "live";
    case DetailLimitMode::Unlimited: return "unlimited";
    }
    return "tier";
}

[[nodiscard]] constexpr bool detailLimitModeFromName(std::string_view name, DetailLimitMode& out) {
    for (const auto mode : {DetailLimitMode::Tier, DetailLimitMode::Live, DetailLimitMode::Unlimited}) {
        if (name == detailLimitModeName(mode)) {
            out = mode;
            return true;
        }
    }
    return false;
}

} // namespace avgen::scene
