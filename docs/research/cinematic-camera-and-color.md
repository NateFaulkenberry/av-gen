# Cinematic camera, exposure and colour

Status: research (2026-09-09). Decisions: ADR-037, ADR-039.

## 1. Physical camera

A lens is described by focal length, sensor size, aperture and focus distance; those four give
field of view, depth of field and exposure together, which is why artists reason in them.

- Field of view: `fovY = 2 atan(sensorHeight / (2 f))`. A 35 mm full-frame sensor with a 24 mm lens
  is a wide establishing shot; 85 mm is a portrait; the current engine only exposes the resulting
  angle, which loses the connection to depth of field.
- Depth of field: circle of confusion `c = A f (|d - s|) / (s (d - f))` for aperture diameter
  `A = f / N`. The existing post effect takes a focus distance and a manual radius, so it cannot
  be driven from a lens. Physically deriving the radius makes focus behave predictably when the
  camera moves. (Sources: Potmesil and Chakravarty 1981; Pharr, Jakob, Humphreys, "Physically Based
  Rendering", 3rd ed., section 6.2.3.)
- Exposure: the standard photographic triangle gives exposure value
  `EV100 = log2(N^2 / t) - log2(S / 100)`, and the linear scale factor is
  `1 / (1.2 * 2^EV100)`. (Lagarde and de Rousiers, "Moving Frostbite to PBR", SIGGRAPH 2014, is the
  canonical treatment for real-time.) Auto-exposure from a histogram or an average-luminance
  reduction with a speed limit keeps a travelling shot legible.

The current Hyperspace baseline blows out to white when the camera flies into the core, which is
exactly the failure of having no exposure control between HDR values and the tone curve.

## 2. Motion blur

A shutter is open for a fraction of the frame; motion blur is the integral over that interval.
Real-time practice is a velocity buffer plus a tile-based reconstruction (McGuire et al. 2012;
Jimenez's "Next Generation Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014).
That needs per-object velocity, which needs previous-frame transforms for meshes, instances,
deformers and particles. The existing camera-only depth reprojection cannot blur a moving object
against a static camera, which is the case that matters for the reassembly scene.

## 3. Tone mapping

- **ACES** (the RRT/ODT fit by Narkowicz or Hill) is the film-standard look: strong highlight
  roll-off, saturated highlights shift toward white.
- **AgX** (Sobotka, adopted by Blender 4.0) keeps hue far better in over-exposed regions and avoids
  the "ACES pink" on saturated emissives, at the price of a flatter default contrast.
- Given this engine's emissive-heavy, saturated palettes, AgX is the better default and ACES stays
  available. Both are already implemented; the decision is which is the default and how contrast is
  restored afterwards in the grade.

## 4. Halation and lens character

Halation is light scattering in the film backing, seen as a warm red-orange bloom around bright
highlights. Implemented as a bloom variant weighted toward the red channel with a wider radius,
applied selectively to the brightest regions. Anamorphic streaks are a horizontally scaled bloom
pass plus optional flare ghosts. Both are strong "authored film" cues and both must be restrained
by default, per the brief's warning against everything glowing.

## 5. Composition guides

Beyond the lens: framing rules are a directorial tool the engine can support directly. A focal
point in world space, a preferred screen position for it (thirds, centre, golden), and a
composition solver that places or nudges the camera so the point lands there. That is what turns
"a camera that orbits" into "a camera that frames".
