# Sonic Garden art restart: sixteen places (the plan, before any build)

The art agent, 2026-10-02, under `03-brief-art-restart.md`. This is pass 1 (concept) for all sixteen scenes, written
before any of them is built, as the brief asks. The failures this plan answers are in `ART-RESTART-ANALYSIS.md`.
Status per scene and the review media are in `PROGRESS-art-restart.md`.

## How the set is varied on purpose

| # | place | time and key light | palette | environment | dominant material | scale | density | camera | particles |
|---|---|---|---|---|---|---|---|---|---|
| 1 | The Cenote | noon; one shaft through a roof opening | turquoise, limestone, sun gold | cave and pool | wet limestone, clear water | large but enclosed | medium | low wide from a ledge, looking up across | dust in the shaft only |
| 2 | Stones at Dawn | sunrise behind fog | pale rose-grey, heather mauve, peach | moor | lichened stone, heather | human | sparse | long lens, low, stones overlapping | none |
| 3 | Moss Garden in Rain | overcast, shadowless | every green, one red maple | temple garden | moss, wet stone, dark wood | intimate | dense | seated, framed by the veranda | rain |
| 4 | Crystal Canyon | hard noon sun | terracotta, sand, pale cyan crystal | slot canyon | sandstone, crystal | enormous | sparse | worm's-eye up between walls | none |
| 5 | Northern Fjord | aurora (night) | blue-black, aurora green, one red cabin | fjord and peaks | snow, rock, still water | enormous | sparse | wide on the shore, mirror below | none |
| 6 | Above the Clouds | sunrise, side-back light | peach, gold, lavender | sky islands over a cloud sea | grass, rock, cloud | enormous | sparse | slightly high, the island off-centre | a few petals |
| 7 | The Drowned Colonnade | sun shafts through water | deep blue, sand, marble | sunken ruins | marble, rippled sand | large | medium | low along the colonnade, deep perspective | plankton |
| 8 | The Palm House | warm lamps and moonlight through glass | deep green, sodium amber, iron black | Victorian glasshouse | glass, iron, leaves, tile | human | dense | low among leaves, off-axis | mist |
| 9 | The Abbey Ruin | low afternoon sun through arches | warm grey stone, grass green, gold | roofless nave | weathered stone, ivy, grass | large | medium | nave floor, piers converging | dust in the light |
| 10 | The Wisp Marsh | a hazed moon (night) | grey-green monochrome, pale cyan wisps | bog | dead wood, black water, reeds | human | medium-sparse | water level, boardwalk as a line | the wisps only |
| 11 | The Fungal Wood | dusk sky and gill glow | umber, olive, amber, violet haze | giant fungal forest | soft fungal flesh, moss | enormous over small | dense | worm's-eye from the moss | spores |
| 12 | The Lighthouse | blue hour | slate blue, one orange band, white tower | sea cliffs | wet rock, calm sea | large | sparse | high on a cliff, long lens across the bay | none |
| 13 | Supercell | golden hour under a storm | storm blue-black, gold, wheat | plains and farm | wheat, wood, cloud | enormous | sparse | low in the wheat, wide | rain shafts |
| 14 | Rain City | neon and sodium (night) | wet black, amber, one magenta | street canyon | wet concrete, metal, glass | large | dense | rooftop edge, looking down | rain |
| 15 | The Lava Field | the lava itself (night) | black, orange-red, steam grey | volcanic shelf | basalt, lava crust | enormous | medium | low on the rock, river as a line | sparks |
| 16 | The Black Hole Shore | the disk (cosmic) | space black, disk gold, dark water | shallow alien sea | glassy water, black rock | cosmic | sparse | low on the shore, horizon low | none |

The variety, counted:
- **Light:** eight bright or day scenes, and eight dark ones. Each dark scene has a different light source:
  aurora, lamps, moon, gill glow, beam, neon, lava, the disk.
- **Particles:** five scenes have none.
- **Density:** four are dense, six sparse.
- **Architecture:** three scenes put it first (the palm house, the abbey, the city), and two more give it a supporting
  role (the cabins, the lighthouse).
- **Water** appears in seven.

The set list runs from quiet and luminous, through the organic and the mysterious, into weather and energy, to the
cosmic finale.

## The rules every scene is built under

