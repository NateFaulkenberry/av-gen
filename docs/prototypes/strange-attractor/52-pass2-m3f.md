# Art Pass 2, M3f: progress

The notes are `51-pass2-m3f-owner-notes.md`. M3f builds on M3e (7fe07cae). Media is in
`~/Desktop/av-gen-review/101-strange-attractor/pass2-m3f/`.

| § | Note | Result |
|---|---|---|
| 1 | Keep the M3e night lighting | Kept. The values are unchanged except the craft's own lights. |
| 2 | Move the craft | Beside the Gray, down-canyon of him, inside the owner's camp camera: 10 m down-canyon and 5 m bankward of the fire, 9.5 m from the Gray, 27° left of that camera's axis. It stands on a pad at the camp's level. (The shore by the water is behind that camera, which looks from the water's edge toward the bank. A first placement on the shore fell outside its frame.) |
| 2 | Beat pulse | The craft's underbody spot, rim lamps and a new window-band spill onto the Gray are multiplied on `beat.pulse`, 0.3× to 4.5× with a 0.6 s fall, and the hull's emissive 0.35× to 4.5×. Multiplied, so a look's 0 by day stays 0. A 9% luminance swing over the craft (5% at 0.5× to 3×, which the owner found too subtle). |
| 2 | The pass clipped geometry | Re-routed on the camp's bank side. Each span is now split where the ground under its middle comes within 3 m of the line: the knots' heights are over the ground at the knots only, and a bank between two knots stood above the line, 2.7 m inside it at 109.8 s. Traced per frame against the terrain and every visible mesh's box: the minimum over the terrain before the glide is 1.3 m (the glide's own descent into the river); the minimum to any mesh box is 2.96 m (the craft). |
| 2a | The night river's streaks | Root cause: M3e's `streak` spreads the water's normal along the view, so every reflection was drawn into vertical streaks, and six reflection rays and glints re-lit them. They were meant for the fire's streaks, but the water takes no local lights (48- F7). Off, with gentler chop (ripple 0.3, roughness 0.06). |
| 2b | Ocean "wobble" | The camera is smooth and identical in M3e and M3f, per frame from 121.3 to 147 s: max yaw rate 3.55°/s, pitch 0.67°/s, roll 0, no pitch reversals. The critic's "shake" is only in 146.48-147.35 s, the sea exit's burst and crossing, whose shards and opening it reads as global motion. The rest of the ocean shot is "smooth". The reef's caustics were slowed and dimmed on a first, wrong guess; that change is kept (mild). |
| 2b | Sea-exit white-out | Root cause: the 2600 cd `bay-light` stands 10 m inside the bay, behind the opening the camera flies through, and it scattered in the ocean's fog at full strength. Within a few metres the in-scatter filled the frame white. With the fog off there was no white-out; the corruption's light, its glow and the bloom made no difference. Its scattering is now off (6% still left a glow and a disc for 3 frames), and the floods' is at 25%. Frame by frame, 145.6-147.0 s is clean. |
| 3 | Hero assets | The H2 talking alien, the v1 Sasquatches (B lifted 1.3 cm), the v1 campfire (`burn`; the flame particles off, the core dimmed, the fire light raised to 1.35 m), the v1 UFO at the new spot carrying the pulse. The seated Sasquatch sits on a broad boulder (1.0 m across, 0.56 m deep, 0.42 m top), placed by `hero/check_contacts.py`'s search: no penetrating vertex in 193 frames, the seat within 3 cm. |

The hero agent's scripts and docs (scene/sa-hero a86276fd) are brought in for reproducibility. The assets stay in the
gitignored `assets/strange-attractor/hero/`.
