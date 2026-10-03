# ADR-1064: Parameters as signals: a `publish` source, so effects can modulate effects

- **Status:** Accepted (2026-10-02), proto/sonic-garden (VFX expansion, brief §5-6)
- **Code:** `src/signals/publish_source.*`; the kind is added in `SourceRack::create`.
- **Tests:** `tests/unit/test_publish_source.cpp` (`[adr1064]`).

## Context

The brief (§5) asks for nested relationships: audio -> signal -> effect intensity -> effect, where one effect's
modulated intensity drives another. Every effect leaf is already a routable parameter (`fx/<id>/<leaf>`), and a
route's depth can come from a signal (`depthSource`). But no parameter's final value reached the bus. The macro bridge
did this only for `macros/<k>` knobs, which costs a knob and a second route per link.

## Decision

A source kind, **`publish`**:

```json
{"kind": "publish", "name": "fx", "settings": {"parameters": ["fx/heroGlow/gain", "post/bloom/intensity"]}}
```

- It publishes each named parameter's final value as a signal whose name is the path with `/` as `.`:
  `fx.heroGlow.gain`, `post.bloom.intensity`.
- A vector's other components are `<name>.1`, `<name>.2`, and so on.
- It reads when the rack updates, before this frame's finals are reset, so it reports the **previous frame's final**:
  one frame late, exactly like the macro bridge.
- Parameters are looked up by path every frame. An effect's parameter that appears later starts publishing when it
  appears, and reads 0 until then.
- It is not pure in time: what it reads is the output of routes, as with the macro bridge. Two renders of the same
  range agree. A seek reaches it through the routes that feed the parameter.

## Consequences

- Chains like these are now one route each way, with no knob between:
  - "bass -> `fx/heroGlow/gain` -> (publish) -> `lights/spill/intensity` and `post/bloom/intensity`";
  - "MIDI velocity -> `fx/warp/strength` -> (publish) -> `depthSource` of the camera shake".
- A cycle (A drives B, B drives A) is one frame apart in each direction: stable and visible, and it is the author's
  own choice.
