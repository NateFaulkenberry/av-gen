# Interaction affordances (Phase D §16–§18, §72)

## Objects offer verbs; characters have capabilities

`InteractionDesc` (ADR-096) lives on the prop: name, stand socket, activity, duration, range,
exclusivity, conditions, effects, completion event — and, from Phase D, **`requires`**: the
capabilities an actor must have. `EntityDesc::capabilities` lists what a body can do.

**Capability + affordance = valid interaction** (§18), checked twice:

1. **by the planner** — `investigate` (and `goal`) with an `affordance` use the verb only when the
   thing offers it *and* the body qualifies; otherwise the character observes instead (§59). So an
   impossible interaction is never selected;
2. **by the executor** — `ActionKind::Interact` fails with `"missing capability"` if handed one
   anyway (a director, a schedule, a hand-written list).

No branch anywhere names a creature or a prop (§77); the second-creature test grazes a cow on grass
with the same code an alien uses to inspect a mushroom.

## Planning (§17)

"Is it reachable? → interaction point → navigate → approach → turn → stop → look → perform → exit
→ choose next": `Move` to a stand-off inside the verb's `range` (60% of it), `Face` the prop,
`Interact` (which publishes the look target on the prop), then the plan drains, the decider marks the
subject investigated and chooses again. Target disappearance: the percept goes, the option goes, the
hold is released (subject unknown), and a new option wins.

## Validation (§58)

`avgen_character_validate` reports an affordance nothing offers (error) and one every offer of which
requires a capability the character lacks (warning: it will observe instead).

## Limitations

- One verb per considerer; no choice among several verbs on one prop.
- Interaction points are the prop's origin or socket; no multiple approach points.
- Exclusive claims are honoured, but a claimant does not queue — it fails "in use" and suppresses.
