# Talking Heads: Analog / Digital Signal Corruption art pass (owner brief, verbatim, 2026-10-25)

---

Talking Heads — Analog / Digital Signal Corruption Art Pass

Objective

Rework the secondary sound-source-reactive effects in the existing Talking Heads scene around a coherent visual language of corrupted video, analog tape damage, CRT interference, and digital image failure.

The current scene has three mannequin busts, animated eyes, and colored rays emitting from their mouths. Those features are working correctly and must remain intact.

The problem is that the current secondary effects are too similar across sources.

Move away from liquid undulation, flowing deformation, and generic wavy distortion. Instead, make the busts behave like damaged video images: tearing, block displacement, static, color-channel misregistration, pixel smearing, frame persistence, and unstable signal reconstruction.

The desired aesthetic is unsettling, experimental, electronic, and slightly retro without looking like a cheap VHS preset applied uniformly over the entire screen.

This task concerns the secondary entity effects and their source-specific visual identities. Do not redesign the heads or rebuild the scene.

1. Audit the existing implementation

Before changing code:

* Inspect the current secondary effects for each bust.
* Identify which effects already work well, especially the drum bust’s distinctive treatment.
* Determine which existing shader, material, render-target, texture, post-processing, or entity-effect systems can support the desired look.
* Establish whether the renderer can access previous-frame color or entity-specific render textures.
* Identify the least expensive way to implement localized corruption.
* Capture the current scene as a visual baseline.

Do not assume true datamoshing, arbitrary object-space distortion, or per-entity post-processing is already supported.

Prefer existing capabilities and small, localized extensions over a new rendering framework.

2. Required effect library

Implement and visually validate at least three substantially different effect families from the following list. Select the best combination based on the existing engine and rendered results.

A. VHS tracking failure

Create horizontal bands that shift portions of the bust sideways, with occasional tracking errors, color fringing, and brief synchronization failures.

Requirements:

* Localized horizontal displacement.
* Variable band positions and widths.
* Short, transient-driven tracking bursts.
* Configurable displacement, frequency, and recovery.
* Occasional restrained color-channel offsets.
* Return to a clean image between events.

Avoid simply drawing static horizontal lines over the head. The image itself must visibly break out of alignment.

B. Datamosh / block corruption

Make regions of the bust appear to lose their correct image reconstruction.

Possible techniques:

* Rectangular block displacement.
* Uneven pixel-grid offsets.
* Displaced image regions that persist for several frames.
* Localized image smearing.
* Sudden discontinuities between neighboring regions.

Prefer actual temporal image persistence or motion-vector-style corruption if the renderer already supports it economically.

If it does not, implement a convincing approximation using block masks, local image displacement, or entity-specific duplicated geometry.

Do not claim true temporal datamoshing if the result is a static block shader.

C. TV static

Apply animated noise to selected regions of the bust.

Requirements:

* Clearly visible, animated noise rather than static grain.
* Configurable density and intensity.
* Localized patches that intermittently obscure the surface.
* Independent control of monochrome noise and colored interference.
* Bursts of stronger interference on selected transients.
* A clean, recognizable bust between events.

Avoid covering the entire face in uniform noise continuously.

D. RGB channel separation

Separate the bust’s red, green, and blue image components.

Requirements:

* Independent spatial offsets.
* Configurable channel displacement.
* Fast, transient-driven separation.
* Slower sustained modulation.
* Controlled recovery.
* Optional color fringing around the silhouette.

Use this as a component of a broader effect identity rather than relying on RGB separation alone.

E. VHS ghosting and temporal persistence

Create offset copies of the bust that behave like lingering or misregistered video frames.

Requirements:

* Two or more controllable ghost layers where practical.
* Independent position or orientation offsets.
* Different opacity and color treatment.
* Decay over a controllable duration.
* Transient-driven separation or sudden duplication.
* No uncontrolled accumulation of copies.

This effect should be visually distinct from ordinary motion blur.

F. Pixel sorting and image smearing

Create localized bands or blocks that stretch image information horizontally or vertically.

Requirements:

* Directional smearing.
* Adjustable band size and displacement.
* Intermittent rather than permanent activation.
* A strong visual relationship between audio intensity and smear magnitude.
* Fast recovery or controlled decay.

If real pixel sorting would require an expensive or unsupported implementation, use a simpler screen-space or texture-based approximation.

G. Tape color bleed

Create chromatic leakage and misregistration around the bust.

Requirements:

* Colored displacement around selected edges.
* Localized horizontal color shifts.
* Variable intensity.
* Stronger activity during selected events.
* Compatibility with the other corruption effects.

This is a supporting effect, not a sufficient standalone profile.

3. Make the effects entity-local

The mouth rays and animated eyes are already working. Preserve them.

Secondary corruption must visibly affect the bust rather than merely the entire scene’s background.

Use the best supported approach:

* Entity-specific render textures or masks, if available.
* Material or shader effects applied to the bust.
* Localized post-processing using a suitable entity mask.
* Carefully controlled duplicated geometry for ghosting or block-like displacement.
* A simple screen-space approximation if the alternatives are disproportionately expensive.

If the current renderer cannot isolate an effect to a bust, document the limitation and implement the smallest viable alternative.

Do not add a full-screen VHS filter as the primary solution. A subtle global atmosphere may be added later, but the defining corruption must belong to each entity.

4. Source-specific profiles

Give each assigned source a distinct default corruption profile.

Drum profile: IMPACT / TRACKING FAILURE

Preserve the existing drum effect where it is already successful.

Emphasize:

