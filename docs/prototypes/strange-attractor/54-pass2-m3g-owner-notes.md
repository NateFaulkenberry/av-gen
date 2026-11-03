# Art Pass 2, M3g: the owner's notes on the M3f full render

The owner reviewed `pass2-m3f/10-full/strange-attractor-m3f-full.mp4` (ef625234) on 2026-11-03. Nothing is merged.
Build M3g on ef625234 plus the 51- notes commit. This file supersedes nothing in 51-; it adds to it. Section 2c of
51- (realistic ocean lighting) is folded into §5 here.

Owner screenshot (read-only, never commit it):
`~/Desktop/av-gen-review/101-strange-attractor/pass2-m3g/00-owner-refs/mars-pyramid-sand-blend-crude.png`.

## 1. Camera direction for the whole film (a standing rule from now on)

This is the most important note. The owner has asked for it several times (M3c Mars "don't spin round the rover",
M3e dusk and night "pass by without focusing on them"), and the camera keeps drifting back to it. Treat it as a
standing rule for every leg, now and in future passes.

**The idea of the film:** a continuous journey from one portal to the next. The camera is a traveller with
somewhere to be. Every leg is: arrive through a portal, cross the world, enter the next portal. The transitions
and the moves are smooth and fluid.

**Where the camera looks:**
- The camera looks where it is going. Its aim stays on, or close to, the direction of travel, and on the next
  portal once that portal is in view. The look and the travel direction never separate by more than a small
  angle (aim for 15° or less; never more than about 25°), except during a dive or climb into a portal (below).
- No "head turns". The camera must never feel like someone walking one way while turning their head to look at
  something else. No look-at targets on characters, props or creatures, and no aim tracks that swing toward them
  and back.
- No orbits, circling, arcs around a subject, push-ins on a subject, or slowing down to show something.
- **No hero shots.** The characters (the Gray, the Sasquatches, the rover, the subs, the creatures) are glimpsed,
  never presented. They stay mysterious and elusive: they appear at the edge of frame or in passing because the
  path happens to go by them, and they are gone a moment later. Place the path so the glimpse happens naturally.
  Do not turn the camera to them.

**What is wanted:**
- Fluid, continuous motion with speed changes that are eased and motivated by the journey (faster across open
  ground, slower near a portal opening). There are no stops and no direction reversals.
- **Swooping and diving: yes.** Climbing over terrain, banking gently into a curve, diving down into a portal (for
  example the night river portal), and rising up out of an arrival are all welcome. Roll, if any, comes only from
  a gentle bank on a curve, never as a wobble.
- **Looking around: no.**

**How to check it** (report it for every leg):
- A per-frame trace of the angle between the camera's forward vector and its velocity vector. Report the maximum
  and the 95th percentile per leg, and flag any span above 25°.
- A per-frame yaw and pitch rate (no reversals outside deliberate curves) and roll (bank only).
- Audit every existing leg against this rule, not only the ones named below: the canyon, Moon, dusk, Mars, night
  and ocean. Remove every look-at and aim track that points at a character or prop.

## 2. Contrast jumps at the portal crossings

- Stepping through the Mars portal, the image contrast changes; the same happens entering the ocean. Find the
  cause (the per-world look or grade, exposure or auto-exposure, the tone-mapping settings, fog or haze density
  switching, or bloom) and make each crossing seamless. If the worlds need different grades, blend them over the
  portal crossing so there is no visible step.
- Check every portal crossing for the same thing. Report the mean luminance and contrast (for example the
  standard deviation of luma) for each frame across ±1 s of each crossing, M3f against M3g, so a step shows up as
  a number.

## 3. Mars

- **Shorten the Mars fly-through,** or make the camera move faster through it. The owner leaves the choice to you;
  if you shorten it, say what changes in the film's timing and keep the music sync.
- The camera rules in §1 apply here: one fluid line from the arrival, over the rover in passing, toward the
  pyramids and into the pyramid portal.
