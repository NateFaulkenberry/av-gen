# ADR-1872: Mesh wire v2: the scan grows out of the portal, and its rock sheds shards

- Status: Proposed (Gate A, dimensional portals), scene/strange-attractor.
- Extends ADR-1862. Reads ADR-1873's corruption block (the focus, the field, the opening) from the frame.
- Code:
  - `scene::MeshWire` gains these keys, all `meshWire/<key>` parameters:
    - `fromFocus`, `pulse`, `pulseSpacing`, `pulseSpeed` and `pulseWidth`;
    - `focusGain` and `rimGain`;
    - `shards` and `shardRate`;
    - `frontColor` (a vec3 parameter).
  - The renderer's parameter block grows from 80 to 128 bytes, and its per-draw block from 128 to 144. The new lane
    flags a corruption target.
  - `shaders/mesh_wire.wgsl` includes `corruption.wgsl`. A second pipeline, `vs_mesh_shard`/`fs_mesh_shard`, draws
    the shards.
- Tests:
  - `tests/unit/test_mesh_wire.cpp` (`[adr1872]`): the keys parse and round-trip, and pulses or shards alone make the
    block active.
  - The ADR-1862 GPU gate test still holds: an inactive block draws nothing and its bytes are unchanged.

## Context

The owner's brief (`docs/prototypes/strange-attractor/12-dimensional-portals-brief.md` §2.1) asks for these changes
to the triangle tracing:
- scan fronts that reveal the terrain progressively;
- a copper-to-gold palette;
- density that varies;
- more scanning toward the portal;
- the scan converging on the opening.

ADR-1862's front was a ring around a point ahead of the camera, in one colour.

## Decision

- **Fronts from the focus.** With `fromFocus`, the event wave (`wave`) grows out of the corruption's focus (3D
  distance), so each front sweeps out of the portal toward the camera.
- **Pulses.** Periodic fronts travel out of the focus, spaced `pulseSpacing` m apart, at `pulseSpeed` m/s.
  - Each front is gated by the corruption field at the edge, so far away there are none. As the camera approaches,
    the field grows and they appear: more scanning near the portal, with no route needed.
  - Their position is `fract((r - t·v) / spacing)`, a pure function of the clock.
- **Heat.** Each line carries a heat value from 0 to 1. The colour is `mix(color, frontColor, heat)`:
  - ahead of a crest the line is hot gold, and behind it the copper cools;
  - the rim is hot.
- **Density near the focus.** Lines brighten by `1 + focusGain·field`, and the screen-density fade relaxes by up to
  30%, so the lattice is denser toward the portal.
- **The opening.**
  - No line is drawn inside the opening once it has split. It is a void.
  - The rim is drawn on the rock's own edges (`rimGain`) rather than as a band painted on the surface, which the
    curled lips would smear.
- **Following the rock.** A target's vertex positions go through the same `crDisplace` as the lit pass, so the
  lines stay on the corrupted rock.
- **Shards.**
  - A share (`shards`, at full opening) of the triangles on the opening's lips, where the edge factor runs from 0.95
    to 2.4, break off.
  - Each is read from the mesh's own buffers and de-indexed on the fly.
  - Each is drawn opaque, flying toward the focus and back into the throat, turning and shrinking. It is lit by the
    directional lights and edged in the glow colour.
  - Each shard runs its own cycle at `shardRate`, so the shedding is continuous and a pure function of the clock.

## Consequences

- With none of the new keys set, the wire is ADR-1862's. `fromFocus` 0 keeps its origin ahead of the camera.
- The shard pass draws every triangle of a target that the camera can see. The shader drops all but the lips at
  once (offline cost, not measured live).
- The shards cast no shadow and write zero velocity.
