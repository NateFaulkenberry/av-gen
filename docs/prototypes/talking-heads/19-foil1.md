# Talking Heads, foil1: the busts in security foil, a darker room, brighter rays

The owner asked:
- "let's try applying D hybrid foil to the busts in talking heads with the music able to drive the colour bands";
- then: "I want this effect merged into talking heads as long as it doesnt severely impact frame rate - another quick
  fix for talking heads - darken the background, strengthen brightness of light beams from mouth".

Branch: scene/talking-heads-foil. Media: `~/Desktop/av-gen-review/92-talking-heads/foil1/`.

## The foil

The source is the Cursed Slammers study's variant D, the "hybrid security foil" (scene/cursed-slammers:
`poc4/variants.py`, `poc5/survey.py`; reports 09 and 10). It is:
- engraved relief lines that follow the form (phase from height and depth), grooves widening away from the key light;
- a silver base;
- spectral colour only inside narrow bands, indexed by the view angle and depth plus a fine grating;
- sparse micro-sparkle.

It now replaces the porcelain on every bust piece (body, neck, head) and on the lids. The eyes and the mouth interior
keep their own materials.

**One program per piece, one continuous engraving.** `localPosition` on the procedural path is the rest mesh's,
before any deformer. Each piece's program maps it into the bust's frame with one `objectProject` op: the body at 0, the
neck at `NECK_FOOT`, the head at `HEAD_REST`, the lids at `HEAD_REST` plus the eyeball. So the lines run unbroken over
every joint and stay glued to the material as the neck bends, the head turns, the jaw drops and fx2's blocks displace
the surface. All four lids of a head share one program. The lines read only height and depth, and the eyes differ only
in x, so one offset serves both eyes and the scene stays inside the 32 program slots (29 used).

**Fitting D into one program with an fx2 tail** (64 ops at most, ADR-1661; D alone was 61):
- the rescale and the piece's offset are one `objectProject`;
- each phase is one unsaturated `gradient`, scaled down and recovered by the palette's frequency;
- the music's phases are routed into the palettes' own index offset (`value`) instead of being separate ops;
- one footprint fade serves the lines and the sparkle;
- D's diagonal crosshatch in shadow is left out (12 ops).

The foil core is 38 ops, and the busiest program (the broadcast profile's) is 58. The fx2 profile tails are reused
unchanged; they read only r4 (emission) and r7 (base colour), and the foil keeps its groove mask in r0 through them.

**Tuned for this scene:**
- **Line density** is set in metres: about 95 lines per metre, about 55 up the bust, 9 px apart in the three-bust
  view. POC 5's density drew about 120 lines, under the 6 px anti-aliasing fade, so they vanished in the full scene.
- **Metallic is 0.7.** All-metal silver reflected only the dark violet room.

**The music on the bands,** per head, from its own source only (its level, its onsets, its gate):
- the bands **drift** with the level (integrated, so they hold their place when the source stops);
- an onset **sweeps** them on by about half a band (an event envelope, integrated: a push, never a jump);
- while the gate is open the bands **widen** and **shine** (smoothed both ways, so no flicker);
- the **sparkle twinkles** with the music and lifts a little on hits.

`foil_routes()` builds these, and `isolation_check` confirms that every head's columns are its own.

**The sparkle flicker, fixed.** In POC 4's D a cell's flare phase was 7 x hash + 6 x fresnel. The fresnel varies
across every cell on a curved surface, so nearly every cell held a thin stripe where it crossed its peak: white dashes
that swam with the view. Now:
- the phase is the hash plus a twinkle phase the music advances, so a whole cell flashes and nothing moves with the
  view;
- the angle only weights the flash's brightness, capped (clamp(fresnel, 0, 0.45), then scaled);
- the cells are 1.5 times POC 4's and fade out below about 4 px, which is where they crowd at the silhouette.

**fx2's profiles on silver.** All four still read: the tracking bands, the codec blocks and smear, the broadcast
static and scanlines, and the ghosting copies. None needed changing.

`build(foil=False)` and `build_variant.py --porcelain` keep fx2's porcelain for comparison.

## The room and the rays

- **Darker background.** The background lights are dimmed together with the routes that drive them, so the motion
  and the reactivity are unchanged:
  - BG_DIM 0.25 on the membranes' colour fields, their contour lines, the arcs, the bands and the filaments, AND on
    their dark bodies (drawn unlit, a body shows its base colour as it is, and that set most of a membrane's
    brightness);
  - SKY_DIM 0.5 on the sky and fog;
  - MIST_DIM 0.35 on the room's mist;
  - HALO_DIM 0.3 on each head's halo. With the mist, the halos were most of the frame's violet-pink wash.

  The frame's mean brightness at 62 s and 82 s fell by about 30%. The faces are lit by the key light, not by these.
- **Brighter rays.** RAY_BRIGHTNESS went from 2.6 to 4.2. A ray is still exactly the mouth's opening x its weight,
  and `ray_check` passes. The bloom threshold rose from 0.9 to 1.2, so the rays bloom while the foil's ridges, bands
  and glints stay crisp.

## Cost

Headless 1080p, foil and porcelain interleaved in one (busy) session: the foil adds +0.9 / +1.6 / +1.4 / +1.2 ms
GPU p50 at one to four busts (it was about +3 ms at four before six ops were trimmed). Added to fx2's clean figures,
four busts come to about 21.9 ms p50 and about 24-25 ms p95, inside 30 fps. Details: `foil1/performance.txt`.

## Not done

- **Live baseline.** The talking-heads row in `docs/perf/live-baseline.json` is still stale. Only the built-in
  120 Hz display was connected, and the row is a 60 Hz (LG) measurement.
- **D's crosshatch** is left out for the op budget.