- **Texture work: Mars still looks crude.** See the owner's screenshot: the sand mass banked against the pyramid
  is a pale, smooth, blotchy blob with a hard edge where it meets the darker ground plane. The ground plane is flat
  and featureless, and the dunes behind use a different, rippled texture. It needs:
  - **blended transitions:** the drift sand fading into the surrounding sand with no hard edge or colour step
    (height or slope-based blending, noise-broken edges, sand creeping into the gaps);
  - **one consistent sand material family** across the drifts, the ground and the dunes (the same colour family,
    ripple scale and roughness), varied by slope and wind exposure rather than switched per mesh;
  - **detail at the camera's distance:** ripples and grain on the flat ground near the path, not a flat colour;
  - the pyramid's base courses partly buried and sand-filled, so stone and sand interpenetrate rather than meet at
    a line.
  Show before/after at the screenshot's camera and at 2–3 path beats.

## 4. The night scene

- **New portal entrance and camera path.** The owner doesn't like the arrival coming from high up and swooping
  down to the camp. Make the arrival a **level pass-by**: arrive at a modest height, travel roughly level past the
  camp (the characters glimpsed in passing, per §1), and continue to the river portal.
- **Keep the river portal.** The owner likes how it opens into the river. The camera may swoop up and dive down
  into it; that is the kind of swoop that is wanted.
- **The seated Sasquatch still looks like it is levitating.** In M3f the boulder sits behind its legs, so it reads
  as squatting in mid-air in front of a rock. Fix the seat so the Sasquatch visibly sits on the rock: the rock is
  under its buttocks, the thighs rest on the rock's top and go over its front edge, the feet are on the ground,
  and there is a contact shadow. Either move or reshape the rock under the body, or change the pose; keep the
  no-penetration check (`check_contacts.py`) over the whole loop. Show it from the passing camera, not only from a
  side view.
- Keep: the night lighting, the craft beside the alien in the owner's camp camera, and the beat pulse.

## 5. The ocean: deep research, then a realism pass

The owner: "it's a decent start", but it needs to look much more realistic. This replaces 51- §2c (lighting) with
a wider brief. **Do deep research first** (research-and-plan-first): reference underwater photography and film, real
continental shelf and reef bathymetry, and how underwater scenes are lit and textured in film and games. Plan
before building, and show a plan with stills before the full pass.

- **Lighting:** as 51- §2c (absorption by wavelength, distance haze and backscatter, downwelling light, caustics
  that fade with depth, grounding, local lights in the murk). **Preserve the base's illumination: the owner says it
  "looks great".**
- **Textures:** the seafloor, rocks and reef textures don't look good. Use realistic sediment, rock and encrusted
  reef materials (photoscanned CC0 sets where they help, with tiling broken up as on Mars and in the canyon).
- **Seafloor geometry:** too jagged. The floor and outcrops are spiky and faceted. Make them read as sediment-draped
  rock: rounded, eroded and smoothed by sediment, with soft drifts and some larger rock forms, not sawtooth peaks.
- **The continental shelf:** its top is too flat across the whole length. Add variety: undulation, terraces,
  gullies and canyons cutting the edge, rock outcrops, sediment slopes and an irregular drop-off line.
- **The base:**
  - It must look integrated with the continental shelf, built into the ledge, partly set into the rock, and covert.
  - Right now it reads as a manufacturing plant (a boxy block with chimney-like towers). Make it simpler and
    more hidden: low, set into the shelf face, with a few openings and the launch bay; decent textures (weathered
    concrete or metal, marine growth, sediment on its upper surfaces).
  - Keep the illumination the owner likes.
  - The owner is open to either building a better base (simple, with decent texturing) or finding a model online.
    The hero agent is searching for a candidate model in parallel; compare a found model against your rebuilt one
    and pick the better.
- **Creatures:** the octopus test showed the creatures looking CG. The lighting work should fix most of that.
  Integrate the octopi only when the coordinator says which model (the hero agent is comparing them).
- **Show:** M3f against M3g at the same camera beats (128, 130, 135 and 140 s), the base before and after, the
  shelf before and after, and the ocean clip 121.5–147.5 s with sound.

## Delivery

- Media: `~/Desktop/av-gen-review/101-strange-attractor/pass2-m3g/`, in numbered folders with exact file names.
- **Stop once after the ocean research and plan** (stills and a written plan) for the owner to OK the direction.
  The other items (§1–§4) can be done before that stop, and shown in the same hand-back.
- Then finish §5 and render the full M3g video with sound to `pass2-m3g/10-full/strange-attractor-m3g-full.mp4`,
  with the per-leg camera traces (§1) and the crossing luminance numbers (§2).
- Don't run the suites or merge until the owner approves.
