# Talking Heads: the secondary-FX and 1-4 sources plan (fx1)

This answers `14-brief-secondary-fx.md`. Both of the brief's investigations, art and lifecycle, were done by this
agent alone, because the agent cap was full.

## Audit: why the bass and synth busts converged

Every head had the same five secondary effects (`talking_heads.routes`): the shake, the world-space shear tear, the
signal bands, the two ghost copies, and the wash with its glow. Only per-instrument weights changed between them:
- drums leaned on shake and tear (weight 1.0);
- bass on the ghosts;
- synth on the wash.

With the colours neutralised, the bass and synth heads did the same things at different strengths. The bass ghosts
were the stop-6 copies seated 3 cm behind the head: the owner's "drop shadow".

## The profiles

Each profile is a different phenomenon, not a different weight. The ghosts' drop shadow is gone. The only copies left
are the optical profile's rim-only copies, which have no dark body.

| | FRAGMENT (drums, kept) | LIQUID (bass) | OPTICAL (synth) | GRAIN (vocal / other / the 4th) |
|---|---|---|---|---|
| Geometry | world-space shear tear: the silhouette slices sideways | a low-frequency world-space wave that travels up the bust (face, neck, chest); silhouette kept | none (the silhouette stays still) | none |
| Surface | signal bands, horizontal, hashed | the porcelain stays whole | a scan band sweeping head to chest, darkening a wide trail (surface inversion); slowly evolving emissive veins | dark patches dissolve into the surface and drift down, glowing at their edges |
| Space | shake (trauma) | two irregular pressure rings that expand outward from the neck and chest on bass onsets | two rim-only copies that split apart on onsets and snap back; a slow sustained chromatic drift | grains shed from the head and drift up and away |
| Time | transient only: abrupt on, about 120 ms off | continuous on the smoothed level; heavy 1.2-1.5 s recovery; rings keep travelling after the event | phrase-scale (the scan's rate follows the level); onset snaps of about 120 ms | continuous with the level, plus grain bursts on onsets |
| Signals | `Open` (the gate's rises) | `role.bass.bass` smoothed; `role.bass.onsetStrength` events | `role.synth.mid` sustained; `role.synth.onsetStrength`; a saw LFO for the scan | the slot's level; its `onsetStrength` |

All profiles keep the mouth, rays, eyes, lids, neck swivel, halo mist and wash palette, which the owner said are good.

## 1-4 busts, from the sources

- **Four slots, one per role:** drums, bass, synth, and a fourth for vocal or other (`role.vocal.*`, else
  `role.other.*`). This is the existing source abstraction (ADR-1630): stems or live inputs, by key and role, and
  `role.<role>.active` says whether one is assigned.
- **A slot is shown when its role is active.** A fifth source of an existing role adds no bust, because
  `role.<role>` is the first active source of that role.
- **Zero sources:** the fourth slot becomes a single hero bust listening to the mix, with the rate-limited mix path
  as before.
- **Layout.** The scene computes:
  - each slot's active flag;
  - the active count;
  - each slot's rank (how many active slots come before it).

  Product maps select the slot's entry in `LAYOUT[count][rank]` (position, heading, scale). Routes through a spring
  glide the bust there when the count changes.
- **An inactive slot** shrinks to nothing, and its gate, rays and effects read silence, so nothing leaks and nothing
  is left stale. Busts appear and leave at runtime as sources come and go, with no rebuild.

| Count | Layout |
|---|---|
| 1 | a large hero, centred, forward |
| 2 | an asymmetric pair: one forward-left, one back-right |
| 3 | the stop-5 stagger |
| 4 | two forward and two back, inward-turned, at a slightly smaller scale, none hidden |

## Validation

- **Neutral, beams hidden:** a neutral-palette build, with the rays knob at 0.
- **One to four busts:** the Feline stems, subset per count. The fourth source is the Feline mix as an `other`
  source.
- **Quiet and loud passages, a one-minute full render,** and profiles at 1, 2, 3 and 4 busts.
