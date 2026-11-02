# Art Pass 2, M3: the night gathering, the ocean, the launch bay

M3 of `33-pass2-plan.md` §6, on the 210.5 s cut. It covers:
- the canyon at night (brief §4): the three Sasquatches, a Gray and a parked UFO at the embers;
- the riverbed portal;
- the deep ocean (brief §5): the continental shelf, an underwater base with a launch bay, submersible discs;
- the bay portal into the UFO (its interior is M4: a first form stands in for it).

It folds in the coordinator's Mars notes and the owner's idle request. The sequence so far runs 0-147 s, to the bay
portal's crossing.

Media is in `~/Desktop/av-gen-review/101-strange-attractor/pass2-m3/`; the numbers are in `07-measures.txt`. No new
engine ADR: the legs (ADR-1895) carry a third leg, and the Mars terms (ADR-1896) were revised in place.

## Timing (bar 17 at 32.0 s, 1.9835 s a bar)

| Time | What |
|---|---|
| 0-101.4 s | M2: the canyon by day, the Moon, the dusk encounter, Mars |
| 101.4 s (bar 52) | crossing 4: out of the outer cliff at night |
| 101.4-116 s | down to the gathering at eye height: three Sasquatches, the Gray, the embers, the UFO on the bank |
| 116-121.2 s | away over the water's edge; the riverbed's field and tear; down into it |
| 121.2 s (bar 62) | crossing 5: out into deep water |
| 121-140 s | over the seabed, the shelf's wall to the left; the base's lights ahead; a disc leaves the bay |
| 140-147 s | turning into the launch bay behind an arriving disc; the membrane tears onto the UFO |
| 147.0 s (bar 75) | crossing 6: into the UFO (a first form; M4 builds the interior) |

## The idles (owner request)

- **What the asset has.** Mat Rex's Bigfoot ships no rig and no clips (a static mesh). The rig, the skin and an
  `idle` clip are ours (`pass2/build_sasquatch.py`).
- **What the engine plays.** The skinned pipeline plays a named clip per node, looping, with a speed. The phase is
  anchored to the piece's start, so seek equals play.
- **What was wrong.** The M1 idle existed, but it moved the chest 2 degrees, the head and the arms a little: nothing
  read at the camera's distance.
- **What I built.** A new 24 s seamless idle per variant, procedural on the existing bones, so nothing needs
  downloading:
  - breathing in the chest, spine and shoulders (4-5 s);
  - a slow weight shift: hips leaning onto one leg, the spine countering;
  - head turns that hold: toward the fire, toward a neighbour, then back;
  - arm drift and easing forearms;
  - one gesture each, timed differently: A scratches its jaw, B shrugs, C looks over its shoulder.

  The three variants have different cycles and phases, so no two move together. The same clip plays at the dusk
  encounter and the night gathering.
- **The Gray.** The downloaded Grey Alien plays its own Mixamo idle at 0.8 speed.
- **No download needed.** A mocap idle (Mixamo needs an account) would be the next step only if the owner wants more
  life than this.
- `08-idles/` has a 24 s clip of the three at dusk.

## The night (coordinator note 4)

- **The light.**
  - A cold moon high to one side, giving the canyon walls and the river a sheen.
  - A moon rim from behind for silhouettes.
  - A sky that keeps the skyline readable.
  - The fire down to embers, with a low red light on the figures.
  - The contour lines off.
- **The camp.** The camera comes down the cliff to eye height and holds 8-12 m off, so the camp, the UFO and the
  river's sheen share the frame.
- **The gathering.**
  - The Gray joins the circle on the camera's side.
  - The UFO stands parked on the bank downstream of the fire: BrunoLatour's model (CC BY), a 12 m disc. It is made
    opaque, with its own glow at a twelfth, by `pass2/prepare_ufo.py`.
  - The model ships every material alpha-blended and emissive up to 4.9x, so as downloaded it drew as a glowing
    ghost.
- **A builder bug the render found.** `build_journey.py` appended the Gray and the UFO again on every run, so earlier
  copies at the model's own scale (a 666 m disc) hung over the camp and shaded the whole night. The builder now
  replaces them.

## The riverbed portal and the ocean (`build_ocean.py`, `pass2/build_base.py`)

- **The riverbed.** The third leg opens in the riverbed below the camp: the field gathers across the water, and the
  camera drops into it. The rotation turns the drop into a level exit into the ocean.
- **The ocean.**
  - The seabed is the lunar generator: the shelf is a giant mesa whose wall runs beside the route, with rock outcrops
    and a rolling, rock-strewn floor.
  - The water is a dense blue-green fog over a deep blue background, with a cold light from above.
  - Marine snow drifts through it.
- **The base** (original geometry) is set into the shelf's apron:
  - stepped hull tiers, buttresses, pipe runs and towers with red beacons;
  - rows of windows;
  - the launch bay: a deep lit hall behind a framed opening lined with light strips, crossed by an energy membrane.
- **The submersibles.** Three discs (the UFO model, 17, 14 and 11 m) are keyed in the project's timeline, each with
  its own light:
  - one leaves the bay and turns away north;
  - one arrives from the south and enters the bay ahead of the camera;
  - one holds station high by the towers.
- **The bay portal.** As the camera closes on the bay, the membrane tears open and shows the UFO's interior. M4 builds
  that interior; the Gate R2 proof hall stands in.

## Mars (coordinator notes 1-3)

1. **The pyramids are weathered stone.**
   - A stone program (`pyramid_stone`): course lines and long, level block joints from cell edges on a stretched
     position, value variation, vertical stains, and dust on the course treads.
   - Casing and core keep their two tones; the core is darker.
   - The casing's patch edges now blend over a short band, so the grid steps are gone. The grid is also finer.
2. **The mesas' grooves are gone.**
   - The outline's erosion runs at two unrelated scales, and the terraces vary in height.
   - The yardangs no longer cut across the walls.
   - The regolith program adds strata bands on the steep faces, two warped cosine bands of unrelated period.
3. **The plain has a foreground.**
   - Boulders are four times denser.
   - Four fresh craters beside the route strew rocks on their rims.
   - The yardangs wander, and gain a fine set.
   - Wind ripples on the flats fade with distance.
4. **The climb to the cliff** turns the eye toward the cliff half a second earlier (note 5, the cheap part).

## Known weaknesses

- The night ground is still a little warm-brown; the red rock under a faint moon.
- The UFO interior is the proof hall, and past the bay crossing its camera tumbles (the room's flight is not
  authored yet). The delivered sequence therefore ends at the crossing, 147.2 s; M4 builds the room and that leg.
- The base is box-built: its scale and lights read, but it lacks greebles up close.
- The submersibles use the craft model: they read as UFOs, not adapted submersibles.
- The pyramid stone's cells are irregular rather than true coursed masonry.
