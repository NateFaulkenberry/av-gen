# ADR-274: A star is project data, and until now it never reached a render

**Status:** Accepted
**Date:** 2026-09-18

Measured: **one hero in the session, zero after a save and a reload.**

Starring an object in the world editor calls `ui::setNodesHero` → `Composition::setHeroes`, and
stops there. A project whose scene came from a file saves that scene **by reference** —
`assets.scene.path` plus a hash of bytes already on disk — and nothing but a "Save Scene As..."
dialog ever writes a scene file. So the star lived in the composition the window was drawing and in
no document any render reads. An offline render builds its own `Engine` and loads the *project*
(`startRenderFromUi` saves the project for you and then renders *that file*), so a starred hero
never reached a deliverable at all.

This is the third instance of one defect. ADR-207 found it for world effects, ADR-230 for
atmospheric effects — "the save kept every number of the aurora and lost the aurora" — and this is
heroes. The family is: **an edit that lands in the `Composition` rather than in a parameter is lost
by a save that writes the scene by reference.**

---

## 1. Why the project and not the scene

ADR-271 settled this for a different subsystem and the argument transfers without change.

Nothing but a file dialog writes a scene file, so a re-authored hero is discarded by the next
Cmd-S. And a scene is shared between projects — `glowmere-stylized.scene.json` backs two — so an
editor that wrote scenes would change a project the user did not open and re-fingerprint the file on
every star.

So `doc["heroes"]` is written by `Engine::saveProject`, beside `cameraAimFollow` and
`cameraShotSpans`, which sit at the same boundary and were already there.

---

## 2. The rule, which is its two siblings' rule

Written **only when the session's list is not the one the scene file holds**, so a project that
opened a scene and rendered it keeps the file it had. Both sides compared after a round trip through
the same `fromJson`/`toJson`, so a hand-typed `0.0055` and the float the engine ran it as are one
authored value rather than an edit.

An **emptied** list is a difference like any other and is written as an empty array. An unstar that
only survives while the process does is the identical defect pointing the other way, and a save that
wrote only additions would look exactly like a working fix from the positive arm.

Nothing is written for an inlined composition: `assets.scene.inline` is `Composition::toJson`, which
already carries `heroes`. A second copy beside it would be a second answer.

On load, the heroes are applied **before** the world effects, and not by taste: a `WorldEffect` may
name a hero as its source, and `setHeroes` is what decides whether that name is real. The scene
file's own reader orders them the same way and says so.

A hero the project cannot read is **refused by name and the project still loads**. Refusing the
whole block rather than skipping the member is ADR-067 and ADR-070, twice bitten: a hero that
quietly failed to load looks exactly like a hero nobody declared.

---

## 3. Why this one matters more than its two siblings

`cameraAimFollow` was already saved, and it names its heroes **by name**.
`Composition::applyDirectedAim` skips an entry whose hero is not in `heroes()`. So before this fix a
project could save a complete directed cut and reload it aimed at nothing — the table survived and
the things it pointed at did not. The two halves of ADR-158's bake were on opposite sides of this
boundary, and only one of them had been noticed.

---

## 4. The arms

`tests/unit/test_hero_project_round_trip.cpp`, each verified to fail before the fix (three of four
cases did, with `heroesAfterReload == 1` reporting `0 == 1` — the defect's own measurement):

* **A** — star, save, reload: the hero is there, with its position, radius, height, importance and
  preferred camera distance intact. Not merely present: ADR-230's shape was a round trip that kept
  the numbers and lost the thing, and this is the opposite check.
* **B** — unstar, save, reload: gone, and stays gone.
* **C** — never star: no `heroes` key is written at all. The control. Without it, arm A would pass on
  a save that wrote an empty array into every project in the repository.
* **D** — the scene file is byte-for-byte unchanged by any of it. ADR-271's rule, and the whole
  argument for putting the star in the project.
