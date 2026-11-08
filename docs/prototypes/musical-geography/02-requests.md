# Musical Geography: engine requests (art → engineering)

The art agent writes requests here, through the coordinator. Engineering marks each one, says where it lands, and
writes its ADR (in the art range, 1970–1979) when it is built. Owner decisions of 2026-11-08 apply: thin slabs, sharp
edges, rolling land only (no canyon, mesa, terrace cliffs or strata), the Glowmere aliens, a short top-down pass.

Source: `04-art-direction.md` §10 at c0e936d1 (art branch). The status mirrors `03-technical-plan.md` §5.1.

| # | Request | Status | Phase | Notes |
|---|---|---|---|---|
| 1970 | Contour edges: four per-side weights per tile, lit where `floor(h/interval)` drops across that side; `contour/interval`, `contour/offset` | **Accepted, adjusted** | 3 | Sharp edges (no chamfer), so the band is drawn on the top face's border from `localPosition`. Four neighbour heights per tile in the effector pass; cost measured, limited to the visible set |
| 1971 | Crest weight per wave set, `crestSharpness`, brighter where trains cross | **Accepted** | 3 | The crest channel |
| 1972 | Terrain palette: a 5-stop land ramp with height stops, a 4-stop water ramp, shallows, path/pad/river colours, drift, edge and crest gains, fog multiplier, exposure offset; OKLab; absolute height | **Accepted, minus** the canyon wall colour, strata amplitude and river (their formations are gone) | 4 | DayNight curves (plan §4.4) |
| 1973 | Region channels for the material: water, shore distance, terrace index, canyon wall, river, path, plateau flatness | **Accepted, reduced** to water, shore distance, flatness (crowns/pads) and path | 2 | Terrace index, canyon wall and river are dropped with their formations |
| 1974 | Per-instance roughness (water 0.16, land 0.62) | **Accepted** | 2 | A material-program check first; built only if a program cannot already write roughness from a field |
| 1975 | Top-only emission and side shading | **Accepted, for a slab** | 2 | Emission masked to the top face; the side shade runs over the slab's normalised local height (no column) |
| 1976 | Distance fade of the gap and edge band by pixel footprint | **Accepted** | 2 | Also the main defence against moiré |
| 1977 | Fog: exp²-like distance curve, height falloff, sun-facing tint | **Mostly existing** | 2 check, 4 fix | The single medium (ADR-705): `horizonDensity`, `fogHeightFalloff`, `fogHeightCurve`, anisotropic sun scattering. The window radius is tied to the 0.98 fog distance by a test |
| 1978 | Lattice coordinates in material programs (never the record index or a camera-relative position) | **Accepted** | 2 | With the lattice-keyed randoms (plan §4.2) |
| 1979 | Flight route through named features; per-leg pitch offset and focal curve | **Accepted, extended** | 5 | Plus the owner's top-down leg |

**Smaller asks (no ADR).**
- Plateau levels: replaced by crown flatness, because the mesas are gone.
- The path spline is exposed to the camera and the characters.
- AgX is already the default tonemap (ADR-039).
- The bloom threshold and intensity are already `post/*` parameters, which DayNight's phase can key.

## New requests

(Append below; engineering answers inline.)

### Art, 2026-11-08 (after Gate 1): two look requirements the owner's review made binding (04- §11)

Coordinator, 2026-11-08: both are relayed to engineering, with no new numbers. **A folds into ADR-1977** (output dither; keep the grain stand-in until it lands). **B folds into ADR-1976** (the separation survives distance).

- **A. Output dither, so the sky and fog never band.**
  - The sky gradients band visibly in the r2 mock-ups.
  - The engine has no output dither today. `post/output/grain` is the only stand-in, and look.py sets it to 0.035
    meanwhile.
  - Ask: a triangular-PDF dither of about ±1 LSB, applied after the tonemap and before 8-bit quantisation (the live
    swapchain and the encoder both), or a higher-precision sky and fog.
  - Check: a large-sky frame in each state, especially dusk and night.
- **B. The tile has to read at reveal altitude. This changes 1976 and §6.1's far field.**
  - Plan §6.1 makes the far ring "a continuous surface" beyond the gap fade, and 1976 fades the gap with distance.
  - Both make the squares disappear exactly where the owner's review says they must still read: at 70–100 tiles up,
    with a 35–50 mm lens, across all the land in the frame.
  - Ask: keep a separation that survives distance instead of fading it. Two ways:
    - a footprint-aware darkened border on each top that stays at least about 1 px wide;
    - a far ring of tiles at 2× or 4× pitch, merged in whole lattice blocks with fog over the seam, so the far field
      stays tiles rather than a smooth mesh.
  - Per-tile value jitter that grows with distance (±2% near, up to ±6% far) helps too, and art can do that in the
    program once 1978's lattice randoms exist.
  - The far *underlay* (the dark sheet under the slabs) is unaffected and still wanted.
