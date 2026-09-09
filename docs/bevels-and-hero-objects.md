# Bevels, and the hero-object benchmark

A mathematically sharp edge is the loudest single tell that geometry was generated rather than
made. Nothing else about a surface -- not its material, not its lighting -- recovers from it,
because a real edge is a small radius that catches a moving highlight, and the eye reads that
highlight before it reads anything else.

## The bevel

`makeBeveledBox(size, subdivisions, bevel, bevelSegments)` builds the Minkowski sum of a smaller
box with a sphere: six flat faces over the inner rectangle, twelve quarter-cylinder fillets along
the inner edges, eight spherical octants at the inner corners. Normals are analytic rather than
averaged, so a 2-unit box with a 5 cm bevel still shades correctly.

`bevel` and `bevelSegments` are part of `SourceSpec`, so they are hashed (an edited bevel
re-uploads the mesh), serialised, and exposed as `source/bevel` and `source/bevelSegments`.
`bevel: 0` reproduces the old primitive exactly, so no existing scene moved.

Two details are worth keeping in mind if this is extended to the other primitives:

- **Patch boundaries have to match exactly.** They do here because `cos(pi/2)` is 4e-8 rather than
  0 in float, and the endpoints of every quarter turn are therefore forced to exact values. Without
  that the fillets sit a hair away from the faces and the corner poles do not collapse.
- **A collapsed edge cannot be found by triangle area.** With FMA contraction `cross(v, v)` does
  not evaluate to exactly zero, so the corner poles are detected by their coincident corners
  instead.

## What the bevel work actually cost, and why

The first version passed a test that checked positions, normals, watertightness and determinism --
and rendered nothing. All six flat faces and all twelve fillets were wound inward, so back-face
culling removed them and only the corner octants ever drew.

The symptom was that flat metal faces were black while curved ones caught light. That is also
exactly what a broken specular IBL looks like, and I diagnosed it as one: a metal cube in a
uniformly bright sky rendered at the background value and did not respond to sky intensity at any
roughness. Every one of those observations was true, and the conclusion was wrong, because the
faces I was measuring were not being drawn at all. The fix took a minute; finding it took an hour
of increasingly baroque shader probes.

**A geometry test that does not check winding does not check that the surface can be seen.** The
test now asserts that every triangle's geometric normal agrees with the normals its vertices
carry.

## The hero-object benchmark

`examples/hero/` is one machined part on a plinth, lit by two softboxes and a rim against
near-black. It exists to answer a single question: can this engine make *one* object look
photographed? If it cannot, more objects will not help.

What the shot is made of:

- **Geometry**: a housing, a raised boss, a bore with a brass collar, six bolts and four ribs.
  Every box is beveled; the bolts and collar give the small scale that makes the large scale
  readable.
- **Environment**: `StudioSoftbox`, plus a procedural sky that is bright across the whole upper
  hemisphere. This matters more than it sounds: a flat horizontal face seen from a three-quarter
  camera reflects towards the *horizon*, not the zenith, so a dome that is bright overhead and
  dark at the horizon leaves every flat face black. For a metal, the environment is the material.
- **Lens**: 85 mm at f/4 with physical depth of field, manual exposure.
- **Materials**: `brushed-metal` on the housing and boss, `dark-steel` on the ribs.

## Where it stands

It reads as a machined part in a studio: the bevels carry a highlight all the way round, the
brushed program gives the sides a directional sheen, the brass reads as a different metal, and the
part sits on its shadow rather than floating.

It is not yet a photograph. The breakup is macro only -- there are no machining marks, no edge
wear, no seam where the boss meets the housing, and no dust. The plinth is a featureless slab that
takes up the lower third of the frame and competes with the subject. The bore and collar read as a
handle rather than as a bore. Those are the next three things, in that order.

## The loop

Every change is recorded in `docs/experiments/` by `tools/experiment.py`: hypothesis, the one
variable that changed, expected, actual, metrics before and after, and KEEP / REJECT / REVISIT.
The rejected ones are the more useful half. Metrics are evidence; a decision line that cites only
numbers is a decision that has not looked at the image.
