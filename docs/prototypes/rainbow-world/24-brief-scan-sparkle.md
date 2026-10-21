# Owner brief 2026-10-21: entity scan sparkle, plus reactivity picks (verbatim)

Context (coordinator note): this came with the owner's answer to the 23-modulation-inventory top-5.
The owner chose:
- **YES:** item 3, bass and synth on big surfaces (breathe across their range).
- **YES:** item 5, remove the bass swell heave.
- **NOT CHOSEN:** item 4, the snare accent on clouds. The owner's note on the sky is below.

Items 1 and 2 (the arch beat pulse, and 3-5 scans) were already in the owner's fix list (22-owner-fixes.md).

Owner's sky note, verbatim: "any effects in the sky might be better if it were like sunrays or something, pulses of light from clouds wont be very visible"

---

## Rainbow World — Entity Scan Sparkle Effect

Create a lightweight, reusable entity VFX for trees and other selected environmental objects. When the rainbow-world scan sweeps across an entity, it should briefly come alive with a burst of color, rotation, and glitter.

**Visual behavior**
- **Scan trigger:** Detect when the existing rainbow scan wave passes an entity. Trigger once per wave crossing, with a short cooldown to prevent repeated firing.
- **Spin:** The entity performs a brief, playful spin or twist, easing in and out. Trees should feel magical and animated, not violently distorted.
- **Glitter:** Emit a small burst of bright, colorful sparkles around the entity, with twinkling points, tiny glints, and short-lived light trails. Particles should fade and shrink naturally.
- **Color:** Pull colors from the scene's active MIDI/audio-modulated palette. Use contrasting highlights and occasional white-hot glints rather than defaulting to a uniform rainbow.
- **Timing:** Stagger nearby entities slightly as the scan travels through the environment, creating a cascading wave of sparkling trees rather than everything firing simultaneously.

**Implementation**
- Reuse the existing scan-wave, palette, entity animation, and particle/VFX systems wherever possible.
- Make the effect configurable per entity: intensity, spin amount, sparkle count, duration, and palette response.
- Keep it performant: pooled or batched particles, minimal allocations, short particle lifetimes, and a sensible cap on simultaneous effects.
- Audio reactivity may modulate sparkle intensity or color, but the scan crossing remains the primary trigger.

**Acceptance criteria:** The scan visibly travels through a stand of trees, triggering a satisfying wave of spins and glitter. The effect feels vivid, playful, and polished at live-performance frame rates, without permanently altering the trees or noticeably worsening scene performance.
