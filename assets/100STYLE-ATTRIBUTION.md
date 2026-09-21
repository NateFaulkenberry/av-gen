# 100STYLE — attribution, terms, and exactly which files were taken

**The corpus itself is not in this repository and must not be.** `assets/100style/` and
`assets/100style-mixed/` are gitignored. This file is the part the repository keeps, so that a
figure measured on the corpus can be reconstructed by someone who does not have it.

## The work

**The 100STYLE Dataset** — Ian Mason, Sebastian Starke, Taku Komura.
*Real-Time Style Modelling of Human Locomotion via Feature-Wise Transformations and Local Motion
Phases*, Proc. ACM Comput. Graph. Interact. Tech. 5(1), 2022. DOI `10.1145/3522618`.

## Terms

**CC BY 4.0** — `https://creativecommons.org/licenses/by/4.0/`.

`docs/dependencies.md` and `CREDITS.md` carry the obligation in full and are the authority; this
file does not restate it. Two things are recorded here because they are facts about *this
download* rather than about the licence:

- **The credit line the authors ask for, verbatim:** `The 100STYLE Dataset - Ian Mason`
- **The per-file BVH downloads carry no licence artefact at all** — no LICENSE file, no README, no
  header comment; every file begins `HIERARCHY` and contains nothing but the hierarchy and the
  motion. **That is an absence, not a confirmation.** The terms above come from the authors' page
  (`ianxmason.com/100style/`) and the Zenodo record's rights field (record 8127870,
  `"id": "cc-by-4.0"`), both read on 2026-09-21. Nothing in the download contradicts them and
  nothing in the download confirms them. The full-corpus `100STYLE.zip` on Zenodo may ship terms
  the individual files do not; it was not downloaded.

**Changes were made**, which CC BY requires be indicated: the motion was subsetted as described
below, scaled from centimetres to metres, resampled to 30 Hz, and reduced to feature vectors. No
derived database, pack or feature set built from it is published from this repository.

## Where it came from

The per-file links in the "Full Dataset Table" on `https://www.ianxmason.com/100style/`, each a
Google Drive `uc?id=...&export=download` URL. The index of 810 file→id pairs was scraped from that
page on 2026-09-21. Files are named `<StyleName>_<MovementType>.bvh`; the movement types are
`FW`/`BW` (forward/backward walk), `FR`/`BR` (run), `SW`/`SR` (sideways), `ID` (idle) and `TR*`
(transitions).

## The two subsets, and why each was chosen

**`assets/100style/` — the style axis.** `*_FW.bvh` for **all 100 styles**: 100 files, 611.8 MB,
868,806 frames at 60 Hz (4.02 hours). One movement type, maximum style diversity — the axis the
duplicate and resolution questions turn on. Manifest digest `7746de4c21a26194`.

**`assets/100style-mixed/` — the movement axis.** **All 8 movement types** for 12 styles taken at
even intervals through the alphabetical style list (`Aeroplane, BeatChest, CrossOver, DuckFoot,
HandsInPockets, LeanLeft, March, OnToesBentForward, RaisedLeftArm, ShieldedRight, Strutting,
Tiptoe`): 96 files, 395.0 MB. **It exists to check a confound rather than to add scale** — a
cross-clip distance measured over 100 forward walks might be small only because every clip is a
forward walk. Manifest digest `7f2e0d2707aa5611`.

The digests are `sha256` over each subset's sorted `name:size` list, truncated to 16 hex
characters — enough to catch a different subset, not a content hash of 1 GB of ASCII.

## How to rebuild the measurement

```
avgen_motion survey  assets/100style --scale 0.01
avgen_motion pack    assets/100style/*.bvh --out <dir> --scale 0.01 \
                     --license CC-BY-4.0 --source 100STYLE \
                     --contacts LeftAnkle,RightAnkle \
                     --redistribution allowed --derivedDataAllowed true
avgen_motion quality <dir> --joints LeftAnkle,RightAnkle,Head --seeds 250
```

ADR-614 has the numbers and the control the same commands produce on the Glowmere corpus.
