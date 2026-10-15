# ADR-1343: An example can be kept out of the menu

- Status: Accepted (2026-10-14), the owner's request
- Amends: ADR-1340 (Examples are Live or Offline) and ADR-1341 (the live scene list).
- Code: `src/app/examples.{hpp,cpp}` (`ExampleInfo::hidden`, `exampleSections`), `src/app/live_scenes.cpp`
  (`liveSceneList`), `examples/index.json`.
- Tests: `tests/unit/test_example_menu.cpp` ("A hidden example is in the index but in no menu", "The real index
  offers six Sonic VFX scenes").

## Context

The owner cut the Sonic VFX family from 16 scenes to six:

- Cymatic Plate
- Feedback Mirror
- Datascape
- Ember Forest
- The Corrupted Cathedral
- Event Horizon

The brief was to determine the blast radius first, and to keep any scene that is easier to keep for testing or code
purposes out of the user menu rather than delete it.

The blast radius of the other ten, from `docs/audits/repository-cleanup/data/scene-removal-impact.json` (all class B,
small):

- **Tests:** only the generic sweeps over every example, and the count/name assertions of the live menu.
- **Tools:** their own generators in `tools/sonic_vfx/scenes/`, plus the default project of
  `tools/sonic_vfx/drum_recall.py` (Salt Flat Mirage).
- **Docs:** ADR-1140, 1142, 1150 and 1153 cite Ferrofluid Crown or Stellar Nursery as render comparisons.
- **Effect coverage:** four are the only authored use of an effect type, and deleting them would leave the type with
  no example:

  | Scene | Effect types |
  |---|---|
  | The Breathing Deep | `pulse`, `breathing` |
  | Ferrofluid Crown | `ripple` |
  | Tesla Choir | `arc`, `discharge`, `electricField` |
  | Storm Cell | `lightning` |

  None of those types is named by a test.

## Decision

- **An index entry may say `"hidden": true`.** It stays in `examples/index.json`. Every index test still sees it:
  "Every example in the index exists and loads" still loads it, and the `sonic.live` ↔ `live` agreement still holds.
  It is offered nowhere: not in File > Examples, not in the Live panel's Scene list, its arrows, PageUp/PageDown or
  MIDI program changes.
- **The ten cut Sonic VFX scenes are hidden, not deleted:** Salt Flat Mirage, Lantern Lake, Aurora Tundra, The
  Breathing Deep, Abyssal Bloom, Silk Theatre, Ferrofluid Crown, Tesla Choir, Storm Cell and Stellar Nursery. No
  coverage is lost, and the cut is reversible by deleting one line per scene.

## Consequences

- The Live menu and the Scene list offer 19 scenes: Sonic Live, Astral Forge Live, Digital Mosh Live, the 9 Sonic
  Abstract scenes, 6 Sonic VFX scenes and THE RIFT LIVE.
- **MIDI program numbers moved again.** Program n is scene n of the 19.
- **Six of the hidden scenes could be deleted outright** with small cleanups: Salt Flat Mirage (repoint
  `drum_recall.py`), Lantern Lake, Aurora Tundra, Abyssal Bloom, Silk Theatre and Stellar Nursery. The other four
  should stay until another scene or a fixture covers their effect types.
