# Navigation architecture (Phase D §12–§15, §23, §72)

Navigation existed before Phase D (ADR-193 onward); this records what Phase D found and changed.

## Layers (§13)

```
goal (a considerer's option)          -> destination (stand-off point; `approach`)
reachability (NavGrid regions, `connected`)
path planner (NavGrid A*, 4 m cells, tie-break on cell index) -> waypoints (string-pulled)
local steering (Navigator::steer; crowd separation; arrive radius)
CharacterIntent HOW (desiredVelocity, facing)  -> MotionRequest (Phase B/C, when enabled)
```

The motion system never sees the path. The choice of a grid rather than Recast/Detour (§12): the
worlds are heightfields with analytic walkability; a 4 m grid answers reachability and routes in
~25 µs (ADR-268) with no dependency. A navmesh would pay for multi-level geometry AV Gen does not
have. Hierarchical pathfinding and flow fields are not needed at the measured cost; crowd simulation
is separation, not ORCA (§14) — see limitations.

## What Phase D added

- **Arrive** (§14): `ActionDesc::arrival` — speed proportional to the distance left inside a
  slowing radius, floored at a fifth of pace. Aware considerers use 2.5 m. The pre-existing
  braking-limit stop held full pace to within centimetres (measured: 1.80 m/s in the last metre
  against 1.08 required).
- **Vector intent** (§15): `Move`/`Face` publish desired velocity and facing; the gait limiter
  rescales it with `speed`, so the request built from it equals the polar one.
- **Failure memory** (§59): an unreachable target is suppressed for `failSeconds`.
- **Commitment** (§20): a running errand is held while its subject is known.

## Wander (§23)

`interest` over percepts is §23's "choose candidates → score novelty/distance/recent visitation →
choose → navigate": candidates are what the body perceives (vistas, shore points, landmarks, glow),
scored by taste × nearness × novelty (visited). Reachability is not pre-filtered; an unreachable
choice fails fast and is suppressed. Open: area-biased and patrol modes.

## Limitations

- Dynamic obstacles: bodies separate; they do not replan around each other.
- No ORCA/RVO; dense crowds interpenetrate briefly (bounded in the 20-alien test).
- The grid refuses to answer for worlds where it disagrees with the analytic terrain (Glowmere),
  falling back to analytic queries — correct, slower.
- Every scene node is a landmark, including terrain; aware deciders refuse the `terrain` tag, the
  legacy ones still may choose it.
