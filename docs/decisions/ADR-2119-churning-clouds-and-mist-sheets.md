# ADR-2119: churning clouds, mist sheets and edge air

- Status: Accepted (2026-11-21), avagent/neon-dreams (Neon Dreams, iteration 12; owner msg 149: "clouds all around
  more animated -- fog layers or distortion/movement", "clouds around the city more dynamic and blended with layers of
  fog" (2.6 s); 59.6 s: the cloud-against-sky edge "too CG")
- Code: `scene::CloudChurn`, `CloudWisps::Layer`, `Clouds::edgeAir` (`src/scene/clouds.{hpp,cpp}`); `animTap`,
  `wispLayers`, `layerSpan` and the churn in `erosionAt`, `densityBase`, `detailErode`, `wispMain` (`shaders/clouds.wgsl`);
  the clocks in `CloudRenderer::encode`.
- Tests: `tests/unit/test_clouds.cpp` (`[adr2119]`), `tests/rendering/test_clouds_it12_gpu.cpp` (`[adr2119]`).

## Context

Every cloud noise was a pure function of position plus a drift: the clouds slid as rigid sheets with the wind and
otherwise never changed. The wisps (ADR-2113) were one layer in authored zones.

## Decision

1. **Churn** (`churn: {amount, period, boil, writhe}`): every erosion octave, the sea's billow tap, the detail tap and
   the wisps' sheet tap is read twice, advected along a boil velocity (rising, turned by the existing curl field) by
   two clocks half a `period` apart, and cross-faded with triangle weights so each clock restarts unseen at zero
   weight; each restart reads a fresh region of the volume (an offset per cycle). The blend is re-contrasted
   (`0.5 + (n - 0.5) / sqrt(wa^2 + wb^2)`) so the noise keeps its spread mid-fade. The curl field's sample point
   drifts (`writhe`), so the tatters twist. The clocks are computed on the CPU from the timeline second in double
   precision: seek = play. `amount` 0 is the ADR-2106 noise exactly (one tap, no writhe).
2. **Mist sheets** (`wisps.layers`, at most 6): horizontal fog banks with their own height band, plan disc (radius 0 =
   the whole sky), density, scale, coverage, stretch and wind; the same warped, torn, churned sheet noise as the wisps.
   They are part of the march's span and of the empty-air skip (a ray never jumps into or across one).
3. **Edge air** (`edgeAir`, 0..1): a sample's radiance is pulled toward the haze colour by up to this share where its
   density is under 0.35, so a thin silhouette melts into the air instead of reading as a cut-out.
4. Routes: `clouds/churn`, `clouds/edgeAir`, `clouds/wisps/layer/<i>/density`.

## Consequences

- An animated tap costs two volume reads instead of one (erosion, billow, detail and sheets), only where it is read;
  `churn.amount` routed abruptly displaces the noise by `flow x phase x period` at once -- route it through an
  envelope.
- Neon Dreams: churn 1.0 over 7 s clocks at 1.2 m/s, edge air 0.35, four sheets (under the decks round the stilts, a
  wide low harbour bank, the open sea before the reveal, a shelf over the vortex).
