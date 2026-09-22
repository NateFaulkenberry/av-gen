# ADR-625: Motion corpora, and what a distributable MotionPack may carry

**Status:** Accepted, as an assessment. Adding either corpus is a separate decision.
**Date:** 2026-09-21
**Resolves:** Phase C §61 ("verify exact license, data license, redistribution rights, attribution,
commercial use, derivative data rules")
**Related:** ADR-542 (a licence check is a refusal), ADR-612 (100STYLE, amended to CC BY 4.0),
`scene::Provenance`, `docs/dependencies.md`, `CREDITS.md`

---

## Context

§61 names three sources: 100STYLE, the ACCAD Open Motion Project, and CMU "under its specific usage
terms". 100STYLE was verified on 2026-09-21 (ADR-612's amendment). ACCAD and CMU had not been
assessed. Nor had the rule for derived data, meaning what a pack or database built from a corpus
may do once it leaves this machine.

**Nothing here adds a corpus.** Neither ACCAD nor CMU is downloaded, and no pack is built from
them.

## What each source says, read 2026-09-21 at the source

| | 100STYLE | ACCAD Open Motion Project | CMU Graphics Lab Motion Capture Database |
|---|---|---|---|
| **Where read** | `ianxmason.com/100style/`; Zenodo record 8127870 rights field | `accad.osu.edu/research/motion-lab/mocap-system-and-data` | `mocap.cs.cmu.edu` (home page and FAQ) |
| **Licence** | CC BY 4.0 | **CC BY 3.0 Unported** | **Bespoke terms, not a standard licence** (no SPDX identifier) |
| **Commercial use** | Yes | Yes | "You may include this data in commercially-sold products" |
| **Redistribution** | Yes, under CC BY | Yes, under CC BY | FAQ: "may be copied, modified, or redistributed without permission". Home page: "**you may not resell this data directly, even in converted form**" |
| **Derived data** | Allowed; changes must be indicated | Allowed; changes must be indicated (3.0 asks for it on adaptations) | Allowed ("modified"), but a pack **sold as data** is the conversion the terms forbid |
| **Attribution** | Required: `The 100STYLE Dataset - Ian Mason` | Required: `Open Motion Project by ACCAD/The Ohio State University` | Requested, not required: "The data used in this project was obtained from mocap.cs.cmu.edu. The database was created with funding from NSF EIA-0196217." |
| **Formats** | BVH | c3d, FBX, bvh, amc, txt | asf/amc, c3d (BVH only through third-party conversions) |

## Decision: the derivative-data rule for a distributable pack

A MotionPack and a motion database built from a corpus are **adaptations of it**. What the corpus
allows travels with every clip, which is why `Provenance` is per clip (ADR-612).

1. **CC BY sources (100STYLE, ACCAD).** A derived pack may be shipped and used commercially, on
   three conditions:
   - it carries the credit line, the licence name and link;
   - it carries a statement that the motion was changed. `Provenance::processing` is that
     statement, and the chain is printed, not summarised;
   - **it adds no restriction the licence does not.** CC BY 4.0 §2(a)(5)(B) forbids applying
     effective technological measures that restrict the recipient's rights. An encrypted or
     obfuscated pack built from 100STYLE is therefore not shippable as-is. That is a question for
     the owner before any such packaging exists.

   `Redistribution::Allowed`, `derivedDataAllowed = true`, `attributionRequired = true`.
2. **CMU.** The data may ship **inside** a product. A pack **distributed or sold on its own as
   motion data** is the "resell ... even in converted form" case, and it is not allowed. The terms
   are not a standard licence, so the SPDX field must name them honestly (`LicenseRef-CMU-mocap`)
   and never borrow a CC identifier. `Redistribution::RequiresReview` until the owner decides
   whether AV Gen output ever ships a pack as a separate artefact.
3. **Never source ACCAD or CMU through AMASS.** AMASS redistributes both. Its own licence (read
   2026-09-21, `amass.is.tue.mpg.de/license.html`) is non-commercial research only and forbids
   redistribution "in whole or in part". It does not say whether that reaches the constituent data,
   so a copy obtained through AMASS inherits the stricter terms. Take the data from the originals.
4. **Converted copies are not the source.** CMU's BVH conversions (cgspeed and others) and
   GitHub mirrors of either corpus are third-party redistributions. A permissive licence on a
   repository's tooling says nothing about the motion in it (§61's own warning). Provenance names
   the original corpus, and the licence is read there.

## What already enforces this, and what does not

- **Enforced:** a pack without a licence or a source does not build (`test_motion_pack.cpp`).
  `RequiresReview` is the default and is treated as forbidden by every check. Provenance is per
  clip and survives the database build (cinfra's pack content digest includes it).
- **Not enforced:** nothing checks that a CC BY pack carries its credit line into a shipped
  artefact, or that it is not wrapped in a restriction. No shipping path exists yet, so there is
  nothing to put the check on. When one exists, the check belongs there, not in the pack builder.

## Consequences

- §61 is assessed. ACCAD is as usable as 100STYLE, with its own credit line. CMU is usable inside a
  product, and a standalone pack built from it needs the owner's decision (point 2).
- **Owner-level question, batched:** will AV Gen ever ship a MotionPack as a separate artefact (a
  download, a marketplace item) rather than inside a rendered output? The answer decides CMU's
  `Redistribution` and whether point 1's no-added-restriction rule constrains packaging.

---

## Amendment, 2026-09-22: the owner's answer

**Ruling (owner, 22 Sep):** AV Gen will not ship a motion library on its own. Packs only ever travel
inside AV Gen projects or builds, never as a separate download or marketplace item.

**What follows:**

- **CMU.** Its one prohibition, "you may not resell this data directly, even in converted form", is
  not triggered by a pack that is never sold separately. The conditions that remain:
  - **inclusion in a commercial product is allowed** ("You may include this data in
    commercially-sold products"), and a pack inside a project or build is that case. Met;
  - **no warranty** ("We do not guarantee the quality of the data"). This imposes no obligation, only
    a risk AV Gen accepts;
  - **the acknowledgement is requested, not required** ("we would appreciate ..."), and only for
    published results. It is carried anyway, in `CREDITS.md`, when CMU data is added: "The data
    used in this project was obtained from mocap.cs.cmu.edu. The database was created with funding
    from NSF EIA-0196217."

  CMU's `Redistribution` becomes **Allowed for embedded use**. The SPDX field stays
  `LicenseRef-CMU-mocap`, never a CC identifier. **No CMU data is added by this amendment.**
- **CC BY §2(a)(5)(B)** (no effective technological measures). It no longer arises for a standalone
  pack, because there is none. **It can still matter for a pack embedded in a shipped build.** If a
  build encrypts or DRM-protects its assets, and a recipient could otherwise extract the CC BY motion,
  the measure restricts the licensed material. The licence text makes no exception for "embedded".
  An ordinary packed or compressed asset format is not a technological measure. So the rule is: **a
  build may pack or compress CC BY motion but must not encrypt or DRM-lock it.** No build path
  encrypts assets today; this is recorded so one does not start to without the question being
  asked.
- **Attribution still travels with the work.** Every project or build that carries a CC BY pack
  carries its credit lines (`CREDITS.md`: 100STYLE, and ACCAD's `Open Motion Project by ACCAD/The
  Ohio State University` if it is added), a licence link, and the pack's printed change chain
  (`Provenance::processing`).
