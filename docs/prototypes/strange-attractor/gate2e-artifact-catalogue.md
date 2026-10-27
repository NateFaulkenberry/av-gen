# Gate 2e: cliff edge artifact catalogue

Each cause was isolated by A/B renders of the same frame with one factor switched off. The diagnostic views were
normal, albedo, shadow, diffuse and cavity, rendered separately.

| # | Artifact (where) | Cause | Fix |
|---|---|---|---|
| 1 | Jagged slivers of light at terminators on lit faces (18.5 s, right) | Two causes. (a) The generator's baked terraces had risers about one texel wide on moderate slopes, so the grid sampled them as staircases, and grazing sun lit the treads as jagged stripes. (b) A hard `max(n.l, 0)` terminator over metre-scale ledges aliases | (a) Each riser is now at least 3 texels wide, and terraces fade where they can't be resolved. (b) A rough-rock soft terminator (matte sandstone) |
| 2 | Comb-toothed shadow edges (18.5 s; 33 s right) | The shadow ray started 1-2 m off the surface. At grazing sun it skimmed the surface's own curvature, and its small ledges cast sub-pixel teeth | The shadow march now starts 25 m out, against the smooth surface. Within 25 m, n.l already decides it |
| 3 | Hair-like fringe on silhouettes of spikes and steep cliffs (26 s pinnacle) | The ray march stepped 0.38 × the vertical clearance. Beside a near-vertical face that clearance is large while the rock is close horizontally, so rays stepped into the flanks inconsistently from pixel to pixel | A Lipschitz step: clearance / (1 + the steepest slope within 5 texels), baked by the generator into the height texture |
| 4 | Serrated silhouette, horizontal scratches, bright speckle along ledges (18.5 s; 33 s mid) | The shader's sub-texel strata "ledge cut" was a sharp step a whole band deep, always below the surface. Its tips were thinner than the march step and its risers made scratch lines | The ledge profile is soft, a third of the depth, and centred on the surface |
| 5 | Ghosted or translucent bands on sloping ground (33 s mid) | Strata colour was painted on talus slopes where the rock isn't cut, so the bands read as an overlay | Bands show only on true faces. Talus is rubble-coloured |
| 6 | Blocky light/dark blocks and vertical combs on far faces (33 s right) | Beyond 4 km the normal came from finite differences of the bilinear surface, whose facets turn at every texel. The shadow also tested the bilinear surface out to 1.5 km | The normal is always the C2 B-spline gradient. The shadow tests the B-spline surface |
| 7 | Speckle and hair on grazing rims | A 1.7 m normal-map grain, sub-pixel beyond a few hundred metres, flipped normals pixel by pixel | Removed. Relief now comes from geometry only |
| 8 | Lumpy opening rock (0-5 s) | Near relief on flats was too strong | Halved |

**Checked and not the cause:**
- **The broad vertical fluting on the big lit escarpment.** It stays the same at 2048² and 4096², with terraces,
  erosion and ridges each off, and with a wider cliff. It is real corrugation from the erosion front's fine octaves:
  the face is fluted.
- **The supersample resolve.** The artifacts are present in single-sample renders too.

**Still visible (honest):**
- A faint fringe on one sunlit spire edge at 26 s.
- Some soft banding on the haze-lit rounded face at 33 s. Those are its real strata, but they read pale.
- The early river is still dark.
