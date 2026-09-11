---
id: sequencer/overlays
title: Lyrics and Graphics
category: Sequencer
summary: Text and shapes over the frame, their styles and presets, and importing timed lyrics.
order: 43
tags: lyrics, text, overlay, composition, title, caption, border
keywords: how do i add text; import lyrics; lrc srt vtt; add a title; subtitle; text over the video
related: sequencer/overview, rendering/offline-render, sequencer/timeline-automation
features: panel.sequence, panel.composition
---

# Lyrics and Graphics

Overlay cues are text and shapes drawn over the finished frame. In the sequence they are **cues**;
when installed they become **composition layers**, which the Composition panel can then inspect.

## Cue properties

| Field | Meaning |
|---|---|
| `content` | the words, or the shape name |
| `start`, `end` | seconds |
| `style` | for text: one of the built-in styles below |
| `anchor` | position as a fraction of the frame, origin bottom left |
| `size` | a multiplier on the style's size |
| `colour` | |
| `preset` | an entrance and exit animation |
| `order` | lower draws first, under everything above it |

## Text styles

Four, in this order in the combo:

| Style | Character |
|---|---|
| `lyric` | the default — medium weight, centred, moderate tracking, a soft shadow |
| `title` | heavy, large, wide tracking |
| `caption` | light, small, very wide tracking, **left** aligned |
| `credit` | light, small, widest tracking, centred |

An unknown style name falls back to `lyric`.

## Presets

`none`, `fade in`, `fade out`, `fade in/out`, `scale pop`, `slide up`, `slide down`. Each writes
keyframes on the layer's parameters — opacity for the fades, scale for the pop, position for the
slides. The entrance duration is capped at half the cue's length.

## Importing lyrics

**Import Lyrics...** reads **LRC**, **SRT** or **WebVTT**. Each line becomes a text cue with the
`lyric` style, centred low in the frame, with a fade in and out.

A re-import replaces rather than doubles: every existing cue whose id starts with `lyric` is
removed first.

## Shapes

**Add Border** creates a rectangle cue drawn under everything else — a full-frame outline with a
thin stroke. Shapes can be `rectangle`, `ellipse` or `line`, with a size, corner radius, stroke
width and colour, and a glow.

> [!WARNING]
> **There is no image layer and no video layer.** An overlay cue of kind `image` will parse,
> validate and save, and then draw nothing while reporting *"the composition system has no image
> layer"*. The Composition panel says the same thing in its Add Layer menu: *"Image, video and
> nested compositions are not in this version."*

## Layers the sequence owns

Layers the sequence creates are named `seq:<cueId>` and are not saved into the project's
composition block — the cues are, and the layers are rebuilt from them. Layers you make by hand in
the Composition panel are yours and are never touched.

> [!NOTE]
> EXR output is the scene-linear image *before* tone mapping, and composition layers are
> display-referred. An EXR sequence will not contain your captions. AV Gen warns about this when a
> render starts with both. PNG and video output include them.
