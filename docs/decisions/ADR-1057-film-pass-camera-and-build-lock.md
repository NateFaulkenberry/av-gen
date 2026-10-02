# ADR-1057: The validator's film pass: the camera and the transforms as played, and build-lock

- Status: Accepted (2026-10-02), proto/liminal-space (All You Got art pass 4, PART 1, PART 7, PART 15)
- Extends: ADR-1051 (static validation) and reuses ADR-1053's offline playback.
- Code: `src/app/film_validate.*`; `src/app/transform_watch.hpp` (the transform classifier, now shared with
  `--trace-jumps`); `src/app/space_validate_cli.cpp` (`--film`, `--fps`, `--range`, `--camera-trace`, `--no-motion`,
  `--no-film-camera`).
- Tests: `tests/integration/test_film_validate.cpp` (`[adr1057]`).

## Context

ADR-1051 checks the camera's AUTHORED path (journey control points at eye height). What the owner sees is the camera
the engine actually places: journey distance keys, look-at blends, breath, the guard, keyed yaw and pitch, and SDF
nodes where their keys and routes put them. The ~2:15 artifact was invisible to the static check: the authored path
was clear; the swing of the look-at put the eye 13 cm from a bookcase. And the owner's "everything is slightly
twitching" is about motion over time, which no static check can see.

## Decision

`avgen --validate-space <project> --film` plays the project through `app::Engine` offline (no window, no GPU), as
`--trace-jumps` does, and after each frame (30 per second by default):

1. **Camera** against the live SDF (each visible `scene::SdfObject`, its live tree and transform, clipped to its march
   bounds, because nothing outside them is drawn):
   - `cameraInside` (critical): the eye is inside solid.
   - `cameraCrossing` (critical): the segment from the previous eye to this one passes through solid while both ends
     are outside (sphere-traced; not across a cut: `cutSerial` changed or the eye jumped over 1.5 m).
   - `nearClip`: 45 frustum rays; any that hits nearer (in view depth) than the camera's near plane. The SDF march
     starts at the near plane, so such geometry is cut open. Critical when it is 10% of the view or more.
   - `closeGeometry` (warning): 35% or more of the rays hit within 0.3 m (wide-angle distortion).
   - `cameraClearance` (info): within 0.12 m.
   - `cameraEarly` (warning): inside a room annotated `"opensAt": s` before that time.
   Runs of frames are grouped; each item has the time and 30 fps frame range, the eye and the point, the object and
   the entity whose box holds the point. `--camera-trace f.csv` writes every sample.
2. **Build-lock** (`buildLock`, PART 15: build, then lock). Every transform parameter of SDF objects and nodes and of
   composition nodes (translation, position, size, scale; rotation one axis at a time) is watched while visible. A
   transform is "built" once it has held still for `settle` (0.3 s). After that, every DIRECTION REVERSAL is an
   event: a burst of motion that goes back the way the previous one came, or a turn within a burst. Reversals no
   more than `clusterGap` (1.5 s) apart form a cluster. A cluster of at least `minReversals` (3) whose range is
   small (0.3 m, 25%, 45 degrees), or which reverses `jitterRate` (3) times a second or more within three times
   that range, is a wobble. One-way moves (a drop into place, a rebuild, a spin, whose wrap is not a reversal) never
   reverse, so a build-and-lock rhythm passes however often it builds. `--motion-trace "<path part>=<f.csv>"` writes
   the samples of the matching transforms.
   - It is a WARNING on a structural transform: one carrying an architecture or furniture entity, or untagged
     (treated as structural). Decor that may move (`moves: true` in the rules: hanging lamps, fans, curtains,
     plants), characters, text and entities tagged `"moves": true` are not watched; `motion.allow` lists path
     substrings to skip.
   - Large back-and-forth motion (flying parts) is INFO `structuralMotion`.
   - Each item names the routes (source, amount, depthSource, chain) and tracks that drive it.
3. **The report.** The film's violations join the static report (`mergeFilmReport`) under the group "film", and its
   statistics go under `film` (camera samples, closest approach; transforms watched, locked, wobbling, moving,
   rebuild events).

## Consequences

- On pass 3 (about 1 minute for the 258 s film): the ~2:15 near-plane clipping and close-up (`StudyBookcase`), a
  camera pass through the walls of tree room `Room_13` at 2:44.93-2:45.03 (the cut into the gallery), and six
  wobbles: the row of houses' keyed shake (28.8-32.4 s, 0.61 m), the row's roof yaw flicks (20.7-28.4 s, 25
  degrees), the skyline towers' heights pulsed on the sixteenth (28.6-36.9 s), and the dance's rocking furniture
  (3:02-3:18). Large swings (INFO): the falling road dashes, the dance's kettle, and the study's C14 lurch (the
  whole study moves 1 m back and forth, 2:15.97-2:20.87).
- Gaps: mesh text and particles are not part of the camera's geometry (SDF only); the near-plane test uses 45 rays,
  so a thin pole can slip between them; `opensAt` must be authored.
