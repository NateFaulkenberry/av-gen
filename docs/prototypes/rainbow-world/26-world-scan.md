# Rainbow World 26: the world scan, glitch and firings (gate 5)

The owner (22-owner-fixes.md): "rainbow world scan only happened once and there was a glitch at the end of the
effect (popped back into view for a second)".

## The glitch

The scan's amount (`macro.scan`) came from the phrase pulse, latched for 6 s, and was gated by the synth stem's
*level* through the route's depth. Depth is applied after the chain, every frame, so it does not latch.

At 51.3 s the level dipped from 1.00 to 0.90 and the depth fell to zero, so the rainbow vanished. At 52.0 s the level
recovered to 0.94 and the rainbow came back at 0.36, then vanished again at 52.5 s. That is the "popped back into
view". Trace: 50.5 s 1.00, 51.0 s 0.61, 51.5 s 0.00, 52.0 s 0.36, 52.5 s 0.00.

A second fault waited behind it. The scan's front reset on *every* phrase pulse, so a scan still on screen 7.9 s
later would have seen its rainbow region snap back to the eye.

## The fix: the scans are baked from the song (showcase)

In `rainbow-world-feline.json`, two timelines, `scan` (the amount) and `scanFront` (the front), replace the live
trigger. Each scan:
- eases in over 1.2 s;
- holds for 4.8 s;
- eases out over 3 s;
- has its front run out from the eye over 4 s.

Nothing resets part-way through. Being timelines, a seek lands exactly where a play does.

## When it fires

At the song's big synth swells and section peaks, from the synth stem's rms over the four bars after each phrase
start against the four before (`music.SCAN_TIMES`):

| Time | What |
|---|---|
| 47.85 s | the drop: the synth's entry, the largest rise (+0.10 rms) |
| 111.3 s | section B: 16th hats under the full synth bed |
| 158.9 s | the synth's brightness climbing to its peak (centroid at 160.9 s) |
| 190.7 s | the riser's top: the synth's loudest phrase (0.21 rms), drums gone |

That is four scans, 63, 48 and 32 s apart, not on a grid. Each starts 300 ms after the phrase start.

Traced amount above 0.05: 48.33-56.77, 111.77-120.23, 159.37-167.83 and 191.17-199.63 s. The largest change in one
frame is 0.041, so there are no jumps.

## Live

The live project keeps the live trigger: the phrase pulse gated by a mapped synth channel. It never fires without
stems. With a synth channel it can still show the gate-4 tail glitch, because a route's depth does not latch. Fixing
it there needs a latched gate, which is an engine change (noted as a limitation).

## 4. The live scan latches (ADR-1574)

The coordinator: "the owner plays these live, so a known tail glitch there isn't acceptable". The live project's scan
now fires on a surge and plays its whole shape. Its envelope ignores its input for 20 s once triggered (30 s on the
mix) and re-arms only after the trigger signal has been "off" for a while. Each trigger is one replay-safe signal
conditioned to on/off, so there is no depth to dip. A spring eases the amount in and out. The front runs on the same
latch, so it never resets mid-scan.

| Input | Trigger | Re-arms after | Fires on Feline Footwear |
|---|---|---|---|
| A synth channel (or stem) | `stem.synth.rms` > 0.16 (the intro's loudest call is 0.146, the drop 0.19) | 2.5 s under | 48.2 s (the drop). The synth bed then holds above 0.16 until the end, so it does not re-arm |
| Plain stereo | `response.sustain` > 0.6 (the mix's pad/synth energy against its adaptive reference; stands aside with a synth stem) | 6 s under, 30 s apart | 12.8, 102.6, 133.8, 172.0 s |

**The trace** (`avgen --sonic-trace` of the live project fed the mix, then the mix and stems;
`build/rw2/livescan/trace_summary.txt`):
- every firing rises to 1.00 with no dip;
- no firing rises again on its way down;
- the largest change in one frame is 0.039 (the spring).

The mix path is approximate. It fires on the mix's own swells of sustained energy, and on this track the first is in
the intro, which uses the refractory time that would otherwise cover the drop. The stem path fires on the drop.
