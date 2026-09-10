# ADR-054: Colour that clusters, and glow that is rare

Status: Accepted

## Context

After ADR-053 the ecology was light, but it was one colour repeated. Every fern in the valley
was the same teal and every mushroom the same violet, which reads as a texture rather than as
a population. The existing `MaterialVariation` offered per-instance random hue, which makes
the opposite mistake: every neighbour disagrees, and a meadow becomes noise. What reads as
living is regions that agree with themselves and differ from the next valley over.

## Decision

**A world-space field, not per-instance noise.** `hueField` is how far hue swings across the
map in turns and `hueFieldScale` how large a region is in metres. The field is sampled at the
instance's placement, so neighbours land in the same part of it. `hueRandom` stays available
as jitter on top.

**One field, shared.** `noise::regionField` is the single definition, because two callers must
agree exactly: the plants, and the lights those plants cast (ADR-053 aggregates). If they
drifted, a patch would cast light of a different colour than itself. It also stretches the
noise about its midpoint — raw `fbm3` bunches around 0.5, so feeding it straight into an
amplitude delivers roughly a third of what the caller asked for, which is why the first
attempt at this looked like nothing had changed.

**Rotate hue in OKLCH.** The legacy rotation spins RGB about the grey axis, which changes
lightness and chroma with the angle; on a bright emitter that reads as flickering brightness
rather than a turning colour. `perceptualHue` selects an OKLCH rotation that leaves L and C
alone. Its clamp had to be raised from 8 to 96: instance colour is carried as a per-channel
*multiplier* of the material's, and a saturated emitter has a channel near zero — the ferns
glow (0.02, 1.0, 0.58). Turning that hue means raising the small channel by tens, and a
ceiling of 8 clipped exactly that, so rotating a saturated colour desaturated it to grey
instead of moving it round the wheel. The legacy path keeps its ceiling of 8.

**`emissiveSparsity`: what fraction of specimens do not light up at all.** Random variation is
symmetric — it makes each specimen somewhat brighter or dimmer than the mean — so it cannot
express "most of this forest is dark and a few trees are lanterns". Given only symmetric
variation, lighting a forest at all lit *all* of it, and the wood became a pastel mass with
no silhouette and no night. Sparsity is chosen by a stable per-instance hash, and the
aggregated light a patch casts is scaled by the lit fraction so the light matches what is
visibly glowing.

**Emission in patches, not over a whole mesh.** A layer may name a material program
(`ScatterLayer::materialProgram`). The tree layers use one: a voronoi over the mesh's own space,
thresholded hard so only cell centres emit, offset by the instance's random so two trees of the
same asset are not lit in the same places, gated by local height so the trunk stays dark, and
multiplied by the instance emissive so the regional hue, per-specimen brightness and sparsity all
still apply. A tree then reads as full of fireflies rather than as a lamp shaped like a tree,
which is what a constant emissive over an imported mesh always looks like.

Two traps cost real time here, both of which fail *silently*:

- The scene-file key for a program's emission output is `emission`, not `emissionRegister`.
  Spelling it wrong leaves the register at -1, which means "leave emission alone" -- so the
  program loads, validates, binds and runs, and changes nothing.
- Programs are registered under the composition's prefix (`mp.name = sanitise(prefix_) +
  src.name`), so a raw name from a scene file matches nothing. Terrain already went through
  `prefixed()` for this reason; scatter layers now do too.

Neither produces a warning. What finally located them was forcing the program to emit flat red
and counting red pixels: 34 out of 230,400 says "not applied" in a way that staring at a dark
image does not.

**One material for a merged asset, chosen by area.** A scatter layer's asset is merged into a
single instanced mesh with a single material, and that material used to be whichever one carried
the most vertices. A tree's trunk is a smooth tapered tube spending plenty of vertices on very
little of what anyone sees; its canopy is hundreds of small leaf cards. The trunk won, and every
leaf was drawn as a slab of bark -- which is what made instanced foliage render as dark angular
shards, a defect that had been sitting in the world since the ecology was first planted and was
invisible only because the trees used to be uniformly bright enough to hide their own silhouette.
Picking by summed triangle area fixes it, and incidentally took about 5 ms off the frame, because
the bark material carries a normal map and the leaf material does not.

Area was the right heuristic while there was only one material to pick. There is a draw per
sub-material now (ADR-044, 2026-09-10) and the heuristic survives as the ordering rule: part 0 --
the one the node's parameters bind to -- is the largest by area.

**Airborne life and organic emission.** Two more pieces of the same idea. Drifting spores are an
ordinary particle system, which already reduces its emissive particles to one aggregate sphere for
the volume march (ADR-040), so the air they fill is lit by them without any new machinery. And the
dense frond layer gets a material program of its own: emission broken up by a noise in the mesh's
own space and pushed toward the tips by the leaf's uv, so a frond has internal structure instead
of reading as one flat sticker of colour.

That last one interacts with ADR-052 in a way worth recording. A uniformly lit frond at about
1.9x scene white sits exactly in the band where the tone curve desaturates hardest and where
chroma retention deliberately does not act, which is why the layer looked pale however its hue was
authored. Breaking the emission up carries the bright pools above that band while the mean stays
put, so the plant keeps its colour without the layer becoming a wall of light.

Both are easy to overdo. The first frond pass multiplied a noise ramp by a tip ramp and peaked
near 7x, which blew the tips to cream; and a regional hue swing wide enough to be obvious on
fungi (0.16 turns) reaches amber on a green-teal plant, which reads as autumn rather than as
something alive. The swings are per layer for that reason.

## Consequences

The background participates: trees carrying points of light in their canopies across a dark
hillside, in hues that vary by region. About 1.5 ms for the aggregated lights, and about 3.7 ms
for the firefly program, which evaluates a voronoi for every tree pixel on screen. Dead wood is deliberately left dark — it is the only thing
left holding a hard silhouette against the glow.

Ground detail in the mid distance and background comes from extending the glowing layers' view
distances (70-150 m to 240-320 m). Worth knowing for anything similar: `minScreenRadius` barely
moved the cost (51.6 ms against 51.9 ms), because what a scatter layer costs is the number of
instances the GPU culls, not the number it draws -- so reach costs roughly the square of itself,
and culling the ones too small to see saves nothing. Painting the glow into the terrain material
instead would cost the same at any distance; it is a few full-screen ops and is not built.

One thing this does not fix: the dense ground layer sits near scene white where the tone curve desaturates hardest and
ADR-052's retention deliberately does not act, so it still reads paler than its authored hue.
