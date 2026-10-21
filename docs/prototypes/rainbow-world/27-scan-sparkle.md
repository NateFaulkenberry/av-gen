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