* Abrupt horizontal tearing.
* Short tracking errors.
* Transient-triggered static bursts.
* Brief channel misalignment.
* Fast recovery.

The drum bust should look as though impacts are disrupting its transmission.

Bass profile: CODEC COLLAPSE

Use a distinctly digital, block-based visual identity.

Emphasize:

* Larger rectangular displacement.
* Persistent displaced regions.
* Pixel or scanline smearing.
* Longer corruption decay.
* Sudden reconstruction failures on strong events.

Use low-frequency energy to control the magnitude and persistence of corruption, not merely the overall brightness.

The bass bust should feel as though its image is being incorrectly reconstructed, rather than physically waving.

Synth profile: BROADCAST INTERFERENCE

Use an analog/electronic interference identity.

Emphasize:

* Animated TV static.
* RGB channel separation.
* Color bleed.
* Scanline shimmer.
* Gradual evolution during sustained energy.
* Occasional strong interference bursts.

Avoid reusing the bass profile’s large block displacement as the dominant effect.

Fourth-source default: TEMPORAL GHOSTING

Use displaced, fading image copies, optionally combined with restrained channel separation.

Emphasize:

* Persistent image offsets.
* Independent ghost-layer opacity.
* Separation on transients.
* Controlled decay.
* A recognizable original bust beneath the effect.

All profiles must be configurable. Any source may select a different profile through existing scene controls or source-effect configuration conventions.

Do not hardcode drums, bass, and synth as the only valid source types. These are default artistic assignments, not a new instrument-classification system.

5. Audio-reactive behavior

Reuse existing AV Gen analysis and modulation systems.

Use available signals such as:

* Low-frequency energy.
* Mid-frequency energy.
* High-frequency energy.
* Broadband amplitude.
* Transient intensity.
* Sustained energy.

Do not introduce duplicate analysis pipelines or depend on retired inferred percussion identities.

Map signals to effect parameters according to their visual behavior:

* Transient intensity controls the severity of short tracking errors.
* Low-frequency energy controls block size, displacement, or corruption persistence in the bass profile.
* High-frequency energy influences static density and fine interference.
* Sustained energy controls gradual RGB separation and ongoing broadcast interference.
* Strong transients trigger temporary ghost separation and localized corruption bursts.

Avoid driving every parameter directly from raw amplitude. Use smoothing, attack, decay, and event-triggered behavior as appropriate.

The bust should not remain maximally corrupted for the entire track. The clean-to-corrupted transition is part of the effect.

6. Dynamic support for one to four sources

The existing scene is expected to support one through four busts according to the number of assigned audio sources.

Preserve or complete this behavior as part of the art pass.

Requirements:

* One through four assigned sources create the corresponding number of busts.
* Each bust has independent audio routing and secondary-effect state.
* Adding and removing sources updates the entity count correctly.
* Reordering sources does not unnecessarily reset persistent state for unchanged source identities.
* Invalid sources do not produce phantom entities.
* No fifth bust is created.
* Unknown source types receive a configurable default profile.
* The composition remains balanced at every supported count.

Do not replace an existing working dynamic-source implementation unnecessarily. Audit it and make only the changes required for correctness and the new effect configurations.

7. Performance and implementation strategy

Maintain the established 1080p/30 FPS live-rendering target.

Prioritize techniques in this order:

1. Existing material and shader effects.
2. Existing localized post-processing or entity masks.
3. Small, controlled render-target extensions if necessary.
4. Duplicated geometry for ghosting or simple spatial approximations.
5. More expensive temporal effects only if their visual benefit justifies their cost.

Avoid unnecessary renderer-wide changes, expensive simulation, excessive transparency, and large numbers of per-frame CPU-generated fragments.

Inspect the renderer’s current frame architecture before introducing temporal history. Do not assume a previous-frame image can be sampled from an arbitrary stage without additional resource management.

Profile one, two, three, and four busts. Record CPU time, GPU time, resolution, and frame rate.

If true datamoshing is too expensive or architecturally invasive, deliver a visually convincing block-corruption approximation and document the distinction.

8. Mandatory visual comparison

Capture actual renders of the scene before and after the changes.

Show:

* Drum profile during a quiet passage and strong transients.
* Bass profile during sustained low-frequency energy and transient events.
* Synth profile during sustained energy and high-frequency activity.
* Fourth-source temporal ghosting profile.
* All profiles together.
* One-bust and four-bust configurations.

Perform an additional comparison with the mouth beams hidden and the bust materials neutralized.

The secondary treatments must still be distinguishable. Different colors or different effect strengths are not enough.

If the bass and synth profiles still look similar, revise their effect combinations before adding more options.

9. Acceptance criteria

The task is complete when:

* At least three distinct corruption effect families are implemented and validated.
* The drum, bass, and synth profiles have unmistakably different visual behavior.
* The effects are attached to the busts, not merely applied as a full-screen overlay.
* Static is animated, tracking errors visibly displace image regions, and ghosting leaves controlled temporal copies.
* Bass uses a recognizably block-based digital corruption style.
* Synth uses a recognizably analog/electronic interference style.
* Effects respond to their assigned audio sources with appropriate temporal behavior.
* The working mouth rays and animated eyes remain intact.
* Dynamic support for one through four sources works correctly.
* Performance remains within the measured live-rendering budget.
* Visual captures demonstrate the improvement.

The artistic goal is for each bust to look like a different kind of corrupted transmission: one suffering violent tracking failure, one losing its digital reconstruction, and one dissolving into analog interference.

Make the differences obvious in the rendered image, not just in the implementation or parameter values.
