# Rainbow World 27: the scan sparkle (gate 5)

The owner's brief is `24-brief-scan-sparkle.md`. As the world scan's front passes a tree, the tree:
- spins once about its trunk, eased;
- nods (tips about 13 degrees and back up);
- throws a burst of glitter round its crown that rises, twinkles and fades.

The front reaches nearer trees first, so a stand cascades. Each tree fires once per scan, four times over the song.

## What it reuses (no engine change)

- **Scene fields (ADR-025).** There are three: spheres about the camera.
  - `scanSpin` is the front itself. Its centre is the camera's position, baked from the ride as three timelines
    (`camX/Y/Z`, the same keys as the ride). Its radius is the scan front's: the same route as the material scan's,
    from `timeline.scanFront` in the showcase or the phrase pulse live.
  - `scanShell` is a thin shell just behind the front: a compound, the product of an outer sphere and an inverted
    inner one.
- **Effectors (ADR-025, ADR-1500),** run on the GPU over the instance records:
  - **Spin:** each tree part (wood, core, leaves) rotates about its own up by 2 pi x `scanSpin`. Inside the front a
    tree is a whole turn round (unchanged), outside it is untouched, and in the front's 120 m soft edge it is part way
    round. So it turns once as the front passes, and never again until the next scan. The front's run is eased and
    takes 7 s, so the spin takes about 0.6-1 s.
  - **Nod:** each tree rotates about its own side axis by 0.22 rad x `scanShell`.
  - **Glitter:** a pool of 28 tiny emissive grains in every crown (9,772 instances in all). They are at scale 0
    except inside the shell, where they swell (scale) and rise 5 m (position offset, along the tree's own up).
- **The glitter's colour** is a material program (`rw_glitter`):
  - each grain takes one of four colours from the active look by its instance random (the look's kick colour,
    horizon, cloud shadow and crest, bound by the palette);
  - a fifth of the grains are white-hot;
  - each twinkles on its own clock;
  - the synth's light lifts its gain.

## Guarantees

- **Seek equals play.** Every input is a pure function of the transport: timelines and stateless field samples. There
  are no events, no per-tree state and no simulated particles.
- **It fires once per wave.** It is a function of where the front is, so there is no cooldown to manage. Between
  scans the front rests at 980 m, beyond the leaves' cull.
- **Pooled and capped.** The pool is fixed at load (28 per crown). It draws as one instanced node of 16-triangle
  spheres. Grains outside the shell are at scale 0.
- **Configurable** in `build_world.py`:
  - `SPIN_SOFT` (the spin's length);
  - `SHELL_W` / `SHELL_SOFT` (the burst's depth);
  - `GLITTER_PER_TREE`;
  - the effectors' strengths (spin, nod, rise; also route targets: `procedural/<node>/effector/<n>/strength`);
  - `glitter_gain`.

It costs one more effector per tree part and the glitter node. That cost is in the gate-5 live profile.


## Gate 6: fireworks (the owner: "the trees to spin and light up with sparkles as the effect flows through the entity ...
more explosive / fireworks looking")

**What read as cheesy, and what real fireworks do instead:**
- **Cheesy:** uniform twinkling confetti that just sat in place.
- **A firework shell:**
  - **Ignition:** it ignites in a white flash.
  - **Flight:** its stars fly out radially fast, slow under drag, and arc down under gravity, streaking along their
    flight (the eye and the camera smear them).
  - **Cooling:** they are white-hot when born and cool to their colour; a few crackle (strobe) as they die.
  - **Cascade:** a stand of trees fires as a staggered chain.

**The timing fix.** A sphere field's soft edge lies OUTSIDE its radius, so the gate-5 spin and glitter played *before*
the front reached a tree. Every stage is now a function of `a`, the metres the front has run past the tree, so all of
it starts as the front arrives and plays while it passes.

**How it is built:**
- **Fields:** constant fields with eased falloffs whose edges ride the front, and sphere ramps whose radius does,
  combined into windows. The GPU allows only one level of compound nesting.
- **Effectors on the trees:**
  - a spin (smootherstep over 120 m, a whole turn);
  - a nod;
  - the tree's own light (instance emission over a 240 m window), so the tree itself lights up.
- **The sparks:** 44 per crown, each a streak 4x longer along its flight, starting at a jittered depth along its own
  ray so the shell is not a perfect sphere.
  - Out along the flight: `1 - (1 - a/280)^3`, decelerating.
  - Down along gravity: `(a/280)^3`, accelerating.
  - Size: a window, up in 6 m and out from 140 m.
  - Heat: the instance emissive, which the `rw_spark` program turns from white-hot into the look's palette colour;
    one in seven strobes.
- **The flash:** one core per crown, a white-gold sphere scaled by a 45 m window at ignition.
- Still no events and no state, so seek equals play.

**Live profile on AC** (`live_g6`, 35-125 s):

| | |
|---|---|
| Ladder (frames) | Ultra 2505, High 195 |
| Frame p50 / p95 | 33.4 / 35.9 ms |
| GPU p50 / p95 | 22.4 / 32.8 ms |
| Deadline misses | 2 |

The scans held their tier. This is within the owner's bar (High outside scans, Medium allowed in them).
