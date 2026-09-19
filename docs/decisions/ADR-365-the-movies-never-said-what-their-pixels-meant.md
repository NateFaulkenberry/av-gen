# ADR-365: The movies never said what their pixels meant

Status: accepted
Date: 2026-09-19
Branch: `agent/mbackend`
Relates to: ADR-020 (native and ffmpeg video), ADR-016 (the post chain and the tone map),
ADR-182 (a probe that cannot fail proves nothing)

*Numbered 365 after 361-364 on this branch; renumber at merge if another lands first.*

## What was wrong

`grep` over `src/assets/video_writer_apple.mm` finds no `AVVideoColorPropertiesKey`, no
`kCVImageBufferColorPrimaries`, no `TransferFunction` and no `YCbCrMatrix`. `ffmpegArguments` in
`video_writer.cpp` carries no `-color_primaries`, no `-color_trc` and no `-colorspace`.

**Every movie this project has ever produced, by either backend, is untagged.** A player reading one
has nothing to go on and falls back to a guess. On an SDR deliverable written from an 8-bit
sRGB-encoded frame the guess is BT.709 and is right, which is exactly why this survived the whole
life of ADR-020 without anybody noticing.

A guess that happens to be right is still a guess. Three things it costs:

* a deliverable handed to somebody else's colour-managed pipeline is interpreted by *their* default
  rather than by what it is;
* the two backends could disagree with one another and nothing would say so;
* nothing HDR can be built on top of an untagged file. If a 10-bit BT.2020/PQ path is ever wanted,
  the tags are the first thing it needs, and adding them at that point would silently change how
  every *existing* file is read.

Found while auditing the offline output pipeline against a brief that asks for "one coherent colour
pipeline" and for HDR video. It is the cheapest true thing in that whole section.

## Decision

Both backends tag their output **BT.709 primaries, BT.709 transfer, BT.709 matrix**.

That is what the pixels actually are. `VideoWriter::writeFrame` takes 8-bit RGBA which
`shaders/tonemap.wgsl` has already tone-mapped and sRGB-encoded (ADR-016). sRGB and BT.709 share
identical primaries and differ only in the toe of their transfer curves; BT.709 is the tag a *video*
deliverable of that carries and is what every editor expects in a `.mov`.

This is **not a picture change**. It writes down the interpretation that was already being assumed.
Anyone who reopens this because the tag is wrong for their delivery is reopening it for the right
reason — an explicit tag can be argued with, and a missing one cannot.

When an HDR path exists, these two places are the ones that learn BT.2020 and PQ, and they are
deliberately the only two.

## The probe, and the control

`VideoInfo` grows `colorPrimaries`, `colorTransfer`, `colorMatrix` and `colorTagged()`, read from
the video track's own `CMFormatDescription` extensions rather than from anything the writer
remembers doing.

Two tests. The arm requires the exact strings `ITU_R_709_2` in all three, not merely that something
is set: a file tagged BT.2020 would pass a non-empty check and would be wrong in a way that only
appears on somebody else's monitor.

The control (ADR-182) writes a **deliberately untagged** file through the machine's own ffmpeg with
`-color_primaries unspecified` and friends, and requires the probe to report it untagged. Without
it, the arm is asserting that a function which always returns true returns true. It passes: an
untagged ProRes comes back with all three fields empty, which is the state every file this project
wrote before today.

Both tests `SKIP` rather than pass where the backend or ffmpeg is absent. A control that cannot run
is not a control, and saying so is better than a green tick.

## What this does not do

It does not make the pipeline HDR. `writeFrame` still takes RGBA8, the AVFoundation pixel pool is
still `kCVPixelFormatType_32BGRA` — so ProRes 4444, a 12-bit format, is still being fed 8-bit source
— and the ffmpeg path still lands in `yuv420p` for the lossy encoders. Those are recorded in
`docs/offline-backend-audit.md` as the real HDR gap and are deliberately a separate, larger piece of
work that nobody has yet named a delivery for.
