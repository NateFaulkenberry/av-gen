# ADR-937: A section's treatment is edited where it is chosen

**Status:** Accepted
**Date:** 2026-09-27
**Follows:** ADR-247 (the shot language: built-ins are code, a project's own treatments are data),
ADR-920 and ADR-921 (a treatment's arc and cut rate reach the director, and the section inspector says
what the chosen one does to the cutting), ADR-387 (the owner's rule: anything visible is controllable)
**Found by:** the GV3 revision's gv3-cut stream (`phase3/cut.md`, "Open, for others": "The app: a custom
treatment's dials have no editor (they are tuned in `songcut.TREATMENTS`)")
**Implemented by:** `ui::treatmentDials`, `TreatmentDial`, `treatmentEditable`, `treatmentUsers`,
`editTreatment`, `copyTreatmentForSection`, `focusLabel`, `framingLabel`, `arcLabel`
(`src/ui/treatment_editor_logic.{hpp,cpp}`, ImGui-free and linked into the CPU suite);
`SequencePanel::drawTreatmentSettings` (`src/ui/sequence_panel.{hpp,cpp}`)
**Tests:** `tests/unit/test_treatment_editor.cpp` (`[adr937]`, 5 cases)

## Context

A treatment (a `song::ShotIntent`) is what Song Mode cuts a section by: who it is about and how tightly,
the range of shot sizes, how much the camera moves, the energy, the variety between shots, the cut
rate, how full the frame is, how many cameras, and how all of that travels across the section (its arc).
It sets the pacing a viewer sees.

Glowmere Valley 3 defines seven of its own in the project's shot language ("GV3: the riser's roll",
...). They appear by name in the Sequence panel's section inspector, in the "shot" picker, with the
Director's reading beside it ("cuts: ...", "cut rate X% at its start, Y% at its end", ADR-921). But
nothing in the app could change one. Their dials lived in the project file (and, for GV3, in the
generator's `songcut.TREATMENTS`), so the thing that decides how a section is cut was visible in the
picture and in the picker, and reachable only by editing JSON.

## Decision

**1. The inspector shows the chosen treatment's dials, under the picker.** Below "shot" and the cut
notes, a tree node reads "treatment settings: <the treatment's name>". Inside it, one row per dial, in
words for what it does to the cut, each with a tooltip:

| Dial (file key) | Row | What it does |
|---|---|---|
| `focus` | "about" | one hero, the group, the world, or no preference |
| `focusStrength` | "holds its subject" | 0 a suggestion, 1 never leave them |
| `framing` | "tightest shot", "widest shot" | the range of shot sizes (each picker offers only sizes that keep tightest <= widest) |
| `movement` | "camera movement" | 0 locked off, 1 always moving |
| `energy` | "energy" | how hard the shots push |
| `variation` | "variety between shots" | 0 every shot alike, 1 every shot different |
| `cutFrequency` | "cut rate" | 0 one shot, 1 as fast as the beat allows |
| `visualDensity` | "how full the frame is" | 0 one thing, 1 a full frame |
| `cameras` | "fewest cameras", "most cameras" | most 0 = "all there are" |
| `arc` | "across the section" | steady, rising (a build), falling (a release), held (a pause), burst then settling (a drop) |

The name and description are there too, as text. The rows are drawn from one table,
`ui::treatmentDials()`, so the table is what a test holds against the treatment's file format: every key
`shotIntentToJson` writes, bar the id, name and description, must be a dial, and nothing else.

**2. A project's own treatment is edited in place.** An edit replaces the definition in the project's
shot language (`ShotLanguage::defineIntent`), so:
- every section using it follows; the inspector says how many before the edit is made ("this project's
  own; an edit reaches the 2 sections using it");
- the project's save writes it (`Sequence::toJson` writes a language's own definitions), and the
  unsaved-changes check (ADR-440) sees it, because it serialises the project and compares;
- each gesture (a slider drag, a picker choice, a text field) is one undo step, because `beginEdit`
  photographs the whole sequence, shot language included;
- Song Mode re-cuts when the gesture ends (`onSectionsEdited`), not on every frame of a drag. The cut
  notes above the dials follow the drag as it happens.

An edit is **refused, not clamped** (ADR-225), with nothing changed, when a dial is out of range, and
when the id is a built-in or unknown.

**3. A built-in treatment is shown, not edited, and offers "Edit a copy".** A built-in is code (ADR-247):
editing it in place would either change the engine for every project or silently shadow it for this
one. So its dials are drawn greyed, and "Edit a copy" defines a copy as one of the project's own (id
`<id>_custom`, then `_custom_2`, ...; name "<name> (custom)"), gives it to this section, and opens it for
editing. The other sections keep the built-in. (Shadowing a built-in for a whole project remains possible
in the file, as ADR-247 describes; a shadow is one of the project's own, so it is editable here too.)

## Consequences

- **Where an artist finds it:** Sequence panel -> click a section in the strip -> Section inspector ->
  under "shot", "treatment settings: <name>" -> the rows above. No existing control moved.
- **GV3's treatments** can be tuned in the app. Saving the project writes the edited definitions, which
  win over the generator's until the generator runs again (it writes the project's shot language from
  `songcut.TREATMENTS`). Carry an app edit back into `songcut.TREATMENTS` to keep it.
- **No existing scene or behaviour changes.** Nothing is drawn differently until a section is selected,
  and nothing is edited until someone edits.
- **Tests** (each with its control):
  - the dial table against the file format's keys (fails for a dial added to one and not the other), and
    each 0..1 dial writing the field its key names;
  - an edit reaching both sections that use the treatment and the cue sheet Song Mode cuts from, while the
    section on a built-in, and its cue, are untouched;
  - the three refusals (a built-in, with "edit a copy" in the message; an unknown id; a cut rate of 1.5),
    with the language byte-for-byte what it was;
  - "Edit a copy": `<id>_custom`, then `<id>_custom_2` for a second section on the same built-in, each
    section on its own copy, and the copy editable;
  - the save: an edit made the panel's way, on the engine's own sequence, marks the project unsaved and
    comes back from the project file exactly (a cut rate of 0.9375, cameras 2 to "all", focus "one
    hero", the description); the same project saved without the edit comes back with the dials it had.
