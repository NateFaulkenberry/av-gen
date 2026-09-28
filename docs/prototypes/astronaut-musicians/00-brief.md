# Astronaut musician prototype (the owner's brief, 2026-09-28)

**Queued; not started.** It starts after the QA pass, in parallel with the GV3 art pass, once the owner has
restarted Claude Code. One dedicated agent runs it, the `astronaut-prototype` type in
`~/.claude/agents/astronaut-prototype.md` (Opus, max effort).

**Before launch, the coordinator checks:**
- **Blender MCP is not yet registered with Claude Code.** On 2026-09-28 Blender 5.2 had the "MCP for Blender" 1.7 add-on
  installed (`~/Library/Application Support/Blender/5.2/scripts/addons/blender_mcp.py`), but no Claude Code
  configuration named an MCP server.
  - Registration: `claude mcp add blender -- uvx blender-mcp` (uvx is at `/opt/homebrew/bin/uvx`).
  - Then, in Blender, start the add-on's server from the sidebar (N panel, "BlenderMCP", Connect).
  - Confirm the blender tools exist in the new session before launching the agent.
- **Assets:** `~/Desktop/musician_assets/` (6.9 MB), as found on 2026-09-28:
  - `Rigged Low Poly Astronaut v1.0 — BLEND + FBX/`: `Untitled.blend`, `Untitled.fbx`;
  - `Basic_Keyboard/`: `BasicKeyboard.{obj,fbx,mtl}`, `BasicKeyboard_IMG.png`;
  - `Folding_Stand/`: `OBJ/Folding_Stand.{obj,mtl}`, `LWO/Folding_Stand.lwo`;
  - `lowpoly-drums/`: `drums.{fbx,obj,mtl,blend}`;
  - `Piano Playing.fbx` and `Playing Drums.fbx` (Mixamo).
- **Licensing is unknown**, and the repository is public. The asset files stay out of git (gitignored, as with the
  farm and alien packs) until their licences are established. The asset report records whatever licence
  information the files carry.

---

# Astronaut Musician Prototype — Asset & Animation Validation

We are going to prototype two low-poly astronaut musicians before committing anything to Glowmere Valley 3.

## Objective

Create a **very simple standalone AV Gen demo project** containing two low-poly astronauts:

1. An astronaut sitting and playing the keyboard
2. An astronaut sitting and playing the drum kit

The purpose of this project is **visual and technical validation**, not yet adding anything to GV3.

I want to be able to load the resulting demo project in AV Gen and immediately judge:

* How good the astronauts look
* Whether the Mixamo animations work correctly on the astronaut rig
* Whether the seated poses and hand positions work convincingly with the instruments
* Whether the instruments are positioned correctly
* Whether the overall aesthetic is appropriate for Glowmere
* Whether this is worth promoting into the actual GV3 project

Do **not** modify GV3 during this task.

---

# Available Assets

All source assets are located in:

`~/Desktop/musician_assets`

Use whatever files are actually present there. Do not assume filenames/extensions beyond what is listed below.

### Astronaut

`Rigged Low Poly Astronaut v1.0 — BLEND + FBX`

This is the character that should be used for BOTH musicians.

The important requirement is that this astronaut must work correctly with the supplied Mixamo animations.

### Keyboard

`Basic_Keyboard`

This is the keyboard for the piano-playing astronaut.

### Keyboard stand

`Folding_Stand`

The keyboard should be positioned correctly on top of this stand.

### Drum kit

`lowpoly-drums`

This is the drum set for the drummer.

### Animations

`Piano Playing.fbx`

Mixamo piano-playing animation.

`Playing Drums.fbx`

Mixamo drum-playing animation.

---

# CRITICAL REQUIREMENT: VALIDATE THE RIG FIRST

Before building the demo scene, investigate the astronaut asset and the two FBX animations.

Determine:

1. What skeleton/armature the astronaut uses.
2. Whether it is already Mixamo-compatible.
3. Whether the supplied Mixamo animations can be applied directly.
4. If not, determine the minimum required retargeting/conversion process.
5. Verify the resulting animation visually in Blender.

Do NOT simply assume that "humanoid" means Mixamo compatibility.

Actually test the animation on the astronaut.

Pay particular attention to:

* skeleton hierarchy
* bone naming
* bone orientation
* root/pelvis behavior
* arm/hand positioning
* leg/foot positioning
* scale
* rest pose
* animation frame rate
* animation duration
* FBX import settings
* any unwanted translation/rotation offsets

If retargeting is necessary, establish a **clean, repeatable workflow** rather than manually hacking individual bones.

---

# USE BLENDER THROUGH THE MCP INTERFACE

Blender is now available through the Blender MCP interface on the machine.

Use the Blender MCP interface for this task where it provides an advantage over the previous headless workflow.

The goal is to actually inspect the models and animation visually in Blender.

You may use Blender's Python API/scripts where appropriate, but do not blindly generate a scene without visually validating the result.

The Blender MCP interface should be treated as the preferred interactive tool for:

* importing assets
* inspecting the armature
* applying/retargeting animations
* positioning the characters
* positioning instruments
* checking hand/instrument alignment
* rendering preview images/video
* making visual corrections

Headless Blender is still acceptable for repeatable asset-processing operations if useful, but the final result must be visually inspected.

---

# SUBAGENT REQUIREMENT

For all 3D/art/asset work, use a dedicated sub-agent running **Opus 5.5 Max**.

Have the sub-agent focus specifically on:

* Blender asset inspection
* character rig compatibility
* Mixamo animation retargeting
* character/instrument positioning
* visual quality
* proportions
* seated pose correctness
* hand placement
* avoiding obvious intersections
* overall visual presentation

