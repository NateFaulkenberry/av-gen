# Rainbow World 25: tree popping, twitching and clumping (gate 5)

The owner (22-owner-fixes.md): "investigate tree popping - might be an LOD issue, but im seeing a lot of twitchy
trees" and "fix trees clumped together in some areas". Later: "if its not LOD on trees it might be them fighting each
other for visibility".

## What it was

**Level of detail, not z-fighting.**

- **Level of detail (the cause).** Every tree is three instanced nodes: wood, core and leaf cards. Each switched level
  by *distance*, so a 5 m pine and a 24 m Great Tree switched at the same metres.
  - The leaves' first switch (to 35% of the cards, thinned) came at 22 m, when a Great Tree is about 440 px across.
  - The leaves stopped dead at 150 m, so a 65 px crown vanished or appeared.
  - With no stagger and no dead zone, a tree near a threshold flipped back and forth as the ride wove past it.
  - Measured over one lap (`scripts/tree_lod_switches.py`): 214 switches a minute in view, 25 a minute on crowns over
    60 px, and 35 a minute that flipped back within 1.5 s. Those flips are the "twitching".
- **Z-fighting (not the cause).**
  - The scatter audit did find trees standing in each other: 3 pairs under 1 m apart, 9 under 3 m, 55 crowns
    overlapping by more than half.
  - A nudge test, with the camera frozen at 36 s on an overlapping weeper pair 57 m away and stepped 0.12 m five
    times, showed only sub-pixel edge changes. There was no speckle flicker where the crowns interpenetrate.
  - Depth precision is not the issue: the near plane is 0.5 m and the far plane 30 km, which gives about 3 mm at
    150 m.
- **The bass heave also moved every tree up to 1.9 m on each bass note.** It is now retired (24 / 23 item 5).

## The fix (no engine change)

**Level of detail.** Every tree level now switches by *screen size*, so every tree makes the same apparent change, and
only once it is small:
- leaves: 130 / 60 / 28 px, culled at 12 px;
- core: 60 / 20 px;
- wood: 80 / 25 px.

Each tree also gets its own offset (`spread` 0.35), so a stand changes over a band. There is a dead zone too
(`hysteresis` 0.2), which the renderer applies live; an offline render carries no history.

| | Before | After |
|---|---|---|
| Crown radius at a switch: median / p90 / max | 26 / 67 / 437 px | 12 / 60 / 133 px |
| Back-and-forth within 1.5 s | 35/min | 5/min |
| Switches on crowns over 60 px | 25/min | 23/min (now at most 133 px, not 437) |

**Clumping.** A Poisson-disk pass over all trees, in planting order, so the deliberate rows and groves keep their
shape. A tree is dropped if it stands closer to an earlier one than 0.75 x the sum of their crown radii. 82 of 431
were dropped. Crowns may still touch, so a grove still reads as one.

The flight path was re-planned around the new trees: 2.9 m from the nearest crown (target 3; one tree in a tight
pair at 6.3 km settles 0.1 m short), 3.6 m over the meadow, 4.1 m off the rim. The camera motion is unchanged
(reversals per minute 5.7 / 6.3 / 1.6, total rate p95 28 deg/s).

The cost of the leaves now reaching 12 px instead of stopping at 150 m is in the gate-5 live profile.
