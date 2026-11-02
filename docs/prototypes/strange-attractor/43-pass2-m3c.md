# Art Pass 2, M3c: the art checkpoint

The owner's notes on M3b are in `42-pass2-m3c-owner-notes.md`, with a later update: no full sequence and no suites at
this checkpoint. This pass is about getting the art direction right in every scene. M3c builds on 4e675db8. The
current canyon stays the default, and the POC files are only regenerated (no POC work).

Media is in `~/Desktop/av-gen-review/101-strange-attractor/pass2-m3c/`, as clips and stills per scene. **The suites
were not run** (owner): they run when the art is approved and the branch heads to merge.

## 1. Portals: back to Gate A

**What the diff found.** Comparing Gate A (21da024a, 65983cf2) with today's code, the scene values had not changed:
the field, the opening, the wire and the glyphs had only been scaled with the world (ADR-1893). The look had changed in
the code, from Gate B on:
- **The shards** thinned as the camera closed in (they were gone over the last 70 m) and streamed along the rim. The
  Gate A storm of gold into the opening had gone.
- **The lips** became strata slabs instead of Gate A's fold, the fan of torn rock.
- **The composite** pulled its plane a fixed 25 m toward the eye. At world scale 0.4 that is far out of the wall, and
  it covered anything in front of the opening.
- **M3b's rim particles.**

**What changed (ADR-1901).**
- The shards and the lips are Gate A's again, on every portal.
- The composite's margin is now a share of the opening's width (0.3). Shards in the tunnel are drawn in front of the
  composited plane, so the storm is drawn over the destination glimpse, which is kept.
- The rim particles are removed: their code, tests and builder (ADR-1898 is withdrawn, ADR-441).

**The regressions.**
- **The Moon's crater-floor exit.** ADR-1899's guard refused any view into a renderer that was handing off. The canyon
  hands its frame to the Moon for the whole Moon leg, so the floor's glimpse of the canyon was never drawn. The guard
  now refuses only a hand-off back. The canyon shows through the floor again, and the camera comes out of the canyon
  wall.
- **The river portal into the sea.** M3b's glide started 25 m out, inside the opening's 26 m reach, so the tear was
  already open. The opening now starts at 14 m, so it tears open during the glide. The straight glide and the tilted
  exit frame are kept.
- **The Mars cliff portal starts sooner.** Its field reaches 260 m (it was 95 m) and its tear starts at 100 m.
- **The pyramid portal starts sooner.** Its field reaches 1100 m (it was 420 m) and its tear starts at 330 m.

Every crossing has a clip and stills in `01-portals/`.

## 2. The Moon

- **The rave is in the domes.** The lasers are gone. The domes' own glass panels are split into five colour bands
  that wind round each dome (`pass2/build_dome_rave.py`). Each band is on its own beat source: magenta on the beat,
  cyan on the highs, green on the low onsets, amber on the mids, violet on the bass. The strobes move round the glass
  and never fire all at once. With audio off the glass holds a steady multicoloured glow. Coloured lights at each
  dome's foot spill onto the ground.
- **The Earth's glow.** Three stylised options (`pass2/build_earth_halo.py`), each a flat emissive figure just behind
  the planet, facing the arrival, pulsed on the beat:
  - a: chromatic rims;
  - b: scanlines;
  - c: a pixel corona, the pick.

  The datamosh echoes are kept.
- **Not cropped.** The Earth is now 10 degrees up, so it is whole at 34 s.

**Flicker.** The Moon clip, 34-47.5 s, measured as the irregularity of the frame-to-frame difference:

| Clip | Median | p99 | Max |
|---|---|---|---|
| Moon, audio on | 0.005 | 0.089 | 0.262 |
| Moon, audio off | 0.004 | 0.125 | 0.253 |
| Portal 1's opening, 24-32 s | 0.005 | 0.082 | 0.498 |

## 3. Mars

- **The rover is the arrival's subject.** The arrival mesa is moved nearer the pyramids and the pyramids 400 m nearer
  the arrival. Curiosity drives just ahead of the exit. The camera comes down behind it, runs low alongside it, passes
  ahead with its eye on the rover, then lifts away to the pyramids. Its tracks and mast flash are kept. Basalt rocks
  are strewn round its ground.
- **The sand.** The drifts are banked against the faces, long and low to windward and shorter to leeward. They fall
  to the plain by a smooth cubic profile, so there is no lip at the crest and none at the toe. The drifts are smooth
  shaded with no geometric ripples, which removes the streak banding. Pads under the drifts keep the plain level
  beneath them.
- **The pyramids.**
  - The stone is darker and varied.
  - Sand has spilled onto the courses near the sand line, and the low courses are rust-stained.
  - Some stones are weathered dark.
  - The smooth white cap is now an eroded, weathered grid of casing stone.
- **The light.** A lower sun gives long shadows, the haze is denser, there are more rocks, and a knob mesa is moved
  clear of the drifts.

## 4. The ocean

The subs whiz. Each launch goes out of the bay and up in a hard climb toward the surface, gone into the gloom in about
three seconds. Each return dives out of it and into the bay just as fast. The five subs are timed through the leg
(126.6, 131.2, 135.4, 140.2 and 143.4 s) and wait parked inside the base.

## Open

- Option a's rims are thin at this distance; if the owner prefers that direction, they want weight.
- The drifts are smooth: the ripple texture the owner mentions would be a sand material or program (not built).
- Through the launch bay, the bay's own floor stands in front of the canyon glimpse for about half a second.
