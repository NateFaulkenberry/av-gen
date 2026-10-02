# Sonic Garden art restart: why the 16-scene pass failed

The art agent, 2026-10-02, under `03-brief-art-restart.md` (the owner's brief, which governs). This is the brief's
"First Step": each rejected scene examined against the brief's thirteen questions, from the stills and clips in
`~/Desktop/av-gen-review/25-sonic-vfx/`. Contact sheets of the old pass at 4 s and 12 s are in
`~/Desktop/av-gen-review/27-sonic-art-restart/analysis/`.

The verdict: the owner is right. The sixteen are effects that sit in a void or on a single plane. None of them
needs fixing. All of them get recomposed.

## The pattern across the set (the systemic failure)

Count the sheet, not the intentions in the scene files:

| failure | scenes | the brief's name for it |
|---|---|---|
| the background is black or near-black | 12 of 16 | "glowing object in fog", "neon soup" |
| no ground at all | 8 of 16 (Abyssal Bloom, Cymatic Plate, Feedback Mirror, Datascape's void, Stellar Nursery, Event Horizon, Breathing Deep, Silk Theatre) | "effect as environment" |
| the only ground is one flat plane | 5 (Salt Flat, Aurora Tundra, Tesla Choir, Storm Cell, Ember Forest) | "object on a plane" |
| any foreground framing | 3 (Storm Cell's wheat, Lantern Lake's jetty, Ember Forest's trunks) | |
| any readable scale cue | 2 (Storm Cell's power poles, Ember Forest's trunks) | |
| a key light that throws shadow shapes across a world | 2 (Salt Flat, Ember Forest) | "everything glows" |
| the hero is a primitive or an SDF blob | 14 | "procedural sameness" |
| a single real (scanned or modelled) asset used | **0** | |

Five causes sit under those numbers.

1. **The scenes were designed from the mapping outward.** Every scene file opens with a thesis of the form "the X
   answers the Y" (the monolith answers the drums, the lanterns are the melody). The composition was whatever
   the mapping needed to be visible. Nothing was ever judged as a picture before the audio was wired. The restart
   inverts this: place, then composition, then a silent still that has to pass, and only then the sound.
2. **No environment was built.** The engine has terrain, water and scatter, plus an asset library of scanned
   cliffs, rocks, roots, dead trunks, ferns, moss and shrubs, stylized trees and fungi, and HDRI skies. The pass
   used none of it. Every world was boxes, cylinders, tori and SDF blobs, so every scene had the same grain: smooth,
   untextured and synthetic.
3. **Light was emission plus fog.** In most scenes the only light is the hero's own glow, seen through haze. With no
   key light there are no shadow shapes, no contact, no rim, no bounce, and no light falling on anything.
   Emission does not compose, so the hierarchy collapses to "the bright thing".
4. **The palette is neon on black in most of the set.** Teal or cyan leads four scenes, orange or amber three,
   magenta two. Twelve backgrounds are black. Day, overcast, high-key, pale, dawn and noon do not appear at all.
5. **Particles stand in for detail.** Lanterns, specks, sparks, meteors and snow fill space the environment should
   have filled: the brief's "particle wallpaper".

What survives is technique, not composition:
- `voronoiEdge` ground cells;
- mirror twins for reflections (the water reflects only the sky);
- Fresnel rim-lit translucent shells;
- keyed box-emitter ribbons;
- glitch on hits only;
- the engine facts in `PROGRESS-vfx-art.md`.

Four good instincts are also worth keeping as instincts:
- Storm Cell's low camera in the crop, with power poles for scale;
- Ember Forest's receding planes of trunks;
- the Tesla hall's attempt at an interior;
- Breathing Deep's premise of a flooded cavern, which it never actually showed.

## Scene by scene

Each scene answers the brief's questions in order:
1. concept;
2. focal point;
3. what the camera says;
4. foreground;
5. midground;
6. background;
7. scale;
8. secondary elements;
9. identity;
10. rhythm;
11. place or effect;
12. what is generic;
13. what to discard.

### 1. Salt Flat Mirage
1. A black monolith on a salt flat at dusk; the sky is the instrument.
2. The monolith's lit foot, on the left third.
3. Emptiness was the intent. What it actually says is "a default view of a plane": eye height, a centred band of
   horizon.
4. A shadow wedge on a textured plane. Nothing physical.
5. None. The monolith stands alone.
6. A thin strip of procedural mountains melted into haze, and a gradient sky.
7. None. The monolith could be 2 m or 200 m: no figure, no vehicle, no footprints, no repeated forms.
8. Particle meteors, and the polygon pattern on the salt.
9. Only the salt polygons.
10. None: one vertical against one horizontal.
11. Barely a place. This is the brief's "object on a plane".
12. The box monolith, the meteors, the uniform polygons, the strip mountains.
13. The monolith as hero, the meteors, and the idea that one plane is a world.

### 2. Lantern Lake
1. Lanterns released from a lake at dusk, mirrored.
2. None. Two shapeless clusters of soft dots compete.
3. Low over the water, with the jetty as a leading line. That instinct is right, but the line leads to nothing.
4. The jetty, as a silhouette of boxes.
5. Particle clouds over the water.
6. One smooth hill and a flat dusk gradient.
7. None. The lanterns read as bokeh or fireflies, so the lake has no size.
8. A few firefly dots on the far shore.
9. "Dusk plus warm dots." No village, no boats, no trees, nobody who released a lantern.
10. None.
11. The jetty and the water make a stage, but the subject is particle wallpaper.
12. Blob particles, the smooth hills, the gradient sky. The mirror twins read as duplicates rather than as a
    reflection.
13. Lanterns as particle clouds, and the empty hills.

### 3. Aurora Tundra
1. An aurora over a frozen lake whose cracks light up.
2. The aurora, which is the whole sky.
3. Dead level, centred: "look, a sky".
4. A flat green plane with white crack lines, which reads as a map or a game's interface.
5. None.
6. A row of identical tiny spruce spikes, and the aurora.
7. None. No cabin, no figure, no shore.
8. Stars. The diamond dust is invisible.
9. The aurora only. The ice reads as green linoleum.
10. The spruce row, mechanically regular.
11. The aurora is an effect and the ground is an effect.
12. The crack lines, the identical trees, the green floor.
13. The lit-crack ice and the tree strip. An aurora is worth keeping only over a real place.

### 4. The Breathing Deep
1. A colossal fungal organism hanging in a flooded cavern.
2. The glowing gill ring, upper right.
3. Looking up from the pool. The idea is right, but the cavern is invisible, so it reads as a lamp in a void.
4. A few glowing caps and a dark lump, bottom left.
5. Threads and teal specks.
6. Black-teal noise. No cavern wall can be read.
7. None. It could be a lampshade.
8. Threads and specks.
9. "Glowing object in fog."
10. Irregular threads, and noise everywhere.
11. An effect. The premise was never delivered.
12. The chandelier shape, the teal speckle, the uniform dark.
13. All of it except the premise. A flooded cavern returns as a real one.

### 5. Abyssal Bloom
1. A siphonophore and jellyfish in the deep sea.
2. The diagonal stem of beads.
3. A descent through black water, with nothing to descend past.
4. None.
5. The same jellyfish, cloned at several sizes.
6. Black, with a magenta and teal glow.
7. None.
8. The jellyfish clones.
9. Neon on black.
10. The bead chain, mechanically regular.
11. No place.
12. Everything.
13. All of it.

### 6. Cymatic Plate
1. Chladni figures in sand on a vibrating plate.
2. The pattern.
3. A high oblique product shot.
4. None.
5. None.
6. None: a plate in a black void.
7. None. No hand, no lab, no table, no speaker.
8. None.
9. None. It is a shader on a quad.
10. The pattern itself.
11. A pure effect.
12. Everything.
13. The scene. The palette-op figure technique can return as a pattern inside a place (sand, frost, salt).

### 7. Silk Theatre
1. A silk ribbon drawn in the air of an empty theatre.
2. The ribbon, at right.
3. Mid height into a brown void.
4. A dark floor plane.
5. One pool from a spotlight.
6. A vague brown curtain gradient. No proscenium, seats or rigging, so no theatre.
7. None.
8. Gold sparkles.
9. "Brown fog plus a ribbon."
10. The ribbon's curve.
11. An effect.
12. The particle ribbon and the gradient fog.
13. All of it.

### 8. Ferrofluid Crown
1. Ferrofluid spikes in a dish, after Kodama.
2. The central spike tower.
3. A macro product shot.
4. The dish rim.
5. The spikes.
6. Pure black.
7. Macro was intended, but there is no context: no studio, hand or magnet.
8. Amber sparks.
9. An object, not a place.
10. The spike field, the one good rhythm in the set.
11. An object on black.
12. The torus dish and the void.
13. The scene. The SDF dish also cost 26 ms.

### 9. Feedback Mirror
1. A sigil inside video feedback.
2. The sigil.
3. Nothing. It is a 2D graphic.
4. None.
5. None.
6. A purple void.
7. None.
8. None.
9. A VJ loop.
10. The echoes.
11. Exactly the brief's "effect as environment".
12. Everything.
13. All of it. Temporal feedback can come back only as a rare accent inside a place.

### 10. Tesla Choir
1. Twelve Tesla coils in a turbine hall; chords drawn as arcs.
2. The core's top-load and the arcs.
3. A low, wide view into a hall: the most cinematic intent in the set.
4. One coil on the bottom edge.
5. The ring of coils and the cables.
6. The hall's wall with orange window slits, too dark to read.
7. Broken. The coils, stacked tori, read as screws on a table. There is no door, stair or person.
8. Cables, sparks.
9. A dark room with props.
10. Repeated coils, but uniform.
11. A stage set, not a place: nothing explains why the room exists.
12. The screw-like coils, the black floor.
13. The coils. A real interior can take the hall's place.

### 11. Datascape
1. A data landscape, after Ikeda.
2. The vanishing point, or a white stripe.
3. A low glide forward.
4. Red lines.
5. White lines.
6. Black, with the numerals as unreadable blobs on the horizon.
7. None.
8. The numerals.
9. A visualizer.
10. The lines.
11. An effect.
12. Everything.
13. All of it.

### 12. Ember Forest
1. A burnt forest the morning after, embers still alive.
2. Unclear. The dancing ember is lost, and a spark burst competes with it.
3. Eye level, centred. The receding trunks give the best depth in the set.
4. Trunks with lit bark seams.
5. More trunks.
6. A bright fire line and orange fog.
7. The trunks give rhythm, but they are identical branchless cylinders. No fallen log, stump or root.
8. Glowing squiggles on the ground, which read as neon worms, and sparks.
9. Orange haze.
10. The trunks. Good.
11. Partly a place, but one material and one colour.
12. The cylinder trunks, the squiggles, the uniform orange.
13. The squiggles and the cylinders. Keep the receding planes.

### 13. The Corrupted Cathedral
1. A nave drawn in lines of light that glitches on the drums.
2. The rose window.
3. One-point perspective down the nave, centred.
4. A cyan grid floor.
5. Wireframe piers.
6. The rose.
7. The proportions of a nave are there, but in wireframe nothing has mass.
8. Glitch blocks.
9. Synthwave.
10. The bays.
11. An effect styled as a place.
12. The neon line look and the Tron floor.
13. The line look. A real ruined nave can keep the one-point idea.

### 14. Storm Cell
1. A tornado on the night prairie.
2. The funnel, right third.
3. Low in the field: right.
4. Wheat silhouettes.
5. A power line and a farm (two boxes and a light).
6. The clear slot under the wall cloud.
7. The power poles: the best scale in the set.
8. Lightning, rain, the farm light.
9. A prairie storm. This is the closest scene to passing.
10. The poles.
11. A place.
12. The funnel is lit flat and reads as a grey mushroom or a cooling tower. The rain is a screen overlay of streaks.
    The field is dead flat. The night grade turns everything murky grey-green.
13. The night and the flat funnel. It returns rebuilt at dusk, with a low sun under the storm.

### 15. Stellar Nursery
1. Pillars of dust against a nebula.
2. The tallest pillar's glowing head.
3. Looking up at the pillars.
4. The pillars themselves, blobby sausages.
5. None.
6. A teal and orange noise texture, like a screensaver.
7. None.
8. None.
9. Blobs on noise.
10. None.
11. An effect.
12. Everything.
13. All of it.

### 16. Event Horizon
1. A black hole and its disk.
2. The shadow and the photon ring, right of centre.
3. Frontal, like a poster.
4. One out-of-focus rock.
5. The disk.
6. Black.
7. None. A black hole has no size without a reference.
8. Particle streaks, rock fragments.
9. An Interstellar homage.
10. The disk's streaks.
11. An object in space.
12. The particle streaks, the fragments, the poster framing.
13. The framing. The lens itself can return as a sky event above a real shore.

## What the restart changes, in one list

- Every scene is a **named place** a viewer could describe ("a flooded sinkhole at noon"), never "a dark environment
  with glowing objects".
- **Silent first:** every scene must pass a still with all modulation removed (routes, interpret sources and
  triggers stripped) before any audio work.
- **Built environments:** real terrain forms, rock and cliff scans, trunks, roots, plants, water and skies, with
  primitives only where a primitive is the honest shape (architecture).
- **Light first:** a key that throws shadows, a fill from the sky, a rim where the silhouette needs one, and
  emission only where the world has a reason to glow, with a real light beside it so that it lights its
  surroundings.
- **Varied on purpose:**
  - day, dawn, dusk, overcast, night, underwater and space;
  - high-key and low-key;
  - sparse and dense;
  - intimate and enormous;
  - several scenes with almost no particles.
- **The camera is composed:**
  - foreground framing;
  - off-centre heroes;
  - low angles for scale;
  - telephoto compression;
  - occlusion.
- **Audio last,** and it moves the world (water, wind, light, weather, creatures), never a layer drawn over it.
