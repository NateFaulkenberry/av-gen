# Research report 5: automated evaluation of procedural VFX and audio-visual correspondence

Sonic Garden VFX expansion, deliverable 5 (engineering agent, 2026-10-02). It covers metrics computable with numpy/PIL
(OpenCV optional) on rendered frames plus the per-frame signal trace, with their formulas, and when a vision-language
judge is better. Section 10 maps them onto the existing Creative Critic, and the tool built from this is in
`../VFX-ARCHITECTURE.md`.

## 1. Composition

**Saliency.** Spectral residual saliency ([Hou & Zhang, CVPR 2007](https://www.semanticscholar.org/paper/Saliency-Detection:-A-Spectral-Residual-Approach-Hou-Zhang/9ce3c18eb4fa86fd19bce46227be39895de4e4ab)) is about ten lines of numpy:
1. downsample to about 64 px wide;
2. compute `F = fft2(I)`, `L = log|F|`, `P = angle F`;
3. compute the residual `R = L - box3(L)`;
4. compute `S = gauss(|ifft2(exp(R + iP))|^2)`.

Itti-Koch-Niebur ([PAMI 1998](https://doi.org/10.1109/34.730558)) adds colour and orientation pop-out at higher cost.
The practical choice is SR on luma averaged with SR on the OKLab a/b channels.

**Composition metrics:**
- **Focal point**: the argmax of blurred S.
- **Concentration**: `1 - H(S)/log N`, the Gini of S, or the top-5% saliency mass.
- **Competing focal points**: run non-max suppression (window 1/10 of the width) and count the peaks above 0.5 max:
  - 1 is a clear hierarchy;
  - 2-3 can be fine;
  - 4 or more splits attention.
- **Dominance ratio**: the mass of the top salient component over the second. At 1.5-2 or more it reads as a clear
  subject.
- **Rule of thirds** ([Liu et al. 2010](https://www.cs.tau.ac.il/~dcor/articles/2010/Optimizing-Photo.pdf)):
  `E_RT = sum_r M_r exp(-D_r^2 / 2 sigma^2) / sum M_r`, with D_r the diagonal-normalised distance from a region's
  centroid to the nearest power point, and sigma 0.17.
- **Balance**: the saliency centre of mass in [-1, 1]^2. Under 0.15 is balanced; over 0.35 is lopsided unless negative
  space is the counterweight (a judgement for a VLM).
- **Symmetry**: the Pearson correlation of a 1/8-scale blurred luma with its mirror. Above 0.8 is either deliberate or
  a procedural tell.
- **Negative space**: the share of the frame with `S < 0.2 mean(S)` and gradient energy below its 30th percentile.
  Under 10% is crowded; 25-60% is typical of strong graphic frames.
- **Learned aesthetics** (AVA, [NIMA](https://arxiv.org/abs/1709.05424)) are trained on photographs. On stylised renders
  they are weak priors, not targets.

## 2. Depth and layering

**With a depth AOV:**
- **Modality**: a 3-component 1-D GMM or a multi-level Otsu on log depth (sky masked). Report the weights and the
  separation `d' = |mu1 - mu2| / sqrt((s1^2 + s2^2)/2)`. Three layers, each with weight >= 0.1 and d' > 2, is clear
  foreground/midground/background.
- **Atmospheric perspective**: Spearman rho(depth, RMS contrast) and rho(depth, chroma) over 5-8 depth bins should both
  be below -0.6. A positive value means the fog is not working, or a distant bright element is fighting the subject.

**Without depth:** Laplacian sharpness and local contrast per tile are a weak proxy (unreliable on stylised, flat-shaded
content).

## 3. Colour

- **Palette**: k-means (k = 5-8) in OKLab, weighted by share. Report:
  - the number of clusters with share >= 5%;
  - the chroma-weighted circular hue spread, `sigma_h = sqrt(-2 ln Rbar)`;
  - the number of hue-histogram peaks.
- **Harmonic templates** ([Cohen-Or et al. 2006](https://igl.ethz.ch/projects/color-harmonization/)): the i, V, L, I,
  T, Y and X sector templates, fitted by rotation. A chroma-weighted arc residual under about 5 degrees is harmonious;
  over 15 is scattered.
- **Colourfulness** ([Hasler & Suesstrunk 2003](https://infoscience.epfl.ch/record/33994)):
  `M = sqrt(s_rg^2 + s_yb^2) + 0.3 sqrt(m_rg^2 + m_yb^2)`, with rg = R - G and yb = (R + G)/2 - B on 0-255 values.
  The anchors are 15 slightly, 33 moderately, 59 quite and 82 highly colourful.
- **Contrast**: RMS (std of OKLab L), and percentile Michelson `(p99 - p1)/(p99 + p1)`.
- **Highlight clipping**: the clipped *connected* area (any channel >= 254). Over 2% is a defect; scattered glints are
  fine.
- **Muddiness**: the share of pixels with OKLab C < 0.04 and 0.3 < L < 0.7. Over 40% in a scene meant to be colourful
  is muddy.
- **Value structure (notan)**: a 2-3 level Otsu, with the area and connected components per level. Strong value design
  is 2-3 levels in fewer than about 8 large shapes. Salt-and-pepper value is noise; one level over 85% is flat.

## 4. Complexity and noise

- Edge density.
- PNG-size complexity (compare within a project, not in absolute terms).
- ITU-T P.910 SI/TI ([VQEG](https://vqeg.github.io/software-tools/quality%20analysis/siti/)): report the mean and
  percentiles, because cuts dominate the maximum.
- High-frequency energy share.
- **Repetition**: autocorrelation off-origin peaks above 0.5 are visible tiling, or clones on a grid.

## 5. Motion

- **Flow**: Farneback or DIS flow (OpenCV), or 16 px block matching in numpy, on about 480 px luma.
- **Global motion and shake**: the median flow (or a RANSAC similarity) is the camera. Shake is the RMS of its
  high-passed path; over 1-2 px at 1080p reads as jitter.
- **Motion hierarchy**: per cell of an 8x6 grid, the median residual speed over the clip. Report:
  - p90/p10 (ignoring still cells);
  - the Gini of the cell speeds;
  - the number of speed tiers (clusters in log speed a factor of 2 apart).
  
  p90/p10 < 2 is "everything wobbles equally", the common procedural failure. A healthy scene puts the focal region in
  its own tier, p90/p10 > 4.
- **Flicker**: the 3-30 Hz share of the mean-luma power spectrum.
- **Flashes**: [WCAG 2.3.1](https://www.w3.org/TR/UNDERSTANDING-WCAG20/seizure-does-not-violate.html) and ITU-R BT.1702.
  A flash is a pair of opposing relative-luminance changes of >= 0.1 with the darker state below 0.8. More than 3 a
  second over 25% of the screen fails. This is a hard limit for a live instrument driven by drums.
- **Temporal noise**: the flow-compensated residual, reported at matched luminance (AV Gen has already been fooled by a
  density change that faked a noise change).

## 6. Audio-visual correspondence

**Visual series.** Per frame, globally and per region: luma change, motion energy, hue, emissive area, saliency mass.

**Lagged cross-correlation.**
- r(tau) for tau in [-250, +500] ms, after prewhitening (difference both series or fit AR(1)), because slow drifts
  correlate spuriously.
- Significance is a z-score against circular-shift nulls (200 or more shifts).
- Hershey & Movellan's Gaussian MI, `-1/2 log(1 - rho^2)` per region
  ([NIPS 1999](https://papers.nips.cc/paper/1686-audio-vision-using-audio-visual-synchrony-to-locate-sounds.pdf)), is a
  **synchrony map**: *where* the picture follows the sound. This checks that the right element answers the right
  instrument.

**Event-locked averaging** (peri-event histogram) over onsets or note-ons, with windows of -0.2..+1.0 s z-scored to the
pre-event baseline. Report:
- **latency** (time to peak; 0-50 ms for hits, over 100 ms reads late);
- **gain** (peak z);
- **decay tau** (peak to 1/e);
- **reliability** (the share of single events whose peak exceeds 2 sigma).

A visual decay much shorter than the sound's looks twitchy; much longer looks smeared. Shuffled onsets are the control.

**Mutual information**: 8 quantile bins, shuffle-corrected. It catches non-linear mappings such as pitch to hue.

**Slow against fast (the modulation spectrum).**
- Compute the Welch PSD of each series over 0.1-15 Hz, then compare the spectral centroids and the band coherence.
- A pad should drive < 1 Hz visual modulation, and hats 4-12 Hz.
- Flags:
  - the visual centroid is more than 2x the audio's (jitter);
  - the visual centroid is less than 0.5x the audio's (sluggish);
  - the coherence is low where the audio has its energy.

This is the direct test of the brief's "slow musical changes produce slower visual evolution" and "transients create
appropriately fast responses".

**Beat alignment.** The Beat Align Score of AI Choreographer ([arXiv:2101.08779](https://arxiv.org/abs/2101.08779)) and
Bailando ([arXiv:2203.13055](https://arxiv.org/abs/2203.13055)) is `BAS = mean_b exp(-min_v (b - v)^2 / 2 sigma^2)`.
It rewards dense visual events, so pair it with precision, recall and F-measure within +-70 ms.

## 7. Defects

| defect | detector |
|---|---|
| empty frame | std(Y) < 0.01, or mean Y < 0.02, or edge density < 0.5% |
| overcrowding | edge density > 25% and >= 4 saliency peaks and negative space < 10% |
| blown highlights | clipped connected area > 2% |
| banding | 1-2-code steps forming long contours in smooth regions; a comb histogram ([CAMBI](https://github.com/Netflix/vmaf)) |
| shimmer | temporal second difference on edges against textured non-edges after flow warp, ratio > 3 |
| intersections, floating | need scene data: depth gap under grounded silhouettes; ID seams with no depth step |

## 8. VLM-as-judge

**What the evidence says:**
- MLLMs agree with humans on *pairwise* comparison and diverge on absolute scores
  ([MLLM-as-a-Judge](https://arxiv.org/abs/2402.04788)).
- They are best at technical quality and colour/light, and weaker at composition and depth
  ([AesBench](https://arxiv.org/abs/2401.08276)).
- Discrete text levels converted to probability-weighted scores are better calibrated than asked-for numbers
  ([Q-Align](https://arxiv.org/abs/2312.17090)).
- Position and verbosity bias are documented ([Zheng et al. 2023](https://arxiv.org/abs/2306.05685)).

**Protocol:**
- fixed rubrics with anchored levels, observations before the verdict;
- A/B comparisons in both orders, a disagreement counting as a tie;
- contact sheets (6-12 timestamped frames, plus frames around a strong onset);
- numeric metrics given as context, never as the answer, with every measurable claim the VLM makes re-checked
  numerically.

**Where each judge belongs:** use the VLM for subject identification, mood and art-direction match, and the "authored or
procedural" gestalt. **Never** use it for timing, latency, flicker, clipping or banding.

## 9. "Authored or procedural" proxies

- **Spacing**: the Clark-Evans ratio `R = mean(nn) / (0.5 / sqrt(density))` over repeated elements. R near 1 everywhere
  is uniform noise scatter; designed-organic spacing sits about 1.2-1.6 with a nearest-neighbour CV of 0.2-0.4; a CV
  under 0.1 is mechanical.
- **Scale**: a heavy-tailed size distribution (a few large, some medium, many small elements) is authored.
- **Symmetry** above 0.85 with no intent flag reads as mirrored procedural output.
- **Hierarchy**: a dominance ratio of 2 or more with 1-2 peaks is authored; flat saliency (normalised entropy > 0.9) is
  procedural.
- **Value**: few large notan shapes are authored; salt-and-pepper is procedural.
- **Repetition**: an autocorrelation peak above 0.5.

## 10. What AV Gen already has (verified 2026-10-02)

The **Creative Critic** is a separate repository (`../creative-critic`): a local FastAPI daemon and a CLI, all classical
numpy/OpenCV/ffmpeg, plus optional pyiqa models. It makes no LLM calls. AV Gen shells out to it (ADR-931), and Sonic
Garden used it in art pass 1 (two jobs, `PROGRESS.md`). It already computes much of the list above:

- **Per frame**: luma percentiles, RMS contrast, `clip_frac`, colourfulness, LAB k-means palette, `lr_symmetry`, bright
  mass and blob count, spectral-residual saliency centroid/entropy/peak.
- **Flow**: DIS flow with a RANSAC camera split, jitter, shimmer and moving area.
- **Temporal**: flicker, flash runs, palette drift.
- **Stability**: high-frequency share.
- **Pacing**: novelty, repeated compositions.
- **Audio reactivity** (`av.reactivity`): an 8x6 grid of luma and frame-difference cells, high-passed, lagged
  correlation over -100..+300 ms against circular-shift nulls, onset-locked ERPs with Welch tests, and the verdicts
  whole-frame / strobe / invisible / hierarchy / lockstep.
- **Route checks**: each configured route is checked for a response in its region.

**Its limits for Sonic Garden:**
1. Its audio features are fixed to five bands plus onset (`FEATURES = ("low", "lowmid", "mid", "high", "onset")`). It
   cannot ask "does the picture follow the kick and not the hat" or "does a sustained note read differently from a
   staccato one".
2. It has no modulation-spectrum (slow against fast) test.
3. It has no motion-hierarchy tiers per region over a clip (it has subject against environment).
4. It has no negative-space, competing-peak count, dominance ratio, notan or harmony metrics.
5. It sees no scene composition metadata for SDF and procedural Sonic scenes (no projection), so its region tests need
   supplied boxes.

`tools/liminal_critic.py` (numpy only, system python) writes Critic `inputs.json` and has its own temporal checks; its
`modulation` block is a dict where the Critic wants a list (fixed in `tools/liminal/critic_pass2.py`).

**The richest ground truth AV Gen has is the engine's own signal trace** (`--sonic-trace`: every `sonic.*`, `notes.*`,
`timbre.*` and `visual.*` per frame). Correspondence metrics should correlate the picture against those exact signals
rather than re-derive onsets from the audio, because the question is whether the *mapping* works, not whether the
detector works.

## Recommendations

I will build `tools/sonic_vfx_critic.py` (system python, numpy + PIL + ffmpeg, no new dependency). It extends the
Critic rather than replacing it:
1. **`inputs`**: writes Critic `inputs.json`, with the trace's response signals mapped onto the Critic's five feature
   names (`low` = bass level, `onset` = the onset envelope, and so on), the note-on and kick times as route `events`,
   the scene's composition metadata as `regions`, and the modulation as a list. The Critic's reactivity, flow, colour
   and defect rules then run unchanged on Sonic scenes.
2. **`measure`** (its own numeric pass, the gaps in section 10, Critic-shaped findings):
   - composition: saliency peaks after NMS, dominance ratio, balance offset, symmetry, negative space, thirds;
   - colour: OKLab palette, hue spread, harmony residual, colourfulness, clipped connected area, muddiness, notan;
   - motion hierarchy: block-matched per-cell speeds, p90/p10, Gini, tiers;
   - **correspondence per response signal**: for each `response.*` and `notes.*` signal, the lagged correlation and
     event-locked latency, gain, decay and reliability, both globally and per region (a synchrony map), plus the
     modulation-spectrum centroid ratio;
   - **specificity**: the kick's response region against the hat's. Do different signals move different things?
   - defects: empty, overcrowded, clipped, flash risk (WCAG), flicker share.
3. **`compare`**: two runs, by stable keys, like `critic compare`.

The VLM-judged dimensions (art direction, authoredness, whether an asymmetry is intended) stay the art agent's. The tool
writes the contact sheet with the numeric findings for that judge.
