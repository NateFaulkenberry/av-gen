---
id: sequencer/camera-direction
title: Directing the Camera to Music
category: Sequencer
summary: What "Enable Auto-director" does, the shot vocabulary it draws on, and how to get the camera back.
order: 45
tags: camera, director, shots, music, structure, drop
keywords: direct to music; automatic camera; camera wont move; hand camera back; cinematic camera; camera automation
related: sequencer/shots, audio/beats, troubleshooting/rendering
features: command.camera.direct, command.camera.hand-back
---

# Directing the Camera to Music

**Camera ▸ Enable Auto-director** folds the loaded track into musical sections and cuts the camera
between the world's heroes, landing a reveal on the drop.

It is disabled without analysed audio — the camera is cut to the track's structure, so there has to
be a track — and it needs a scene with **heroes** declared. A generated world has them; a scene
file declares them in its `heroes` block.

## What it does, step by step

1. Walks the whole analysed track through a fresh musical-event detector and folds the result into
   sections.
2. Casts the shots: the first hero is the hero, the rest are supporting. `Intro`, `Build`, `Drop`,
   `FinalBuild`, `FinalDrop` and `Outro` belong to the hero; everything else goes to the supporting
   cast in a deterministic order.
3. Chooses a shot kind per section:

   | Section | Shot |
   |---|---|
   | Intro | establish |
   | Build | discovery |
   | Phrase | drift |
   | Drop | hero reveal |
   | Verse | transition |
   | Breakdown | approach |
   | FinalBuild | ascent |
   | FinalDrop | reveal |
   | Outro | establish |

4. Groups sections into shots, with one hard rule: **a drop always opens its own shot, exactly on
   the drop.** Elsewhere a section only starts a new shot if the run so far is long enough.
5. Bakes camera keys, lifts any key that would be inside the terrain, the canopy or a hero, and
   installs the result on the timeline.

By default the result is **one unbroken move whose intent changes** rather than a series of cuts:
each shot starts where the last one ended. A breakdown always cuts.

## The shot vocabulary

Fourteen kinds, all composed in multiples of the subject's radius:

| Kind | What it is |
|---|---|
| `establish` | hold wide; the world, not the subject |
| `approach` | move towards; its scale becomes apparent |
| `reveal` | start on something ambiguous, pull back until it reads |
| `entry` | travel into the interior of something |
| `passage` | move through, elements passing on both sides |
| `descent` | travel downward through layers |
| `ascent` | rise towards a luminous opening |
| `orbit` | move around, when the changing silhouette is the point |
| `track` | follow, holding the subject in one place in frame |
| `discovery` | travel towards something partly hidden, arcing so it emerges |
| `heroReveal` | move around a hero while opening out |
| `flyby` | pass close at speed, holding the subject in frame |
| `drift` | lateral travel with the aim held: parallax, not a pan |
| `transition` | leave one subject and find another |

There is no camera shake anywhere in AV Gen, deliberately.

## Getting the camera back

**Camera ▸ Disable Auto-director** removes the camera's automation.

This exists because of what automation does. While the timeline drives the camera, a viewport drag
writes a value the timeline replaces on the next frame — so the mouse appears to do nothing. The
menu item erases the six tracks the director owns:

```
camera/position            camera/lens/aperture
camera/target              camera/lens/focusDistance
camera/lens/focalLength    camera/focus/emphasis
```

It deliberately does **not** disable the whole timeline: a project may automate other things, and
handing the camera back is not a reason to stop those.

## From the command line

`--direct` does the same thing at startup. It needs a scene and heroes, and reports which is
missing.
