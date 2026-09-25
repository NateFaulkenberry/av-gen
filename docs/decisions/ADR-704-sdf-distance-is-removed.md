# ADR-704: `FieldKind::SdfDistance` is removed

**Status:** Accepted
**Date:** 2026-09-24
**Resolves:** ADR-575's "implemented or removed", and supersedes ADR-576's "refuse at load, do not
remove". **Decided by:** the owner's ruling of 2026-09-24 ("remove it").
**Implemented by:** `src/spatial/field.{hpp,cpp}`, `shaders/fields.wgsl`, `src/graph/builtin_nodes.cpp`
**Tests:** `tests/unit/test_fields.cpp` ("a file naming the removed sdfDistance kind is refused by name")

## Context

ADR-027 reserved `FieldKind::SdfDistance`, meaning "the distance to a named SDF object", and never
bound it. It evaluated to 0 on the CPU and 0 on the GPU. ADR-575 found it among §18's fog routes: a
density field using it produced no fog at all. It asked for the kind to be implemented or removed.
ADR-576 chose a third option for the interim, refusing it at load, and left the removal-or-binding
decision open because binding it is a feature. No tracked content names it (`git grep sdfDistance --
examples assets` is empty).

The owner ruled: remove it.

## Decision

Under ADR-441 (no shims), the kind is removed outright:

- the enumerator;
- its sampling, name and validation arms;
- its entry in the kind list;
- the WGSL constant;
- the graph node's choice;
- the documentation rows.

A file that still names `sdfDistance` is an **unknown field kind**. It is refused by the ordinary
enum reader, with a message that names the key. It never loads as some other kind.

**The numbering moved.** Every kind after `Distance` is one lower, and `shaders/fields.wgsl`'s
`FIELD_*` constants were renumbered to match (`FIELD_WAVE` 10 → 9 … `FIELD_GRID` 25 → 24).
Fields are serialised by NAME (`fieldKindName`), so no file changes. The numbers exist only
between the CPU packer (`g.kind = static_cast<uint32_t>(field.kind)`) and the shader, and both
moved together. The GPU field tests render every kind through that pair, which is how the
renumbering is checked.

## Consequences

- One declared kind fewer that answered with silence.
- If an SDF-distance field is wanted later, it is a new kind with a real binding (`Scene::sdfs` exists
  and `SdfRenderer` draws it). It gets a new ADR, not a revival of this enumerator.