Do not use a weaker model for the core 3D-art judgment if the Opus 5.5 Max sub-agent is available.

Have the sub-agent review the result after the initial implementation and make corrections where necessary.

---

# DEMO SCENE

Create a tiny standalone demo project specifically for this experiment.

Do NOT recreate Glowmere.

The scene only needs enough environment to make the characters easy to evaluate.

Something like:

* simple ground plane
* neutral lighting
* neutral/dark background
* two astronauts separated enough to inspect individually
* keyboard astronaut on one side
* drummer on the other

The scene should be visually clean and uncluttered.

The purpose is to evaluate the **characters, animation and props**, not environment art.

---

# KEYBOARD ASTRONAUT

Create one astronaut using:

`Rigged Low Poly Astronaut v1.0 — BLEND + FBX`

Apply:

`Piano Playing.fbx`

Place the astronaut in the correct seated position relative to:

`Basic_Keyboard`

Place the keyboard on:

`Folding_Stand`

The keyboard must actually sit naturally on the stand.

Check:

* astronaut hips/body are positioned naturally
* hands reach the keyboard
* arms don't obviously intersect the keyboard
* hands aren't floating substantially above/below the keys
* keyboard is at an appropriate height
* astronaut feet/legs don't create obvious intersections
* the character's orientation makes sense relative to the instrument

If the Mixamo animation assumes a different instrument position than the supplied keyboard, adjust the **character/instrument transforms**, rather than destructively modifying the source animation, unless modification is genuinely necessary.

The goal is to preserve the animation as a reusable animation clip.

---

# DRUMMER

Create a second astronaut using the same astronaut source asset.

Apply:

`Playing Drums.fbx`

Place the astronaut naturally behind:

`lowpoly-drums`

Again, prioritize visual plausibility.

Check:

* seated position
* pelvis/legs
* torso
* arm positions
* hands/sticks
* drum placement
* kick drum position
* snare position
* toms
* cymbals
* overall proportions

The drummer should look like they are actually sitting at and playing the kit.

If the animation's hand positions don't perfectly correspond to the supplied drum model, make sensible spatial adjustments to the drum kit before considering modifications to the animation.

---

# CHARACTER APPEARANCE

Keep the astronaut asset essentially as supplied.

Do not redesign the character.

Do not add elaborate materials or effects.

We are specifically evaluating whether this character works as a performer in AV Gen.

Minor material/lighting adjustments are fine if they improve readability.

---

# AV GEN INTEGRATION

Once the Blender-side assets and animations have been validated, create the smallest appropriate AV Gen demo project using the existing scene/entity infrastructure.

Follow the existing conventions used by the project's other simple demonstration scenes, including the existing farm-animal-style demos.

Do not introduce a new generalized character architecture yet.

Do not build Character Synthesis yet.

Do not refactor the renderer or scene system unless a genuine blocker is discovered.

The purpose of this prototype is to answer:

> Can AV Gen currently represent and render a rigged low-poly character playing an instrument using an imported skeletal animation?

If the current engine already supports the necessary functionality, use it.

If something is missing, identify the smallest implementation required to make this prototype work.

---

# IMPORTANT: DO NOT OVERENGINEER

This is an investigation/prototype.

Do NOT:

* modify GV3
* create a generalized character synthesis system
* build a character editor
* build an animation state machine
* build procedural character generation
* create new character schemas unless absolutely required
* add elaborate environment art
* add audio reactivity
* add director integration
* add AI character generation
* build a reusable character authoring UI

Those may become future work if this prototype succeeds.

For now we want:

**two astronauts + two instruments + two animations + working AV Gen scene**

---

# VISUAL QUALITY BAR

Even though this is a prototype, don't accept an obviously broken result.

Before declaring success, visually inspect the animations.

Look specifically for:

* broken shoulders
* twisted elbows
* wrist rotations
* hands passing through instruments
* feet clipping through the floor
* character floating above the chair/instrument
* incorrect scale
* animation sliding
* unnatural root motion
* instruments floating
* obvious mesh intersections
* incorrect drum/keyboard proportions
* animation stopping unexpectedly
* animation looping badly

The result doesn't need to be cinematic.

It needs to look **credible enough that I can make an informed decision about whether these characters belong in Glowmere Valley.**

---

# VALIDATION / DELIVERABLES

At the end, provide:

## 1. Working demo project

A project I can open in AV Gen containing both performers.

## 2. Blender validation scene

Keep the Blender scene used to validate the character rigs and animations so it can be inspected later.

## 3. Animation compatibility report

Document:

* astronaut skeleton structure
* whether Mixamo animations work directly
* any retargeting required
* exact conversion/retargeting process
* any limitations discovered
* whether the resulting animation can be reused on additional astronaut instances

## 4. Asset report

For each asset:

* source filename
* format
* approximate polygon/triangle count where available
* materials/textures
* whether conversion was necessary
* any licensing information that can be established from the supplied asset/source

## 5. Visual review

Include preview renders/screenshots of:

* keyboard astronaut
* drummer
* both astronauts together
* representative animation frames

If practical, create a short preview render showing both animations playing.

## 6. Recommendation for next step

Do NOT give a generic "looks good."

Instead answer concretely:

* Does the astronaut work well with the Mixamo animations?
* Are the performances visually convincing?
* Are the instruments appropriately scaled?
* What, if anything, would need to be improved before putting them into GV3?
* What pieces of this prototype would be reusable toward a future AV Gen character-animation/Character Synthesis system?

---

# STOP CONDITION

Do not add these characters to Glowmere Valley 3.

Once the standalone demo is working and visually validated, stop.

I will review the result and decide whether the astronauts are good enough to become part of GV3.