1. **A named place first.** The premise is a place a viewer could describe. It is never "a dark environment with
   glowing objects".
2. **Blockout before detail:**
   - terrain and big forms, the hero, the camera and the key light;
   - rendered silent at 960x540;
   - judged against the brief's quality gate before anything small is added.
3. **The silent still has to pass** (`variant.make_silent`: no routes, no interpret sources, no triggers, no audio)
   before any audio work. A scene whose look depends on a route fails by construction. The base look lives in
   parameters.
4. **The key light throws shadows.** Emission appears only where the world has a reason to glow, and it has a real
   light beside it so that it lights its surroundings.
5. **Audio moves the world, never a layer over it:**
   - water, wind, weather, light, creatures and growth;
   - post effects only as punctuation;
   - glitch only on hits, and only where the place is digital (the city's signs).
6. **Live has to hold:** 60 fps at 1080p realtime, with the editor's adaptive scale as the safety net, never the plan.

## The sixteen

Each scene lists its emotion, hero and art direction, then:
- **camera:** lens, height, angle, and where the hero sits;
- **light:** key, fill, rim and practicals;
- **palette;**
- **materials;**
- **scale cue;**
- **references;**
- **silent life:** what moves with no music;
- **audio:** what the sound moves, after the silent pass.

### 1. The Cenote: a flooded sinkhole at noon

*Serenity, reverence.*

**Hero.** A single column of noon light falling through a round opening in a cave roof onto a small rock island where
ferns and a young tree grow, in turquoise water.

> Wide view from a wet limestone ledge inside a cavern. The cave roof is a dark vault. A ragged round opening in its
> upper left lets in one column of noon sun, which falls at a slant onto a small island of rock and ferns in the
> right-centre of the frame, on the lower third. Hanging roots trail from the opening's rim down through the shaft
> and catch its light. The pool is clear turquoise in the shaft and deep teal-black at the walls; pale sand shows
> under the water at the island's foot. A wooden walkway with a rope rail runs from the foreground ledge out
> toward the island: human scale and a leading line. Stalactites fringe the vault; the far wall is lost in a blue
> haze. Dust motes hang in the shaft. Almost nothing glows: the drama is one light.

- **Camera:** 22 mm, 1.5 m above the water on the ledge, tilted up about 10 degrees. The shaft runs diagonally from
  upper left to the island on the right third.
- **Light:** the key is the sun through the opening, a shadowed light with its shaft in the haze. The fill is a cool
  teal bounce from the water. The walls get bounce only, and the island's sand a warm bounce.
- **Palette:** turquoise `#2fb5b0`, deep teal `#0b3a40`, limestone `#8a8170` and `#2a2722`, sun `#fff1cf`,
  fern `#4f7a3a`.
- **Materials:** wet limestone (scanned rock), clear turquoise water, scanned ferns and roots, and the walkway's wood.
- **Scale cue:** the walkway and its rail, the island's tree, the size of the opening.
- **References:**
  - Cenote Suytun's platform in its beam;
  - Ik Kil's hanging vines;
  - the daylit dolines of Son Doong (Ryan Deboodt's photographs);
  - Deakins' single-source shafts.
- **Silent life:** the motes drift, the water's surface breathes, caustics shimmer on the island, the roots sway a
  little.
- **Audio:**
  - sustain: the shaft breathes, as if a cloud passed over the opening;
  - kick: a drop falls through the shaft and a ring spreads on the pool;
  - notes: drips along the rim, placed left to right by pitch, each a glint and a ring;
  - bass: the caustics' strength on the walls;
  - hats: the motes sparkle.

### 2. Stones at Dawn: a stone circle on a moor

*Mystery, the ancient.*

**Hero.** The tallest stone of a ring, leaning slightly, backlit by the rising sun through fog.

> A long-lens view across a heather moor at sunrise, with fog lying in the hollows. A ring of weathered standing
> stones stands on a low rise in the midground. The tallest leans a little and stands on the right third with the
> low sun just behind its shoulder, so its edges burn while its face stays cool and dark. The stones overlap in
> depth, the far ones paler in the fog. In the foreground, out of focus, tufts of heather and a lichen-crusted
> boulder frame the lower left. Behind the ring the land falls away in layers of hills, each paler and pinker than
> the last. The sun's rays fan through the gaps between the stones and across the fog. Nothing glows: the only
> light is the sun.

- **Camera:** 85 mm, 1.2 m, looking slightly up the rise. The compression stacks the stones and the hills.
- **Light:** a low sun behind the ring. It throws long shadows toward the camera, and forward scattering in the fog
  draws the rays. The fill is the cool sky.
- **Palette:** fog `#c9b8b5`, heather `#6e5466`, lichen stone `#6f6d62`, sun `#ffd7a8`, shadow blue `#3e4758`.
- **Materials:** scanned lichened stone and boulders, heather and grass, wet ground.
- **Scale cue:** the heather and the foreground boulder against the stones; the hills behind.
- **References:**
  - Callanish at dawn;
  - Friedrich's *Dolmen in the Snow*;
  - Constable's *Stonehenge* watercolour.
- **Silent life:** fog drifts through the ring, the heather stirs, the rays shift slowly.
- **Audio:**
  - sustain: the fog thickens and lifts;
  - notes: the sun's rim brightens on the stone that holds the note's pitch class (subtle, never a glow);
  - kick: a gust runs through the heather and lifts the fog;
  - snare: birds lift from behind the ring;
  - hats: dew glints in the heather.

### 3. Moss Garden in Rain: a Kyoto temple garden

*Serenity, intimacy.*

**Hero.** A red maple leaning over a still pond beside a stone lantern. It is the only warm colour in the frame,
seen through a temple veranda.

> A seated view from the dark wooden veranda of a temple, looking out into a moss garden in steady rain. The
> veranda's eave and a post frame the top and left edges in near-black, and its polished boards, wet at the edge,
> run in along the bottom. Beyond, the ground is a continuous carpet of moss in every shade of green, rolling over
> roots and stones toward a small pond. A Japanese maple in full autumn red leans over the pond on the right third,
> with a granite lantern beneath it whose fire window is faintly warm. Cedar trunks rise into soft grey mist
> behind. The rain shows as a fine veil against the dark trees and as rings on the pond. The light is overcast and
> shadowless: the green is luminous because nothing is bright.

- **Camera:** 35 mm, 1.0 m (seated), level. The veranda frames the left and the top.
- **Light:** an overcast sky, soft, with no hard sun. The lantern is a very weak warm point. Nothing glows.
- **Palette:** moss `#4f6b2f` and `#7c9a45`, dark wood `#1c140e`, granite `#8a8a84`, maple `#b8321f`,
  mist `#b9c1bd`.
- **Materials:** scanned moss, wet stone, dark wood with wet highlights, a pond with rain rings, leaves.
- **Scale cue:** the veranda's boards and post, the lantern, the trunks.
- **References:**
  - Saihō-ji and Gio-ji in rain;
  - Ozu's seated camera;
  - Kawase Hasui's rain prints.
- **Silent life:** rain, rings on the pond, the maple's leaves stirring, mist drifting.
- **Audio:**
  - hats: the rain's density;
  - kick: a heavy drop from the eave splashes on the boards and rings the pond;
  - notes: a maple leaf falls for each note and floats;
  - sustain: the lantern's flame and the mist;
  - bass: the maple sways.

### 4. Crystal Canyon: an impossible geode in a slot canyon at noon

*Wonder, heat.*

**Hero.** A colossal cluster of pale cyan-violet crystal prisms bursting out of the sandstone canyon wall and catching
the sun.

> A low-angle view up a narrow sandstone canyon at midday. Terracotta walls rise on both sides and frame a strip of
> white-hot sky. On the right wall, two thirds of the way up, a cluster of enormous translucent crystals erupts
> from the rock like a geode turned inside out. They run from pale cyan to violet, each prism the height of a house,
> throwing hard glints. Smaller crystals stud the canyon floor, a dry riverbed of pebbles and rippled sand leading
> away from the camera. A single dead tree clings to a ledge for scale. The light is hard, with black shadows and
> warm bounce filling the shadow side of the canyon. There is no haze except a faint shimmer at the far end, and
> almost no particles.

- **Camera:** 20 mm, 1.0 m, tilted up about 15 degrees. The walls converge, and the crystals sit upper right.
- **Light:** a hard high sun. The fill is the warm bounce from the sandstone. The crystals carry a subtle inner glow
  and a Fresnel rim.
- **Palette:** terracotta `#b4532a` and `#7a3418`, sand `#d9a66b`, sky `#f4efe6`, crystal `#a6e3f0` and
  `#8f7fe0`, shadow `#3a1d14`.
- **Materials:** scanned sandstone cliffs, glassy crystal, scanned pebbles and stones, dead wood.
- **Scale cue:** the dead tree on its ledge, the boulders, the pebbles.
- **References:**
  - Antelope Canyon and the Zion Narrows;
  - the Naica crystal cave;
  - Moebius's desert canyons.
- **Silent life:** the shimmer and slow glints.
- **Audio:**
  - notes: a crystal rings, its inner light flaring up the prism (low notes in the big crystals);
  - sustain: the cluster's inner glow;
  - kick: a pulse of light through the cluster and a sift of sand from the wall;
  - hats: facet glints;
  - bass: the shimmer's strength.

### 5. Northern Fjord: aurora over Lofoten

*Wonder, stillness.*

**Hero.** A green aurora arching over a sharp snow peak, doubled in the still black fjord. Red fishing cabins give the
one warm light.

> A wide night view across a still Arctic fjord. A steep snow-streaked peak rises left of centre, and the aurora's
> green curtain arcs over it from the upper left into the right of the sky, its rays hanging down. The fjord
> mirrors all of it. On the right third, on a rocky point, three red cabins stand on wooden stilts over the water,
> one window lit warm. Snowy boulders and seaweed-dark rocks fill the foreground shore, with a small rowboat pulled
> up on them. The far shore is a dark band of mountains with snow on their shoulders. The moonless sky is deep
> blue-black with stars, and the snow takes a faint green from the aurora.

- **Camera:** 24 mm, 1.5 m on the shore rocks. The water's mirror fills the lower third.
- **Light:** the aurora is the key. The fill is a faint, cool moonless sky. The cabin window is a warm point.
- **Palette:** night `#070b16`, deep blue `#10213a`, aurora `#4cf08c` with a violet top `#8a5cf0`,
  snow `#cfd8e3`, cabin red `#8e1f17`, window `#ffb35a`.
- **Materials:** snow and rock, black mirror water, wooden cabins.
- **Scale cue:** the cabins, the boat, the boulders.
- **References:**
  - Hamnøy and Reine aurora photographs;
  - Peder Balke's northern seascapes;
  - Harald Sohlberg's *Winter Night in the Mountains*.
- **Silent life:** the aurora drifts slowly, the mirror trembles faintly.
- **Audio:**
  - sustain: the curtain's brightness;
  - chords and tension: the violet fringe;
  - notes: rays travel along the curtain at the pitch's place;
  - kick: the curtain's lower edge flares and a ripple runs through the mirror;
  - bass: the swell under the reflection.

### 6. Above the Clouds: a floating garden at sunrise

*Transcendence.*

**Hero.** A floating island with a great tree, its waterfalls pouring off into the cloud sea.

> A wide, slightly elevated view across a sea of cloud at sunrise. The floating island hangs on the left third, its
> tree catching the first gold light on its crown while its rocky underside trails roots and vanishes into blue
> shadow. Two thin waterfalls spill off its edge and dissolve into mist above the cloud tops. Far away, smaller
> islands repeat the shape in paler blues and give the scale. The sun is just under the horizon on the right, so
> the cloud sea glows peach where it faces the sun and turns lavender in shadow. In the foreground, the grassy edge
> of another island with a few wildflowers crops the bottom right, close and soft.

- **Camera:** 28 mm, from the foreground island's edge, looking slightly down.
- **Light:** a low sun from the side and behind. The fill is the sky; the cloud sea is bright.
- **Palette:** peach `#f6b98b`, gold `#ffd27a`, lavender `#9a8fc4`, sky `#a9c6e8`, shadow `#4b5a86`.
- **Materials:** the island's rock and grass, cloud, falling water.
- **Scale cue:** the distant islands, the waterfalls, birds.
- **References:**
  - Roger Dean;
  - *Laputa*;
  - Bierstadt's light.
- **Silent life:** clouds drift, waterfalls fall, birds glide.
- **Audio:**
  - sustain: the sun's rays;
  - bass: the cloud sea swells;
  - notes: petals fall from the tree;
  - kick: a gust and a burst of spray from the falls;
  - hats: glints in the spray.

### 7. The Drowned Colonnade: sunken ruins

*Melancholy, mystery.*

**Hero.** A toppled colossal marble head lying on the sand at the end of a sunken colonnade, in slanting sun shafts.

> Underwater, a few metres above a sandy floor, looking along a double row of broken marble columns that recede into
> blue. Some columns stand to full height, some are snapped, and one has fallen across the path. At the end of the
> row, on the left third, a colossal carved head lies on its side, half buried in sand, its face turned toward us.
> Sun shafts from the unseen surface slant down through the water from the upper right and lay moving caustic nets
> over the sand and the marble. Sea grass grows at the column bases and sways, and a school of small silver fish
> turns in the midground. Beyond 40 m the water swallows everything into one blue. Nothing glows.

- **Camera:** 24 mm, 3 m above the floor, a gentle downward look. The columns are receding lines.
- **Light:** the sun from above, with shafts and caustics. Blue absorbing water. The sand is the bright ground.
- **Palette:** deep blue `#0b2f4d`, teal `#2a7f8f`, sand `#c9b48a`, marble `#d8d2c4`, sea grass `#3f6b3a`.
- **Materials:** rough white marble, rippled sand, sea grass.
- **Scale cue:** the columns, the head, the fish.
- **References:**
  - Baia's sunken city;
  - Jason deCaires Taylor's underwater sculpture;
  - the Pharos ruins at Alexandria.
- **Silent life:** the caustics move, the sea grass sways, the school circles, plankton drifts.
- **Audio:**
  - bass: the caustics' strength and sway;
  - notes: the school flashes silver as it turns, placed by pitch;
  - kick: a puff of sand at the head and a pulse in the light;
  - sustain: the shafts' brightness;
  - hats: plankton glints.

### 8. The Palm House: a Victorian glasshouse at night

*Lush mystery, warmth.*

**Hero.** The central palm under the glass dome, rising above a round pool, lit by warm lamps.

> Inside a Victorian palm house at night. Wrought-iron ribs curve up into a glass dome, and their rhythm frames the
> top of the frame. A tall palm rises from the centre into the dome, and round its base a stone pool reflects warm
> lamplight. The camera is low among big leaves: philodendron and fern crowd the left and bottom foreground in near
> silhouette. Along a tiled path, cast-iron lamp posts glow sodium-warm, each lighting a pool of tile and leaves.
> Through the glass, the moonlit night is cool blue. Mist hangs among the leaves.

- **Camera:** 28 mm, 0.8 m, looking slightly up toward the dome. The palm sits on the right third.
- **Light:** warm practical lights at the lamps, leaves throwing their shadows. Cool, dim moonlight through the glass.
  Mist in the air.
- **Palette:** deep green `#12301c` and `#2f5b2a`, sodium `#ffae4a`, iron `#121212`, moon blue `#5c79a8`,
  tile `#b7a68c`.
- **Materials:** glass, dark iron, scanned tropical leaves (anthurium, calathea, pachira, fern), tile, stone, water.
- **Scale cue:** the lamp posts, a bench, the path.
- **References:**
  - Kew's Palm House;
  - the Royal Greenhouses of Laeken;
  - Henri Rousseau's jungles.
- **Silent life:** mist drifts, leaves stir, the pool trembles.
- **Audio:**
  - notes: the lamps along the path brighten in pitch order;
  - kick: a mist sprinkler bursts;
  - sustain: the mist glows;
  - bass: the leaves sway;
  - snare: lightning outside flashes cold through the glass.

### 9. The Abbey Ruin: a roofless Gothic nave in afternoon sun

*Transcendence, melancholy.*

**Hero.** The great empty window at the end of the nave, its tracery against a bright sky, with light pouring through
the arcade.

> Standing in the grass of a roofless Gothic nave in late afternoon. Stone piers march toward the east end, their
> arches open to the sky, ivy climbing them. At the end, the great window, its glass long gone and its tracery
> mostly intact, frames a bright sky and a tree beyond. A low sun from the right comes through the empty arcade and
> lays long bars of gold across the grass and the fallen stones. A young ash grows from the nave floor on the left.
> Fallen tracery and moss-covered blocks lie in the foreground grass. Dust and pollen hang in the bars of light.
> The sky in the window is the brightest thing in the frame.

- **Camera:** 24 mm, 1.6 m, slightly off the nave's axis. The piers converge.
- **Light:** a low sun with shadows, and bars of light in the air. The fill is the sky.
- **Palette:** stone `#9c9282` and `#5d564c`, grass `#5d7a35`, ivy `#2e4a22`, sun `#ffcf7a`, sky `#dfe9f2`.
- **Materials:** weathered stone, ivy, grass, moss.
- **Scale cue:** the piers, the tree, the fallen blocks.
- **References:**
  - Turner's *Interior of Tintern Abbey*;
  - Friedrich's *Abbey in the Oakwood*, daylit;
  - Fountains Abbey.
- **Silent life:** the grass sways, dust drifts in the bars, the ivy stirs.
- **Audio:**
  - sustain: the bars' brightness;
  - notes: doves lift from the ledges;
  - kick: dust sifts down from the walls;
  - bass: the ash sways;
  - hats: pollen glints.

### 10. The Wisp Marsh: a bog at night

*Unease, quiet dread.*

**Hero.** A crooked dead tree on a hummock in a black pool, with pale wisps hovering near it.

> At water level in a night marsh. Black, still water fills the lower half and mirrors the fog. A broken boardwalk
> of grey planks leads in from the bottom left and ends, half sunk, short of a hummock where a crooked dead tree
> stands on the right third, its branches reaching. Reeds and sedge crowd the near water in silhouette. Dead trunks
> stand in the fog behind, each fainter than the last. A hazed moon behind the fog gives a cold grey-green light.
> Three or four small pale lights hover low over the water between the trunks: not bright, just there.

- **Camera:** 24 mm, 0.5 m above the water, slightly up. The tree sits on the right third.
- **Light:** the moon behind the fog. The wisps are small lights with a short reach.
- **Palette:** fog `#6f7d72`, water `#070a09`, dead wood `#3b3a33`, moon `#c9d4cc`, wisp `#bff7ff`.
- **Materials:** scanned dead trunks and roots, reeds, black water, planks.
- **Scale cue:** the boardwalk's planks, the reeds.
- **References:**
  - Rackham's bogs;
  - Grimshaw's moonlight;
  - the swamp in *Annihilation*.
- **Silent life:** fog drifts, the wisps hover, the reeds sway.
- **Audio:**
  - notes: wisps appear over the water at the pitch's place and drift;
  - kick: a ring from something under the water;
  - sustain: the moon through the fog;
  - bass: the fog heaves;
  - snare: the wisps gutter.

### 11. The Fungal Wood: a forest of giant fungi at dusk

*Alien abundance, awe.*

**Hero.** One colossal mushroom whose cap fills the top of the frame, its gills glowing amber, spores falling like
snow.

> A worm's-eye view from a mossy forest floor into a forest of giant fungi at dusk. On the right, the stem of a
> colossal mushroom rises out of frame. Its cap spreads over the top of the frame, and its underside of fine radial
> gills glows a soft amber that lights the spores drifting down and the moss below. More giant stems recede into
> blue-violet mist, each smaller. The floor is a thick carpet of moss, ferns and smaller mushrooms, with fallen
> logs. A path of stones far off gives the scale. The palette is earth: umber, olive, fawn, with violet only in the
> far air. The glow is gentle; nothing is neon.

- **Camera:** 18 mm, 0.3 m, looking up about 25 degrees.
- **Light:** the dusk sky as fill. The gills are a warm light pointed down at the floor. Mist.
- **Palette:** umber `#4a3426`, olive `#5c6b2e`, fawn `#c7a57c`, gill amber `#ffb45e`, violet haze `#6f5f9a`.
- **Materials:** soft fungal flesh, moss, bark, ferns.
- **Scale cue:** ferns and small mushrooms at the lens against the giant cap.
- **References:**
  - the Sea of Corruption in *Nausicaä* (Miyazaki);
  - Haeckel's fungi plates.
- **Silent life:** spores fall, mist drifts, ferns stir.
- **Audio:**
  - kick: caps release puffs of spores;
  - sustain: the gill glow;
  - notes: small mushrooms along the floor light in pitch order;
  - bass: the mist moves;
  - hats: spore glitter.

### 12. The Lighthouse: blue hour on a calm sea

*Isolation, melancholy.*

**Hero.** A white lighthouse on a dark rock island, its beam beginning to sweep through the sea mist.

> From a high cliff top at blue hour, looking down and across a calm sea to a small rocky island with a white
> lighthouse and its keeper's cottage. The beam is just starting to turn, a pale cone in the low mist on the water.
> The last orange band of sunset glows on the horizon behind the island; everything else is cool blue. In the
> foreground, the cliff edge with sea pinks and grass and a weathered fence post. Dark sea stacks stand off the
> coast on the left. The calm sea holds the band of sunset light. A few gulls.

- **Camera:** 50 mm, on the cliff top 40 m above the sea. The lighthouse sits on the right third; the horizon is high.
- **Light:** the blue-hour sky and the sunset band. The lamp and its beam turn, a light in the mist. The cottage
  window is warm.
- **Palette:** blue `#2b3f63` and `#4a6a96`, band `#f29b5a`, white `#e9ecef`, rock `#1d2026`, grass `#3d4f33`.
- **Materials:** scanned coastal cliffs, calm sea, grass.
- **Scale cue:** the cottage, the tower, the gulls.
- **References:**
  - Hopper's *The Lighthouse at Two Lights*;
  - Fastnet;
  - Turner's seascapes.
- **Silent life:** the beam turns, mist drifts, the sea swells, gulls glide.
- **Audio:**
  - kick: a swell breaks white on the island's rocks;
  - sustain: the mist;
  - notes: the harbour's and the cottage's lights come on in pitch order;
  - bass: the swell;
  - snare: the beam's flash.

### 13. Supercell: a storm over a farm at golden hour

*Awe, dread.*

**Hero.** The supercell's rotating updraft and wall cloud, lit gold from under its edge by the low sun, over a farm.

> Low in a wheat field at golden hour. The sky is split. On the right, a vast supercell with a striated, rotating
> updraft and a dark wall cloud reaches down toward the plains. On the left, under its edge, the setting sun throws
> gold light across the field. A farmstead (a red barn, a white house, a windmill) stands on the left third,
> glowing in that light against the storm's blue-black. A line of power poles leads from the right foreground
> toward the farm. Rain shafts hang under the far side of the storm. The wheat is gold and moves in waves.

- **Camera:** 21 mm, 1.2 m in the wheat. The storm fills two thirds of the frame.
- **Light:** a low warm sun from the left lights the storm's underside; the storm's body is dark.
- **Palette:** storm `#1e2733` and `#3a4b5c`, gold `#f3b04b`, wheat `#d8b46a`, barn `#9b2b1e`.
- **Materials:** wheat, painted wood, cloud.
- **Scale cue:** the barn, the poles, the windmill.
- **References:**
  - Mitch Dobrowner's storm photographs;
  - Wyeth's farmhouse on its rise.
- **Silent life:** the wheat waves, the storm turns, the windmill spins.
- **Audio:**
  - bass: the rotation;
  - snare: lightning;
  - kick: thunder lights the cloud from within;
  - hats: the rain shafts;
  - notes: light crawls in the cloud base at the pitch's place;
  - sustain: wind on the wheat.

### 14. Rain City: a street canyon from a rooftop at night

*Energy, loneliness.*

**Hero.** A narrow street canyon seen from a rooftop edge, a tall vertical sign across the street, rain.

> From the wet roof of a tenement, looking down into a narrow street canyon at night in rain. Water tanks and an
> antenna mast stand in silhouette in the right foreground, and the parapet runs along the bottom. Across the
> street, a tall building carries a vertical sign in red-magenta, the brightest thing in the frame, its light
> bleeding into the rain. Down in the street, sodium lamps and the signs' reflections smear on wet asphalt. Stacked
> windows, air conditioners, fire escapes and pipes give the walls their detail; most windows are dark and a few are
> lit warm. Far off, the towers of downtown dissolve into low cloud lit amber from below.

- **Camera:** 35 mm at the rooftop edge, looking down about 20 degrees.
- **Light:** practicals (the sign, the lamps, the windows), wet reflections, cloud lit from below.
- **Palette:** wet black `#0b0c10`, sodium `#ff9a3c`, sign `#ff2f6a`, one cyan `#39d0e6`, cloud `#6a3d2a`.
- **Materials:** wet concrete, metal, glass, asphalt.
- **References:**
  - *Blade Runner*;
  - Christopher Doyle's Hong Kong;
  - Liam Wong's Tokyo nights.
- **Silent life:** rain, steam from a vent, the sign's slow cycle.
- **Audio:**
  - kick: the sign buzzes and flickers;
  - snare: a glitch tears through the sign wall (the one scene where glitch belongs, because the place is
    electric);
  - hats: rain;
  - notes: windows light across the facade in pitch order;
  - sustain: the cloud's glow.

### 15. The Lava Field: a fissure eruption at night

*Energy, danger.*

**Hero.** A fountaining fissure feeding a river of lava that winds toward the camera.

> Low on a black basalt shelf at night. A river of lava winds from the right middle distance toward the left
> foreground, its crust broken into black plates with incandescent seams, glowing white-orange where it pours over
> a step. Behind it, a fissure fountains lava a few metres high and lights the base of a towering plume of steam and
> ash whose underside glows orange. Ropy, glassy black rock fills the foreground and catches the orange on its
> edges. Distant cones stand in silhouette on the horizon. Sparks rise and steam drifts across.

- **Camera:** 24 mm, 1 m. The river is the leading line.
- **Light:** the lava, with real lights along the river; the plume lit from below; faint stars.
- **Palette:** black `#0a0807`, lava `#ff6a1a` and `#ffd27a`, ember `#b8240f`, ash `#4d4844`.
- **Materials:** scanned dark rock, a lava crust (cells with glowing seams), steam.
- **References:**
  - Fagradalsfjall and Kīlauea eruption photographs;
  - Turner's *Vesuvius in Eruption*.
- **Silent life:** the crust drifts, the fountain plays, steam rolls, sparks rise.
- **Audio:**
  - bass: the lava's glow and flow;
  - kick: the fountain bursts higher with a flash and sparks;
  - hats: sparks;
  - notes: vents puff steam at the pitch's place;
  - sustain: the plume's glow.

### 16. The Black Hole Shore: the finale

*Awe, the sublime.*

**Hero.** A black hole and its tilted disk, rising above the horizon of a shallow alien sea.

> At the edge of a shallow ocean on an alien world. The water is ankle-deep and glassy to the horizon, broken only
> by low black rocks. Above the horizon on the right third, a black hole hangs enormous in the sky, its disk tilted
> and its lensed light bent over the shadow. The disk's warm light lays a path across the water toward the camera.
> A long, slow swell crosses the flat. In the foreground, wet black rocks and a lone marker post give the scale.
> Near the hole, the stars are bent.

- **Camera:** 24 mm, 1.5 m. The horizon is on the lower third.
- **Light:** the disk is the key. The stars are the only fill.
- **Palette:** space `#020205`, disk `#ffcf8a` and `#ff8a3a`, water `#0b0d14`, rock `#111111`.
- **Materials:** glassy water, black wet rock.
- **References:**
  - *Interstellar* (Miller's planet and Gargantua);
  - Chesley Bonestell.
- **Silent life:** the disk turns, the swell crosses the flat.
- **Audio:**
  - bass: the hole's mass (the lensing);
  - notes: stars fall into the disk;
  - kick: a swell pulse across the flat;
  - sustain: the disk's brightness.

## Build order (batches, so each batch teaches the next)

1. **Rock, water and one light:** the Cenote, the Drowned Colonnade, the Lava Field, the Wisp Marsh. These share
   the terrain and rock, the water, the shaft and the absorbing medium.
2. **Open land and sky:**
   - Stones at Dawn;
   - Crystal Canyon;
   - Supercell;
   - the Lighthouse;
   - the Northern Fjord.
3. **Organic and architectural density:**
   - the Moss Garden;
   - the Fungal Wood;
   - the Palm House;
   - the Abbey Ruin;
   - Rain City.
4. **Sky and cosmos:** Above the Clouds, the Black Hole Shore.

Within each batch, the order is: blockouts for all of them, then one review, then detail. A scene that fails its
blockout is redesigned, not embellished.
